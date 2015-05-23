/**************************************************************************
 *
 * CMD.C -  Nagios Command CGI
 *
 *
 * License:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *************************************************************************/

#include "../include/config.h"
#include "../include/common.h"
#include "../include/objects.h"
#include "../include/comments.h"
#include "../include/downtime.h"

#include "../include/cgiutils.h"
#include "../include/cgiauth.h"
#include "../include/getcgi.h"

extern const char *extcmd_get_name(int id);

extern char main_config_file[MAX_FILENAME_LENGTH];
extern char url_html_path[MAX_FILENAME_LENGTH];
extern char url_images_path[MAX_FILENAME_LENGTH];
extern char command_file[MAX_FILENAME_LENGTH];

extern char url_stylesheets_path[MAX_FILENAME_LENGTH];

extern char *http_charset;

extern int  nagios_process_state;

extern int  use_authentication;

extern int  lock_author_names;


#define MAX_AUTHOR_LENGTH	64
#define MAX_COMMENT_LENGTH	1024

#define HTML_CONTENT   0
#define WML_CONTENT    1


char *host_name = "";
char *hostgroup_name = "";
char *servicegroup_name = "";
char *service_desc = "";
char *comment_author = "";
char *comment_data = "";
char *start_time_string = "";
char *end_time_string = "";

unsigned long comment_id = 0;
unsigned long downtime_id = 0;
int notification_delay = 0;
int schedule_delay = 0;
int persistent_comment = FALSE;
int sticky_ack = FALSE;
int send_notification = FALSE;
int force_check = FALSE;
int plugin_state = STATE_OK;
char plugin_output[MAX_INPUT_BUFFER] = "";
char performance_data[MAX_INPUT_BUFFER] = "";
time_t start_time = 0L;
time_t end_time = 0L;
int affect_host_and_services = FALSE;
int propagate_to_children = FALSE;
int fixed = FALSE;
unsigned long duration = 0L;
unsigned long triggered_by = 0L;
int child_options = 0;
int force_notification = 0;
int broadcast_notification = 0;

int command_type = CMD_NONE;
int command_mode = CMDMODE_REQUEST;

int content_type = HTML_CONTENT;

int display_header = TRUE;

authdata current_authdata;

void show_command_help(int);
void request_command_data(int);
void commit_command_data(int);
int commit_command(int);
int write_command_to_file(char *);
void clean_comment_data(char *);

void document_header(int);
void document_footer(void);
int process_cgivars(void);

int string_to_time(char *, time_t *);



int main(void) {
	int result = OK;

	/* Initialize shared configuration variables */                             
	init_shared_cfg_vars(1);

	/* get the arguments passed in the URL */
	process_cgivars();

	/* reset internal variables */
	reset_cgi_vars();

	/* read the CGI configuration file */
	result = read_cgi_config_file(get_cgi_config_location());
	if(result == ERROR) {
		document_header(FALSE);
		if(content_type == WML_CONTENT)
			printf("<p>错误: CGI配置文件无法打开!</p>\n");
		else
			cgi_config_file_error(get_cgi_config_location());
		document_footer();
		return ERROR;
		}

	/* read the main configuration file */
	result = read_main_config_file(main_config_file);
	if(result == ERROR) {
		document_header(FALSE);
		if(content_type == WML_CONTENT)
			printf("<p>错误: 主配置文件无法打开!</p>\n");
		else
			main_config_file_error(main_config_file);
		document_footer();
		return ERROR;
		}

	/* This requires the date_format parameter in the main config file */
	if(strcmp(start_time_string, ""))
		string_to_time(start_time_string, &start_time);

	if(strcmp(end_time_string, ""))
		string_to_time(end_time_string, &end_time);


	/* read all object configuration data */
	result = read_all_object_configuration_data(main_config_file, READ_ALL_OBJECT_DATA);
	if(result == ERROR) {
		document_header(FALSE);
		if(content_type == WML_CONTENT)
			printf("<p>错误: 无法读取对象配置数据!</p>\n");
		else
			object_data_error();
		document_footer();
		return ERROR;
		}

	document_header(TRUE);

	/* get authentication information */
	get_authentication_information(&current_authdata);

	if(display_header == TRUE) {

		/* begin top table */
		printf("<table border=0 width=100%%>\n");
		printf("<tr>\n");

		/* left column of the first row */
		printf("<td align=left valign=top width=33%%>\n");
		display_info_table("外部命令接口", FALSE, &current_authdata);
		printf("</td>\n");

		/* center column of the first row */
		printf("<td align=center valign=top width=33%%>\n");
		printf("</td>\n");

		/* right column of the first row */
		printf("<td align=right valign=bottom width=33%%>\n");

		/* display context-sensitive help */
		if(command_mode == CMDMODE_COMMIT)
			display_context_help(CONTEXTHELP_CMD_COMMIT);
		else
			display_context_help(CONTEXTHELP_CMD_INPUT);

		printf("</td>\n");

		/* end of top table */
		printf("</tr>\n");
		printf("</table>\n");
		}

	/* authorized_for_read_only should take priority */
	if(is_authorized_for_read_only(&current_authdata) == TRUE) {
		printf("<P><DIV CLASS='errorMessage'>看起来像是你没有权限提交您所请求的命令...</DIV></P>\n");
		printf("<P><DIV CLASS='errorDescription'>如果你认为这是一个错误，请检查HTTP服务器访问CGI的身份验证要求<br>");
		printf("并在你的CGI的配置文件中检查授权选项。</DIV></P>\n");

		document_footer();

		/* free allocated memory */
		free_memory();
		free_object_data();

		return OK;
        }

	/* if no command was specified... */
	if(command_type == CMD_NONE) {
		if(content_type == WML_CONTENT)
			printf("<p>错误: 无指定命令!</p>\n");
		else
			printf("<P><DIV CLASS='errorMessage'>Error: No command was specified</DIV></P>\n");
		}

	/* if this is the first request for a command, present option */
	else if(command_mode == CMDMODE_REQUEST)
		request_command_data(command_type);

	/* the user wants to commit the command */
	else if(command_mode == CMDMODE_COMMIT)
		commit_command_data(command_type);

	document_footer();

	/* free allocated memory */
	free_memory();
	free_object_data();

	return OK;
	}



void document_header(int use_stylesheet) {

	if(content_type == WML_CONTENT) {

		printf("Content-type: text/vnd.wap.wml; charset=\"%s\"\r\n\r\n", http_charset);

		printf("<?xml version=\"1.0\"?>\n");
		printf("<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.1//EN\" \"http://www.wapforum.org/DTD/wml_1.1.xml\">\n");

		printf("<wml>\n");

		printf("<card id='card1' title='命令结果'>\n");
		}

	else {

		printf("Content-type: text/html; charset=\"%s\"\r\n\r\n", http_charset);

		printf("<html>\n");
		printf("<head>\n");
		printf("<link rel=\"shortcut icon\" href=\"%sfavicon.ico\" type=\"image/ico\">\n", url_images_path);
		printf("<title>\n");
		printf("外部命令接口\n");
		printf("</title>\n");

		if(use_stylesheet == TRUE) {
			printf("<LINK REL='stylesheet' TYPE='text/css' HREF='%s%s'>\n", url_stylesheets_path, COMMON_CSS);
			printf("<LINK REL='stylesheet' TYPE='text/css' HREF='%s%s'>\n", url_stylesheets_path, COMMAND_CSS);
			}

		printf("</head>\n");

		printf("<body CLASS='cmd'>\n");

		/* include user SSI header */
		include_ssi_files(COMMAND_CGI, SSI_HEADER);
		}

	return;
	}


void document_footer(void) {

	if(content_type == WML_CONTENT) {
		printf("</card>\n");
		printf("</wml>\n");
		}

	else {

		/* include user SSI footer */
		include_ssi_files(COMMAND_CGI, SSI_FOOTER);

		printf("</body>\n");
		printf("</html>\n");
		}

	return;
	}


int process_cgivars(void) {
	char **variables;
	int error = FALSE;
	int x;

	variables = getcgivars();

	for(x = 0; variables[x] != NULL; x++) {

		/* do some basic length checking on the variable identifier to prevent buffer overflows */
		if(strlen(variables[x]) >= MAX_INPUT_BUFFER - 1) {
			continue;
			}

		/* we found the command type */
		else if(!strcmp(variables[x], "cmd_typ")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			command_type = atoi(variables[x]);
			}

		/* we found the command mode */
		else if(!strcmp(variables[x], "cmd_mod")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			command_mode = atoi(variables[x]);
			}

		/* we found the comment id */
		else if(!strcmp(variables[x], "com_id")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			comment_id = strtoul(variables[x], NULL, 10);
			}

		/* we found the downtime id */
		else if(!strcmp(variables[x], "down_id")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			downtime_id = strtoul(variables[x], NULL, 10);
			}

		/* we found the notification delay */
		else if(!strcmp(variables[x], "not_dly")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			notification_delay = atoi(variables[x]);
			}

		/* we found the schedule delay */
		else if(!strcmp(variables[x], "sched_dly")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			schedule_delay = atoi(variables[x]);
			}

		/* we found the comment author */
		else if(!strcmp(variables[x], "com_author")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			if((comment_author = (char *)strdup(variables[x])) == NULL)
				comment_author = "";
			strip_html_brackets(comment_author);
			}

		/* we found the comment data */
		else if(!strcmp(variables[x], "com_data")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			if((comment_data = (char *)strdup(variables[x])) == NULL)
				comment_data = "";
			strip_html_brackets(comment_data);
			}

		/* we found the host name */
		else if(!strcmp(variables[x], "host")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			if((host_name = (char *)strdup(variables[x])) == NULL)
				host_name = "";
			strip_html_brackets(host_name);
			}

		/* we found the hostgroup name */
		else if(!strcmp(variables[x], "hostgroup")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			if((hostgroup_name = (char *)strdup(variables[x])) == NULL)
				hostgroup_name = "";
			strip_html_brackets(hostgroup_name);
			}

		/* we found the service name */
		else if(!strcmp(variables[x], "service")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			if((service_desc = (char *)strdup(variables[x])) == NULL)
				service_desc = "";
			strip_html_brackets(service_desc);
			}

		/* we found the servicegroup name */
		else if(!strcmp(variables[x], "servicegroup")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			if((servicegroup_name = (char *)strdup(variables[x])) == NULL)
				servicegroup_name = "";
			strip_html_brackets(servicegroup_name);
			}

		/* we got the persistence option for a comment */
		else if(!strcmp(variables[x], "persistent"))
			persistent_comment = TRUE;

		/* we got the notification option for an acknowledgement */
		else if(!strcmp(variables[x], "send_notification"))
			send_notification = TRUE;

		/* we got the acknowledgement type */
		else if(!strcmp(variables[x], "sticky_ack"))
			sticky_ack = TRUE;

		/* we got the service check force option */
		else if(!strcmp(variables[x], "force_check"))
			force_check = TRUE;

		/* we got the option to affect host and all its services */
		else if(!strcmp(variables[x], "ahas"))
			affect_host_and_services = TRUE;

		/* we got the option to propagate to child hosts */
		else if(!strcmp(variables[x], "ptc"))
			propagate_to_children = TRUE;

		/* we got the option for fixed downtime */
		else if(!strcmp(variables[x], "fixed")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			fixed = (atoi(variables[x]) > 0) ? TRUE : FALSE;
			}

		/* we got the triggered by downtime option */
		else if(!strcmp(variables[x], "trigger")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			triggered_by = strtoul(variables[x], NULL, 10);
			}

		/* we got the child options */
		else if(!strcmp(variables[x], "childoptions")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			child_options = atoi(variables[x]);
			}

		/* we found the plugin output */
		else if(!strcmp(variables[x], "plugin_output")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			/* protect against buffer overflows */
			if(strlen(variables[x]) >= MAX_INPUT_BUFFER - 1) {
				error = TRUE;
				break;
				}
			else
				strcpy(plugin_output, variables[x]);
			}

		/* we found the performance data */
		else if(!strcmp(variables[x], "performance_data")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			/* protect against buffer overflows */
			if(strlen(variables[x]) >= MAX_INPUT_BUFFER - 1) {
				error = TRUE;
				break;
				}
			else
				strcpy(performance_data, variables[x]);
			}

		/* we found the plugin state */
		else if(!strcmp(variables[x], "plugin_state")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			plugin_state = atoi(variables[x]);
			}

		/* we found the hour duration */
		else if(!strcmp(variables[x], "hours")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			if(atoi(variables[x]) < 0) {
				error = TRUE;
				break;
				}
			duration += (unsigned long)(atoi(variables[x]) * 3600);
			}

		/* we found the minute duration */
		else if(!strcmp(variables[x], "minutes")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			if(atoi(variables[x]) < 0) {
				error = TRUE;
				break;
				}
			duration += (unsigned long)(atoi(variables[x]) * 60);
			}

		/* we found the start time */
		else if(!strcmp(variables[x], "start_time")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			start_time_string = (char *)malloc(strlen(variables[x]) + 1);
			if(start_time_string == NULL)
				start_time_string = "";
			else
				strcpy(start_time_string, variables[x]);
			}

		/* we found the end time */
		else if(!strcmp(variables[x], "end_time")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			end_time_string = (char *)malloc(strlen(variables[x]) + 1);
			if(end_time_string == NULL)
				end_time_string = "";
			else
				strcpy(end_time_string, variables[x]);
			}

		/* we found the content type argument */
		else if(!strcmp(variables[x], "content")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}
			if(!strcmp(variables[x], "wml")) {
				content_type = WML_CONTENT;
				display_header = FALSE;
				}
			else
				content_type = HTML_CONTENT;
			}

		/* we found the forced notification option */
		else if(!strcmp(variables[x], "force_notification"))
			force_notification = NOTIFICATION_OPTION_FORCED;

		/* we found the broadcast notification option */
		else if(!strcmp(variables[x], "broadcast_notification"))
			broadcast_notification = NOTIFICATION_OPTION_BROADCAST;

		}

	/* free memory allocated to the CGI variables */
	free_cgivars(variables);

	return error;
	}



