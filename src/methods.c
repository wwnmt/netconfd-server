/*
 * Copyright (C) 2014 Sartura, Ltd.
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

#include <stdlib.h>
#include <string.h>
#include <roxml.h>
#include <stdint.h>
#include <time.h>

#include "netconfd/netconfd.h"
#include "netconfd/netconf.h"
#include "netconfd/plugin.h"

#include "netconf.h"
#include "methods.h"
#include "messages.h"
#include "config.h"


#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))
#endif


static int method_handle_get(struct rpc_data *data);
static int method_handle_get_config(struct rpc_data *data);
static int method_handle_edit_config(struct rpc_data *data);
static int method_handle_copy_config(struct rpc_data *data);
static int method_handle_delete_config(struct rpc_data *data);
static int method_handle_lock(struct rpc_data *data);
static int method_handle_unlock(struct rpc_data *data);
static int method_handle_close_session(struct rpc_data *data);
static int method_handle_kill_session(struct rpc_data *data);
static int method_handle_stream(struct rpc_data *data);
static int method_handle_create_subscription(struct rpc_data *data);

const struct rpc_method rpc_methods[] =
{
	{ "get", method_handle_get},
	{ "get-config", method_handle_get_config },
	{ "edit-config", method_handle_edit_config },
	{ "copy-config", method_handle_copy_config },
	{ "delete-config", method_handle_delete_config },
	{ "lock", method_handle_lock },
	{ "unlock", method_handle_unlock },
	{ "close-session", method_handle_close_session },
	{ "kill-session", method_handle_kill_session },
	{ "stream", method_handle_stream},
	{ "create-subscription", method_handle_create_subscription},
};

/*
 * method_analyze_message_hello() - analyze rpc hello message
 *
 * @char*:	xml message for parsing
 * @int*:	netconf 'base' we deduce from message
 *
 * Checks if rpc message is a valid hello message and parse rcp base version
 * client supports.
 */
int method_analyze_message_hello(char *xml_in, int *base)
{
	int rc = -1, num_nodes = 0;
	node_t **nodes;
	int tbase = -1;

	node_t *root = roxml_load_buf(xml_in);

	if (!root) goto exit;

	node_t *hello = roxml_get_nodes(root, ROXML_ELM_NODE, "hello", 0);

	if (!hello) goto exit;

	/* rfc: must not have */
	node_t *session_id = roxml_get_nodes(hello, ROXML_ELM_NODE, "session-id", 0);

	if (session_id) goto exit;

	nodes = roxml_xpath(root, "//capabilities/capability", &num_nodes);

	for (int i = 0; i < num_nodes; i++)
	{
		if (!nodes[i]) continue;

		char *value = roxml_get_content(nodes[i], NULL, 0, NULL);

		if (strcmp(value, "urn:ietf:params:netconf:base:1.1") == 0)
		{
			tbase = 1;
		}
		else if (strcmp(value, "urn:ietf:params:netconf:base:1.0") == 0)
		{
			tbase = 0;
		}
	}

	if (tbase == -1)
		goto exit;

	*base = tbase;

	rc = 0;

exit:

	roxml_release(RELEASE_ALL);
	roxml_close(root);

	return rc;
}

int method_create_notification_netconf(char **xml_out)
{
	int rc = -1, len;

	node_t *root = roxml_load_buf(XML_NOTIFICATION_TEMPLATE);

	if (!root)
	{
		ERROR("unable to load 'notification_netconf' message template\n");
		goto exit;
	}	
	node_t *eventtime_n = roxml_get_chld(root, NULL, 0);

	if (!eventtime_n)
	{
		ERROR("unable to parse 'notification_netconf' message template\n");
		goto exit;
	}
	/* add notify messages here */
	// time
	time_t timep;
	time (&timep);
	roxml_add_node(eventtime_n, 0, ROXML_ELM_NODE, "eventTime", ctime(&timep));
	
	len = roxml_commit_changes(root, NULL, xml_out, 0);

	if (len <= 0)
	{
		ERROR("unable to create 'netconf hello' message\n");
		goto exit;
	}

	rc = 0;

exit:

	roxml_close(root);

	return rc;
}

