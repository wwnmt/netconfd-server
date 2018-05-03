/*
 * Copyright (C) 2014 Cisco Systems, Inc.
 * Copyright (C) 2014 Sartura, Ltd.
 *
 * Author: Zvonimir Fras <zvonimir.fras@sartura.hr>
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

#include "netconfd/netconfd.h"
#include "netconfd/netconf.h"

#include "netconf.h"
#include "messages.h"

#include <dirent.h>
#include <string.h>
#include <stdlib.h>

char *rpc_error_tags[__RPC_ERROR_TAG_COUNT] =
{
	"operation-failed",
	"operation-not-supported",
	"in-use",
	"invalid-value",
	"data-missing",
	"data-exists"
};

char *rpc_error_types[__RPC_ERROR_TYPE_COUNT] =
{
	"transport",
	"rpc",
	"protocol",
	"application"
};


char *rpc_error_severities[__RPC_ERROR_SEVERITY_COUNT] =
{
	"error",
	"warning"
};

char *netconf_rpc_error(char *msg, rpc_error_tag_t rpc_error_tag, rpc_error_type_t rpc_error_type, rpc_error_severity_t rpc_error_severity, char *error_app_tag)
{
	// defaults
	char *tag = "operation-failed";
	char *type = "rpc";
	char *severity = "error";

	char *rpc_error = NULL;
	int rc;

	// truncate too big messages
	if (!msg || strlen(msg) > 400)
		msg = "";

	if (rpc_error_tag > 0 && rpc_error_tag < __RPC_ERROR_TAG_COUNT)
		tag = rpc_error_tags[rpc_error_tag];

	if (rpc_error_type > 0 && rpc_error_type < __RPC_ERROR_TYPE_COUNT)
		type = rpc_error_types[rpc_error_type];

	if (rpc_error_severity > 0 && rpc_error_severity < __RPC_ERROR_SEVERITY_COUNT)
		severity = rpc_error_severities[rpc_error_severity];

	char *error_app_tag_buff = NULL;

	if (error_app_tag){
		rc = asprintf(&error_app_tag_buff, "<error-app-tag>%s</error-app-tag>", error_app_tag);
		if (rc != 0)
			ERROR("asprintf error\n");
	}

	rc = asprintf(&rpc_error, "<error-type>%s</error-type><error-tag>%s</error-tag>"
			 "<error-severity>%s</error-severity><error-message xml:lang=\"en\">%s</error-message>%s", type, tag, severity, msg, error_app_tag_buff ? error_app_tag_buff : "");
	if (rc != 0)
		ERROR("asprintf error\n");
	free(error_app_tag_buff);

	return rpc_error;
}