void request_command_data(int cmd) {
	time_t t;
	char start_time_str[MAX_DATETIME_LENGTH];
	char buffer[MAX_INPUT_BUFFER];
	contact *temp_contact;
	scheduled_downtime *temp_downtime;


	/* get default name to use for comment author */
	temp_contact = find_contact(current_authdata.username);
	if(temp_contact != NULL && temp_contact->alias != NULL)
		comment_author = temp_contact->alias;
	else
		comment_author = current_authdata.username;


	printf("<P><DIV ALIGN=CENTER CLASS='cmdType'>你正在请求:");

	switch(cmd) {

		case CMD_ADD_HOST_COMMENT:
		case CMD_ADD_SVC_COMMENT:
			printf("增加%s注释", (cmd == CMD_ADD_HOST_COMMENT) ? "主机" : "服务");
			break;

		case CMD_DEL_HOST_COMMENT:
		case CMD_DEL_SVC_COMMENT:
			printf("删除%s注释", (cmd == CMD_DEL_HOST_COMMENT) ? "主机" : "服务");
			break;

		case CMD_DELAY_HOST_NOTIFICATION:
		case CMD_DELAY_SVC_NOTIFICATION:
			printf("延时%s通知", (cmd == CMD_DELAY_HOST_NOTIFICATION) ? "主机" : "服务");
			break;

		case CMD_SCHEDULE_SVC_CHECK:
			printf("检查服务列表");
			break;

		case CMD_ENABLE_SVC_CHECK:
		case CMD_DISABLE_SVC_CHECK:
			printf("%s主动检测指定服务", (cmd == CMD_ENABLE_SVC_CHECK) ? "启用":"禁用");
			break;

		case CMD_ENABLE_NOTIFICATIONS:
		case CMD_DISABLE_NOTIFICATIONS:
			printf("%s通知", (cmd == CMD_ENABLE_NOTIFICATIONS) ? "启用":"禁用");
			break;

		case CMD_SHUTDOWN_PROCESS:
		case CMD_RESTART_PROCESS:
			printf("%sNagios进程", (cmd == CMD_SHUTDOWN_PROCESS) ? "关闭":"重启");
			break;

		case CMD_ENABLE_HOST_SVC_CHECKS:
		case CMD_DISABLE_HOST_SVC_CHECKS:
			printf("%s主机所有服务主动检查", (cmd == CMD_ENABLE_HOST_SVC_CHECKS) ? "启用":"禁用");
			break;

		case CMD_SCHEDULE_HOST_SVC_CHECKS:
			printf("主机上所有的服务执行一次检查");
			break;

		case CMD_DEL_ALL_HOST_COMMENTS:
		case CMD_DEL_ALL_SVC_COMMENTS:
			printf("删除%s所有的注释", (cmd == CMD_DEL_ALL_HOST_COMMENTS) ? "主机" : "服务");
			break;

		case CMD_ENABLE_SVC_NOTIFICATIONS:
		case CMD_DISABLE_SVC_NOTIFICATIONS:
			printf("%s服务通知", (cmd == CMD_ENABLE_SVC_NOTIFICATIONS) ? "启用":"禁用");
			break;

		case CMD_ENABLE_HOST_NOTIFICATIONS:
		case CMD_DISABLE_HOST_NOTIFICATIONS:
			printf("%s主机通知", (cmd == CMD_ENABLE_HOST_NOTIFICATIONS) ? "启用":"禁用");
			break;

		case CMD_ENABLE_ALL_NOTIFICATIONS_BEYOND_HOST:
		case CMD_DISABLE_ALL_NOTIFICATIONS_BEYOND_HOST:
			printf("%s所有主机和服务的通知", (cmd == CMD_ENABLE_ALL_NOTIFICATIONS_BEYOND_HOST) ? "启用":"禁用");
			break;

		case CMD_ENABLE_HOST_SVC_NOTIFICATIONS:
		case CMD_DISABLE_HOST_SVC_NOTIFICATIONS:
			printf("%s主机所有服务的通知", (cmd == CMD_ENABLE_HOST_SVC_NOTIFICATIONS) ? "启用":"禁用");
			break;

		case CMD_ACKNOWLEDGE_HOST_PROBLEM:
		case CMD_ACKNOWLEDGE_SVC_PROBLEM:
			printf("%s 的问题确认", (cmd == CMD_ACKNOWLEDGE_HOST_PROBLEM) ? "主机" : "服务");
			break;

		case CMD_START_EXECUTING_SVC_CHECKS:
		case CMD_STOP_EXECUTING_SVC_CHECKS:
			printf("%s执行主动服务检查", (cmd == CMD_START_EXECUTING_SVC_CHECKS) ? "开始":"停止");
			break;

		case CMD_START_ACCEPTING_PASSIVE_SVC_CHECKS:
		case CMD_STOP_ACCEPTING_PASSIVE_SVC_CHECKS:
			printf("%s接受被动服务检查", (cmd == CMD_START_ACCEPTING_PASSIVE_SVC_CHECKS) ? "开始":"停止");
			break;

		case CMD_ENABLE_PASSIVE_SVC_CHECKS:
		case CMD_DISABLE_PASSIVE_SVC_CHECKS:
			printf("%s 接受指定服务的被动检查", (cmd == CMD_ENABLE_PASSIVE_SVC_CHECKS) ? "开始":"停止");
			break;

		case CMD_ENABLE_EVENT_HANDLERS:
		case CMD_DISABLE_EVENT_HANDLERS:
			printf("%s事件处理程序", (cmd == CMD_ENABLE_EVENT_HANDLERS) ? "启用":"禁用");
			break;

		case CMD_ENABLE_HOST_EVENT_HANDLER:
		case CMD_DISABLE_HOST_EVENT_HANDLER:
			printf("%s指定主机的事件处理程序", (cmd == CMD_ENABLE_HOST_EVENT_HANDLER) ? "启用":"禁用");
			break;

		case CMD_ENABLE_SVC_EVENT_HANDLER:
		case CMD_DISABLE_SVC_EVENT_HANDLER:
			printf("%s指定服务的事件处理程序", (cmd == CMD_ENABLE_SVC_EVENT_HANDLER) ? "启用":"禁用");
			break;

		case CMD_ENABLE_HOST_CHECK:
		case CMD_DISABLE_HOST_CHECK:
			printf("%s指定主机的主动检查", (cmd == CMD_ENABLE_HOST_CHECK) ? "启用":"禁用");
			break;

		case CMD_STOP_OBSESSING_OVER_SVC_CHECKS:
		case CMD_START_OBSESSING_OVER_SVC_CHECKS:
			printf("%s Obsessing Over 服务检查", (cmd == CMD_STOP_OBSESSING_OVER_SVC_CHECKS) ? "stop" : "start");
			break;

		case CMD_REMOVE_HOST_ACKNOWLEDGEMENT:
		case CMD_REMOVE_SVC_ACKNOWLEDGEMENT:
			printf("删除%s确认", (cmd == CMD_REMOVE_HOST_ACKNOWLEDGEMENT) ? "主机" : "服务");
			break;

		case CMD_SCHEDULE_HOST_DOWNTIME:
		case CMD_SCHEDULE_SVC_DOWNTIME:
			printf("指定%s的宕机时间调度", (cmd == CMD_SCHEDULE_HOST_DOWNTIME) ? "主机" : "服务");
			break;

		case CMD_SCHEDULE_HOST_SVC_DOWNTIME:
			printf("指定主机上所有服务宕机时间调度");
			break;

		case CMD_PROCESS_HOST_CHECK_RESULT:
		case CMD_PROCESS_SERVICE_CHECK_RESULT:
			printf("提交指定%s的被动检查结果", (cmd == CMD_PROCESS_HOST_CHECK_RESULT) ? "主机" : "服务");
			break;

		case CMD_ENABLE_HOST_FLAP_DETECTION:
		case CMD_DISABLE_HOST_FLAP_DETECTION:
			printf("%s指定主机的心跳检查", (cmd == CMD_ENABLE_HOST_FLAP_DETECTION) ? "启用":"禁用");
			break;

		case CMD_ENABLE_SVC_FLAP_DETECTION:
		case CMD_DISABLE_SVC_FLAP_DETECTION:
			printf("%s指定服务的心跳检查", (cmd == CMD_ENABLE_SVC_FLAP_DETECTION) ? "启用":"禁用");
			break;

		case CMD_ENABLE_FLAP_DETECTION:
		case CMD_DISABLE_FLAP_DETECTION:
			printf("%s主机和服务的心跳检查", (cmd == CMD_ENABLE_FLAP_DETECTION) ? "启用":"禁用");
			break;

		case CMD_ENABLE_HOSTGROUP_SVC_NOTIFICATIONS:
		case CMD_DISABLE_HOSTGROUP_SVC_NOTIFICATIONS:
			printf("%s指定主机组所有服务的通知", (cmd == CMD_ENABLE_HOSTGROUP_SVC_NOTIFICATIONS) ? "启用":"禁用");
			break;

		case CMD_ENABLE_HOSTGROUP_HOST_NOTIFICATIONS:
		case CMD_DISABLE_HOSTGROUP_HOST_NOTIFICATIONS:
			printf("%s指定主机组所有主机的通知", (cmd == CMD_ENABLE_HOSTGROUP_HOST_NOTIFICATIONS) ? "启用":"禁用");
			break;

		case CMD_ENABLE_HOSTGROUP_SVC_CHECKS:
		case CMD_DISABLE_HOSTGROUP_SVC_CHECKS:
			printf("%s指定主机组所有服务的主动检查", (cmd == CMD_ENABLE_HOSTGROUP_SVC_CHECKS) ? "启用":"禁用");
			break;

		case CMD_DEL_HOST_DOWNTIME:
		case CMD_DEL_SVC_DOWNTIME:
			printf("取消指定%s的宕机时间调度", (cmd == CMD_DEL_HOST_DOWNTIME) ? "主机" : "服务");
			break;

		case CMD_ENABLE_PERFORMANCE_DATA:
		case CMD_DISABLE_PERFORMANCE_DATA:
			printf("%s主机和服务的性能数据处理", (cmd == CMD_ENABLE_PERFORMANCE_DATA) ? "启用":"禁用");
			break;

		case CMD_SCHEDULE_HOSTGROUP_HOST_DOWNTIME:
			printf("指定主机组所有主机的宕机时间调度");
			break;

		case CMD_SCHEDULE_HOSTGROUP_SVC_DOWNTIME:
			printf("指定主机组所有服务的宕机时间调度");
			break;

		case CMD_START_EXECUTING_HOST_CHECKS:
		case CMD_STOP_EXECUTING_HOST_CHECKS:
			printf("%s执行主机检查", (cmd == CMD_START_EXECUTING_HOST_CHECKS) ? "开始":"停止");
			break;

		case CMD_START_ACCEPTING_PASSIVE_HOST_CHECKS:
		case CMD_STOP_ACCEPTING_PASSIVE_HOST_CHECKS:
			printf("%s接受主机被动检查", (cmd == CMD_START_ACCEPTING_PASSIVE_HOST_CHECKS) ? "开始":"停止");
			break;

		case CMD_ENABLE_PASSIVE_HOST_CHECKS:
		case CMD_DISABLE_PASSIVE_HOST_CHECKS:
			printf("%s接受指定主机被动检查", (cmd == CMD_ENABLE_PASSIVE_HOST_CHECKS) ? "开始":"停止");
			break;

		case CMD_START_OBSESSING_OVER_HOST_CHECKS:
		case CMD_STOP_OBSESSING_OVER_HOST_CHECKS:
			printf("%s Obsessing Over 主机检查", (cmd == CMD_START_OBSESSING_OVER_HOST_CHECKS) ? "开始":"停止");
			break;

		case CMD_SCHEDULE_HOST_CHECK:
			printf("主机检查调度");
			break;

		case CMD_START_OBSESSING_OVER_SVC:
		case CMD_STOP_OBSESSING_OVER_SVC:
			printf("%s Obsessing Over 指定服务", (cmd == CMD_START_OBSESSING_OVER_SVC) ? "开始":"停止");
			break;

		case CMD_START_OBSESSING_OVER_HOST:
		case CMD_STOP_OBSESSING_OVER_HOST:
			printf("%s Obsessing Over 指定主机", (cmd == CMD_START_OBSESSING_OVER_HOST) ? "开始":"停止");
			break;

		case CMD_ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS:
		case CMD_DISABLE_SERVICEGROUP_SVC_NOTIFICATIONS:
			printf("%s指定服务组所有服务的通知", (cmd == CMD_ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS) ? "启用":"禁用");
			break;

		case CMD_ENABLE_SERVICEGROUP_HOST_NOTIFICATIONS:
		case CMD_DISABLE_SERVICEGROUP_HOST_NOTIFICATIONS:
			printf("%s指定服务组所有主机的通知", (cmd == CMD_ENABLE_SERVICEGROUP_HOST_NOTIFICATIONS) ? "启用":"禁用");
			break;

		case CMD_ENABLE_SERVICEGROUP_SVC_CHECKS:
		case CMD_DISABLE_SERVICEGROUP_SVC_CHECKS:
			printf("%s指定服务组所有服务主动检查", (cmd == CMD_ENABLE_SERVICEGROUP_SVC_CHECKS) ? "启用":"禁用");
			break;

		case CMD_SCHEDULE_SERVICEGROUP_HOST_DOWNTIME:
			printf("安排服务组上所有主机的宕机时间调度");
			break;

		case CMD_SCHEDULE_SERVICEGROUP_SVC_DOWNTIME:
			printf("安排服务组上所有服务的宕机时间调度");
			break;

		case CMD_SEND_CUSTOM_HOST_NOTIFICATION:
		case CMD_SEND_CUSTOM_SVC_NOTIFICATION:
			printf("发送一个自定义\"%s\"通知", (cmd == CMD_SEND_CUSTOM_HOST_NOTIFICATION) ? "主机" : "服务");
			break;

		default:
			printf("未知命令，会有彩蛋么？</DIV>");
			return;
		}

	printf("</DIV></p>\n");

	printf("<p>\n");
	printf("<div align='center'>\n");

	printf("<table border=0 width=90%%>\n");
	printf("<tr>\n");
	printf("<td align=center valign=top>\n");

	printf("<DIV ALIGN=CENTER CLASS='optBoxTitle'>命令选项</DIV>\n");

	printf("<TABLE CELLSPACING=0 CELLPADDING=0 BORDER=1 CLASS='optBox'>\n");
	printf("<TR><TD CLASS='optBoxItem'>\n");
	printf("<form method='post' action='%s'>\n", COMMAND_CGI);
	printf("<TABLE CELLSPACING=0 CELLPADDING=0 CLASS='optBox'>\n");

	printf("<tr><td><INPUT TYPE='HIDDEN' NAME='cmd_typ' VALUE='%d'><INPUT TYPE='HIDDEN' NAME='cmd_mod' VALUE='%d'></td></tr>\n", cmd, CMDMODE_COMMIT);

	switch(cmd) {

		case CMD_ADD_HOST_COMMENT:
		case CMD_ACKNOWLEDGE_HOST_PROBLEM:
			printf("<tr><td CLASS='optBoxRequiredItem'>主机名:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='host' VALUE='%s'>", escape_string(host_name));
			printf("</b></td></tr>\n");
			if(cmd == CMD_ACKNOWLEDGE_HOST_PROBLEM) {
				printf("<tr><td CLASS='optBoxItem'>保持确认:</td><td><b>");
				printf("<INPUT TYPE='checkbox' NAME='sticky_ack' CHECKED>");
				printf("</b></td></tr>\n");
				printf("<tr><td CLASS='optBoxItem'>发送通知:</td><td><b>");
				printf("<INPUT TYPE='checkbox' NAME='send_notification' CHECKED>");
				printf("</b></td></tr>\n");
				}
			printf("<tr><td CLASS='optBoxItem'>保持%s:</td><td><b>", (cmd == CMD_ACKNOWLEDGE_HOST_PROBLEM) ? "注释":"");
			printf("<INPUT TYPE='checkbox' NAME='persistent' %s>", (cmd == CMD_ACKNOWLEDGE_HOST_PROBLEM) ? "" : "CHECKED");
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxRequiredItem'>作者:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='com_author' VALUE='%s' %s>", escape_string(comment_author), (lock_author_names == TRUE) ? "READONLY DISABLED" : "");
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxRequiredItem'>注释:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='com_data' VALUE='%s' SIZE=40>", escape_string(comment_data));
			printf("</b></td></tr>\n");
			break;

		case CMD_ADD_SVC_COMMENT:
		case CMD_ACKNOWLEDGE_SVC_PROBLEM:
			printf("<tr><td CLASS='optBoxRequiredItem'>主机名:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='host' VALUE='%s'>", escape_string(host_name));
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxRequiredItem'>服务:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='service' VALUE='%s'>", escape_string(service_desc));
			if(cmd == CMD_ACKNOWLEDGE_SVC_PROBLEM) {
				printf("<tr><td CLASS='optBoxItem'>保持确认:</td><td><b>");
				printf("<INPUT TYPE='checkbox' NAME='sticky_ack' CHECKED>");
				printf("</b></td></tr>\n");
				printf("<tr><td CLASS='optBoxItem'>发送通知:</td><td><b>");
				printf("<INPUT TYPE='checkbox' NAME='send_notification' CHECKED>");
				printf("</b></td></tr>\n");
				}
			printf("<tr><td CLASS='optBoxItem'>保持%s:</td><td><b>", (cmd == CMD_ACKNOWLEDGE_SVC_PROBLEM) ? "注释":"");
			printf("<INPUT TYPE='checkbox' NAME='persistent' %s>", (cmd == CMD_ACKNOWLEDGE_SVC_PROBLEM) ? "" : "CHECKED");
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxRequiredItem'>作者:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='com_author' VALUE='%s' %s>", escape_string(comment_author), (lock_author_names == TRUE) ? "READONLY DISABLED" : "");
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxRequiredItem'>注释:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='com_data' VALUE='%s' SIZE=40>", escape_string(comment_data));
			printf("</b></td></tr>\n");
			break;

		case CMD_DEL_HOST_COMMENT:
		case CMD_DEL_SVC_COMMENT:
			printf("<tr><td CLASS='optBoxRequiredItem'>注释ID:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='com_id' VALUE='%lu'>", comment_id);
			printf("</b></td></tr>\n");
			break;

		case CMD_DELAY_HOST_NOTIFICATION:
			printf("<tr><td CLASS='optBoxRequiredItem'>主机名:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='host' VALUE='%s'>", escape_string(host_name));
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxRequiredItem'>通知延时(从现在开始的分钟数):</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='not_dly' VALUE='%d'>", notification_delay);
			printf("</b></td></tr>\n");
			break;

		case CMD_DELAY_SVC_NOTIFICATION:
			printf("<tr><td CLASS='optBoxRequiredItem'>主机名:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='host' VALUE='%s'>", escape_string(host_name));
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxRequiredItem'>服务:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='service' VALUE='%s'>", escape_string(service_desc));
			printf("<tr><td CLASS='optBoxRequiredItem'>通知延时(从现在开始的分钟数):</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='not_dly' VALUE='%d'>", notification_delay);
			printf("</b></td></tr>\n");
			break;

		case CMD_SCHEDULE_SVC_CHECK:
		case CMD_SCHEDULE_HOST_CHECK:
		case CMD_SCHEDULE_HOST_SVC_CHECKS:
			printf("<tr><td CLASS='optBoxRequiredItem'>主机名:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='host' VALUE='%s'>", escape_string(host_name));
			printf("</b></td></tr>\n");
			if(cmd == CMD_SCHEDULE_SVC_CHECK) {
				printf("<tr><td CLASS='optBoxRequiredItem'>服务:</td><td><b>");
				printf("<INPUT TYPE='TEXT' NAME='service' VALUE='%s'>", escape_string(service_desc));
				printf("</b></td></tr>\n");
				}
			time(&t);
			get_time_string(&t, buffer, sizeof(buffer) - 1, SHORT_DATE_TIME);
			printf("<tr><td CLASS='optBoxRequiredItem'>检查时间:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='start_time' VALUE='%s'>", buffer);
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxItem'>强制检查:</td><td><b>");
			printf("<INPUT TYPE='checkbox' NAME='force_check' %s>", (force_check == TRUE) ? "CHECKED" : "");
			printf("</b></td></tr>\n");
			break;

		case CMD_ENABLE_SVC_CHECK:
		case CMD_DISABLE_SVC_CHECK:
		case CMD_DEL_ALL_SVC_COMMENTS:
		case CMD_ENABLE_SVC_NOTIFICATIONS:
		case CMD_DISABLE_SVC_NOTIFICATIONS:
		case CMD_ENABLE_PASSIVE_SVC_CHECKS:
		case CMD_DISABLE_PASSIVE_SVC_CHECKS:
		case CMD_ENABLE_SVC_EVENT_HANDLER:
		case CMD_DISABLE_SVC_EVENT_HANDLER:
		case CMD_REMOVE_SVC_ACKNOWLEDGEMENT:
		case CMD_ENABLE_SVC_FLAP_DETECTION:
		case CMD_DISABLE_SVC_FLAP_DETECTION:
		case CMD_START_OBSESSING_OVER_SVC:
		case CMD_STOP_OBSESSING_OVER_SVC:
			printf("<tr><td CLASS='optBoxRequiredItem'>主机名:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='host' VALUE='%s'>", escape_string(host_name));
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxRequiredItem'>服务:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='service' VALUE='%s'>", escape_string(service_desc));
			printf("</b></td></tr>\n");
			break;

		case CMD_ENABLE_HOST_SVC_CHECKS:
		case CMD_DISABLE_HOST_SVC_CHECKS:
		case CMD_DEL_ALL_HOST_COMMENTS:
		case CMD_ENABLE_HOST_NOTIFICATIONS:
		case CMD_DISABLE_HOST_NOTIFICATIONS:
		case CMD_ENABLE_ALL_NOTIFICATIONS_BEYOND_HOST:
		case CMD_DISABLE_ALL_NOTIFICATIONS_BEYOND_HOST:
		case CMD_ENABLE_HOST_SVC_NOTIFICATIONS:
		case CMD_DISABLE_HOST_SVC_NOTIFICATIONS:
		case CMD_ENABLE_HOST_EVENT_HANDLER:
		case CMD_DISABLE_HOST_EVENT_HANDLER:
		case CMD_ENABLE_HOST_CHECK:
		case CMD_DISABLE_HOST_CHECK:
		case CMD_REMOVE_HOST_ACKNOWLEDGEMENT:
		case CMD_ENABLE_HOST_FLAP_DETECTION:
		case CMD_DISABLE_HOST_FLAP_DETECTION:
		case CMD_ENABLE_PASSIVE_HOST_CHECKS:
		case CMD_DISABLE_PASSIVE_HOST_CHECKS:
		case CMD_START_OBSESSING_OVER_HOST:
		case CMD_STOP_OBSESSING_OVER_HOST:
			printf("<tr><td CLASS='optBoxRequiredItem'>主机名:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='host' VALUE='%s'>", escape_string(host_name));
			printf("</b></td></tr>\n");
			if(cmd == CMD_ENABLE_HOST_SVC_CHECKS || cmd == CMD_DISABLE_HOST_SVC_CHECKS || cmd == CMD_ENABLE_HOST_SVC_NOTIFICATIONS || cmd == CMD_DISABLE_HOST_SVC_NOTIFICATIONS) {
				printf("<tr><td CLASS='optBoxItem'>同时对主机%s:</td><td><b>", (cmd == CMD_ENABLE_HOST_SVC_CHECKS || cmd == CMD_ENABLE_HOST_SVC_NOTIFICATIONS) ? "启用":"禁用");
				printf("<INPUT TYPE='checkbox' NAME='ahas'>");
				printf("</b></td></tr>\n");
				}
			if(cmd == CMD_ENABLE_HOST_NOTIFICATIONS || cmd == CMD_DISABLE_HOST_NOTIFICATIONS) {
				printf("<tr><td CLASS='optBoxItem'>同时对子节点主机的通知%s:</td><td><b>", (cmd == CMD_ENABLE_HOST_NOTIFICATIONS) ? "启用":"禁用");
				printf("<INPUT TYPE='checkbox' NAME='ptc'>");
				printf("</b></td></tr>\n");
				}
			break;

		case CMD_ENABLE_NOTIFICATIONS:
		case CMD_DISABLE_NOTIFICATIONS:
		case CMD_SHUTDOWN_PROCESS:
		case CMD_RESTART_PROCESS:
		case CMD_START_EXECUTING_SVC_CHECKS:
		case CMD_STOP_EXECUTING_SVC_CHECKS:
		case CMD_START_ACCEPTING_PASSIVE_SVC_CHECKS:
		case CMD_STOP_ACCEPTING_PASSIVE_SVC_CHECKS:
		case CMD_ENABLE_EVENT_HANDLERS:
		case CMD_DISABLE_EVENT_HANDLERS:
		case CMD_START_OBSESSING_OVER_SVC_CHECKS:
		case CMD_STOP_OBSESSING_OVER_SVC_CHECKS:
		case CMD_ENABLE_FLAP_DETECTION:
		case CMD_DISABLE_FLAP_DETECTION:
		case CMD_ENABLE_PERFORMANCE_DATA:
		case CMD_DISABLE_PERFORMANCE_DATA:
		case CMD_START_EXECUTING_HOST_CHECKS:
		case CMD_STOP_EXECUTING_HOST_CHECKS:
		case CMD_START_ACCEPTING_PASSIVE_HOST_CHECKS:
		case CMD_STOP_ACCEPTING_PASSIVE_HOST_CHECKS:
		case CMD_START_OBSESSING_OVER_HOST_CHECKS:
		case CMD_STOP_OBSESSING_OVER_HOST_CHECKS:
			printf("<tr><td CLASS='optBoxItem' colspan=2>此命令无选项。<BR>点击\"确定\"按钮提交该命令</td></tr>");
			break;

		case CMD_PROCESS_HOST_CHECK_RESULT:
		case CMD_PROCESS_SERVICE_CHECK_RESULT:
			printf("<tr><td CLASS='optBoxRequiredItem'>主机名:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='host' VALUE='%s'>", escape_string(host_name));
			printf("</b></td></tr>\n");
			if(cmd == CMD_PROCESS_SERVICE_CHECK_RESULT) {
				printf("<tr><td CLASS='optBoxRequiredItem'>服务:</td><td><b>");
				printf("<INPUT TYPE='TEXT' NAME='service' VALUE='%s'>", escape_string(service_desc));
				printf("</b></td></tr>\n");
				}
			printf("<tr><td CLASS='optBoxRequiredItem'>检查结果:</td><td><b>");
			printf("<SELECT NAME='plugin_state'>");
			if(cmd == CMD_PROCESS_SERVICE_CHECK_RESULT) {
				printf("<OPTION VALUE=%d SELECTED>正常\n", STATE_OK);
				printf("<OPTION VALUE=%d>警告\n", STATE_WARNING);
				printf("<OPTION VALUE=%d>未知\n", STATE_UNKNOWN);
				printf("<OPTION VALUE=%d>紧急\n", STATE_CRITICAL);
				}
			else {
				printf("<OPTION VALUE=0 SELECTED>运行\n");
				printf("<OPTION VALUE=1>宕机\n");
				printf("<OPTION VALUE=2>不可达\n");
				}
			printf("</SELECT>\n");
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxRequiredItem'>检查输出:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='plugin_output' VALUE=''>");
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxItem'>性能数据:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='performance_data' VALUE=''>");
			printf("</b></td></tr>\n");
			break;

		case CMD_SCHEDULE_HOST_DOWNTIME:
		case CMD_SCHEDULE_HOST_SVC_DOWNTIME:
		case CMD_SCHEDULE_SVC_DOWNTIME:

			printf("<tr><td CLASS='optBoxRequiredItem'>主机名:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='host' VALUE='%s'>", escape_string(host_name));
			printf("</b></td></tr>\n");
			if(cmd == CMD_SCHEDULE_SVC_DOWNTIME) {
				printf("<tr><td CLASS='optBoxRequiredItem'>服务:</td><td><b>");
				printf("<INPUT TYPE='TEXT' NAME='service' VALUE='%s'>", escape_string(service_desc));
				}
			printf("<tr><td CLASS='optBoxRequiredItem'>作者:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='com_author' VALUE='%s' %s>", escape_string(comment_author), (lock_author_names == TRUE) ? "READONLY DISABLED" : "");
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxRequiredItem'>注释:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='com_data' VALUE='%s' SIZE=40>", escape_string(comment_data));
			printf("</b></td></tr>\n");

			printf("<tr><td CLASS='optBoxItem'><br></td></tr>\n");

			printf("<tr><td CLASS='optBoxItem'>触发自:</td><td>\n");
			printf("<select name='trigger'>\n");
			printf("<option value='0'>N/A\n");

			for(temp_downtime = scheduled_downtime_list; temp_downtime != NULL; temp_downtime = temp_downtime->next) {
				if(temp_downtime->type != HOST_DOWNTIME)
					continue;
				printf("<option value='%lu'>", temp_downtime->downtime_id);
				get_time_string(&temp_downtime->start_time, start_time_str, sizeof(start_time_str), SHORT_DATE_TIME);
				printf("ID: %lu, 主机 '%s' 运行 @ %s\n", temp_downtime->downtime_id, temp_downtime->host_name, start_time_str);
				}
			for(temp_downtime = scheduled_downtime_list; temp_downtime != NULL; temp_downtime = temp_downtime->next) {
				if(temp_downtime->type != SERVICE_DOWNTIME)
					continue;
				printf("<option value='%lu'>", temp_downtime->downtime_id);
				get_time_string(&temp_downtime->start_time, start_time_str, sizeof(start_time_str), SHORT_DATE_TIME);
				printf("ID: %lu, 服务 '%s' 在主机'%s' 运行 @ %s\n", temp_downtime->downtime_id, temp_downtime->service_description, temp_downtime->host_name, start_time_str);
				}

			printf("</select>\n");
			printf("</td></tr>\n");

			printf("<tr><td CLASS='optBoxItem'><br></td></tr>\n");

			time(&t);
			get_time_string(&t, buffer, sizeof(buffer) - 1, SHORT_DATE_TIME);
			printf("<tr><td CLASS='optBoxRequiredItem'>开始时间:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='start_time' VALUE='%s'>", buffer);
			printf("</b></td></tr>\n");
			t += (unsigned long)7200;
			get_time_string(&t, buffer, sizeof(buffer) - 1, SHORT_DATE_TIME);
			printf("<tr><td CLASS='optBoxRequiredItem'>结束时间:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='end_time' VALUE='%s'>", buffer);
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxItem'>类型:</td><td><b>");
			printf("<SELECT NAME='fixed'>");
			printf("<OPTION VALUE=1>固定\n");
			printf("<OPTION VALUE=0>可变\n");
			printf("</SELECT>\n");
			printf("</b></td></tr>\n");

			printf("<tr><td CLASS='optBoxItem'>如果是可变的持续时间:</td><td>");
			printf("<table border=0><tr>\n");
			printf("<td align=right><INPUT TYPE='TEXT' NAME='hours' VALUE='2' SIZE=2 MAXLENGTH=2></td>\n");
			printf("<td align=left>小时</td>\n");
			printf("<td align=right><INPUT TYPE='TEXT' NAME='minutes' VALUE='0' SIZE=2 MAXLENGTH=2></td>\n");
			printf("<td align=left>分钟</td>\n");
			printf("</tr></table>\n");
			printf("</td></tr>\n");

			printf("<tr><td CLASS='optBoxItem'><br></td></tr>\n");

			if(cmd == CMD_SCHEDULE_HOST_DOWNTIME) {
				printf("<tr><td CLASS='optBoxItem'>子节点主机:</td><td><b>");
				printf("<SELECT name='childoptions'>");
				printf("<option value='0'>子节点主机不受任何影响\n");
				printf("<option value='1'>安排所有子节点主机触发宕机时间调度\n");
				printf("<option value='2'>安排所有子节点主机停止触发宕机时间调度\n");
				printf("</SELECT>\n");
				printf("</b></td></tr>\n");
				}

			printf("<tr><td CLASS='optBoxItem'><br></td></tr>\n");

			break;

		case CMD_ENABLE_HOSTGROUP_SVC_NOTIFICATIONS:
		case CMD_DISABLE_HOSTGROUP_SVC_NOTIFICATIONS:
		case CMD_ENABLE_HOSTGROUP_HOST_NOTIFICATIONS:
		case CMD_DISABLE_HOSTGROUP_HOST_NOTIFICATIONS:
		case CMD_ENABLE_HOSTGROUP_SVC_CHECKS:
		case CMD_DISABLE_HOSTGROUP_SVC_CHECKS:
			printf("<tr><td CLASS='optBoxRequiredItem'>主机组名:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='hostgroup' VALUE='%s'>", escape_string(hostgroup_name));
			printf("</b></td></tr>\n");
			if(cmd == CMD_ENABLE_HOSTGROUP_SVC_CHECKS || cmd == CMD_DISABLE_HOSTGROUP_SVC_CHECKS || cmd == CMD_ENABLE_HOSTGROUP_SVC_NOTIFICATIONS || cmd == CMD_DISABLE_HOSTGROUP_SVC_NOTIFICATIONS) {
				printf("<tr><td CLASS='optBoxItem'>同时对主机%s:</td><td><b>", (cmd == CMD_ENABLE_HOSTGROUP_SVC_CHECKS || cmd == CMD_ENABLE_HOSTGROUP_SVC_NOTIFICATIONS) ? "启用":"禁用");
				printf("<INPUT TYPE='checkbox' NAME='ahas'>");
				printf("</b></td></tr>\n");
				}
			break;

		case CMD_ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS:
		case CMD_DISABLE_SERVICEGROUP_SVC_NOTIFICATIONS:
		case CMD_ENABLE_SERVICEGROUP_HOST_NOTIFICATIONS:
		case CMD_DISABLE_SERVICEGROUP_HOST_NOTIFICATIONS:
		case CMD_ENABLE_SERVICEGROUP_SVC_CHECKS:
		case CMD_DISABLE_SERVICEGROUP_SVC_CHECKS:
			printf("<tr><td CLASS='optBoxRequiredItem'>服务组名:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='servicegroup' VALUE='%s'>", escape_string(servicegroup_name));
			printf("</b></td></tr>\n");
			if(cmd == CMD_ENABLE_SERVICEGROUP_SVC_CHECKS || cmd == CMD_DISABLE_SERVICEGROUP_SVC_CHECKS || cmd == CMD_ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS || cmd == CMD_DISABLE_SERVICEGROUP_SVC_NOTIFICATIONS) {
				printf("<tr><td CLASS='optBoxItem'>同时对主机%s:</td><td><b>", (cmd == CMD_ENABLE_SERVICEGROUP_SVC_CHECKS || cmd == CMD_ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS) ? "启用":"禁用");
				printf("<INPUT TYPE='checkbox' NAME='ahas'>");
				printf("</b></td></tr>\n");
				}
			break;

		case CMD_DEL_HOST_DOWNTIME:
		case CMD_DEL_SVC_DOWNTIME:
			printf("<tr><td CLASS='optBoxRequiredItem'>宕机时间调度编号:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='down_id' VALUE='%lu'>", downtime_id);
			printf("</b></td></tr>\n");
			break;


		case CMD_SCHEDULE_HOSTGROUP_HOST_DOWNTIME:
		case CMD_SCHEDULE_HOSTGROUP_SVC_DOWNTIME:
		case CMD_SCHEDULE_SERVICEGROUP_HOST_DOWNTIME:
		case CMD_SCHEDULE_SERVICEGROUP_SVC_DOWNTIME:

			if(cmd == CMD_SCHEDULE_HOSTGROUP_HOST_DOWNTIME || cmd == CMD_SCHEDULE_HOSTGROUP_SVC_DOWNTIME) {
				printf("<tr><td CLASS='optBoxRequiredItem'>主机组名:</td><td><b>");
				printf("<INPUT TYPE='TEXT' NAME='hostgroup' VALUE='%s'>", escape_string(hostgroup_name));
				printf("</b></td></tr>\n");
				}
			else {
				printf("<tr><td CLASS='optBoxRequiredItem'>服务组名:</td><td><b>");
				printf("<INPUT TYPE='TEXT' NAME='servicegroup' VALUE='%s'>", escape_string(servicegroup_name));
				printf("</b></td></tr>\n");
				}
			printf("<tr><td CLASS='optBoxRequiredItem'>作者:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='com_author' VALUE='%s' %s>", escape_string(comment_author), (lock_author_names == TRUE) ? "READONLY DISABLED" : "");
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxRequiredItem'>注释:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='com_data' VALUE='%s' SIZE=40>", escape_string(comment_data));
			printf("</b></td></tr>\n");
			time(&t);
			get_time_string(&t, buffer, sizeof(buffer) - 1, SHORT_DATE_TIME);
			printf("<tr><td CLASS='optBoxRequiredItem'>开始时间:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='start_time' VALUE='%s'>", buffer);
			printf("</b></td></tr>\n");
			t += (unsigned long)7200;
			get_time_string(&t, buffer, sizeof(buffer) - 1, SHORT_DATE_TIME);
			printf("<tr><td CLASS='optBoxRequiredItem'>结束时间:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='end_time' VALUE='%s'>", buffer);
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxItem'>类型:</td><td><b>");
			printf("<SELECT NAME='fixed'>");
			printf("<OPTION VALUE=1>固定\n");
			printf("<OPTION VALUE=0>可变\n");
			printf("</SELECT>\n");
			printf("</b></td></tr>\n");

			printf("<tr><td CLASS='optBoxItem'>如果是可变的持续时间:</td><td>");
			printf("<table border=0><tr>\n");
			printf("<td align=right><INPUT TYPE='TEXT' NAME='hours' VALUE='2' SIZE=2 MAXLENGTH=2></td>\n");
			printf("<td align=left>小时</td>\n");
			printf("<td align=right><INPUT TYPE='TEXT' NAME='minutes' VALUE='0' SIZE=2 MAXLENGTH=2></td>\n");
			printf("<td align=left>分钟</td>\n");
			printf("</tr></table>\n");
			printf("</td></tr>\n");
			if(cmd == CMD_SCHEDULE_HOSTGROUP_SVC_DOWNTIME || cmd == CMD_SCHEDULE_SERVICEGROUP_SVC_DOWNTIME) {
				printf("<tr><td CLASS='optBoxItem'>安排宕机时间调度的主机:</td><td><b>");
				printf("<INPUT TYPE='checkbox' NAME='ahas'>");
				printf("</b></td></tr>\n");
				}
			break;

		case CMD_SEND_CUSTOM_HOST_NOTIFICATION:
		case CMD_SEND_CUSTOM_SVC_NOTIFICATION:
			printf("<tr><td CLASS='optBoxRequiredItem'>主机名:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='host' VALUE='%s'>", escape_string(host_name));
			printf("</b></td></tr>\n");

			if(cmd == CMD_SEND_CUSTOM_SVC_NOTIFICATION) {
				printf("<tr><td CLASS='optBoxRequiredItem'>服务:</td><td><b>");
				printf("<INPUT TYPE='TEXT' NAME='service' VALUE='%s'>", escape_string(service_desc));
				printf("</b></td></tr>\n");
				}

			printf("<tr><td CLASS='optBoxItem'>强迫式:</td><td><b>");
			printf("<INPUT TYPE='checkbox' NAME='force_notification' ");
			printf("</b></td></tr>\n");

			printf("<tr><td CLASS='optBoxItem'>广播式:</td><td><b>");
			printf("<INPUT TYPE='checkbox' NAME='broadcast_notification' ");
			printf("</b></td></tr>\n");

			printf("<tr><td CLASS='optBoxRequiredItem'>作者:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='com_author' VALUE='%s' %s>", escape_string(comment_author), (lock_author_names == TRUE) ? "READONLY DISABLED" : "");
			printf("</b></td></tr>\n");
			printf("<tr><td CLASS='optBoxRequiredItem'>注释:</td><td><b>");
			printf("<INPUT TYPE='TEXT' NAME='com_data' VALUE='%s' SIZE=40>", escape_string(comment_data));
			printf("</b></td></tr>\n");
			break;

		default:
			printf("<tr><td CLASS='optBoxItem'>不应该发生....:-(</td><td></td></tr>\n");
		}


	printf("<tr><td CLASS='optBoxItem' COLSPAN=2></td></tr>\n");
	printf("<tr><td CLASS='optBoxItem'></td><td CLASS='optBoxItem'><INPUT TYPE='submit' NAME='btnSubmit' VALUE='确定'> <INPUT TYPE='reset' VALUE='重置'></td></tr>\n");

	printf("</table>\n");
	printf("</form>\n");
	printf("</td>\n");
	printf("</tr>\n");
	printf("</table>\n");

	printf("</td>\n");
	printf("<td align=center valign=top width=50%%>\n");

	/* show information about the command... */
	show_command_help(cmd);

	printf("</td>\n");
	printf("</tr>\n");
	printf("</table>\n");

	printf("</div>\n");
	printf("</p>\n");

	printf("<P><DIV CLASS='infoMessage'>请在提交命令前输入所有必须输入的项。<br>红色为必须输入的项。</DIV></P>");

	return;
	}