int method_create_message_hello(char **xml_out)
{
	int rc = -1, len;
	char c_session_id[BUFSIZ];
	static uint32_t session_id = 0;

	/* prevent variable overflow */
	if (++session_id == 0)
		session_id = 1;

	node_t *root = roxml_load_buf(XML_NETCONF_HELLO);

	if (!root)
	{
		ERROR("unable to load 'netconf hello' message template\n");
		goto exit;
	}

	node_t *n_hello = roxml_get_chld(root, NULL, 0);

	if (!n_hello)
	{
		ERROR("unable to parse 'netconf hello' message template\n");
		goto exit;
	}

	len = snprintf(c_session_id, BUFSIZ, "%d", session_id);

	if (len <= 0)
	{
		ERROR("unable to convert session_id\n");
		goto exit;
	}

	node_t *n_session_id = roxml_add_node(n_hello, 0, ROXML_ELM_NODE, "session-id", c_session_id);

	if (!n_session_id)
	{
		ERROR("unable to add session id node\n");
		goto exit;
	}

	len = roxml_commit_changes(root, NULL, xml_out, 0);

	if (len <= 0)
	{
		ERROR("unable to create 'netconf hello' message\n");
		goto exit;
	}

	rc = 0;

exit:

	roxml_close(root);

	return rc;
}

/*
 * method_handle_message - handle all rpc messages
 *
 * @char*:	xml message for parsing
 * @char**:	xml message we create for response
 *
 * Get netconf method from rpc message and call apropriate rpc method which
 * will parse and return response message.
 */
int method_handle_message_rpc(char *xml_in, char **xml_out)
{
	int rc = -1;
	char *operation_name = NULL;
	char *ns = NULL;
	struct rpc_data data = { NULL, NULL, NULL, 0};

	//xml
	node_t *root_in = roxml_load_buf(xml_in);

	if (!root_in) goto exit;

	//rpc
	node_t *rpc_in = roxml_get_chld(root_in, NULL, 0);

	if (!rpc_in) goto exit;

	//op
	node_t *operation = roxml_get_chld(rpc_in, NULL, 0);

	if (!operation) goto exit;

	node_t *n_ns = roxml_get_ns(operation);

	if (!n_ns) goto exit;

	//op_name
	operation_name = roxml_get_name(operation, NULL, 0);
	ns = roxml_get_content(n_ns, NULL, 0, NULL);

	if (!operation_name || !ns)
	{
		ERROR("unable to extract rpc and namespace\n");
		goto exit;
	}

	DEBUG("received rpc '%s' (%s)\n", operation_name, ns);

	data.out = roxml_load_buf(XML_NETCONF_REPLY_TEMPLATE);
	node_t *rpc_out = roxml_get_chld(data.out, NULL, 0);

	/* copy all attribute from rpc to rpc-reply */
	int args = roxml_get_attr_nb(rpc_in);

	for (int i = 0; i < args; i++)
	{
		int flags = ROXML_ATTR_NODE;
		node_t *n_arg = roxml_get_attr(rpc_in, NULL, i);

		char *name = roxml_get_name(n_arg, NULL, 0);

		// default namespace
		if (!strcmp(name, ""))
			flags |= ROXML_NS_NODE;

		char *value = roxml_get_content(n_arg, NULL, 0, NULL);

		roxml_add_node(rpc_out, 0, flags, name, value);
	}
	

	data.in = operation;
	data.out = rpc_out;

	const struct rpc_method *method = NULL;

	for (int i = 0; i < ARRAY_SIZE(rpc_methods); i++)
	{
		if (!strcmp(operation_name, rpc_methods[i].query))
		{
			method = &rpc_methods[i];
			break;
		}
	}

	if (!method)
	{
		ERROR("method not supported\n");
		data.error = netconf_rpc_error("method not supported", RPC_ERROR_TAG_OPERATION_NOT_SUPPORTED, 0, 0, NULL);
		rc = RPC_ERROR;
	}
	else
	{
		rc = method->handler(&data);
	}

	switch (rc)
	{
		case RPC_OK:
			roxml_add_node(data.out, 0, ROXML_ELM_NODE, "ok", NULL);
			rc = 0;
			break;

		case RPC_OK_CLOSE:
			roxml_add_node(data.out, 0, ROXML_ELM_NODE, "ok", NULL);
			rc = 1;
			break;

		case RPC_DATA:
			rc = 0;
			break;

		case RPC_NOTIFY_NETCONF_OK:
			roxml_add_node(data.out, 0, ROXML_ELM_NODE, "netconf-ok", NULL);
			rc = 3;
			break;

		case RPC_NOTIFY_SNMP_OK:
			roxml_add_node(data.out, 0, ROXML_ELM_NODE, "snmp-ok", NULL);
			rc = 4;
			break;

		case RPC_ERROR:
			if (!data.error)
				data.error = netconf_rpc_error("UNKNOWN ERROR", 0, 0, 0, NULL);

			roxml_add_node(data.out, 0, ROXML_ELM_NODE, "rpc-error", data.error);

			free(data.error);
			data.error = NULL;

			rc = 0;
			break;

		case RPC_DATA_EXISTS:
			if (!data.error)
				data.error = netconf_rpc_error("Data exists!", RPC_ERROR_TAG_DATA_EXISTS, RPC_ERROR_TYPE_RPC, RPC_ERROR_SEVERITY_ERROR, NULL);

			roxml_add_node(data.out, 0, ROXML_ELM_NODE, "rpc-error", data.error);

			free(data.error);
			data.error = NULL;

			rc = 0;
			break;

		case RPC_DATA_MISSING:
			if (!data.error)
				data.error = netconf_rpc_error("Data missing!", RPC_ERROR_TAG_DATA_MISSING, RPC_ERROR_TYPE_RPC, RPC_ERROR_SEVERITY_ERROR, NULL);

			roxml_add_node(data.out, 0, ROXML_ELM_NODE, "rpc-error", data.error);

			free(data.error);
			data.error = NULL;

			rc = 0;
			break;
		case RPC_NOTIFY_ERROR:
			if (!data.error)
				data.error = netconf_rpc_error("Stream missing!", RPC_ERROR_TAG_DATA_MISSING, RPC_ERROR_TYPE_RPC, RPC_ERROR_SEVERITY_ERROR, NULL);

			roxml_add_node(data.out, 0, ROXML_ELM_NODE, "rpc-error", data.error);

			free(data.error);
			data.error = NULL;

			rc = 0;
			break;
	}

exit:

	if (data.out)
	{
		roxml_commit_changes(data.out, NULL, xml_out, 0);
		roxml_close(data.out);
	}

	roxml_release(RELEASE_ALL);
	roxml_close(root_in);
	return rc;
}

