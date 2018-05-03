/*
 * Copyright (C) 2014 Cisco Systems, Inc.
 *
 * Author: Luka Perkov <luka.perkov@sartura.hr>
 * Author: Petar Koretic <petar.koretic@sartura.hr>
 *
 * freenetconfd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with freenetconfd. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>

#include <libubox/uloop.h>
#include <libubox/usock.h>
#include <libubox/ustream.h>

#include "netconfd/netconfd.h"

#include "config.h"
#include "messages.h"
#include "connection.h"
#include "methods.h"

static void connection_accept_cb(struct uloop_fd *fd, unsigned int events);
static void connection_close(struct ustream *s);

static struct uloop_fd server = { .cb = connection_accept_cb };
static struct connection *next_connection = NULL;



enum netconf_msg_step
{
	NETCONF_MSG_STEP_HELLO,
	NETCONF_MSG_STEP_HEADER_0,
	NETCONF_MSG_STEP_HEADER_1,
	NETCONF_MSG_STEP_HEADER_1_BUF,
	NETCONF_MSG_STEP_DATA_0,
	NETCONF_MSG_STEP_DATA_0_BUF,
	NETCONF_MSG_STEP_DATA_1,
	NETCONF_MSG_STEP_DATA_1_BUF,
	__NETCONF_MSG_STEP_MAX
};

enum stream
{
	STREAM_NONE,
	STREAM_NETCONF,
	STREAM_SNMP,
	_STREAM_MAX,
};

struct connection
{
	struct sockaddr_in sin;
	struct ustream_fd us;
	int step;
	int base;
	uint64_t msg_len;
	char *buf;
	int stream;
};

#define MAX_CONNECTION_NUM 10
static struct connection *global_conn[MAX_CONNECTION_NUM];
static int global_count = 0;

static void notify_state(struct ustream *s)
{
	struct connection *c = container_of(s, struct connection, us.stream);

	ustream_free(&c->us.stream);
	close(c->us.fd.fd);

	free(c);

	LOG("connection closed\n");
}

static void notify_read(struct ustream *s, int bytes)
{
	struct connection *c = container_of(s, struct connection, us.stream);

	char *data, *buf = NULL, *buf1 = NULL, *buf2 = NULL;
	int data_len, rc;

	DEBUG("starting to read incoming data\n");

	do
	{
		data = ustream_get_read_buf(s, &data_len);

		if (!data) break;

		switch (c->step)
		{
			case NETCONF_MSG_STEP_HELLO:
				DEBUG("handling hello\n");

				buf1 = strchr(data, '<');

				if (!buf1)
				{
					LOG("start of hello message was not found\n");
					connection_close(s);
					return;
				}

				if (buf1 != data)
				{
					LOG("start of hello message not found where expected\n");
					connection_close(s);
					return;
				}

				buf2 = strstr(buf1, XML_NETCONF_BASE_1_0_END);

				if (!buf2)
				{
					DEBUG("end of netconf message was not found in this buffer\n");
					return;
				}

				*buf2 = '\0';

				rc = method_analyze_message_hello(buf1, &c->base);

				if (rc)
				{
					connection_close(s);
					return;
				}

				ustream_consume(s, buf2 + strlen(XML_NETCONF_BASE_1_0_END) - data);

				if (c->base)
					c->step = NETCONF_MSG_STEP_DATA_1;
				else
					c->step = NETCONF_MSG_STEP_DATA_0;

				LOG("establishment completed\n");
				break;

			default:
				break;
		}

		data = ustream_get_read_buf(s, &data_len);

		if (!data) break;
		switch (c->step)
		{
			case NETCONF_MSG_STEP_DATA_0:

				if (c->msg_len > (data_len + strlen(XML_NETCONF_BASE_1_0_END)))
				{
					c->step = NETCONF_MSG_STEP_DATA_1_BUF;
					break;
				}

				if (data_len < strlen(XML_NETCONF_BASE_1_0_END))
				{
					/* leftovers from netcat testing */
					ustream_consume(s, data_len);
					break;
				}

				buf2 = strstr(data, XML_NETCONF_BASE_1_0_END);

				if (!buf2)
				{
					connection_close(s);
					return;
				}

				*buf2 = '\0';

				DEBUG("received rpc\n\n %s\n\n", data);
				rc = method_handle_message_rpc(data, &buf);
				if (rc == -1)
				{
					/* FIXME */
					connection_close(s);
					return;
				}
				else if (rc == 3){
					LOG("new client join netconf stream\n");
					c->stream = STREAM_NETCONF;					
					/* add c to global_conn */
					global_conn[++global_count] = c;
					if (global_conn[global_count] != c){
						ERROR("store connetion failed\n");
						return;
					}
					if (global_count > MAX_CONNECTION_NUM){
						ERROR("reached the maximum number of connections\n");
						return;
					}		
				}
				else if (rc == 4){
					LOG("new client join netconf snmp");
					c->stream = STREAM_SNMP;
					/* add c to global_conn */
					global_conn[++global_count] = c;
					if (global_conn[global_count] != c){
						ERROR("store connetion failed\n");
						return;
					}
					if (global_count > MAX_CONNECTION_NUM){
						ERROR("reached the maximum number of connections\n");
						return;
					}	
				}
				if (rc == -1)
				{
					/* FIXME */
					connection_close(s);
					return;
				}

				DEBUG("sending rpc-reply\n\n %s\n\n", buf);
				ustream_printf(s, "\n#%zu\n%s%s", strlen(buf), buf, XML_NETCONF_BASE_1_0_END);
				free(buf);

				ustream_consume(s, buf2 + strlen(XML_NETCONF_BASE_1_0_END) - data);
				c->msg_len = 0;

				if (rc == 1)
				{
					connection_close(s);
					return;
				}

				break;

			case NETCONF_MSG_STEP_DATA_1:

				if (c->msg_len > (data_len + strlen(XML_NETCONF_BASE_1_1_END)))
				{
					c->step = NETCONF_MSG_STEP_DATA_1_BUF;
					break;
				}

				if (data_len < strlen(XML_NETCONF_BASE_1_1_END))
				{
					/* leftovers from netcat testing */
					ustream_consume(s, data_len);
					break;
				}

				buf2 = strstr(data, XML_NETCONF_BASE_1_1_END);

				if (!buf2)
				{
					connection_close(s);
					return;
				}

				*buf2 = '\0';

				DEBUG("received rpc\n\n %s\n\n", data);
				rc = method_handle_message_rpc(data, &buf);
				DEBUG("sending rpc-reply\n\n %s\n\n", buf);
				ustream_printf(s, "\n#%zu\n%s%s", strlen(buf), buf, XML_NETCONF_BASE_1_1_END);
				free(buf);

				ustream_consume(s, buf2 + strlen(XML_NETCONF_BASE_1_1_END) - data);
				c->msg_len = 0;

				if (rc == 1)
				{
					connection_close(s);
					return;
				}

				break;

			default:
				break;
		}
	}
	while (1);
}