void commit_command_data(int cmd) {
	char *error_string = NULL;
	int result = OK;
	int authorized = FALSE;
	service *temp_service;
	host *temp_host;
	hostgroup *temp_hostgroup;
	comment *temp_comment;
	scheduled_downtime *temp_downtime;
	servicegroup *temp_servicegroup = NULL;
	contact *temp_contact = NULL;


	/* get authentication information */
	get_authentication_information(&current_authdata);

	/* get name to use for author */
	if(lock_author_names == TRUE) {
		temp_contact = find_contact(current_authdata.username);
		if(temp_contact != NULL && temp_contact->alias != NULL)
			comment_author = temp_contact->alias;
		else
			comment_author = current_authdata.username;
		}

	switch(cmd) {
		case CMD_ADD_HOST_COMMENT:
		case CMD_ACKNOWLEDGE_HOST_PROBLEM:

			/* make sure we have author name, and comment data... */
			if(!strcmp(comment_author, "")) {
				if(!error_string)
					error_string = strdup("没有输入作者");
				}
			if(!strcmp(comment_data, "")) {
				if(!error_string)
					error_string = strdup("没有输入注释");
				}

			/* clean up the comment data */
			clean_comment_data(comment_author);
			clean_comment_data(comment_data);

			/* see if the user is authorized to issue a command... */
			temp_host = find_host(host_name);
			if(is_authorized_for_host_commands(temp_host, &current_authdata) == TRUE)
				authorized = TRUE;
			break;

		case CMD_ADD_SVC_COMMENT:
		case CMD_ACKNOWLEDGE_SVC_PROBLEM:

			/* make sure we have author name, and comment data... */
			if(!strcmp(comment_author, "")) {
				if(!error_string)
					error_string = strdup("没有输入作者");
				}
			if(!strcmp(comment_data, "")) {
				if(!error_string)
					error_string = strdup("没有输入注释");
				}

			/* clean up the comment data */
			clean_comment_data(comment_author);
			clean_comment_data(comment_data);

			/* see if the user is authorized to issue a command... */
			temp_service = find_service(host_name, service_desc);
			if(is_authorized_for_service_commands(temp_service, &current_authdata) == TRUE)
				authorized = TRUE;
			break;

		case CMD_DEL_HOST_COMMENT:
		case CMD_DEL_SVC_COMMENT:

			/* check the sanity of the comment id */
			if(comment_id == 0) {
				if(!error_string)
					error_string = strdup("注释编号不能为0");
				}

			/* find the comment */
			if(cmd == CMD_DEL_HOST_COMMENT)
				temp_comment = find_host_comment(comment_id);
			else
				temp_comment = find_service_comment(comment_id);

			/* see if the user is authorized to issue a command... */
			if(cmd == CMD_DEL_HOST_COMMENT && temp_comment != NULL) {
				temp_host = find_host(temp_comment->host_name);
				if(is_authorized_for_host_commands(temp_host, &current_authdata) == TRUE)
					authorized = TRUE;
				}
			if(cmd == CMD_DEL_SVC_COMMENT && temp_comment != NULL) {
				temp_service = find_service(temp_comment->host_name, temp_comment->service_description);
				if(is_authorized_for_service_commands(temp_service, &current_authdata) == TRUE)
					authorized = TRUE;
				}

			/* free comment data */
			free_comment_data();

			break;

		case CMD_DEL_HOST_DOWNTIME:
		case CMD_DEL_SVC_DOWNTIME:

			/* check the sanity of the downtime id */
			if(downtime_id == 0) {
				if(!error_string)
					error_string = strdup("宕机编号不能为0");
				}

			/* find the downtime entry */
			if(cmd == CMD_DEL_HOST_DOWNTIME)
				temp_downtime = find_host_downtime(downtime_id);
			else
				temp_downtime = find_service_downtime(downtime_id);

			/* see if the user is authorized to issue a command... */
			if(cmd == CMD_DEL_HOST_DOWNTIME && temp_downtime != NULL) {
				temp_host = find_host(temp_downtime->host_name);
				if(is_authorized_for_host_commands(temp_host, &current_authdata) == TRUE)
					authorized = TRUE;
				}
			if(cmd == CMD_DEL_SVC_DOWNTIME && temp_downtime != NULL) {
				temp_service = find_service(temp_downtime->host_name, temp_downtime->service_description);
				if(is_authorized_for_service_commands(temp_service, &current_authdata) == TRUE)
					authorized = TRUE;
				}

			/* free downtime data */
			free_downtime_data();

			break;

		case CMD_SCHEDULE_SVC_CHECK:
		case CMD_ENABLE_SVC_CHECK:
		case CMD_DISABLE_SVC_CHECK:
		case CMD_DEL_ALL_SVC_COMMENTS:
		case CMD_ENABLE_SVC_NOTIFICATIONS:
		case CMD_DISABLE_SVC_NOTIFICATIONS:
		case CMD_ENABLE_PASSIVE_SVC_CHECKS:
		case CMD_DISABLE_PASSIVE_SVC_CHECKS:
		case CMD_ENABLE_SVC_EVENT_HANDLER:
		case CMD_DISABLE_SVC_EVENT_HANDLER:
		case CMD_REMOVE_SVC_ACKNOWLEDGEMENT:
		case CMD_PROCESS_SERVICE_CHECK_RESULT:
		case CMD_SCHEDULE_SVC_DOWNTIME:
		case CMD_DELAY_SVC_NOTIFICATION:
		case CMD_ENABLE_SVC_FLAP_DETECTION:
		case CMD_DISABLE_SVC_FLAP_DETECTION:
		case CMD_START_OBSESSING_OVER_SVC:
		case CMD_STOP_OBSESSING_OVER_SVC:

			/* make sure we have author name and comment data... */
			if(cmd == CMD_SCHEDULE_SVC_DOWNTIME) {
				if(!strcmp(comment_data, "")) {
					if(!error_string)
						error_string = strdup("没有输入注释");
					}
				else if(!strcmp(comment_author, "")) {
					if(!error_string)
						error_string = strdup("没有输入作者");
					}
				}

			/* see if the user is authorized to issue a command... */
			temp_service = find_service(host_name, service_desc);
			if(is_authorized_for_service_commands(temp_service, &current_authdata) == TRUE)
				authorized = TRUE;

			/* make sure we have passive check info (if necessary) */
			if(cmd == CMD_PROCESS_SERVICE_CHECK_RESULT && !strcmp(plugin_output, "")) {
				if(!error_string)
					error_string = strdup("插件输出不能为空");
				}

			/* make sure we have a notification delay (if necessary) */
			if(cmd == CMD_DELAY_SVC_NOTIFICATION && notification_delay <= 0) {
				if(!error_string)
					error_string = strdup("通知延时必须大于0");
				}

			/* clean up the comment data if scheduling downtime */
			if(cmd == CMD_SCHEDULE_SVC_DOWNTIME) {
				clean_comment_data(comment_author);
				clean_comment_data(comment_data);
				}

			/* make sure we have check time (if necessary) */
			if(cmd == CMD_SCHEDULE_SVC_CHECK && start_time == (time_t)0) {
				if(!error_string)
					error_string = strdup("开始时间必须是非零或提交的格式错误。");
				}

			/* make sure we have start/end times for downtime (if necessary) */
			if(cmd == CMD_SCHEDULE_SVC_DOWNTIME && (start_time == (time_t)0 || end_time == (time_t)0 || end_time < start_time)) {
				if(!error_string)
					error_string = strdup("开始或结束时间无效");
				}

			break;

		case CMD_ENABLE_NOTIFICATIONS:
		case CMD_DISABLE_NOTIFICATIONS:
		case CMD_SHUTDOWN_PROCESS:
		case CMD_RESTART_PROCESS:
		case CMD_START_EXECUTING_SVC_CHECKS:
		case CMD_STOP_EXECUTING_SVC_CHECKS:
		case CMD_START_ACCEPTING_PASSIVE_SVC_CHECKS:
		case CMD_STOP_ACCEPTING_PASSIVE_SVC_CHECKS:
		case CMD_ENABLE_EVENT_HANDLERS:
		case CMD_DISABLE_EVENT_HANDLERS:
		case CMD_START_OBSESSING_OVER_SVC_CHECKS:
		case CMD_STOP_OBSESSING_OVER_SVC_CHECKS:
		case CMD_ENABLE_FLAP_DETECTION:
		case CMD_DISABLE_FLAP_DETECTION:
		case CMD_ENABLE_PERFORMANCE_DATA:
		case CMD_DISABLE_PERFORMANCE_DATA:
		case CMD_START_EXECUTING_HOST_CHECKS:
		case CMD_STOP_EXECUTING_HOST_CHECKS:
		case CMD_START_ACCEPTING_PASSIVE_HOST_CHECKS:
		case CMD_STOP_ACCEPTING_PASSIVE_HOST_CHECKS:
		case CMD_START_OBSESSING_OVER_HOST_CHECKS:
		case CMD_STOP_OBSESSING_OVER_HOST_CHECKS:

			/* see if the user is authorized to issue a command... */
			if(is_authorized_for_system_commands(&current_authdata) == TRUE)
				authorized = TRUE;
			break;

		case CMD_ENABLE_HOST_SVC_CHECKS:
		case CMD_DISABLE_HOST_SVC_CHECKS:
		case CMD_DEL_ALL_HOST_COMMENTS:
		case CMD_SCHEDULE_HOST_SVC_CHECKS:
		case CMD_ENABLE_HOST_NOTIFICATIONS:
		case CMD_DISABLE_HOST_NOTIFICATIONS:
		case CMD_ENABLE_ALL_NOTIFICATIONS_BEYOND_HOST:
		case CMD_DISABLE_ALL_NOTIFICATIONS_BEYOND_HOST:
		case CMD_ENABLE_HOST_SVC_NOTIFICATIONS:
		case CMD_DISABLE_HOST_SVC_NOTIFICATIONS:
		case CMD_ENABLE_HOST_EVENT_HANDLER:
		case CMD_DISABLE_HOST_EVENT_HANDLER:
		case CMD_ENABLE_HOST_CHECK:
		case CMD_DISABLE_HOST_CHECK:
		case CMD_REMOVE_HOST_ACKNOWLEDGEMENT:
		case CMD_SCHEDULE_HOST_DOWNTIME:
		case CMD_SCHEDULE_HOST_SVC_DOWNTIME:
		case CMD_DELAY_HOST_NOTIFICATION:
		case CMD_ENABLE_HOST_FLAP_DETECTION:
		case CMD_DISABLE_HOST_FLAP_DETECTION:
		case CMD_PROCESS_HOST_CHECK_RESULT:
		case CMD_ENABLE_PASSIVE_HOST_CHECKS:
		case CMD_DISABLE_PASSIVE_HOST_CHECKS:
		case CMD_SCHEDULE_HOST_CHECK:
		case CMD_START_OBSESSING_OVER_HOST:
		case CMD_STOP_OBSESSING_OVER_HOST:

			/* make sure we have author name and comment data... */
			if(cmd == CMD_SCHEDULE_HOST_DOWNTIME || cmd == CMD_SCHEDULE_HOST_SVC_DOWNTIME) {
				if(!strcmp(comment_data, "")) {
					if(!error_string)
						error_string = strdup("没有输入注释");
					}
				else if(!strcmp(comment_author, "")) {
					if(!error_string)
						error_string = strdup("没有输入作者");
					}
				}

			/* see if the user is authorized to issue a command... */
			temp_host = find_host(host_name);
			if(is_authorized_for_host_commands(temp_host, &current_authdata) == TRUE)
				authorized = TRUE;

			/* clean up the comment data if scheduling downtime */
			if(cmd == CMD_SCHEDULE_HOST_DOWNTIME || cmd == CMD_SCHEDULE_HOST_SVC_DOWNTIME) {
				clean_comment_data(comment_author);
				clean_comment_data(comment_data);
				}

			/* make sure we have a notification delay (if necessary) */
			if(cmd == CMD_DELAY_HOST_NOTIFICATION && notification_delay <= 0) {
				if(!error_string)
					error_string = strdup("通知延时必须大于0");
				}

			/* make sure we have start/end times for downtime (if necessary) */
			if((cmd == CMD_SCHEDULE_HOST_DOWNTIME || cmd == CMD_SCHEDULE_HOST_SVC_DOWNTIME) && (start_time == (time_t)0 || end_time == (time_t)0 || start_time > end_time)) {
				if(!error_string)
					error_string = strdup("开始或结束时间无效");
				}

			/* make sure we have check time (if necessary) */
			if((cmd == CMD_SCHEDULE_HOST_CHECK || cmd == CMD_SCHEDULE_HOST_SVC_CHECKS) && start_time == (time_t)0) {
				if(!error_string)
					error_string = strdup("开始时间必须是非零或提交的格式错误。");
				}

			/* make sure we have passive check info (if necessary) */
			if(cmd == CMD_PROCESS_HOST_CHECK_RESULT && !strcmp(plugin_output, "")) {
				if(!error_string)
					error_string = strdup("插件输出不能为空");
				}

			break;

		case CMD_ENABLE_HOSTGROUP_SVC_NOTIFICATIONS:
		case CMD_DISABLE_HOSTGROUP_SVC_NOTIFICATIONS:
		case CMD_ENABLE_HOSTGROUP_HOST_NOTIFICATIONS:
		case CMD_DISABLE_HOSTGROUP_HOST_NOTIFICATIONS:
		case CMD_ENABLE_HOSTGROUP_SVC_CHECKS:
		case CMD_DISABLE_HOSTGROUP_SVC_CHECKS:
		case CMD_SCHEDULE_HOSTGROUP_HOST_DOWNTIME:
		case CMD_SCHEDULE_HOSTGROUP_SVC_DOWNTIME:

			/* make sure we have author and comment data */
			if(cmd == CMD_SCHEDULE_HOSTGROUP_HOST_DOWNTIME || cmd == CMD_SCHEDULE_HOSTGROUP_SVC_DOWNTIME) {
				if(!strcmp(comment_data, "")) {
					if(!error_string)
						error_string = strdup("没有输入注释");
					}
				else if(!strcmp(comment_author, "")) {
					if(!error_string)
						error_string = strdup("没有输入作者");
					}
				}

			/* make sure we have start/end times for downtime */
			if((cmd == CMD_SCHEDULE_HOSTGROUP_HOST_DOWNTIME || cmd == CMD_SCHEDULE_HOSTGROUP_SVC_DOWNTIME) && (start_time == (time_t)0 || end_time == (time_t)0 || start_time > end_time)) {
				if(!error_string)
					error_string = strdup("开始或结束时间无效");
				}

			/* see if the user is authorized to issue a command... */
			temp_hostgroup = find_hostgroup(hostgroup_name);
			if(is_authorized_for_hostgroup_commands(temp_hostgroup, &current_authdata) == TRUE)
				authorized = TRUE;

			/* clean up the comment data if scheduling downtime */
			if(cmd == CMD_SCHEDULE_HOSTGROUP_HOST_DOWNTIME || cmd == CMD_SCHEDULE_HOSTGROUP_SVC_DOWNTIME) {
				clean_comment_data(comment_author);
				clean_comment_data(comment_data);
				}

			break;

		case CMD_ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS:
		case CMD_DISABLE_SERVICEGROUP_SVC_NOTIFICATIONS:
		case CMD_ENABLE_SERVICEGROUP_HOST_NOTIFICATIONS:
		case CMD_DISABLE_SERVICEGROUP_HOST_NOTIFICATIONS:
		case CMD_ENABLE_SERVICEGROUP_SVC_CHECKS:
		case CMD_DISABLE_SERVICEGROUP_SVC_CHECKS:
		case CMD_SCHEDULE_SERVICEGROUP_HOST_DOWNTIME:
		case CMD_SCHEDULE_SERVICEGROUP_SVC_DOWNTIME:

			/* make sure we have author and comment data */
			if(cmd == CMD_SCHEDULE_SERVICEGROUP_HOST_DOWNTIME || cmd == CMD_SCHEDULE_SERVICEGROUP_SVC_DOWNTIME) {
				if(!strcmp(comment_data, "")) {
					if(!error_string)
						error_string = strdup("没有输入注释");
					}
				else if(!strcmp(comment_author, "")) {
					if(!error_string)
						error_string = strdup("没有输入作者");
					}
				}

			/* make sure we have start/end times for downtime */
			if((cmd == CMD_SCHEDULE_SERVICEGROUP_HOST_DOWNTIME || cmd == CMD_SCHEDULE_SERVICEGROUP_SVC_DOWNTIME) && (start_time == (time_t)0 || end_time == (time_t)0 || start_time > end_time)) {
				if(!error_string)
					error_string = strdup("开始或结束时间无效");
				}

			/* see if the user is authorized to issue a command... */

			temp_servicegroup = find_servicegroup(servicegroup_name);
			if(is_authorized_for_servicegroup_commands(temp_servicegroup, &current_authdata) == TRUE)
				authorized = TRUE;

			break;

		case CMD_SEND_CUSTOM_HOST_NOTIFICATION:
		case CMD_SEND_CUSTOM_SVC_NOTIFICATION:

			/* make sure we have author and comment data */
			if(!strcmp(comment_data, "")) {
				if(!error_string)
					error_string = strdup("没有输入注释");
				}
			else if(!strcmp(comment_author, "")) {
				if(!error_string)
					error_string = strdup("没有输入作者");
				}

			/* see if the user is authorized to issue a command... */
			if(cmd == CMD_SEND_CUSTOM_HOST_NOTIFICATION) {
				temp_host = find_host(host_name);
				if(is_authorized_for_host_commands(temp_host, &current_authdata) == TRUE)
					authorized = TRUE;
				}
			else {
				temp_service = find_service(host_name, service_desc);
				if(is_authorized_for_service_commands(temp_service, &current_authdata) == TRUE)
					authorized = TRUE;
				}
			break;

		default:
			if(!error_string) error_string = strdup("在处理您的命令时出错!");
		}


	/* to be safe, we are going to REQUIRE that the authentication functionality is enabled... */
	if(use_authentication == FALSE) {
		if(content_type == WML_CONTENT)
			printf("<p>错误: CGI的认证被禁用!</p>\n");
		else {
			printf("<P>\n");
			printf("<DIV CLASS='errorMessage'>抱歉! 此树是我栽，此路是我开...</DIV><br>");
			printf("<DIV CLASS='errorDescription'>");
			printf("可能是CGI的认证功能没有开启<br><br>");
			printf("在没有认证的情况下，Nagios将不能保证使用的结果是正确的。");
			printf("如果你确实想在无认证的情况下使用这个功能，可能要降低Nagios对权限认证的要求。:-(<br><br>");
			printf("<strong>在线的HTML帮助里有关于CGI认证权限相关的设置信息以及为何你需要设置认证的原因。</strong>\n");
			printf("</DIV>\n");
			printf("</P>\n");
			}
		}

	/* the user is not authorized to issue the given command */
	else if(authorized == FALSE) {
		if(content_type == WML_CONTENT)
			printf("<p>错误: 无权限提交命令!</p>\n");
		else {
			printf("<P><DIV CLASS='errorMessage'>抱歉，你未被授权提交上述命令。</DIV></P>\n");
			printf("<P><DIV CLASS='errorDescription'>请参阅CGI认证相关信息。<BR><BR>\n");
			printf("<A HREF='javascript:window.history.go(-2)'>返回上一步</A></DIV></P>\n");
			}
		}

	/* some error occurred (data was probably missing) */
	else if(error_string) {
		if(content_type == WML_CONTENT)
			printf("<p>%s</p>\n", error_string);
		else {
			printf("<P><DIV CLASS='errorMessage'>%s</DIV></P>\n", error_string);
			free(error_string);
			printf("<P><DIV CLASS='errorDescription'>Go <A HREF='javascript:window.history.go(-1)'>返回</A> 检查是否必须的输入项都正确。<BR>\n");
			printf("<A HREF='javascript:window.history.go(-2)'>返回上一步</A></DIV></P>\n");
			}
		}

	/* if Nagios isn't checking external commands, don't do anything... */
	else if(check_external_commands == FALSE) {
		if(content_type == WML_CONTENT)
			printf("<p>错误: Nagios不检查外部命令!</p>\n");
		else {
			printf("<P><DIV CLASS='errorMessage'>抱歉，Nagios当前配置是不检查外部命令，所以你的命令不会被提交!</DIV></P>\n");
			printf("<P><DIV CLASS='errorDescription'>如何开启外部命令功能请参考手册。<BR><BR>\n");
			printf("<A HREF='javascript:window.history.go(-2)'>返回上一步</A></DIV></P>\n");
			}
		}

	/* everything looks okay, so let's go ahead and commit the command... */
	else {

		/* commit the command */
		result = commit_command(cmd);

		if(result == OK) {
			if(content_type == WML_CONTENT)
				printf("<p>命令提交成功...</p>\n");
			else {
				printf("<P><DIV CLASS='infoMessage'>你的命令请求已成功提交给Nagios进行处理。<BR><BR>\n");
				printf("注意: 命令执行可能需要一段时间。<BR><BR>\n");
				printf("<A HREF='javascript:window.history.go(-2)'>完成</A></DIV></P>");
				}
			}
		else {
			if(content_type == WML_CONTENT)
				printf("<p>提交命令时出错!</p>\n");
			else {
				printf("<P><DIV CLASS='errorMessage'>在尝试提交您的命令进行处理时发生错误。<BR><BR>\n");
				printf("<A HREF='javascript:window.history.go(-2)'>返回上一步</A></DIV></P>\n");
				}
			}
		}

	return;
	}