static int
method_handle_get(struct rpc_data *data)
{
	node_t *n_data = roxml_add_node(data->out, 0, ROXML_ELM_NODE, "data", NULL);

	int nb = 0;
	node_t **n_filter, *n, *filter;

	/* get messages from device */
	char buf[80] = {0};
	sampleKernelVersion(buf);
	node_t *n_systems = roxml_add_node(n_data, 0, ROXML_ELM_NODE, "systems", NULL);
	node_t *n_system_type = roxml_add_node(n_systems, 0, ROXML_ELM_NODE, "os_type", buf);
	sampleOsRelease(buf);
	node_t *n_system_release = roxml_add_node(n_systems, 0, ROXML_ELM_NODE, "os_release", buf);
	sampleKernelVersion(buf);
	node_t *n_system_version = roxml_add_node(n_systems, 0, ROXML_ELM_NODE, "kernel_version", buf);

	if (data->get_config == 1){
		int count = roxml_get_chld_nb(data->in);
		int i;
		for (i = 0; i < count; i++){
			node_t *rpc_arg = roxml_get_chld(data->in, NULL, i);
			char *arg = roxml_get_name(rpc_arg, NULL, 0);
			node_t *rpc_name = roxml_get_chld(rpc_arg, NULL, i);
			char *name = roxml_get_name(rpc_name, NULL, 0);
			printf("%s : %s\n", arg, name);
		}
	}

	filter = roxml_get_chld(data->in, "filter", 0);

	if ((n_filter = roxml_xpath(data->in, "//filter", &nb)))
	{
		nb = roxml_get_chld_nb(n_filter[0]);

		/* empty filter */
		if (!nb)
			return RPC_DATA;

		while (--nb >= 0)
		{


		// 	n = roxml_get_chld(n_filter[0], NULL, nb);
		// 	char *module = roxml_get_name(n, NULL, 0);
		// 	char *ns = roxml_get_content(roxml_get_ns(n), NULL, 0, NULL);
		// 	DEBUG("filter for module: %s (%s)\n", module, ns);

		// //	list_for_each_entry(elem, modules, list)
		// 	{
		// //		DEBUG("module: %s\n", elem->name);

		// //		if (!strcmp(ns, elem->m->ns))
		// 		{
		// //			DEBUG("calling module: %s (%s) \n", module, ns);
		// //			struct rpc_data d = {n, n_data, NULL, data->get_config};

		// //			get(&d, elem->m->datastore);

		// 			break;
		// 		}
		// 	}
		}
	}
	else
	{
		DEBUG("no filter requested, processing all modules\n");
		return RPC_DATA;
	}
	return RPC_DATA;
}