static void connection_accept_cb(struct uloop_fd *fd, unsigned int events)
{
	struct connection *c;
	unsigned int sl = sizeof(struct sockaddr_in);
	int sfd, rc;
	char *hello_message = NULL;

	LOG("received new connection\n");

	if (!next_connection)
	{
		next_connection = calloc(1, sizeof(*next_connection));
	}

	if (!next_connection)
	{
		ERROR("not enough memory to accept connection\n");
		return;
	}

	c = next_connection;
	sfd = accept(server.fd, (struct sockaddr *) &c->sin, &sl);

//	printf("id : %d, port : %d\n", inet_ntoa(c->sin.sin_addr), c->sin.sin_port);
	if (sfd < 0)
	{
		ERROR("failed accepting connection\n");
		return;
	}

	DEBUG("configuring connection parameters\n");

	c->us.stream.string_data = true;
	c->us.stream.notify_read = notify_read;
	c->us.stream.notify_state = notify_state;
	c->us.stream.r.buffer_len = 16384;
	c->step = NETCONF_MSG_STEP_HELLO;
	c->stream = STREAM_NONE;

	DEBUG("crafting hello message\n");
	rc = method_create_message_hello(&hello_message);

	if (rc)
	{
		ERROR("failed to create hello message\n");
		close(sfd);
		return;
	}

	ustream_fd_init(&c->us, sfd);
	next_connection = NULL;

	DEBUG("sending hello message\n");
	ustream_printf(&c->us.stream, "%s%s", hello_message, XML_NETCONF_BASE_1_0_END);
	free(hello_message);
}

static void
connection_close(struct ustream *s)
{
	struct connection *c = container_of(s, struct connection, us.stream);

	char *data;
	int data_len;
	int i;

	for (i = 1; i <= global_count; i++){
		if (global_conn[i] == c){
			LOG("remove notify client\n");
			if (i == global_count){
				global_conn[i] = NULL;
			}
			else{
				global_conn[i] = global_conn[global_count];
				global_conn[global_count] = NULL;
			}
			global_count--;
		}
	}

	data = ustream_get_read_buf(s, &data_len);

	if (data)
	{
		ustream_consume(s, data_len);
	}

	ustream_set_read_blocked(s, true);

	close(c->us.fd.fd);

	LOG("closing connection\n");
}

int
server_init()
{
	server.fd = usock(USOCK_TCP | USOCK_SERVER, config.addr, config.port);

	if (server.fd < 0)
	{
		ERROR("unable to open socket %s:%s\n", config.addr, config.port);
		return -1;
	}

	uloop_fd_add(&server, ULOOP_READ);

	return 0;
}

void *
subscription_netconf()
{
	LOG("subscription_netconf start\n");
	struct connection *c;
	int i = 1;
	char *hello_message = NULL;
	int flag = 0;
	int rc;

	rc = method_create_notification_netconf(&hello_message);
	if (rc)
	{
		ERROR("failed to create notification_netconf message\n");
		pthread_exit(0);
	}
	do{
		if (global_count <= 0){
			if (flag == 0){
				LOG("none client notify stream_netconf\n");
				flag = 1;
			}
			continue;
		}
		else {
			if (i > global_count){
				i = 1;
				continue;
			}
			else {
				c = global_conn[i];
				if (c->stream != STREAM_NETCONF)
					continue;
				sleep(3);
				ustream_printf(&c->us.stream, "%s", hello_message);
				i++;
			}
		}
	}while(1);
}

int 
subscription_init()
{
	pthread_t stream_netconf_pid;

    pthread_attr_t attr1;  
    pthread_attr_init(&attr1);  
    pthread_attr_setdetachstate(&attr1, PTHREAD_CREATE_DETACHED); 

    // 创建stream netconf的订阅上报线程
    if(pthread_create(&stream_netconf_pid, &attr1, subscription_netconf, NULL) == -1){
        ERROR("fail to create pthread subscription_netconf\n");
        return -1;
    }
	if(pthread_join(stream_netconf_pid, NULL) == -1){
        ERROR("fail to join pthread subscription_netconf\n");
        return -1;
    }
    return 0;
}