__attribute__((format(printf, 2, 3)))
static int cmd_submitf(int id, const char *fmt, ...) {
	char cmd[MAX_EXTERNAL_COMMAND_LENGTH];
	const char *command_name;
	int len;
	int len2;
	va_list ap;

	command_name = extcmd_get_name(id);
	/*
	 * We disallow sending 'CHANGE' commands from the cgi's
	 * until we do proper session handling to prevent cross-site
	 * request forgery
	 */
	if(!command_name || (strlen(command_name) > 6 && !memcmp("CHANGE", command_name, 6)))
		return ERROR;

	len = snprintf(cmd, sizeof(cmd), "[%lu] %s;", time(NULL), command_name);
	if(len < 0 || len >= sizeof(cmd))
		return ERROR;

	if(fmt) {
		va_start(ap, fmt);
		len2 = vsnprintf(cmd + len, sizeof(cmd) - len, fmt, ap);
		va_end(ap);
		len += len2;
		if(len2 < 0 || len >= sizeof(cmd))
			return ERROR;
		}

	cmd[len] = 0; /* 0 <= len < sizeof(cmd) */
	return write_command_to_file(cmd);
	}



/* commits a command for processing */
int commit_command(int cmd) {
	time_t current_time;
	time_t scheduled_time;
	time_t notification_time;
	int result;

	/* get the current time */
	time(&current_time);

	/* get the scheduled time */
	scheduled_time = current_time + (schedule_delay * 60);

	/* get the notification time */
	notification_time = current_time + (notification_delay * 60);

	/*
	 * these are supposed to be implanted inside the
	 * completed commands shipped off to nagios and
	 * must therefore never contain ';'
	 */
	if(host_name && strchr(host_name, ';'))
		return ERROR;
	if(service_desc && strchr(service_desc, ';'))
		return ERROR;
	if(comment_author && strchr(comment_author, ';'))
		return ERROR;
	if(hostgroup_name && strchr(hostgroup_name, ';'))
		return ERROR;
	if(servicegroup_name && strchr(servicegroup_name, ';'))
		return ERROR;

	/* decide how to form the command line... */
	switch(cmd) {

			/* commands without arguments */
		case CMD_START_EXECUTING_SVC_CHECKS:
		case CMD_STOP_EXECUTING_SVC_CHECKS:
		case CMD_START_ACCEPTING_PASSIVE_SVC_CHECKS:
		case CMD_STOP_ACCEPTING_PASSIVE_SVC_CHECKS:
		case CMD_ENABLE_EVENT_HANDLERS:
		case CMD_DISABLE_EVENT_HANDLERS:
		case CMD_START_OBSESSING_OVER_SVC_CHECKS:
		case CMD_STOP_OBSESSING_OVER_SVC_CHECKS:
		case CMD_ENABLE_FLAP_DETECTION:
		case CMD_DISABLE_FLAP_DETECTION:
		case CMD_ENABLE_PERFORMANCE_DATA:
		case CMD_DISABLE_PERFORMANCE_DATA:
		case CMD_START_EXECUTING_HOST_CHECKS:
		case CMD_STOP_EXECUTING_HOST_CHECKS:
		case CMD_START_ACCEPTING_PASSIVE_HOST_CHECKS:
		case CMD_STOP_ACCEPTING_PASSIVE_HOST_CHECKS:
		case CMD_START_OBSESSING_OVER_HOST_CHECKS:
		case CMD_STOP_OBSESSING_OVER_HOST_CHECKS:
			result = cmd_submitf(cmd, NULL);
			break;

			/** simple host commands **/
		case CMD_ENABLE_HOST_FLAP_DETECTION:
		case CMD_DISABLE_HOST_FLAP_DETECTION:
		case CMD_ENABLE_PASSIVE_HOST_CHECKS:
		case CMD_DISABLE_PASSIVE_HOST_CHECKS:
		case CMD_START_OBSESSING_OVER_HOST:
		case CMD_STOP_OBSESSING_OVER_HOST:
		case CMD_DEL_ALL_HOST_COMMENTS:
		case CMD_ENABLE_ALL_NOTIFICATIONS_BEYOND_HOST:
		case CMD_DISABLE_ALL_NOTIFICATIONS_BEYOND_HOST:
		case CMD_ENABLE_HOST_EVENT_HANDLER:
		case CMD_DISABLE_HOST_EVENT_HANDLER:
		case CMD_ENABLE_HOST_CHECK:
		case CMD_DISABLE_HOST_CHECK:
		case CMD_REMOVE_HOST_ACKNOWLEDGEMENT:
			result = cmd_submitf(cmd, "%s", host_name);
			break;

			/** simple service commands **/
		case CMD_ENABLE_SVC_FLAP_DETECTION:
		case CMD_DISABLE_SVC_FLAP_DETECTION:
		case CMD_ENABLE_PASSIVE_SVC_CHECKS:
		case CMD_DISABLE_PASSIVE_SVC_CHECKS:
		case CMD_START_OBSESSING_OVER_SVC:
		case CMD_STOP_OBSESSING_OVER_SVC:
		case CMD_DEL_ALL_SVC_COMMENTS:
		case CMD_ENABLE_SVC_NOTIFICATIONS:
		case CMD_DISABLE_SVC_NOTIFICATIONS:
		case CMD_ENABLE_SVC_EVENT_HANDLER:
		case CMD_DISABLE_SVC_EVENT_HANDLER:
		case CMD_ENABLE_SVC_CHECK:
		case CMD_DISABLE_SVC_CHECK:
		case CMD_REMOVE_SVC_ACKNOWLEDGEMENT:
			result = cmd_submitf(cmd, "%s;%s", host_name, service_desc);
			break;

		case CMD_ADD_HOST_COMMENT:
			result = cmd_submitf(cmd, "%s;%d;%s;%s", host_name, persistent_comment, comment_author, comment_data);
			break;

		case CMD_ADD_SVC_COMMENT:
			result = cmd_submitf(cmd, "%s;%s;%d;%s;%s", host_name, service_desc, persistent_comment, comment_author, comment_data);
			break;

		case CMD_DEL_HOST_COMMENT:
		case CMD_DEL_SVC_COMMENT:
			result = cmd_submitf(cmd, "%lu", comment_id);
			break;

		case CMD_DELAY_HOST_NOTIFICATION:
			result = cmd_submitf(cmd, "%s;%lu", host_name, notification_time);
			break;

		case CMD_DELAY_SVC_NOTIFICATION:
			result = cmd_submitf(cmd, "%s;%s;%lu", host_name, service_desc, notification_time);
			break;

		case CMD_SCHEDULE_SVC_CHECK:
		case CMD_SCHEDULE_FORCED_SVC_CHECK:
			if(force_check == TRUE)
				cmd = CMD_SCHEDULE_FORCED_SVC_CHECK;
			result = cmd_submitf(cmd, "%s;%s;%lu", host_name, service_desc, start_time);
			break;

		case CMD_DISABLE_NOTIFICATIONS:
		case CMD_ENABLE_NOTIFICATIONS:
		case CMD_SHUTDOWN_PROCESS:
		case CMD_RESTART_PROCESS:
			result = cmd_submitf(cmd, "%lu", scheduled_time);
			break;

		case CMD_ENABLE_HOST_SVC_CHECKS:
		case CMD_DISABLE_HOST_SVC_CHECKS:
			result = cmd_submitf(cmd, "%s", host_name);
			if(affect_host_and_services == TRUE) {
				cmd = (cmd == CMD_ENABLE_HOST_SVC_CHECKS) ? CMD_ENABLE_HOST_CHECK : CMD_DISABLE_HOST_CHECK;
				result |= cmd_submitf(cmd, "%s", host_name);
				}
			break;

		case CMD_SCHEDULE_HOST_SVC_CHECKS:
			if(force_check == TRUE)
				cmd = CMD_SCHEDULE_FORCED_HOST_SVC_CHECKS;
			result = cmd_submitf(cmd, "%s;%lu", host_name, scheduled_time);
			break;

		case CMD_ENABLE_HOST_NOTIFICATIONS:
		case CMD_DISABLE_HOST_NOTIFICATIONS:
			if(propagate_to_children == TRUE)
				cmd = (cmd == CMD_ENABLE_HOST_NOTIFICATIONS) ? CMD_ENABLE_HOST_AND_CHILD_NOTIFICATIONS : CMD_DISABLE_HOST_AND_CHILD_NOTIFICATIONS;
			result = cmd_submitf(cmd, "%s", host_name);
			break;

		case CMD_ENABLE_HOST_SVC_NOTIFICATIONS:
		case CMD_DISABLE_HOST_SVC_NOTIFICATIONS:
			result = cmd_submitf(cmd, "%s", host_name);
			if(affect_host_and_services == TRUE) {
				cmd = (cmd == CMD_ENABLE_HOST_SVC_NOTIFICATIONS) ? CMD_ENABLE_HOST_NOTIFICATIONS : CMD_DISABLE_HOST_NOTIFICATIONS;
				result |= cmd_submitf(cmd, "%s", host_name);
				}
			break;

		case CMD_ACKNOWLEDGE_HOST_PROBLEM:
			result = cmd_submitf(cmd, "%s;%d;%d;%d;%s;%s", host_name, (sticky_ack == TRUE) ? ACKNOWLEDGEMENT_STICKY : ACKNOWLEDGEMENT_NORMAL, send_notification, persistent_comment, comment_author, comment_data);
			break;

		case CMD_ACKNOWLEDGE_SVC_PROBLEM:
			result = cmd_submitf(cmd, "%s;%s;%d;%d;%d;%s;%s", host_name, service_desc, (sticky_ack == TRUE) ? ACKNOWLEDGEMENT_STICKY : ACKNOWLEDGEMENT_NORMAL, send_notification, persistent_comment, comment_author, comment_data);
			break;

		case CMD_PROCESS_SERVICE_CHECK_RESULT:
			result = cmd_submitf(cmd, "%s;%s;%d;%s|%s", host_name, service_desc, plugin_state, plugin_output, performance_data);
			break;

		case CMD_PROCESS_HOST_CHECK_RESULT:
			result = cmd_submitf(cmd, "%s;%d;%s|%s", host_name, plugin_state, plugin_output, performance_data);
			break;

		case CMD_SCHEDULE_HOST_DOWNTIME:
			if(child_options == 1)
				cmd = CMD_SCHEDULE_AND_PROPAGATE_TRIGGERED_HOST_DOWNTIME;
			else if(child_options == 2)
				cmd = CMD_SCHEDULE_AND_PROPAGATE_HOST_DOWNTIME;

			result = cmd_submitf(cmd, "%s;%lu;%lu;%d;%lu;%lu;%s;%s", host_name, start_time, end_time, fixed, triggered_by, duration, comment_author, comment_data);
			break;

		case CMD_SCHEDULE_HOST_SVC_DOWNTIME:
			result = cmd_submitf(cmd, "%s;%lu;%lu;%d;%lu;%lu;%s;%s", host_name, start_time, end_time, fixed, triggered_by, duration, comment_author, comment_data);
			break;

		case CMD_SCHEDULE_SVC_DOWNTIME:
			result = cmd_submitf(cmd, "%s;%s;%lu;%lu;%d;%lu;%lu;%s;%s", host_name, service_desc, start_time, end_time, fixed, triggered_by, duration, comment_author, comment_data);
			break;

		case CMD_DEL_HOST_DOWNTIME:
		case CMD_DEL_SVC_DOWNTIME:
			result = cmd_submitf(cmd, "%lu", downtime_id);
			break;

		case CMD_SCHEDULE_HOST_CHECK:
			if(force_check == TRUE)
				cmd = CMD_SCHEDULE_FORCED_HOST_CHECK;
			result = cmd_submitf(cmd, "%s;%lu", host_name, start_time);
			break;

		case CMD_SEND_CUSTOM_HOST_NOTIFICATION:
			result = cmd_submitf(cmd, "%s;%d;%s;%s", host_name, (force_notification | broadcast_notification), comment_author, comment_data);
			break;

		case CMD_SEND_CUSTOM_SVC_NOTIFICATION:
			result = cmd_submitf(cmd, "%s;%s;%d;%s;%s", host_name, service_desc, (force_notification | broadcast_notification), comment_author, comment_data);
			break;


			/***** HOSTGROUP COMMANDS *****/

		case CMD_ENABLE_HOSTGROUP_SVC_NOTIFICATIONS:
		case CMD_DISABLE_HOSTGROUP_SVC_NOTIFICATIONS:
			result = cmd_submitf(cmd, "%s", hostgroup_name);
			if(affect_host_and_services == TRUE) {
				cmd = (cmd == CMD_ENABLE_HOSTGROUP_SVC_NOTIFICATIONS) ? CMD_ENABLE_HOSTGROUP_HOST_NOTIFICATIONS : CMD_DISABLE_HOSTGROUP_HOST_NOTIFICATIONS;
				result |= cmd_submitf(cmd, "%s", hostgroup_name);
				}
			break;

		case CMD_ENABLE_HOSTGROUP_HOST_NOTIFICATIONS:
		case CMD_DISABLE_HOSTGROUP_HOST_NOTIFICATIONS:
			result = cmd_submitf(cmd, "%s", hostgroup_name);
			break;

		case CMD_ENABLE_HOSTGROUP_SVC_CHECKS:
		case CMD_DISABLE_HOSTGROUP_SVC_CHECKS:
			result = cmd_submitf(cmd, "%s", hostgroup_name);
			if(affect_host_and_services == TRUE) {
				cmd = (cmd == CMD_ENABLE_HOSTGROUP_SVC_CHECKS) ? CMD_ENABLE_HOSTGROUP_HOST_CHECKS : CMD_DISABLE_HOSTGROUP_HOST_CHECKS;
				result |= cmd_submitf(cmd, "%s", hostgroup_name);
				}
			break;

		case CMD_SCHEDULE_HOSTGROUP_HOST_DOWNTIME:
			result = cmd_submitf(cmd, "%s;%lu;%lu;%d;0;%lu;%s;%s", hostgroup_name, start_time, end_time, fixed, duration, comment_author, comment_data);
			break;

		case CMD_SCHEDULE_HOSTGROUP_SVC_DOWNTIME:
			result = cmd_submitf(cmd, "%s;%lu;%lu;%d;0;%lu;%s;%s", hostgroup_name, start_time, end_time, fixed, duration, comment_author, comment_data);
			if(affect_host_and_services == TRUE)
				result |= cmd_submitf(CMD_SCHEDULE_HOSTGROUP_HOST_DOWNTIME, "%s;%lu;%lu;%d;0;%lu;%s;%s", hostgroup_name, start_time, end_time, fixed, duration, comment_author, comment_data);
			break;


			/***** SERVICEGROUP COMMANDS *****/

		case CMD_ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS:
		case CMD_DISABLE_SERVICEGROUP_SVC_NOTIFICATIONS:
			result = cmd_submitf(cmd, "%s", servicegroup_name);
			if(affect_host_and_services == TRUE) {
				cmd = (cmd == CMD_ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS) ? CMD_ENABLE_SERVICEGROUP_HOST_NOTIFICATIONS : CMD_DISABLE_SERVICEGROUP_HOST_NOTIFICATIONS;
				result |= cmd_submitf(cmd, "%s", servicegroup_name);
				}
			break;

		case CMD_ENABLE_SERVICEGROUP_HOST_NOTIFICATIONS:
		case CMD_DISABLE_SERVICEGROUP_HOST_NOTIFICATIONS:
			result = cmd_submitf(cmd, "%s", servicegroup_name);
			break;

		case CMD_ENABLE_SERVICEGROUP_SVC_CHECKS:
		case CMD_DISABLE_SERVICEGROUP_SVC_CHECKS:
			result = cmd_submitf(cmd, "%s", servicegroup_name);
			if(affect_host_and_services == TRUE) {
				cmd = (cmd == CMD_ENABLE_SERVICEGROUP_SVC_CHECKS) ? CMD_ENABLE_SERVICEGROUP_HOST_CHECKS : CMD_DISABLE_SERVICEGROUP_HOST_CHECKS;
				result |= cmd_submitf(cmd, "%s", servicegroup_name);
				}
			break;

		case CMD_SCHEDULE_SERVICEGROUP_HOST_DOWNTIME:
			result = cmd_submitf(cmd, "%s;%lu;%lu;%d;0;%lu;%s;%s", servicegroup_name, start_time, end_time, fixed, duration, comment_author, comment_data);
			break;

		case CMD_SCHEDULE_SERVICEGROUP_SVC_DOWNTIME:
			result = cmd_submitf(cmd, "%s;%lu;%lu;%d;0;%lu;%s;%s", servicegroup_name, start_time, end_time, fixed, duration, comment_author, comment_data);
			if(affect_host_and_services == TRUE)
				result |= cmd_submitf(CMD_SCHEDULE_SERVICEGROUP_HOST_DOWNTIME, "%s;%lu;%lu;%d;0;%lu;%s;%s", servicegroup_name, start_time, end_time, fixed, duration, comment_author, comment_data);
			break;

		default:
			return ERROR;
			break;
		}

	return result;
	}