static int
method_handle_get_config(struct rpc_data *data)
{
	// TODO: merge with get

	data->get_config = 1;
	return method_handle_get(data);
}

static int
method_handle_edit_config(struct rpc_data *data)
{
	node_t *config = roxml_get_chld(data->in, "config", 0);


	node_t *rpc_arg = roxml_get_chld(data->in, NULL, 0);
	char *arg = roxml_get_name(rpc_arg, NULL, 0);
	node_t *rpc_name = roxml_get_chld(rpc_arg, NULL, 0);
	char *target_database = roxml_get_name(rpc_name, NULL, 0);


	if (!config) return RPC_ERROR;


	int rc = RPC_OK;

	int child_count = roxml_get_chld_nb(config);

	for (int i = 0; i < child_count; i++)
	{
		node_t *cur = roxml_get_chld(config, NULL, i);

		char *module = roxml_get_name(cur, NULL, 0);
		char *ns = roxml_get_content(roxml_get_ns(cur), NULL, 0, NULL);
	}

	return rc;
}

static int
method_handle_copy_config(struct rpc_data *data)
{
	int count = roxml_get_chld_nb(data->in);
	int i;
	char *database[2];
	for (i = 0; i < count; i++){
		node_t *rpc_arg = roxml_get_chld(data->in, NULL, i);
		char *arg = roxml_get_name(rpc_arg, NULL, 0);
		node_t *rpc_name = roxml_get_chld(rpc_arg, NULL, 0);
		database[i] = roxml_get_name(rpc_name, NULL, 0);
	}
	return RPC_OK;
}

static int
method_handle_delete_config(struct rpc_data *data)
{
	node_t *rpc_arg = roxml_get_chld(data->in, NULL, 0);
	char *arg = roxml_get_name(rpc_arg, NULL, 0);
	node_t *rpc_name = roxml_get_chld(rpc_arg, NULL, 0);
	char *target_database = roxml_get_name(rpc_name, NULL, 0);
	return RPC_OK;
}

static int
method_handle_lock(struct rpc_data *data)
{
	node_t *rpc_arg = roxml_get_chld(data->in, NULL, 0);
	char *arg = roxml_get_name(rpc_arg, NULL, 0);
	node_t *rpc_name = roxml_get_chld(rpc_arg, NULL, 0);
	char *target_database = roxml_get_name(rpc_name, NULL, 0);

	return RPC_OK;
}

static int
method_handle_unlock(struct rpc_data *data)
{
	node_t *rpc_arg = roxml_get_chld(data->in, NULL, 0);
	char *arg = roxml_get_name(rpc_arg, NULL, 0);
	node_t *rpc_name = roxml_get_chld(rpc_arg, NULL, 0);
	char *target_database = roxml_get_name(rpc_name, NULL, 0);
	return RPC_OK;
}

static int
method_handle_close_session(struct rpc_data *data)
{
	return RPC_OK_CLOSE;
}

static int
method_handle_kill_session(struct rpc_data *data)
{
	return RPC_OK_CLOSE;
}

static int
method_handle_stream(struct rpc_data *data)
{
	return method_handle_get(data);
}

static int
method_handle_create_subscription(struct rpc_data *data)
{
	node_t *streams = roxml_get_chld(data->in, "stream", 0);
	if (!streams) return RPC_ERROR;

	node_t *stream = roxml_get_chld(streams, NULL, 0);
	char *name = roxml_get_name(stream, NULL, 0);	
	
	if (!strcmp(name, "netconf")){
		// printf("stream : %s\n", name);
		return RPC_NOTIFY_NETCONF_OK;
	}
	else if (!strcmp(name, "snmp")){
		// printf("stream : %s\n", name);
		return RPC_NOTIFY_SNMP_OK;
	}
	else
		return RPC_NOTIFY_ERROR;
}