/* write a command entry to the command file */
int write_command_to_file(char *cmd) {
	FILE *fp;
	struct stat statbuf;

	/*
	 * Commands are not allowed to have newlines in them, as
	 * that allows malicious users to hand-craft requests that
	 * bypass the access-restrictions.
	 */
	if(!cmd || !*cmd || strchr(cmd, '\n'))
		return ERROR;

	/* bail out if the external command file doesn't exist */
	if(stat(command_file, &statbuf)) {

		if(content_type == WML_CONTENT)
			printf("<p>错误: 无法统计的外部命令文件!</p>\n");
		else {
			printf("<P><DIV CLASS='errorMessage'>错误: 无法统计的外部命令文件 '%s'!</DIV></P>\n", command_file);
			printf("<P><DIV CLASS='errorDescription'>");
			printf("外部命令文件可能丢失，Nagios可能未运行，异或Nagios不能检查外部命令。\n");
			printf("</DIV></P>\n");
			}

		return ERROR;
		}

	/* open the command for writing (since this is a pipe, it will really be appended) */
	fp = fopen(command_file, "w");
	if(fp == NULL) {

		if(content_type == WML_CONTENT)
			printf("<p>错误: 无法打开命令文件更新!</p>\n");
		else {
			printf("<P><DIV CLASS='errorMessage'>错误: 无法打开命令文件'%s'来更新!</DIV></P>\n", command_file);
			printf("<P><DIV CLASS='errorDescription'>");
			printf("外部命令文件或目录的权限可能不正确，阅读FAQ了解如何设置适当的权限的常见问题解答。\n");
			printf("</DIV></P>\n");
			}

		return ERROR;
		}

	/* write the command to file */
	fprintf(fp, "%s\n", cmd);

	/* flush buffer */
	fflush(fp);

	fclose(fp);

	return OK;
	}


/* strips out semicolons from comment data */
void clean_comment_data(char *buffer) {
	int x;
	int y;

	y = (int)strlen(buffer);

	for(x = 0; x < y; x++) {
		if(buffer[x] == ';')
			buffer[x] = ' ';
		}

	return;
	}


/* display information about a command */
void show_command_help(int cmd) {

	printf("<DIV ALIGN=CENTER CLASS='descriptionTitle'>命令说明</DIV>\n");
	printf("<TABLE BORDER=1 CELLSPACING=0 CELLPADDING=0 CLASS='commandDescription'>\n");
	printf("<TR><TD CLASS='commandDescription'>\n");

	/* decide what information to print out... */
	switch(cmd) {

		case CMD_ADD_HOST_COMMENT:
			printf("该命令用于对指定的主机增加注释。如果与其他管理员协作，该命令有助于分享主机信息\n");
			printf("当多个人使用时，如果不选择\"保持\"选项，则下次Nagios启动时注释会被自动删除。\n");
			break;

		case CMD_ADD_SVC_COMMENT:
			printf("该命令用于对指定的服务增加注释。如果与其他管理员协作，该命令有助于分享主机信息\n");
			printf("当多个人使用时，如果不选择\"保持\"选项，则下次Nagios启动时注释会自动被删除。\n");
			break;

		case CMD_DEL_HOST_COMMENT:
			printf("该命令用于删除指定主机的注释。\n");
			break;

		case CMD_DEL_SVC_COMMENT:
			printf("该命令用于删除指定服务的注释。\n");
			break;

		case CMD_DELAY_HOST_NOTIFICATION:
			printf("该命令用于延时发出对指定主机的故障通知。当主机状态在下一次调度发送通知前发生改变时，通知延时将被忽略\n");
			printf("该命令对于状态正常的主机无意义\n");
			break;

		case CMD_DELAY_SVC_NOTIFICATION:
			printf("该命令用于延时发出对指定主机的故障通知。当主机状态在下一次调度发送通知前发生改变时，通知延时将被忽略\n");
			printf("该命令对于状态正常的服务无意义。\n");
			break;

		case CMD_SCHEDULE_SVC_CHECK:
			printf("该命令是用于计划下次对服务的检查。Nagios在设置的指定时间内进行服务检查。\n");
			printf("如果选择了<b>强制检查</b>选项，Nagios将强制将强制检查服务状态而无视按设置时间段来检查（无论你设置过检查时间段与否）。\n");
			break;

		case CMD_ENABLE_SVC_CHECK:
			printf("该命令用于启动对一个服务的检查。\n");
			break;

		case CMD_DISABLE_SVC_CHECK:
			printf("该命令用于停止对一个服务的检查。\n");
			break;

		case CMD_DISABLE_NOTIFICATIONS:
			printf("该命令用于设置主机与服务的通知无效，依赖于外部程序扩展。\n");
			break;

		case CMD_ENABLE_NOTIFICATIONS:
			printf("该命令用于设置主机与服务的通知有效，依赖于外部程序扩展。\n");
			break;

		case CMD_SHUTDOWN_PROCESS:
			printf("该命令用于关闭Nagios进程。注意: 一旦Nagios服务被关闭，不能通过Web接口来启动Nagios进程!\n");
			break;

		case CMD_RESTART_PROCESS:
			printf("该命令命令用于重启Nagios服务，执行该命令相当于向Nagios进程发出HUP信号。\n");
			printf("全部内存中的信息将被保存下来配置将重新调入，Nagios将以新的配置内容进行监控。\n");
			break;

		case CMD_ENABLE_HOST_SVC_CHECKS:
			printf("该命令用于启用指定主机上的所有服务的主动检查。这个将<b>不会</b>对主机进行检查除非你同时选择了\"启用主机检查\"选项。\n");
			break;

		case CMD_DISABLE_HOST_SVC_CHECKS:
			printf("该命令用于禁止对指定主机上的所有服务检查。当指定服务被禁用检查后将不再监控这个服务，针对该服务也将不再产生报警；要让Nagios的检查指定服务，你必须重新启用该服务检查。\n");
			printf("注意这<b>不会</b>停止对主机的检查除非你同时选择\"禁用主机检查\"选项。\n");
			printf("");
			break;

		case CMD_SCHEDULE_HOST_SVC_CHECKS:
			printf("该命令用于规划指定主机上的所有的服务下次到检查时间。如果选择了<b>强制检查</b>选项，Nagios将强行检查全部的主机服务而不管你是否在计划中设置了下次检查看主机服务的时间段。\n");
			break;

		case CMD_DEL_ALL_HOST_COMMENTS:
			printf("该命令用于删除指定主机的所有注释。\n");
			break;

		case CMD_DEL_ALL_SVC_COMMENTS:
			printf("该命令用于删除指定服务的所有注释。\n");
			break;

		case CMD_ENABLE_SVC_NOTIFICATIONS:
			printf("该命令用于使能对指定服务的通知。通知服务仅仅是在你设置的状态产生时才被发送。\n");
			break;

		case CMD_DISABLE_SVC_NOTIFICATIONS:
			printf("该命令用于关闭指定的服务状态通知。今后你可以重新开启该所对应的服务状态的通知功能。\n");
			break;

		case CMD_ENABLE_HOST_NOTIFICATIONS:
			printf("该命令用于启用对指定主机的通知。通知服务仅仅是当你指定的主机处于某设置状态时才被发送。\n");
			printf("注意这个命令并<b>不是</b>开启与该主机相关联服务的通知。\n");
			break;

		case CMD_DISABLE_HOST_NOTIFICATIONS:
			printf("该命令用于对指定主机的通知服务的关闭。你可以重新开启该通知。\n");
			printf("注意这个命令并<b>不是</b>关闭了与指定主机相关服务的通知。\n");
			break;

		case CMD_ENABLE_ALL_NOTIFICATIONS_BEYOND_HOST:
			printf("该命令用于开启全部主机及服务的通知服务。\n");
			break;

		case CMD_DISABLE_ALL_NOTIFICATIONS_BEYOND_HOST:
			printf("该命令用于关闭全部主机及服务的通知服务。\n");
			break;

		case CMD_ENABLE_HOST_SVC_NOTIFICATIONS:
			printf("该命令用于打开指定主机上全部服务的通知。通知仅在你设置的通知服务状态产生时才被发出。\n");
			printf("该命令<b>并不</b>启用主机的状态检查通知除非你同时选择了\"启用主机通知\"选项。\n");
			break;

		case CMD_DISABLE_HOST_SVC_NOTIFICATIONS:
			printf("该命令用于阻止指定主机所有服务的通知。当你的指定主机相关的服务需要向外发出通知服务时，你需要重新启用相关服务通知。\n");
			printf("该命令<b>并不</b>禁用对指定主机的检查通知除非你同时选择了\"禁用主机通知\"选项。\n");
			break;

		case CMD_ACKNOWLEDGE_HOST_PROBLEM:
			printf("该命令用于标记主机问题。当主机问题被标记后，该故障通知将临时被关闭直到它的状态发生改变。如果想直到主机恢复后再使用通知功能，须将选项'保存确认'勾选。\n");
			printf("联系主机确认发送问题确认信息，该主机相关的人员可能关注这个问题，让他们知道有人正在研究这个问题。此外，注释也将被添加到该主机，请确保输入你的名称，并填写你正在做的注释字段什么的简要说明。\n");
			printf("如果你希望Nagios重启动后保存这些注释，勾选'保存注释'的选项。如果你不想把将标记故障通知发送给该主机的联系人，不要勾选'发送通知'选项。\n");
			break;

		case CMD_ACKNOWLEDGE_SVC_PROBLEM:
			printf("该命令用于标记服务问题。当服务问题被标记后，该故障通知将临时被关闭直到它的状态发生改变。如果想直到服务恢复后再使用通知功能，须将选项'保存确认'勾选。\n");
			printf("联系服务确认发送问题确认信息，该服务相关的人员可能关注这个问题，让他们知道有人正在研究这个问题。此外，注释也将被添加到该服务，请确保输入你的名称，并填写你正在做的注释字段什么的简要说明。\n");
			printf("如果你希望Nagios重启动后保存这些注释，勾选'保存注释'的选项。如果你不想把将标记故障通知发送给该服务的联系人，不要勾选'发送通知'选项。\n");
			break;

		case CMD_START_EXECUTING_SVC_CHECKS:
			printf("该命令用于程序设置执行主动服务检查。非法服务依然不会被检查。\n");
			break;

		case CMD_STOP_EXECUTING_SVC_CHECKS:
			printf("该命令用于临时关闭所有的服务检查。这可能对防止已经被发出通知过程产生影响。\n");
			printf("服务检查将不会执行直到你重新选择了启用服务检查。\n");
			break;

		case CMD_START_ACCEPTING_PASSIVE_SVC_CHECKS:
			printf("该命令用于启用Nagios接受由外部命令文件产生的被动服务检查结果。\n");
			break;

		case CMD_STOP_ACCEPTING_PASSIVE_SVC_CHECKS:
			printf("该命令用于禁用Nagios接受由外部命令文件产生的被动服务检查结果，所有被动检查结果都将被忽略。\n");
			break;

		case CMD_ENABLE_PASSIVE_SVC_CHECKS:
			printf("该命令用于启用Nagios接受由外部命令文件产生针对指定服务的被动检查结果。\n");
			break;

		case CMD_DISABLE_PASSIVE_SVC_CHECKS:
			printf("该命令用于禁用Nagios接受由外部命令文件产生针对指定服务的被动检查结果，所有被动检查结果都将被忽略。\n");
			break;

		case CMD_ENABLE_EVENT_HANDLERS:
			printf("该命令用于启用Nagios运行主机和服务的事件处理。\n");
			break;

		case CMD_DISABLE_EVENT_HANDLERS:
			printf("该命令用于暂时禁用Nagios的运行任何主机或服务的事件处理。\n");
			break;

		case CMD_ENABLE_SVC_EVENT_HANDLER:
			printf("该命令用于启用对指定服务(如果定义了一个服务的话)的事件处理。\n");
			break;

		case CMD_DISABLE_SVC_EVENT_HANDLER:
			printf("该命令用于禁用对指定服务(如果定义了一个服务的话)的事件处理。\n");
			break;

		case CMD_ENABLE_HOST_EVENT_HANDLER:
			printf("该命令用于启用对指定主机(如果定义了一个的话)的事件处理。\n");
			break;

		case CMD_DISABLE_HOST_EVENT_HANDLER:
			printf("该命令用于禁用对指定主机(如果定义了一个的话)的事件处理。\n");
			break;

		case CMD_ENABLE_HOST_CHECK:
			printf("该命令用于对主机启用检查。\n");
			break;

		case CMD_DISABLE_HOST_CHECK:
			printf("该命令用于禁用对主机检查，如果Nagios要检测它要先假定以前被中止的状态没有发生改变。\n");
			break;

		case CMD_START_OBSESSING_OVER_SVC_CHECKS:
			printf("该命令用于使能Nagios的Obsessing over服务检查。需要阅读文档的分布式检测内容获得更多信息。\n");
			break;

		case CMD_STOP_OBSESSING_OVER_SVC_CHECKS:
			printf("该命令用于禁用Nagios的Obsessing over服务检查。\n");
			break;

		case CMD_REMOVE_HOST_ACKNOWLEDGEMENT:
			printf("该命令用于取消对主机问题的确认标记。一旦标记被取消，主机发生问题时将会有通知发出。\n");
			break;

		case CMD_REMOVE_SVC_ACKNOWLEDGEMENT:
			printf("该命令用于取消对服务问题的确认标记。一旦标记被取消，主机上的服务发生问题时将会有通知发出。\n");
			break;

		case CMD_PROCESS_SERVICE_CHECK_RESULT:
			printf("该命令用于对指定服务被动检查结果的确认。这用于在有安全相关的服务状态一旦被检测后设置为OK状态时比较有用。\n");
			break;

		case CMD_PROCESS_HOST_CHECK_RESULT:
			printf("该命令用于对指定主机被动检查结果的确认。\n");
			break;

		case CMD_SCHEDULE_HOST_DOWNTIME:
			printf("该命令用于设置对指定主机设置宕机计划时间段。在宕机时间段中，Nagios将不会发送主机问题通知。\n");
			printf("在宕机计划时间段外，Nagios将按计划进行检查并发出问题通知。宕机时间调度在程序的关闭和重启中都会被保存。\n");
			printf("宕机计划时间段设置的开始时间与结束时间格式应是<b>mm/dd/yyy hh:mm:ss</b>。\n");
			printf("如果你选择了<b>固定</b>选项，宕机时间将在指定的设置的开始时间到结束时间内起作用。如果没有选择<b>固定</b>选项，Nagios将力图\"柔性\"地来处理宕机时间。\n");
			printf("柔性的处理宕机时间将视主机宕机开始直接你设置的时间结束或是最后你发出阶段时间结束为止，这个阶段时间长度将不可控。\n");
			break;

		case CMD_SCHEDULE_HOST_SVC_DOWNTIME:
			printf("该命令用于设置对指定主机的所有服务设置宕机计划时间段。在宕机时间段中，Nagios将不会发送相关主机问题通知。\n");
			printf("通常，一台处于宕机计划时间内的主机是不会发送任何服务的错误状态，这个选项用于确定该主机的所有服务处于宕机时间调度状态。\n");
			printf("在宕机计划时间段外，Nagios将按计划进行检查并发出问题通知。宕机时间调度在程序的关闭和重启中都会被保存。\n");
			printf("宕机计划时间段设置的开始时间与结束时间格式应是<b>mm/dd/yyy hh:mm:ss</b>。\n");
			printf("如果你选择了<b>固定</b>选项，宕机时间将在指定的设置的开始时间到结束时间内起作用。如果没有选择<b>固定</b>选项，Nagios将力图\"柔性\"地来处理宕机时间。\n");
			printf("柔性的处理宕机时间将视主机宕机开始直接你设置的时间结束或是最后你发出阶段时间结束为止，这个阶段时间长度将不可控。\n");
			break;

		case CMD_SCHEDULE_SVC_DOWNTIME:
			printf("该命令用于设置对指定服务设置宕机计划时间段。在宕机时间段中，Nagios将不会发送相关服务问题通知。\n");
			printf("在宕机计划时间段外，Nagios将按计划进行检查并发出问题通知。宕机时间调度在程序的关闭和重启中都会被保存。\n");
			printf("宕机计划时间段设置的开始时间与结束时间格式应是<b>mm/dd/yyy hh:mm:ss</b>。\n");
			printf("如果你选择了<b>固定</b>选项，宕机时间将在指定的设置的开始时间到结束时间内起作用。如果没有选择<b>固定</b>选项，Nagios将力图\"柔性\"地来处理宕机时间。\n");
			printf("柔性的处理宕机时间将视服务宕机开始直接你设置的时间结束或是最后你发出阶段时间结束为止，这个阶段时间长度将不可控。\n");
			break;

		case CMD_ENABLE_HOST_FLAP_DETECTION:
			printf("该命令用于启用对指定主机的心跳监测，如果心跳检查程序被禁用，此选项无效。\n");
			break;

		case CMD_DISABLE_HOST_FLAP_DETECTION:
			printf("该命令用于禁用指定主机的心跳监测。\n");
			break;

		case CMD_ENABLE_SVC_FLAP_DETECTION:
			printf("该命令用于启用对指定服务的心跳监测，如果心跳检查程序被禁用，此选项无效。\n");
			break;

		case CMD_DISABLE_SVC_FLAP_DETECTION:
			printf("该命令用于禁用指定服务的心跳监测。\n");
			break;

		case CMD_ENABLE_FLAP_DETECTION:
			printf("该命令用于启用对主机和服务的心跳监测程序，独立的主机和服务可以禁用心跳监测。\n");
			break;

		case CMD_DISABLE_FLAP_DETECTION:
			printf("该命令用于禁用对主机和服务的心跳监测程序。\n");
			break;

		case CMD_ENABLE_HOSTGROUP_SVC_NOTIFICATIONS:
			printf("该命令用于启用指定主机组上全部服务的通知。通知仅在你设置的通知服务状态产生时才被发出。\n");
			printf("该命令<b>并不</b>启用主机组的状态检查通知除非你同时选择了\"启用主机组通知\"选项。\n");
			break;

		case CMD_DISABLE_HOSTGROUP_SVC_NOTIFICATIONS:
			printf("该命令用于禁用指定主机组所有服务的通知。当你的指定主机组相关的服务需要向外发出通知服务时，你需要重新启用通知选项。\n");
			printf("该命令<b>并不</b>禁用对指定主机组的检查通知除非你同时选择了\"禁用主机组通知\"选项。\n");
			break;

		case CMD_ENABLE_HOSTGROUP_HOST_NOTIFICATIONS:
			printf("该命令用于启用指定主机组上全部主机的通知。通知仅在你设置的通知主机状态产生时才被发出。\n");
			break;

		case CMD_DISABLE_HOSTGROUP_HOST_NOTIFICATIONS:
			printf("该命令用于禁用指定主机组所有主机的通知。当你的指定主机组相关的主机需要向外发出通知服务时，你需要重新启用通知选项。\n");
			break;

		case CMD_ENABLE_HOSTGROUP_SVC_CHECKS:
			printf("该命令用于启用指定主机组上的所有服务的主动检查。该命令<b>并不</b>启用对指定主机组主机的主动检查除非你同时选择了\"启用主机组主动检查\"选项。\n");
			break;

		case CMD_DISABLE_HOSTGROUP_SVC_CHECKS:
			printf("该命令用于禁用对特定主机组上的所有服务的主动检查。该命令<b>并不</b>禁用对主机组的主动检查除非你同时选择了\"禁用主机组主动检查\"选项。\n");
			break;

		case CMD_DEL_HOST_DOWNTIME:
			printf("该命令用于取消或放弃对指定主机的宕机时间的设置。\n");
			break;

		case CMD_DEL_SVC_DOWNTIME:
			printf("该命令用于取消或放弃对指定服务的宕机时间的设置。\n");
			break;

		case CMD_ENABLE_PERFORMANCE_DATA:
			printf("该命令用于启用对主机与服务性能数据处理程序设置。独立的主机和服务可以禁用服务性能数据处理程序。\n");
			break;

		case CMD_DISABLE_PERFORMANCE_DATA:
			printf("该命令用于禁用对主机与服务性能数据处理程序设置。\n");
			break;

		case CMD_SCHEDULE_HOSTGROUP_HOST_DOWNTIME:
			printf("该命令用于设置对指定主机组内的主机设置宕机计划时间段。在宕机时间段中，Nagios将不会发送相关主机的问题通知。\n");
			printf("在宕机计划时间段外，Nagios将按计划进行检查并发出问题通知。宕机时间调度在程序的关闭和重启中都会被保存。\n");
			printf("宕机计划时间段设置的开始时间与结束时间格式应是<b>mm/dd/yyy hh:mm:ss</b>。\n");
			printf("如果你选择了<b>固定</b>选项，宕机时间将在指定的设置的开始时间到结束时间内起作用。如果没有选择<b>固定</b>选项，Nagios将力图\"柔性\"地来处理宕机时间。\n");
			printf("柔性的处理宕机时间将视主机宕机开始直接你设置的时间结束或是最后你发出阶段时间结束为止，这个阶段时间长度将不可控。\n");
			break;

		case CMD_SCHEDULE_HOSTGROUP_SVC_DOWNTIME:
			printf("该命令用于设置对指定主机组内的全部服务设置宕机计划时间段。在宕机时间段中，Nagios将不会发送相关服务问题通知。\n");
			printf("在宕机计划时间段外，Nagios将按计划进行检查并发出问题通知。宕机时间调度在程序的关闭和重启中都会被保存。\n");
			printf("宕机计划时间段设置的开始时间与结束时间格式应是<b>mm/dd/yyy hh:mm:ss</b>。\n");
			printf("如果你选择了<b>固定</b>选项，宕机时间将在指定的设置的开始时间到结束时间内起作用。如果没有选择<b>固定</b>选项，Nagios将力图\"柔性\"地来处理宕机时间。\n");
			printf("柔性的处理宕机时间将视服务宕机开始直接你设置的时间结束或是最后你发出阶段时间结束为止，这个阶段时间长度将不可控。\n");
			printf("注意此服务宕机计划不会自动设置相关主机的宕机调度，如果你想配置主机组的所有主机宕机计划时间，勾选\"启用主机宕机调度\"选项\n");
			break;

		case CMD_START_EXECUTING_HOST_CHECKS:
			printf("该命令用于启用对主机用程序设置值进行主动检查。\n");
			break;

		case CMD_STOP_EXECUTING_HOST_CHECKS:
			printf("该命令用于禁用对主机用程序设置值进行主动检查。\n");
			break;

		case CMD_START_ACCEPTING_PASSIVE_HOST_CHECKS:
			printf("该命令用于开始Nagios的Obsessing over主机检查。需要阅读文档的分布式检测内容获得更多信息。\n");
			break;

		case CMD_STOP_ACCEPTING_PASSIVE_HOST_CHECKS:
			printf("该命令用于中止Nagios的Obsessing over主机检查。\n");
			break;

		case CMD_ENABLE_PASSIVE_HOST_CHECKS:
			printf("该命令用于启用Nagios接受由外部命令文件产生针对指定主机的被动检查结果。\n");
			break;

		case CMD_DISABLE_PASSIVE_HOST_CHECKS:
			printf("该命令用于禁用Nagios接受由外部命令文件产生针对指定主机的被动检查结果，所有被动检查结果都将被忽略。\n");
			break;

		case CMD_START_OBSESSING_OVER_HOST_CHECKS:
			printf("该命令用于开始Nagios的Obsessing over主机检查。需要阅读文档的分布式检测内容获得更多信息。\n");
			break;

		case CMD_STOP_OBSESSING_OVER_HOST_CHECKS:
			printf("该命令用于中止Nagios的Obsessing over主机检查。\n");
			break;

		case CMD_SCHEDULE_HOST_CHECK:
			printf("该命令是用于计划下次对主机的检查。Nagios在设置的特定时间内进行主机检查。\n");
			printf("如果选择了<b>强制检查</b>选项，Nagios将强制将强制检查主机状态而无视按设置时间段来检查（无论你设置过检查时间段与否）。\n");
			break;

		case CMD_START_OBSESSING_OVER_SVC:
			printf("该命令用于开始Nagios针对指定服务针对指定服务的Obsessing over检查。需要阅读文档的分布式检测内容获得更多信息。\n");
			break;

		case CMD_STOP_OBSESSING_OVER_SVC:
			printf("该命令用于中止Nagios针对指定服务的Obsessing over检查。\n");
			break;

		case CMD_START_OBSESSING_OVER_HOST:
			printf("该命令用于开始Nagios针对指定主机的Obsessing over检查。\n");
			break;

		case CMD_STOP_OBSESSING_OVER_HOST:
			printf("该命令用于中止Nagios针对指定主机的Obsessing over检查。\n");
			break;

		case CMD_ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS:
			printf("该命令用于启用指定服务组上全部服务的通知。通知仅在你设置的通知服务状态产生时才被发出。\n");
			printf("该命令<b>并不</b>启用主机的状态检查通知除非你同时选择了\"启用主机通知\"选项。\n");
			break;

		case CMD_DISABLE_SERVICEGROUP_SVC_NOTIFICATIONS:
			printf("该命令用于禁用指定服务组所有服务的通知。当你的指定服务组相关的服务需要向外发出通知服务时，你需要重新启用通知选项。\n");
			printf("该命令<b>并不</b>禁用对指定服务组主机的检查通知除非你同时选择了\"禁用主机通知\"选项。\n");
			break;

		case CMD_ENABLE_SERVICEGROUP_HOST_NOTIFICATIONS:
			printf("该命令用于启用指定服务组上全部主机的通知。通知仅在你设置的通知主机状态产生时才被发出。\n");
			break;

		case CMD_DISABLE_SERVICEGROUP_HOST_NOTIFICATIONS:
			printf("该命令用于禁用指定服务组所有主机的通知。当你的指定服务组相关的主机需要向外发出通知服务时，你需要重新启用通知选项。\n");
			break;

		case CMD_ENABLE_SERVICEGROUP_SVC_CHECKS:
			printf("该命令用于启用指定服务组上的所有服务的主动检查。该命令<b>并不</b>启用对指定服务组主机的主动检查除非你同时选择了\"启用主机主动检查\"选项。\n");
			break;

		case CMD_DISABLE_SERVICEGROUP_SVC_CHECKS:
			printf("该命令用于禁用指定服务组上的所有服务的主动检查。该命令<b>并不</b>禁用对指定服务组主机的主动检查除非你同时选择了\"禁用主机主动检查\"选项。\n");
			break;

		case CMD_SCHEDULE_SERVICEGROUP_HOST_DOWNTIME:
			printf("该命令用于设置对指定服务组内的全部主机设置宕机计划时间段。在宕机时间段中，Nagios将不会发送相关主机问题通知。\n");
			printf("在宕机计划时间段外，Nagios将按计划进行检查并发出问题通知。宕机时间调度在程序的关闭和重启中都会被保存。\n");
			printf("宕机计划时间段设置的开始时间与结束时间格式应是<b>mm/dd/yyy hh:mm:ss</b>。\n");
			printf("如果你选择了<b>固定</b>选项，宕机时间将在指定的设置的开始时间到结束时间内起作用。如果没有选择<b>固定</b>选项，Nagios将力图\"柔性\"地来处理宕机时间。\n");
			printf("柔性的处理宕机时间将视主机宕机开始直接你设置的时间结束或是最后你发出阶段时间结束为止，这个阶段时间长度将不可控。\n");
			break;

		case CMD_SCHEDULE_SERVICEGROUP_SVC_DOWNTIME:
			printf("该命令用于设置对指定服务组内的全部服务设置宕机计划时间段。在宕机时间段中，Nagios将不会发送相关服务问题通知。\n");
			printf("在宕机计划时间段外，Nagios将按计划进行检查并发出问题通知。宕机时间调度在程序的关闭和重启中都会被保存。\n");
			printf("宕机计划时间段设置的开始时间与结束时间格式应是<b>mm/dd/yyy hh:mm:ss</b>。\n");
			printf("如果你选择了<b>固定</b>选项，宕机时间将在指定的设置的开始时间到结束时间内起作用。如果没有选择<b>固定</b>选项，Nagios将力图\"柔性\"地来处理宕机时间。\n");
			printf("柔性的处理宕机时间将视服务宕机开始直接你设置的时间结束或是最后你发出阶段时间结束为止，这个阶段时间长度将不可控。\n");
			printf("注意此服务宕机计划不会自动设置相关主机的宕机调度，如果你想配置服务组的所有主机宕机计划时间，勾选\"启用主机宕机调度\"选项\n");
			break;

		case CMD_SEND_CUSTOM_HOST_NOTIFICATION:
		case CMD_SEND_CUSTOM_SVC_NOTIFICATION:
			printf("该命令用于发送由客户定制的特定的%s警告。它可被用于在对一些特定的系统或服务的监视状态下，当发生紧急情况时将你所需要的信息通知给管理员。\n", (cmd == CMD_SEND_CUSTOM_HOST_NOTIFICATION) ? "主机" : "服务");
			printf("Nagios一般是在警告通知逻辑中来处理客户定制警告。在对象定义里选择<i>强制(Forced)</i>选项来强制送出定制的通知而不必顾及时间周期设置、通知警告是否使能等等的设置；选择<i>广播式(Broadcast)</i>选项可以在全部正常情况下(非异常处理)发送通知给联系人。这些选项通常可以覆盖一般的报警逻辑里的设置而送出你所需要的重要信息。\n");
			break;

		default:
			printf("抱歉，无该命令的相关信息。");
		}

	printf("</TD></TR>\n");
	printf("</TABLE>\n");

	return;
	}



/* converts a time string to a UNIX timestamp, respecting the date_format option */
int string_to_time(char *buffer, time_t *t) {
	struct tm lt;
	int ret = 0;


	/* Initialize some variables just in case they don't get parsed
	   by the sscanf() call.  A better solution is to also check the
	   CGI input for validity, but this should suffice to prevent
	   strange problems if the input is not valid.
	   Jan 15 2003  Steve Bonds */
	lt.tm_mon = 0;
	lt.tm_mday = 1;
	lt.tm_year = 1900;
	lt.tm_hour = 0;
	lt.tm_min = 0;
	lt.tm_sec = 0;
	lt.tm_wday = 0;
	lt.tm_yday = 0;


	if(date_format == DATE_FORMAT_EURO)
		ret = sscanf(buffer, "%02d-%02d-%04d %02d:%02d:%02d", &lt.tm_mday, &lt.tm_mon, &lt.tm_year, &lt.tm_hour, &lt.tm_min, &lt.tm_sec);
	else if(date_format == DATE_FORMAT_ISO8601 || date_format == DATE_FORMAT_STRICT_ISO8601)
		ret = sscanf(buffer, "%04d-%02d-%02d%*[ T]%02d:%02d:%02d", &lt.tm_year, &lt.tm_mon, &lt.tm_mday, &lt.tm_hour, &lt.tm_min, &lt.tm_sec);
	else
		ret = sscanf(buffer, "%02d-%02d-%04d %02d:%02d:%02d", &lt.tm_mon, &lt.tm_mday, &lt.tm_year, &lt.tm_hour, &lt.tm_min, &lt.tm_sec);

	if(ret != 6)
		return ERROR;

	lt.tm_mon--;
	lt.tm_year -= 1900;

	/* tell mktime() to try and compute DST automatically */
	lt.tm_isdst = -1;

	*t = mktime(&lt);

	return OK;
	}
