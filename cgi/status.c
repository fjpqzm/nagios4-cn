/**************************************************************************
 *
 * STATUS.C -  Nagios Status CGI
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
 * along with Tthis program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *************************************************************************/

#include "../include/config.h"
#include "../include/common.h"
#include "../include/objects.h"
#include "../include/comments.h"
#include "../include/macros.h"
#include "../include/statusdata.h"

#include "../include/cgiutils.h"
#include "../include/getcgi.h"
#include "../include/cgiauth.h"

extern int             refresh_rate;
extern int			   result_limit;

extern char main_config_file[MAX_FILENAME_LENGTH];
extern char url_html_path[MAX_FILENAME_LENGTH];
extern char url_docs_path[MAX_FILENAME_LENGTH];
extern char url_images_path[MAX_FILENAME_LENGTH];
extern char url_stylesheets_path[MAX_FILENAME_LENGTH];
extern char url_logo_images_path[MAX_FILENAME_LENGTH];
extern char url_media_path[MAX_FILENAME_LENGTH];
extern char url_js_path[MAX_FILENAME_LENGTH];

extern char *http_charset;

extern char *service_critical_sound;
extern char *service_warning_sound;
extern char *service_unknown_sound;
extern char *host_down_sound;
extern char *host_unreachable_sound;
extern char *normal_sound;

extern char *notes_url_target;
extern char *action_url_target;

extern int suppress_alert_window;

extern int enable_splunk_integration;

extern int navbar_search_addresses;
extern int navbar_search_aliases;

extern hoststatus *hoststatus_list;
extern servicestatus *servicestatus_list;

static nagios_macros *mac;

#define MAX_MESSAGE_BUFFER		40960

#define DISPLAY_HOSTS			0
#define DISPLAY_HOSTGROUPS		1
#define DISPLAY_SERVICEGROUPS           2

#define STYLE_OVERVIEW			0
#define STYLE_DETAIL			1
#define STYLE_SUMMARY			2
#define STYLE_GRID                      3
#define STYLE_HOST_DETAIL               4


/* HOSTSORT structure */
typedef struct hostsort_struct {
	hoststatus *hststatus;
	struct hostsort_struct *next;
	} hostsort;

/* SERVICESORT structure */
typedef struct servicesort_struct {
	servicestatus *svcstatus;
	struct servicesort_struct *next;
	} servicesort;

hostsort *hostsort_list = NULL;
servicesort *servicesort_list = NULL;

int sort_services(int, int);						/* sorts services */
int sort_hosts(int, int);                                               /* sorts hosts */
int compare_servicesort_entries(int, int, servicesort *, servicesort *);	/* compares service sort entries */
int compare_hostsort_entries(int, int, hostsort *, hostsort *);         /* compares host sort entries */
void free_servicesort_list(void);
void free_hostsort_list(void);

void show_host_status_totals(void);
void show_service_status_totals(void);
void show_service_detail(void);
void show_host_detail(void);
void show_servicegroup_overviews(void);
void show_servicegroup_overview(servicegroup *);
void show_servicegroup_summaries(void);
void show_servicegroup_summary(servicegroup *, int);
void show_servicegroup_host_totals_summary(servicegroup *);
void show_servicegroup_service_totals_summary(servicegroup *);
void show_servicegroup_grids(void);
void show_servicegroup_grid(servicegroup *);
void show_hostgroup_overviews(void);
void show_hostgroup_overview(hostgroup *);
void show_servicegroup_hostgroup_member_overview(hoststatus *, int, void *);
void show_servicegroup_hostgroup_member_service_status_totals(char *, void *);
void show_hostgroup_summaries(void);
void show_hostgroup_summary(hostgroup *, int);
void show_hostgroup_host_totals_summary(hostgroup *);
void show_hostgroup_service_totals_summary(hostgroup *);
void show_hostgroup_grids(void);
void show_hostgroup_grid(hostgroup *);

void show_filters(void);
void create_pagenumbers(int total_entries, char *temp_url,int type_service);
void create_page_limiter(int result_limit,char *temp_url);

int passes_host_properties_filter(hoststatus *);
int passes_service_properties_filter(servicestatus *);

void document_header(int);
void document_footer(void);
int process_cgivars(void);


authdata current_authdata;
time_t current_time;

char alert_message[MAX_MESSAGE_BUFFER];
char *host_name = NULL;
char *host_address = NULL;
char *host_filter = NULL;
char *hostgroup_name = NULL;
char *servicegroup_name = NULL;
char *service_filter = NULL;
int host_alert = FALSE;
int show_all_hosts = TRUE;
int show_all_hostgroups = TRUE;
int show_all_servicegroups = TRUE;
int display_type = DISPLAY_HOSTS;
int overview_columns = 3;
int max_grid_width = 8;
int group_style_type = STYLE_OVERVIEW;
int navbar_search = FALSE;

/* experimental paging feature */
int temp_result_limit;
int page_start;
int limit_results = TRUE;


int service_status_types = SERVICE_PENDING | SERVICE_OK | SERVICE_UNKNOWN | SERVICE_WARNING | SERVICE_CRITICAL;
int all_service_status_types = SERVICE_PENDING | SERVICE_OK | SERVICE_UNKNOWN | SERVICE_WARNING | SERVICE_CRITICAL;

int host_status_types = HOST_PENDING | SD_HOST_UP | SD_HOST_DOWN | SD_HOST_UNREACHABLE;
int all_host_status_types = HOST_PENDING | SD_HOST_UP | SD_HOST_DOWN | SD_HOST_UNREACHABLE;

int all_service_problems = SERVICE_UNKNOWN | SERVICE_WARNING | SERVICE_CRITICAL;
int all_host_problems = SD_HOST_DOWN | SD_HOST_UNREACHABLE;

unsigned long host_properties = 0L;
unsigned long service_properties = 0L;




int sort_type = SORT_NONE;
int sort_option = SORT_HOSTNAME;

int problem_hosts_down = 0;
int problem_hosts_unreachable = 0;
int problem_services_critical = 0;
int problem_services_warning = 0;
int problem_services_unknown = 0;

int embedded = FALSE;
int display_header = TRUE;



int main(void) {
	char *sound = NULL;
	host *temp_host = NULL;
	hostgroup *temp_hostgroup = NULL;
	servicegroup *temp_servicegroup = NULL;
	int regex_i = 1, i = 0;
	int len;

	mac = get_global_macros();

	time(&current_time);

	/* get the arguments passed in the URL */
	process_cgivars();

	/* reset internal variables */
	reset_cgi_vars();

	cgi_init(document_header, document_footer, READ_ALL_OBJECT_DATA, READ_ALL_STATUS_DATA);

	/* initialize macros */
	init_macros();

	document_header(TRUE);

	/* get authentication information */
	get_authentication_information(&current_authdata);

	/* if a navbar search was performed, find the host by name, address or partial name */
	if(navbar_search == TRUE) {
		if(host_name != NULL && NULL != strstr(host_name, "*")) {
			/* allocate for 3 extra chars, ^, $ and \0 */
			host_filter = malloc(sizeof(char) * (strlen(host_name) * 2 + 3));
			len = strlen(host_name);
			for(i = 0; i < len; i++, regex_i++) {
				if(host_name[i] == '*') {
					host_filter[regex_i++] = '.';
					host_filter[regex_i] = '*';
					}
				else
					host_filter[regex_i] = host_name[i];
				}
			host_filter[0] = '^';
			host_filter[regex_i++] = '$';
			host_filter[regex_i] = '\0';
			}
		else {
			if((temp_host = find_host(host_name)) == NULL) {
				for(temp_host = host_list; temp_host != NULL; temp_host = temp_host->next) {
					if(is_authorized_for_host(temp_host, &current_authdata) == FALSE)
						continue;
					if(!strcmp(host_name, temp_host->address)) {
						host_address = strdup(temp_host->address);
						host_filter = malloc(sizeof(char) * (strlen(host_address) * 2 + 3));
						len = strlen(host_address);
						for(i = 0; i < len; i++, regex_i++) {
							host_filter[regex_i] = host_address[i];
						}
						host_filter[0] = '^';
						host_filter[regex_i++] = '$';
						host_filter[regex_i] = '\0';
						break;
						}
					}
				if(temp_host == NULL) {
					for(temp_host = host_list; temp_host != NULL; temp_host = temp_host->next) {
						if(is_authorized_for_host(temp_host, &current_authdata) == FALSE)
							continue;
						if((strstr(temp_host->name, host_name) == temp_host->name) || !strncasecmp(temp_host->name, host_name, strlen(host_name))) {
							free(host_name);
							host_name = strdup(temp_host->name);
							break;
							}
						}
					}
				}
			/* last effort, search hostgroups then servicegroups */
			if(temp_host == NULL) {
				if((temp_hostgroup = find_hostgroup(host_name)) != NULL) {
					display_type = DISPLAY_HOSTGROUPS;
					show_all_hostgroups = FALSE;
					free(host_name);
					hostgroup_name = strdup(temp_hostgroup->group_name);
					}
				else if((temp_servicegroup = find_servicegroup(host_name)) != NULL) {
					display_type = DISPLAY_SERVICEGROUPS;
					show_all_servicegroups = FALSE;
					free(host_name);
					servicegroup_name = strdup(temp_servicegroup->group_name);
					}
				}
			}
		}

	if(display_header == TRUE) {

		/* begin top table */
		printf("<table class='headertable'>\n");
		printf("<tr>\n");

		/* left column of the first row */
		printf("<td align=left valign=top width=33%%>\n");

		/* info table */
		display_info_table("当前网络状态", TRUE, &current_authdata);

		printf("<table class='linkBox'>\n");
		printf("<tr><td class='linkBox'>\n");

		if(display_type == DISPLAY_HOSTS) {
			printf("<a href='%s?host=%s'>%s的历史信息</a><br>\n", HISTORY_CGI, (show_all_hosts == TRUE) ? "all" : url_encode(host_name), (show_all_hosts == TRUE) ? "所有主机" : "该主机");
			printf("<a href='%s?host=%s'>%s的通知历史信息</a>\n", NOTIFICATIONS_CGI, (show_all_hosts == TRUE) ? "all" : url_encode(host_name), (show_all_hosts == TRUE) ? "所有主机" : "该主机");
			if(show_all_hosts == FALSE)
				printf("<br /><a href='%s?host=all'>所有主机的服务详细状态</a>\n", STATUS_CGI);
			else
				printf("<br /><a href='%s?hostgroup=all&style=hostdetail'>所有主机的主机详细状态</a>\n", STATUS_CGI);
			}
		else if(display_type == DISPLAY_SERVICEGROUPS) {
			if(show_all_servicegroups == FALSE) {

				if(group_style_type == STYLE_OVERVIEW || group_style_type == STYLE_GRID || group_style_type == STYLE_SUMMARY)
					printf("<a href='%s?servicegroup=%s&style=detail'>该服务组的服务详细状态</a><br>\n", STATUS_CGI, url_encode(servicegroup_name));
				if(group_style_type == STYLE_DETAIL || group_style_type == STYLE_GRID || group_style_type == STYLE_SUMMARY)
					printf("<a href='%s?servicegroup=%s&style=overview'>该服务组的状态概要</a><br>\n", STATUS_CGI, url_encode(servicegroup_name));
				if(group_style_type == STYLE_DETAIL || group_style_type == STYLE_OVERVIEW || group_style_type == STYLE_GRID)
					printf("<a href='%s?servicegroup=%s&style=summary'>该服务组的状态汇总</a><br>\n", STATUS_CGI, url_encode(servicegroup_name));
				if(group_style_type == STYLE_DETAIL || group_style_type == STYLE_OVERVIEW || group_style_type == STYLE_SUMMARY)
					printf("<a href='%s?servicegroup=%s&style=grid'>该服务组的服务状态表</a><br>\n", STATUS_CGI, url_encode(servicegroup_name));

				if(group_style_type == STYLE_DETAIL)
					printf("<a href='%s?servicegroup=all&style=detail'>所有服务组的服务详细状态</a><br>\n", STATUS_CGI);
				if(group_style_type == STYLE_OVERVIEW)
					printf("<a href='%s?servicegroup=all&style=overview'>所有服务组的状态概要</a><br>\n", STATUS_CGI);
				if(group_style_type == STYLE_SUMMARY)
					printf("<a href='%s?servicegroup=all&style=summary'>所有服务组的状态汇总</a><br>\n", STATUS_CGI);
				if(group_style_type == STYLE_GRID)
					printf("<a href='%s?servicegroup=all&style=grid'>所有服务组的服务状态表</a><br>\n", STATUS_CGI);

				}
			else {
				if(group_style_type == STYLE_OVERVIEW || group_style_type == STYLE_GRID || group_style_type == STYLE_SUMMARY)
					printf("<a href='%s?servicegroup=all&style=detail'>所有服务组的服务详细状态</a><br>\n", STATUS_CGI);
				if(group_style_type == STYLE_DETAIL || group_style_type == STYLE_GRID || group_style_type == STYLE_SUMMARY)
					printf("<a href='%s?servicegroup=all&style=overview'>所有服务组的状态概要</a><br>\n", STATUS_CGI);
				if(group_style_type == STYLE_DETAIL || group_style_type == STYLE_OVERVIEW || group_style_type == STYLE_GRID)
					printf("<a href='%s?servicegroup=all&style=summary'>所有服务组的状态汇总</a><br>\n", STATUS_CGI);
				if(group_style_type == STYLE_DETAIL || group_style_type == STYLE_OVERVIEW || group_style_type == STYLE_SUMMARY)
					printf("<a href='%s?servicegroup=all&style=grid'>所有服务组的服务状态表</a><br>\n", STATUS_CGI);
				}

			}
		else {
			if(show_all_hostgroups == FALSE) {

				if(group_style_type == STYLE_DETAIL)
					printf("<a href='%s?hostgroup=all&style=detail'>所有主机组的服务详细状态</a><br>\n", STATUS_CGI);
				if(group_style_type == STYLE_HOST_DETAIL)
					printf("<a href='%s?hostgroup=all&style=hostdetail'>所有主机组的主机详细状态</a><br>\n", STATUS_CGI);
				if(group_style_type == STYLE_OVERVIEW)
					printf("<a href='%s?hostgroup=all&style=overview'>所有主机组的状态概要</a><br>\n", STATUS_CGI);
				if(group_style_type == STYLE_SUMMARY)
					printf("<a href='%s?hostgroup=all&style=summary'>所有主机组的状态汇总</a><br>\n", STATUS_CGI);
				if(group_style_type == STYLE_GRID)
					printf("<a href='%s?hostgroup=all&style=grid'>所有主机组的状态表</a><br>\n", STATUS_CGI);

				if(group_style_type == STYLE_OVERVIEW || group_style_type == STYLE_SUMMARY || group_style_type == STYLE_GRID || group_style_type == STYLE_HOST_DETAIL)
					printf("<a href='%s?hostgroup=%s&style=detail'>该主机组的服务详细状态</a><br>\n", STATUS_CGI, url_encode(hostgroup_name));
				if(group_style_type == STYLE_OVERVIEW || group_style_type == STYLE_DETAIL || group_style_type == STYLE_SUMMARY || group_style_type == STYLE_GRID)
					printf("<a href='%s?hostgroup=%s&style=hostdetail'>该主机组的主机详细状态</a><br>\n", STATUS_CGI, url_encode(hostgroup_name));
				if(group_style_type == STYLE_DETAIL || group_style_type == STYLE_SUMMARY || group_style_type == STYLE_GRID || group_style_type == STYLE_HOST_DETAIL)
					printf("<a href='%s?hostgroup=%s&style=overview'>该主机组的状态概要</a><br>\n", STATUS_CGI, url_encode(hostgroup_name));
				if(group_style_type == STYLE_OVERVIEW || group_style_type == STYLE_DETAIL || group_style_type == STYLE_GRID || group_style_type == STYLE_HOST_DETAIL)
					printf("<a href='%s?hostgroup=%s&style=summary'>该主机组的状态汇总</a><br>\n", STATUS_CGI, url_encode(hostgroup_name));
				if(group_style_type == STYLE_OVERVIEW || group_style_type == STYLE_DETAIL || group_style_type == STYLE_SUMMARY || group_style_type == STYLE_HOST_DETAIL)
					printf("<a href='%s?hostgroup=%s&style=grid'>该主机组的状态表</a><br>\n", STATUS_CGI, url_encode(hostgroup_name));
				}
			else {
				if(group_style_type == STYLE_OVERVIEW || group_style_type == STYLE_SUMMARY || group_style_type == STYLE_GRID || group_style_type == STYLE_HOST_DETAIL)
					printf("<a href='%s?hostgroup=all&style=detail'>所有主机组的服务详细状态</a><br>\n", STATUS_CGI);
				if(group_style_type == STYLE_OVERVIEW || group_style_type == STYLE_DETAIL || group_style_type == STYLE_SUMMARY || group_style_type == STYLE_GRID)
					printf("<a href='%s?hostgroup=all&style=hostdetail'>所有主机组的主机详细状态</a><br>\n", STATUS_CGI);
				if(group_style_type == STYLE_DETAIL || group_style_type == STYLE_SUMMARY || group_style_type == STYLE_GRID || group_style_type == STYLE_HOST_DETAIL)
					printf("<a href='%s?hostgroup=all&style=overview'>所有主机组的状态概要</a><br>\n", STATUS_CGI);
				if(group_style_type == STYLE_OVERVIEW || group_style_type == STYLE_DETAIL || group_style_type == STYLE_GRID || group_style_type == STYLE_HOST_DETAIL)
					printf("<a href='%s?hostgroup=all&style=summary'>所有主机组的状态汇总</a><br>\n", STATUS_CGI);
				if(group_style_type == STYLE_OVERVIEW || group_style_type == STYLE_DETAIL || group_style_type == STYLE_SUMMARY || group_style_type == STYLE_HOST_DETAIL)
					printf("<a href='%s?hostgroup=all&style=grid'>所有主机组的状态表</a><br>\n", STATUS_CGI);
				}
			}

		printf("</td></tr>\n");
		printf("</table>\n");

		printf("</td>\n");

		/* middle column of top row */
		printf("<td align=center valign=top width=33%%>\n");
		show_host_status_totals();
		printf("</td>\n");

		/* right hand column of top row */
		printf("<td align=center valign=top width=33%%>\n");
		show_service_status_totals();
		printf("</td>\n");

		/* display context-sensitive help */
		printf("<td align=right valign=bottom>\n");
		if(display_type == DISPLAY_HOSTS)
			display_context_help(CONTEXTHELP_STATUS_DETAIL);
		else if(display_type == DISPLAY_SERVICEGROUPS) {
			if(group_style_type == STYLE_HOST_DETAIL)
				display_context_help(CONTEXTHELP_STATUS_DETAIL);
			else if(group_style_type == STYLE_OVERVIEW)
				display_context_help(CONTEXTHELP_STATUS_SGOVERVIEW);
			else if(group_style_type == STYLE_SUMMARY)
				display_context_help(CONTEXTHELP_STATUS_SGSUMMARY);
			else if(group_style_type == STYLE_GRID)
				display_context_help(CONTEXTHELP_STATUS_SGGRID);
			}
		else {
			if(group_style_type == STYLE_HOST_DETAIL)
				display_context_help(CONTEXTHELP_STATUS_HOST_DETAIL);
			else if(group_style_type == STYLE_OVERVIEW)
				display_context_help(CONTEXTHELP_STATUS_HGOVERVIEW);
			else if(group_style_type == STYLE_SUMMARY)
				display_context_help(CONTEXTHELP_STATUS_HGSUMMARY);
			else if(group_style_type == STYLE_GRID)
				display_context_help(CONTEXTHELP_STATUS_HGGRID);
			}
		printf("</td>\n");

		/* end of top table */
		printf("</tr>\n");
		printf("</table>\n");
		}


	/* embed sound tag if necessary... */
	if(problem_hosts_unreachable > 0 && host_unreachable_sound != NULL)
		sound = host_unreachable_sound;
	else if(problem_hosts_down > 0 && host_down_sound != NULL)
		sound = host_down_sound;
	else if(problem_services_critical > 0 && service_critical_sound != NULL)
		sound = service_critical_sound;
	else if(problem_services_warning > 0 && service_warning_sound != NULL)
		sound = service_warning_sound;
	else if(problem_services_unknown > 0 && service_unknown_sound != NULL)
		sound = service_unknown_sound;
	else if(problem_services_unknown == 0 && problem_services_warning == 0 && problem_services_critical == 0 && problem_hosts_down == 0 && problem_hosts_unreachable == 0 && normal_sound != NULL)
		sound = normal_sound;
	if(sound != NULL) {
		printf("<object type=\"audio/x-wav\" data=\"%s%s\" height=\"1\" width=\"1\">", url_media_path, sound);
		printf("<param name=\"filename\" value=\"%s%s\">", url_media_path, sound);
		printf("<param name=\"autostart\" value=\"true\">");
		printf("<param name=\"playcount\" value=\"1\">");
		printf("</object>");
		}


	/* bottom portion of screen - service or hostgroup detail */
	if(display_type == DISPLAY_HOSTS)
		show_service_detail();
	else if(display_type == DISPLAY_SERVICEGROUPS) {
		if(group_style_type == STYLE_OVERVIEW)
			show_servicegroup_overviews();
		else if(group_style_type == STYLE_SUMMARY)
			show_servicegroup_summaries();
		else if(group_style_type == STYLE_GRID)
			show_servicegroup_grids();
		else if(group_style_type == STYLE_HOST_DETAIL)
			show_host_detail();
		else
			show_service_detail();
		}
	else {
		if(group_style_type == STYLE_OVERVIEW)
			show_hostgroup_overviews();
		else if(group_style_type == STYLE_SUMMARY)
			show_hostgroup_summaries();
		else if(group_style_type == STYLE_GRID)
			show_hostgroup_grids();
		else if(group_style_type == STYLE_HOST_DETAIL)
			show_host_detail();
		else
			show_service_detail();
		}

	document_footer();

	/* free all allocated memory */
	free_memory();
	free_comment_data();

	/* free memory allocated to the sort lists */
	free_servicesort_list();
	free_hostsort_list();

	return OK;
	}


void document_header(int use_stylesheet) {
	char date_time[MAX_DATETIME_LENGTH];
	time_t expire_time;

	printf("Cache-Control: no-store\r\n");
	printf("Pragma: no-cache\r\n");
	printf("Refresh: %d\r\n", refresh_rate);

	get_time_string(&current_time, date_time, (int)sizeof(date_time), HTTP_DATE_TIME);
	printf("Last-Modified: %s\r\n", date_time);

	expire_time = (time_t)0L;
	get_time_string(&expire_time, date_time, (int)sizeof(date_time), HTTP_DATE_TIME);
	printf("Expires: %s\r\n", date_time);

	printf("Content-type: text/html; charset=\"%s\"\r\n\r\n", http_charset);

	if(embedded == TRUE)
		return;

	printf("<html>\n");
	printf("<head>\n");
	printf("<link rel=\"shortcut icon\" href=\"%sfavicon.ico\" type=\"image/ico\">\n", url_images_path);
	printf("<title>\n");
	printf("当前网络状态\n");
	printf("</title>\n");

	if(use_stylesheet == TRUE) {
		printf("<link rel='stylesheet' type='text/css' href='%s%s' />\n", url_stylesheets_path, COMMON_CSS);
		printf("<link rel='stylesheet' type='text/css' href='%s%s' />\n", url_stylesheets_path, STATUS_CSS);
		}

	/* added jquery library 1/31/2012 */
	printf("<script type='text/javascript' src='%s%s'></script>\n",url_js_path, JQUERY_JS);
	/* JS function to append content to elements on page */
	printf("<script type='text/javascript'>\n");
	printf("$(document).ready(function() { $('#top_page_numbers').append($('#bottom_page_numbers').html() ); });");
	printf("function set_limit(url) { \nthis.location = url+'&limit='+$('#limit').val();\n  }");
	printf("</script>\n");

	printf("</head>\n");

	printf("<body class='status'>\n");

	/* include user SSI header */
	include_ssi_files(STATUS_CGI, SSI_HEADER);

	return;
	}


void document_footer(void) {

	if(embedded == TRUE)
		return;

	/* include user SSI footer */
	include_ssi_files(STATUS_CGI, SSI_FOOTER);

	printf("</body>\n");
	printf("</html>\n");

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

		/* we found the navbar search argument */
		else if(!strcmp(variables[x], "navbarsearch")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}
			navbar_search = TRUE;
			}

		/* we found the hostgroup argument */
		else if(!strcmp(variables[x], "hostgroup")) {
			display_type = DISPLAY_HOSTGROUPS;
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			hostgroup_name = (char *)strdup(variables[x]);
			strip_html_brackets(hostgroup_name);

			if(hostgroup_name != NULL && !strcmp(hostgroup_name, "all"))
				show_all_hostgroups = TRUE;
			else
				show_all_hostgroups = FALSE;
			}

		/* we found the servicegroup argument */
		else if(!strcmp(variables[x], "servicegroup")) {
			display_type = DISPLAY_SERVICEGROUPS;
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			servicegroup_name = strdup(variables[x]);
			strip_html_brackets(servicegroup_name);

			if(servicegroup_name != NULL && !strcmp(servicegroup_name, "all"))
				show_all_servicegroups = TRUE;
			else
				show_all_servicegroups = FALSE;
			}

		/* we found the host argument */
		else if(!strcmp(variables[x], "host")) {
			display_type = DISPLAY_HOSTS;
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			host_name = strdup(variables[x]);
			strip_html_brackets(host_name);

			if(host_name != NULL && !strcmp(host_name, "all"))
				show_all_hosts = TRUE;
			else
				show_all_hosts = FALSE;
			}

		/* we found the columns argument */
		else if(!strcmp(variables[x], "columns")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			overview_columns = atoi(variables[x]);
			if(overview_columns <= 0)
				overview_columns = 1;
			}

		/* we found the service status type argument */
		else if(!strcmp(variables[x], "servicestatustypes")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			service_status_types = atoi(variables[x]);
			}

		/* we found the host status type argument */
		else if(!strcmp(variables[x], "hoststatustypes")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			host_status_types = atoi(variables[x]);
			}

		/* we found the service properties argument */
		else if(!strcmp(variables[x], "serviceprops")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			service_properties = strtoul(variables[x], NULL, 10);
			}

		/* we found the host properties argument */
		else if(!strcmp(variables[x], "hostprops")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			host_properties = strtoul(variables[x], NULL, 10);
			}

		/* we found the host or service group style argument */
		else if(!strcmp(variables[x], "style")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			if(!strcmp(variables[x], "overview"))
				group_style_type = STYLE_OVERVIEW;
			else if(!strcmp(variables[x], "detail"))
				group_style_type = STYLE_DETAIL;
			else if(!strcmp(variables[x], "summary"))
				group_style_type = STYLE_SUMMARY;
			else if(!strcmp(variables[x], "grid"))
				group_style_type = STYLE_GRID;
			else if(!strcmp(variables[x], "hostdetail"))
				group_style_type = STYLE_HOST_DETAIL;
			else
				group_style_type = STYLE_DETAIL;
			}

		/* we found the sort type argument */
		else if(!strcmp(variables[x], "sorttype")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			sort_type = atoi(variables[x]);
			}

		/* we found the sort option argument */
		else if(!strcmp(variables[x], "sortoption")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}

			sort_option = atoi(variables[x]);
			}

		/* we found the embed option */
		else if(!strcmp(variables[x], "embedded"))
			embedded = TRUE;

		/* we found the noheader option */
		else if(!strcmp(variables[x], "noheader"))
			display_header = FALSE;

		/* servicefilter cgi var */
		else if(!strcmp(variables[x], "servicefilter")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}
			service_filter = strdup(variables[x]);
			strip_html_brackets(service_filter);
			}

		/* experimental page limit feature */
		else if(!strcmp(variables[x], "start")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}
			page_start = atoi(variables[x]);
			}
		else if(!strcmp(variables[x], "limit")) {
			x++;
			if(variables[x] == NULL) {
				error = TRUE;
				break;
				}
			temp_result_limit = atoi(variables[x]);
			if(temp_result_limit == 0)
				limit_results = FALSE;
			else
				limit_results = TRUE;
			}

		}


	/* free memory allocated to the CGI variables */
	free_cgivars(variables);

	return error;
	}



/* display table with service status totals... */
void show_service_status_totals(void) {
	int total_ok = 0;
	int total_warning = 0;
	int total_unknown = 0;
	int total_critical = 0;
	int total_pending = 0;
	int total_services = 0;
	int total_problems = 0;
	servicestatus *temp_servicestatus;
	service *temp_service;
	host *temp_host;
	int count_service;


	/* check the status of all services... */
	for(temp_servicestatus = servicestatus_list; temp_servicestatus != NULL; temp_servicestatus = temp_servicestatus->next) {

		/* find the host and service... */
		temp_host = find_host(temp_servicestatus->host_name);
		temp_service = find_service(temp_servicestatus->host_name, temp_servicestatus->description);

		/* make sure user has rights to see this service... */
		if(is_authorized_for_service(temp_service, &current_authdata) == FALSE)
			continue;

		count_service = 0;

		if(display_type == DISPLAY_HOSTS && (show_all_hosts == TRUE || !strcmp(host_name, temp_servicestatus->host_name)))
			count_service = 1;
		else if(display_type == DISPLAY_SERVICEGROUPS && (show_all_servicegroups == TRUE || (is_service_member_of_servicegroup(find_servicegroup(servicegroup_name), temp_service) == TRUE)))
			count_service = 1;
		else if(display_type == DISPLAY_HOSTGROUPS && (show_all_hostgroups == TRUE || (is_host_member_of_hostgroup(find_hostgroup(hostgroup_name), temp_host) == TRUE)))
			count_service = 1;

		if(count_service) {

			if(temp_servicestatus->status == SERVICE_CRITICAL) {
				total_critical++;
				if(temp_servicestatus->problem_has_been_acknowledged == FALSE && (temp_servicestatus->checks_enabled == TRUE || temp_servicestatus->accept_passive_checks == TRUE) && temp_servicestatus->notifications_enabled == TRUE && temp_servicestatus->scheduled_downtime_depth == 0)
					problem_services_critical++;
				}
			else if(temp_servicestatus->status == SERVICE_WARNING) {
				total_warning++;
				if(temp_servicestatus->problem_has_been_acknowledged == FALSE && (temp_servicestatus->checks_enabled == TRUE || temp_servicestatus->accept_passive_checks == TRUE) && temp_servicestatus->notifications_enabled == TRUE && temp_servicestatus->scheduled_downtime_depth == 0)
					problem_services_warning++;
				}
			else if(temp_servicestatus->status == SERVICE_UNKNOWN) {
				total_unknown++;
				if(temp_servicestatus->problem_has_been_acknowledged == FALSE && (temp_servicestatus->checks_enabled == TRUE || temp_servicestatus->accept_passive_checks == TRUE) && temp_servicestatus->notifications_enabled == TRUE && temp_servicestatus->scheduled_downtime_depth == 0)
					problem_services_unknown++;
				}
			else if(temp_servicestatus->status == SERVICE_OK)
				total_ok++;
			else if(temp_servicestatus->status == SERVICE_PENDING)
				total_pending++;
			else
				total_ok++;
			}
		}

	total_services = total_ok + total_unknown + total_warning + total_critical + total_pending;
	total_problems = total_unknown + total_warning + total_critical;


	printf("<div class='serviceTotals'>服务状态概要</div>\n");

	printf("<table border='0' cellspacing='0' cellpadding='0'>\n");
	printf("<tr><td>\n");

	printf("<table class='serviceTotals'>\n");
	printf("<tr>\n");

	printf("<th class='serviceTotals'>");
	printf("<a class='serviceTotals' href='%s?", STATUS_CGI);
		/* paging */
	if(temp_result_limit)
		printf("limit=%i&",temp_result_limit);
	if(display_type == DISPLAY_HOSTS)
		printf("host=%s", (host_name == NULL) ? "all" : url_encode(host_name));
	else if(display_type == DISPLAY_SERVICEGROUPS)
		printf("servicegroup=%s&style=detail", url_encode(servicegroup_name));
	else
		printf("hostgroup=%s&style=detail", url_encode(hostgroup_name));
	printf("&servicestatustypes=%d", SERVICE_OK);
	printf("&hoststatustypes=%d'>", host_status_types);
	printf("正常</a></th>\n");

	printf("<th class='serviceTotals'>");
	printf("<a class='serviceTotals' href='%s?", STATUS_CGI);
		/* paging */
	if(temp_result_limit)
		printf("limit=%i&",temp_result_limit);
	if(display_type == DISPLAY_HOSTS)
		printf("host=%s", (host_name == NULL) ? "all" : url_encode(host_name));
	else if(display_type == DISPLAY_SERVICEGROUPS)
		printf("servicegroup=%s&style=detail", url_encode(servicegroup_name));
	else
		printf("hostgroup=%s&style=detail", url_encode(hostgroup_name));
	printf("&servicestatustypes=%d", SERVICE_WARNING);
	printf("&hoststatustypes=%d'>", host_status_types);
	printf("警告</a></th>\n");

	printf("<th class='serviceTotals'>");
	printf("<a class='serviceTotals' href='%s?", STATUS_CGI);
		/* paging */
	if(temp_result_limit)
		printf("limit=%i&",temp_result_limit);
	if(display_type == DISPLAY_HOSTS)
		printf("host=%s", (host_name == NULL) ? "all" : url_encode(host_name));
	else if(display_type == DISPLAY_SERVICEGROUPS)
		printf("servicegroup=%s&style=detail", url_encode(servicegroup_name));
	else
		printf("hostgroup=%s&style=detail", url_encode(hostgroup_name));
	printf("&servicestatustypes=%d", SERVICE_UNKNOWN);
	printf("&hoststatustypes=%d'>", host_status_types);
	printf("未知</a></th>\n");

	printf("<th class='serviceTotals'>");
	printf("<a class='serviceTotals' href='%s?", STATUS_CGI);
		/* paging */
	if(temp_result_limit)
		printf("limit=%i&",temp_result_limit);
	if(display_type == DISPLAY_HOSTS)
		printf("host=%s", (host_name == NULL) ? "all" : url_encode(host_name));
	else if(display_type == DISPLAY_SERVICEGROUPS)
		printf("servicegroup=%s&style=detail", url_encode(servicegroup_name));
	else
		printf("hostgroup=%s&style=detail", url_encode(hostgroup_name));
	printf("&servicestatustypes=%d", SERVICE_CRITICAL);
	printf("&hoststatustypes=%d'>", host_status_types);
	printf("紧急</a></th>\n");

	printf("<th class='serviceTotals'>");
	printf("<a class='serviceTotals' href='%s?", STATUS_CGI);
		/* paging */
	if(temp_result_limit)
		printf("limit=%i&",temp_result_limit);
	if(display_type == DISPLAY_HOSTS)
		printf("host=%s", (host_name == NULL) ? "all" : url_encode(host_name));
	else if(display_type == DISPLAY_SERVICEGROUPS)
		printf("servicegroup=%s&style=detail", url_encode(servicegroup_name));
	else
		printf("hostgroup=%s&style=detail", url_encode(hostgroup_name));
	printf("&servicestatustypes=%d", SERVICE_PENDING);
	printf("&hoststatustypes=%d'>", host_status_types);
	printf("未决</a></th>\n");

	printf("</tr>\n");

	printf("<tr>\n");


	/* total services ok */
	printf("<td class='serviceTotals%s'>%d</td>\n", (total_ok > 0) ? "OK" : "", total_ok);

	/* total services in warning state */
	printf("<td class='serviceTotals%s'>%d</td>\n", (total_warning > 0) ? "WARNING" : "", total_warning);

	/* total services in unknown state */
	printf("<td class='serviceTotals%s'>%d</td>\n", (total_unknown > 0) ? "UNKNOWN" : "", total_unknown);

	/* total services in critical state */
	printf("<td class='serviceTotals%s'>%d</td>\n", (total_critical > 0) ? "CRITICAL" : "", total_critical);

	/* total services in pending state */
	printf("<td class='serviceTotals%s'>%d</td>\n", (total_pending > 0) ? "PENDING" : "", total_pending);


	printf("</tr>\n");
	printf("</table>\n");

	printf("</td></tr><tr><td align='center'>\n");

	printf("<table class='serviceTotals'>\n");
	printf("<tr>\n");

	printf("<th class='serviceTotals'>");
	printf("<a class='serviceTotals' href='%s?", STATUS_CGI);
		/* paging */
	if(temp_result_limit)
		printf("limit=%i&",temp_result_limit);
	if(display_type == DISPLAY_HOSTS)
		printf("host=%s", (host_name == NULL) ? "all" : url_encode(host_name));
	else if(display_type == DISPLAY_SERVICEGROUPS)
		printf("servicegroup=%s&style=detail", url_encode(servicegroup_name));
	else
		printf("hostgroup=%s&style=detail", url_encode(hostgroup_name));
	printf("&servicestatustypes=%d", SERVICE_UNKNOWN | SERVICE_WARNING | SERVICE_CRITICAL);
	printf("&hoststatustypes=%d'>", host_status_types);
	printf("<em>所有故障</em></a></th>\n");

	printf("<th class='serviceTotals'>");
	printf("<a class='serviceTotals' href='%s?", STATUS_CGI);
		/* paging */
	if(temp_result_limit)
		printf("limit=%i&",temp_result_limit);
	if(display_type == DISPLAY_HOSTS)
		printf("host=%s", (host_name == NULL) ? "all" : url_encode(host_name));
	else if(display_type == DISPLAY_SERVICEGROUPS)
		printf("servicegroup=%s&style=detail", url_encode(servicegroup_name));
	else
		printf("hostgroup=%s&style=detail", url_encode(hostgroup_name));
	printf("&hoststatustypes=%d'>", host_status_types);
	printf("<em>所有类型</em></a></th>\n");


	printf("</tr><tr>\n");

	/* total service problems */
	printf("<td class='serviceTotals%s'>%d</td>\n", (total_problems > 0) ? "PROBLEMS" : "", total_problems);

	/* total services */
	printf("<td class='serviceTotals'>%d</td>\n", total_services);

	printf("</tr>\n");
	printf("</table>\n");

	printf("</td></tr>\n");
	printf("</table>\n");

	printf("</div>\n");

	return;
	}


/* display a table with host status totals... */
void show_host_status_totals(void) {
	int total_up = 0;
	int total_down = 0;
	int total_unreachable = 0;
	int total_pending = 0;
	int total_hosts = 0;
	int total_problems = 0;
	hoststatus *temp_hoststatus;
	host *temp_host;
	int count_host;


	/* check the status of all hosts... */
	for(temp_hoststatus = hoststatus_list; temp_hoststatus != NULL; temp_hoststatus = temp_hoststatus->next) {

		/* find the host... */
		temp_host = find_host(temp_hoststatus->host_name);

		/* make sure user has rights to view this host */
		if(is_authorized_for_host(temp_host, &current_authdata) == FALSE)
			continue;

		count_host = 0;

		if(display_type == DISPLAY_HOSTS && (show_all_hosts == TRUE || !strcmp(host_name, temp_hoststatus->host_name)))
			count_host = 1;
		else if(display_type == DISPLAY_SERVICEGROUPS) {
			if(show_all_servicegroups == TRUE) {
				count_host = 1;
				}
			else if(is_host_member_of_servicegroup(find_servicegroup(servicegroup_name), temp_host) == TRUE) {
				count_host = 1;
				}
			}
		else if(display_type == DISPLAY_HOSTGROUPS && (show_all_hostgroups == TRUE || (is_host_member_of_hostgroup(find_hostgroup(hostgroup_name), temp_host) == TRUE)))
			count_host = 1;

		if(count_host) {

			if(temp_hoststatus->status == SD_HOST_UP)
				total_up++;
			else if(temp_hoststatus->status == SD_HOST_DOWN) {
				total_down++;
				if(temp_hoststatus->problem_has_been_acknowledged == FALSE && temp_hoststatus->notifications_enabled == TRUE && temp_hoststatus->checks_enabled == TRUE && temp_hoststatus->scheduled_downtime_depth == 0)
					problem_hosts_down++;
				}
			else if(temp_hoststatus->status == SD_HOST_UNREACHABLE) {
				total_unreachable++;
				if(temp_hoststatus->problem_has_been_acknowledged == FALSE && temp_hoststatus->notifications_enabled == TRUE && temp_hoststatus->checks_enabled == TRUE && temp_hoststatus->scheduled_downtime_depth == 0)
					problem_hosts_unreachable++;
				}

			else if(temp_hoststatus->status == HOST_PENDING)
				total_pending++;
			else
				total_up++;
			}
		}

	total_hosts = total_up + total_down + total_unreachable + total_pending;
	total_problems = total_down + total_unreachable;

	printf("<div class='hostTotals'>主机状态概要</div>\n");

	printf("<table border=0 cellspacing=0 cellpadding=0>\n");
	printf("<tr><td>\n");


	printf("<table class='hostTotals'>\n");
	printf("<tr>\n");

	printf("<th class='hostTotals'>");
	printf("<a class='hostTotals' href='%s?", STATUS_CGI);
	/* paging */
	if(temp_result_limit)
		printf("limit=%i&",temp_result_limit);
	if(display_type == DISPLAY_HOSTS)
		printf("host=%s", (host_name == NULL) ? "all" : url_encode(host_name));
	else if(display_type == DISPLAY_SERVICEGROUPS)
		printf("servicegroup=%s", url_encode(servicegroup_name));
	else {
		printf("hostgroup=%s", url_encode(hostgroup_name));
		if((service_status_types != all_service_status_types) || group_style_type == STYLE_DETAIL)
			printf("&style=detail");
		else if(group_style_type == STYLE_HOST_DETAIL)
			printf("&style=hostdetail");
		}
	if(service_status_types != all_service_status_types)
		printf("&servicestatustypes=%d", service_status_types);
	printf("&hoststatustypes=%d'>", SD_HOST_UP);
	printf("运行</a></th>\n");

	printf("<th class='hostTotals'>");
	printf("<a class='hostTotals' href='%s?", STATUS_CGI);
	/* paging */
	if(temp_result_limit)
		printf("limit=%i&",temp_result_limit);
	if(display_type == DISPLAY_HOSTS)
		printf("host=%s", (host_name == NULL) ? "all" : url_encode(host_name));
	else if(display_type == DISPLAY_SERVICEGROUPS)
		printf("servicegroup=%s", url_encode(servicegroup_name));
	else {
		printf("hostgroup=%s", url_encode(hostgroup_name));
		if((service_status_types != all_service_status_types) || group_style_type == STYLE_DETAIL)
			printf("&style=detail");
		else if(group_style_type == STYLE_HOST_DETAIL)
			printf("&style=hostdetail");
		}
	if(service_status_types != all_service_status_types)
		printf("&servicestatustypes=%d", service_status_types);
	printf("&hoststatustypes=%d'>", SD_HOST_DOWN);
	printf("宕机</a></th>\n");

	printf("<th class='hostTotals'>");
	printf("<a class='hostTotals' href='%s?", STATUS_CGI);
	/* paging */
	if(temp_result_limit)
		printf("limit=%i&",temp_result_limit);
	if(display_type == DISPLAY_HOSTS)
		printf("host=%s", (host_name == NULL) ? "all" : url_encode(host_name));
	else if(display_type == DISPLAY_SERVICEGROUPS)
		printf("servicegroup=%s", url_encode(servicegroup_name));
	else {
		printf("hostgroup=%s", url_encode(hostgroup_name));
		if((service_status_types != all_service_status_types) || group_style_type == STYLE_DETAIL)
			printf("&style=detail");
		else if(group_style_type == STYLE_HOST_DETAIL)
			printf("&style=hostdetail");
		}
	if(service_status_types != all_service_status_types)
		printf("&servicestatustypes=%d", service_status_types);
	printf("&hoststatustypes=%d'>", SD_HOST_UNREACHABLE);
	printf("不可达</a></th>\n");

	printf("<th class='hostTotals'>");
	printf("<a class='hostTotals' href='%s?", STATUS_CGI);
	/* paging */
	if(temp_result_limit)
		printf("limit=%i&",temp_result_limit);
	if(display_type == DISPLAY_HOSTS)
		printf("host=%s", (host_name == NULL) ? "all" : url_encode(host_name));
	else if(display_type == DISPLAY_SERVICEGROUPS)
		printf("servicegroup=%s", url_encode(servicegroup_name));
	else {
		printf("hostgroup=%s", url_encode(hostgroup_name));
		if((service_status_types != all_service_status_types) || group_style_type == STYLE_DETAIL)
			printf("&style=detail");
		else if(group_style_type == STYLE_HOST_DETAIL)
			printf("&style=hostdetail");
		}
	if(service_status_types != all_service_status_types)
		printf("&servicestatustypes=%d", service_status_types);
	printf("&hoststatustypes=%d'>", HOST_PENDING);
	printf("未决</a></th>\n");

	printf("</tr>\n");


	printf("<tr>\n");

	/* total hosts up */
	printf("<td class='hostTotals%s'>%d</td>\n", (total_up > 0) ? "UP" : "", total_up);

	/* total hosts down */
	printf("<td class='hostTotals%s'>%d</td>\n", (total_down > 0) ? "DOWN" : "", total_down);

	/* total hosts unreachable */
	printf("<td class='hostTotals%s'>%d</td>\n", (total_unreachable > 0) ? "UNREACHABLE" : "", total_unreachable);

	/* total hosts pending */
	printf("<td class='hostTotals%s'>%d</td>\n", (total_pending > 0) ? "PENDING" : "", total_pending);

	printf("</tr>\n");
	printf("</table>\n");

	printf("</td></tr><tr><td align='center'>\n");

	printf("<table class='hostTotals'>\n");
	printf("<tr>\n");

	printf("<th class='hostTotals'>");
	printf("<a class='hostTotals' href='%s?", STATUS_CGI);
	/* paging */
	if(temp_result_limit)
		printf("limit=%i&",temp_result_limit);
	if(display_type == DISPLAY_HOSTS)
		printf("host=%s", (host_name == NULL) ? "all" : url_encode(host_name));
	else if(display_type == DISPLAY_SERVICEGROUPS)
		printf("servicegroup=%s", url_encode(servicegroup_name));
	else {
		printf("hostgroup=%s", url_encode(hostgroup_name));
		if((service_status_types != all_service_status_types) || group_style_type == STYLE_DETAIL)
			printf("&style=detail");
		else if(group_style_type == STYLE_HOST_DETAIL)
			printf("&style=hostdetail");
		}
	if(service_status_types != all_service_status_types)
		printf("&servicestatustypes=%d", service_status_types);
	printf("&hoststatustypes=%d'>", SD_HOST_DOWN | SD_HOST_UNREACHABLE);
	printf("<em>所有故障</em></a></th>\n");

	printf("<th class='hostTotals'>");
	printf("<a class='hostTotals' href='%s?", STATUS_CGI);
	/* paging */
	if(temp_result_limit)
		printf("limit=%i&",temp_result_limit);
	if(display_type == DISPLAY_HOSTS)
		printf("host=%s", (host_name == NULL) ? "all" : url_encode(host_name));
	else if(display_type == DISPLAY_SERVICEGROUPS)
		printf("servicegroup=%s", url_encode(servicegroup_name));
	else {
		printf("hostgroup=%s", url_encode(hostgroup_name));
		if((service_status_types != all_service_status_types) || group_style_type == STYLE_DETAIL)
			printf("&style=detail");
		else if(group_style_type == STYLE_HOST_DETAIL)
			printf("&style=hostdetail");
		}
	if(service_status_types != all_service_status_types)
		printf("&servicestatustypes=%d", service_status_types);
	printf("'>");
	printf("<em>所有类型</em></a></th>\n");

	printf("</tr><tr>\n");

	/* total hosts with problems */
	printf("<td class='hostTotals%s'>%d</td>\n", (total_problems > 0) ? "PROBLEMS" : "", total_problems);

	/* total hosts */
	printf("<td class='hostTotals'>%d</td>\n", total_hosts);

	printf("</tr>\n");
	printf("</table>\n");

	printf("</td></tr>\n");
	printf("</table>\n");

	printf("</div>\n");

	return;
	}



/* display a detailed listing of the status of all services... */
void show_service_detail(void) {
	regex_t preg, preg_hostname;
	time_t t;
	char date_time[MAX_DATETIME_LENGTH];
	char state_duration[48];
	char status[MAX_INPUT_BUFFER];
	char temp_buffer[MAX_INPUT_BUFFER];
	char temp_url[MAX_INPUT_BUFFER];
	char *processed_string = NULL;
	const char *status_class = "";
	const char *status_bg_class = "";
	const char *host_status_bg_class = "";
	const char *last_host = "";
	int new_host = FALSE;
	servicestatus *temp_status = NULL;
	hostgroup *temp_hostgroup = NULL;
	servicegroup *temp_servicegroup = NULL;
	hoststatus *temp_hoststatus = NULL;
	host *temp_host = NULL;
	service *temp_service = NULL;
	int odd = 0;
	int total_comments = 0;
	int user_has_seen_something = FALSE;
	servicesort *temp_servicesort = NULL;
	int use_sort = FALSE;
	int result = OK;
	int first_entry = TRUE;
	int days;
	int hours;
	int minutes;
	int seconds;
	int duration_error = FALSE;
	int total_entries = 0;
	int show_service = FALSE;
	int visible_entries = 0;


	/* sort the service list if necessary */
	if(sort_type != SORT_NONE) {
		result = sort_services(sort_type, sort_option);
		if(result == ERROR)
			use_sort = FALSE;
		else
			use_sort = TRUE;
		}
	else
		use_sort = FALSE;


	printf("<table class='pageTitle' border='0' width='100%%'>\n");
	printf("<tr>\n");

	printf("<td valign=top align=left width=33%%>\n");

	if(display_header == TRUE)
		show_filters();

	printf("</td>");

	printf("<td valign=top align=center width=33%%>\n");

	printf("<div align='center' class='statusTitle'>");
	if(display_type == DISPLAY_HOSTS) {
		if(show_all_hosts == TRUE)
			printf("所有主机");
		else
			printf("主机 '%s'", host_name);
		}
	else if(display_type == DISPLAY_SERVICEGROUPS) {
		if(show_all_servicegroups == TRUE)
			printf("所有服务组");
		else
			printf("服务组 '%s' ", url_encode(servicegroup_name));
		}
	else {
		if(show_all_hostgroups == TRUE)
			printf("所有主机组");
		else
			printf("主机组 '%s' ", hostgroup_name);
		}
	printf("的服务状态详细</div>\n");

	if(use_sort == TRUE) {
		printf("<div align='center' class='statusSort'>排序: <b>");
		if(sort_option == SORT_HOSTNAME)
			printf("主机名");
		else if(sort_option == SORT_SERVICENAME)
			printf("服务名");
		else if(sort_option == SORT_SERVICESTATUS)
			printf("服务状态");
		else if(sort_option == SORT_LASTCHECKTIME)
			printf("最近检查时间");
		else if(sort_option == SORT_CURRENTATTEMPT)
			printf("尝试次数");
		else if(sort_option == SORT_STATEDURATION)
			printf("持续时间");
		printf("</b> (%s)\n", (sort_type == SORT_ASCENDING) ? "升序" : "降序");
		printf("</div>\n");
		}

	if(service_filter != NULL)
		printf("<div align='center' class='statusSort'>按服务匹配 \'%s\' 过滤</div>", service_filter);

	printf("<br>");

	printf("</td>\n");

	printf("<td valign=top align=right width=33%%></td>\n");

	printf("</tr>\n");
	printf("</table>\n");




	/* handle navigation GET variables */
	snprintf(temp_url, sizeof(temp_url) - 1, "%s?", STATUS_CGI);
	temp_url[sizeof(temp_url) - 1] = '\x0';
	if(display_type == DISPLAY_HOSTS)
	     snprintf(temp_buffer, sizeof(temp_buffer) - 1, "%shost=%s", (navbar_search == TRUE) ? "&navbarsearch=1&" : "", (host_name == NULL) ? "all" : url_encode(host_name));
	else if(display_type == DISPLAY_SERVICEGROUPS)
		snprintf(temp_buffer, sizeof(temp_buffer) - 1, "servicegroup=%s&style=detail", url_encode(servicegroup_name));
	else
		snprintf(temp_buffer, sizeof(temp_buffer) - 1, "hostgroup=%s&style=detail", url_encode(hostgroup_name));
	temp_buffer[sizeof(temp_buffer) - 1] = '\x0';
	strncat(temp_url, temp_buffer, sizeof(temp_url) - strlen(temp_url) - 1);
	temp_url[sizeof(temp_url) - 1] = '\x0';
	if(service_status_types != all_service_status_types) {
		snprintf(temp_buffer, sizeof(temp_buffer) - 1, "&servicestatustypes=%d", service_status_types);
		temp_buffer[sizeof(temp_buffer) - 1] = '\x0';
		strncat(temp_url, temp_buffer, sizeof(temp_url) - strlen(temp_url) - 1);
		temp_url[sizeof(temp_url) - 1] = '\x0';
		}
	if(host_status_types != all_host_status_types) {
		snprintf(temp_buffer, sizeof(temp_buffer) - 1, "&hoststatustypes=%d", host_status_types);
		temp_buffer[sizeof(temp_buffer) - 1] = '\x0';
		strncat(temp_url, temp_buffer, sizeof(temp_url) - strlen(temp_url) - 1);
		temp_url[sizeof(temp_url) - 1] = '\x0';
		}
	if(service_properties != 0) {
		snprintf(temp_buffer, sizeof(temp_buffer) - 1, "&serviceprops=%lu", service_properties);
		temp_buffer[sizeof(temp_buffer) - 1] = '\x0';
		strncat(temp_url, temp_buffer, sizeof(temp_url) - strlen(temp_url) - 1);
		temp_url[sizeof(temp_url) - 1] = '\x0';
		}
	if(host_properties != 0) {
		snprintf(temp_buffer, sizeof(temp_buffer) - 1, "&hostprops=%lu", host_properties);
		temp_buffer[sizeof(temp_buffer) - 1] = '\x0';
		strncat(temp_url, temp_buffer, sizeof(temp_url) - strlen(temp_url) - 1);
		temp_url[sizeof(temp_url) - 1] = '\x0';
		}
	/*
	if(temp_result_limit) {
		snprintf(temp_buffer, sizeof(temp_buffer) - 1, "&limit=%i", temp_result_limit);
		temp_buffer[sizeof(temp_buffer) - 1] = '\x0';
		strncat(temp_url, temp_buffer, sizeof(temp_url) - strlen(temp_url) - 1);
		temp_url[sizeof(temp_url) - 1] = '\x0';
		}
	*/

	/* GET input can override cgi.cfg */
	if(limit_results==TRUE)
		result_limit = temp_result_limit ? temp_result_limit : result_limit;
	else
		result_limit = 0;
	/* select box to set result limit */
	create_page_limiter(result_limit,temp_url);

	/* the main list of services */
	printf("<table border=0 width=100%% class='status'>\n");
	printf("<tr>\n");

	printf("<th class='status'>主机&nbsp;<a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='主机名排序(升序)' TITLE='主机名排序(升序)'></a><a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='主机名排序(降序)' TITLE='主机名排序(降序)'></a></th>", temp_url, SORT_ASCENDING, SORT_HOSTNAME, url_images_path, UP_ARROW_ICON, temp_url, SORT_DESCENDING, SORT_HOSTNAME, url_images_path, DOWN_ARROW_ICON);

	printf("<th class='status'>服务&nbsp;<a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='服务名排序(升序)' TITLE='服务名排序(升序)'></a><a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='服务名排序(降序)' TITLE='服务名排序(降序)'></a></th>", temp_url, SORT_ASCENDING, SORT_SERVICENAME, url_images_path, UP_ARROW_ICON, temp_url, SORT_DESCENDING, SORT_SERVICENAME, url_images_path, DOWN_ARROW_ICON);

	printf("<th class='status'>状态&nbsp;<a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='Sort by service status (ascending)' TITLE='Sort by service status (ascending)'></a><a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='Sort by service status (descending)' TITLE='Sort by service status (descending)'></a></th>", temp_url, SORT_ASCENDING, SORT_SERVICESTATUS, url_images_path, UP_ARROW_ICON, temp_url, SORT_DESCENDING, SORT_SERVICESTATUS, url_images_path, DOWN_ARROW_ICON);

	printf("<th class='status'>最近检查时间&nbsp;<a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='最近检查时间排序(升序)' TITLE='最近检查时间排序(升序)'></a><a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='最近检查时间排序(降序)' TITLE='最近检查时间排序(降序)'></a></th>", temp_url, SORT_ASCENDING, SORT_LASTCHECKTIME, url_images_path, UP_ARROW_ICON, temp_url, SORT_DESCENDING, SORT_LASTCHECKTIME, url_images_path, DOWN_ARROW_ICON);

	printf("<th class='status'>持续时间&nbsp;<a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='持续时间排序(升序)' TITLE='持续时间排序(升序)'></a><a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='Sort by state duration time (descending)' TITLE='Sort by state duration time (descending)'></a></th>", temp_url, SORT_ASCENDING, SORT_STATEDURATION, url_images_path, UP_ARROW_ICON, temp_url, SORT_DESCENDING, SORT_STATEDURATION, url_images_path, DOWN_ARROW_ICON);

	printf("<th class='status'>尝试次数&nbsp;<a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='尝试次数排序(升序)' TITLE='尝试次数排序(升序)'></a><a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='尝试次数排序(降序)' TITLE='尝试次数排序(降序)'></a></th>", temp_url, SORT_ASCENDING, SORT_CURRENTATTEMPT, url_images_path, UP_ARROW_ICON, temp_url, SORT_DESCENDING, SORT_CURRENTATTEMPT, url_images_path, DOWN_ARROW_ICON);

	printf("<th class='status'>状态信息</th>\n");
	printf("</tr>\n");


	if(service_filter != NULL)
		regcomp(&preg, service_filter, 0);
	if(host_filter != NULL)
		regcomp(&preg_hostname, host_filter, REG_ICASE);

	temp_hostgroup = find_hostgroup(hostgroup_name);
	temp_servicegroup = find_servicegroup(servicegroup_name);

	/* check all services... */
	while(1) {

		/* get the next service to display */
		if(use_sort == TRUE) {
			if(first_entry == TRUE)
				temp_servicesort = servicesort_list;
			else
				temp_servicesort = temp_servicesort->next;
			if(temp_servicesort == NULL)
				break;
			temp_status = temp_servicesort->svcstatus;
			}
		else {
			if(first_entry == TRUE)
				temp_status = servicestatus_list;
			else
				temp_status = temp_status->next;
			}

		if(temp_status == NULL)
			break;

		first_entry = FALSE;

		/* find the service  */
		temp_service = find_service(temp_status->host_name, temp_status->description);

		/* if we couldn't find the service, go to the next service */
		if(temp_service == NULL)
			continue;

		/* find the host */
		temp_host = find_host(temp_service->host_name);

		/* make sure user has rights to see this... */
		if(is_authorized_for_service(temp_service, &current_authdata) == FALSE)
			continue;

		user_has_seen_something = TRUE;

		/* get the host status information */
		temp_hoststatus = find_hoststatus(temp_service->host_name);

		/* see if we should display services for hosts with tis type of status */
		if(!(host_status_types & temp_hoststatus->status))
			continue;

		/* see if we should display this type of service status */
		if(!(service_status_types & temp_status->status))
			continue;

		/* check host properties filter */
		if(passes_host_properties_filter(temp_hoststatus) == FALSE)
			continue;

		/* check service properties filter */
		if(passes_service_properties_filter(temp_status) == FALSE)
			continue;

		/* servicefilter cgi var */
		if(service_filter != NULL)
			if(regexec(&preg, temp_status->description, 0, NULL, 0))
				continue;

		show_service = FALSE;

		if(display_type == DISPLAY_HOSTS) {
			if(show_all_hosts == TRUE)
				show_service = TRUE;
			else if(host_filter != NULL && 0 == regexec(&preg_hostname, temp_status->host_name, 0, NULL, 0))
				show_service = TRUE;
			else if(host_filter != NULL && navbar_search_addresses == TRUE && 0 == regexec(&preg_hostname, temp_host->address, 0, NULL, 0))
				show_service = TRUE;
			else if(host_filter != NULL && navbar_search_aliases == TRUE && 0 == regexec(&preg_hostname, temp_host->alias, 0, NULL, 0))
				show_service = TRUE;
			else if(!strcmp(host_name, temp_status->host_name))
				show_service = TRUE;
			else if(navbar_search_addresses == TRUE && !strcmp(host_name, temp_host->address))
				show_service = TRUE;
			else if(navbar_search_aliases == TRUE && !strcmp(host_name, temp_host->alias))
				show_service = TRUE;
			}

		else if(display_type == DISPLAY_HOSTGROUPS) {
			if(show_all_hostgroups == TRUE)
				show_service = TRUE;
			else if(is_host_member_of_hostgroup(temp_hostgroup, temp_host) == TRUE)
				show_service = TRUE;
			}

		else if(display_type == DISPLAY_SERVICEGROUPS) {
			if(show_all_servicegroups == TRUE)
				show_service = TRUE;
			else if(is_service_member_of_servicegroup(temp_servicegroup, temp_service) == TRUE)
				show_service = TRUE;
			}

		/* final checks for display visibility, add to total results.  Used for page numbers */
		if(result_limit == 0)
			limit_results = FALSE;

		if( (limit_results == TRUE && show_service== TRUE)  && ( (total_entries < page_start) || (total_entries >= (page_start + result_limit)) )  ) {
			total_entries++;
			show_service = FALSE;
			}

		/* a visible entry */
		if(show_service == TRUE) {
			if(strcmp(last_host, temp_status->host_name) || visible_entries == 0 )
				new_host = TRUE;
			else
				new_host = FALSE;

			if(new_host == TRUE) {
				if(strcmp(last_host, "")) {
					printf("<tr><td colspan='6'></td></tr>\n");
					printf("<tr><td colspan='6'></td></tr>\n");
					}
				}

			if(odd)
				odd = 0;
			else
				odd = 1;

			/* keep track of total number of services we're displaying */
			visible_entries++;
			total_entries++;

			/* get the last service check time */
			t = temp_status->last_check;
			get_time_string(&t, date_time, (int)sizeof(date_time), SHORT_DATE_TIME);
			if((unsigned long)temp_status->last_check == 0L)
				strcpy(date_time, "N/A");

			if(temp_status->status == SERVICE_PENDING) {
				strncpy(status, "未决", sizeof(status));
				status_class = "PENDING";
				status_bg_class = (odd) ? "Even" : "Odd";
				}
			else if(temp_status->status == SERVICE_OK) {
				strncpy(status, "正常", sizeof(status));
				status_class = "OK";
				status_bg_class = (odd) ? "Even" : "Odd";
				}
			else if(temp_status->status == SERVICE_WARNING) {
				strncpy(status, "警告", sizeof(status));
				status_class = "WARNING";
				if(temp_status->problem_has_been_acknowledged == TRUE)
					status_bg_class = "BGWARNINGACK";
				else if(temp_status->scheduled_downtime_depth > 0)
					status_bg_class = "BGWARNINGSCHED";
				else
					status_bg_class = "BGWARNING";
				}
			else if(temp_status->status == SERVICE_UNKNOWN) {
				strncpy(status, "未知", sizeof(status));
				status_class = "UNKNOWN";
				if(temp_status->problem_has_been_acknowledged == TRUE)
					status_bg_class = "BGUNKNOWNACK";
				else if(temp_status->scheduled_downtime_depth > 0)
					status_bg_class = "BGUNKNOWNSCHED";
				else
					status_bg_class = "BGUNKNOWN";
				}
			else if(temp_status->status == SERVICE_CRITICAL) {
				strncpy(status, "紧急", sizeof(status));
				status_class = "CRITICAL";
				if(temp_status->problem_has_been_acknowledged == TRUE)
					status_bg_class = "BGCRITICALACK";
				else if(temp_status->scheduled_downtime_depth > 0)
					status_bg_class = "BGCRITICALSCHED";
				else
					status_bg_class = "BGCRITICAL";
				}
			status[sizeof(status) - 1] = '\x0';


			printf("<tr>\n");

			/* host name column */
			if(new_host == TRUE) {

				/* grab macros */
				grab_host_macros_r(mac, temp_host);

				if(temp_hoststatus->status == SD_HOST_DOWN) {
					if(temp_hoststatus->problem_has_been_acknowledged == TRUE)
						host_status_bg_class = "HOSTDOWNACK";
					else if(temp_hoststatus->scheduled_downtime_depth > 0)
						host_status_bg_class = "HOSTDOWNSCHED";
					else
						host_status_bg_class = "HOSTDOWN";
					}
				else if(temp_hoststatus->status == SD_HOST_UNREACHABLE) {
					if(temp_hoststatus->problem_has_been_acknowledged == TRUE)
						host_status_bg_class = "HOSTUNREACHABLEACK";
					else if(temp_hoststatus->scheduled_downtime_depth > 0)
						host_status_bg_class = "HOSTUNREACHABLESCHED";
					else
						host_status_bg_class = "HOSTUNREACHABLE";
					}
				else
					host_status_bg_class = (odd) ? "Even" : "Odd";

				printf("<td class='status%s'>", host_status_bg_class);

				printf("<table border=0 width='100%%' cellpadding=0 cellspacing=0>\n");
				printf("<tr>\n");
				printf("<td align='left'>\n");
				printf("<table border=0 cellpadding=0 cellspacing=0>\n");
				printf("<tr>\n");
				printf("<td align=left valign=center class='status%s'><a href='%s?type=%d&host=%s' title='%s'>%s</a></td>\n", host_status_bg_class, EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name), temp_host->address, temp_status->host_name);
				printf("</tr>\n");
				printf("</table>\n");
				printf("</td>\n");
				printf("<td align=right valign=center>\n");
				printf("<table border=0 cellpadding=0 cellspacing=0>\n");
				printf("<tr>\n");
				total_comments = number_of_host_comments(temp_host->name);
				if(temp_hoststatus->problem_has_been_acknowledged == TRUE) {
					printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s#comments'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该主机的问题已经确认' TITLE='该主机的问题已经确认'></a></td>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name), url_images_path, ACKNOWLEDGEMENT_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
					}
				/* only show comments if this is a non-read-only user */
				if(is_authorized_for_read_only(&current_authdata) == FALSE) {
					if(total_comments > 0)
						printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s#comments'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该主机有%d相关注释' TITLE='该主机有%d相关注释'></a></td>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name), url_images_path, COMMENT_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, total_comments, (total_comments == 1) ? "" : "s", total_comments, (total_comments == 1) ? "" : "s");
					}
				if(temp_hoststatus->notifications_enabled == FALSE) {
					printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该主机的通知已被禁用' TITLE='该主机的通知已被禁用'></a></td>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name), url_images_path, NOTIFICATIONS_DISABLED_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
					}
				if(temp_hoststatus->checks_enabled == FALSE) {
					printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该主机的检查已禁用' TITLE='该主机的检查已禁用'></a></td>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name), url_images_path, DISABLED_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
					}
				if(temp_hoststatus->is_flapping == TRUE) {
					printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该主机处于抖动状态中' TITLE='该主机处于抖动状态中'></a></td>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name), url_images_path, FLAPPING_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
					}
				if(temp_hoststatus->scheduled_downtime_depth > 0) {
					printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该主机当前处于宕机设置的时间段中' TITLE='该主机当前处于宕机设置的时间段中'></a></td>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name), url_images_path, SCHEDULED_DOWNTIME_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
					}
				if(temp_host->notes_url != NULL) {
					printf("<td align=center valign=center>");
					printf("<a href='");
					process_macros_r(mac, temp_host->notes_url, &processed_string, 0);
					printf("%s", processed_string);
					free(processed_string);
					printf("' TARGET='%s'>", (notes_url_target == NULL) ? "_blank" : notes_url_target);
					printf("<IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", url_images_path, NOTES_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "查看主机备注信息", "查看主机备注信息");
					printf("</a>");
					printf("</td>\n");
					}
				if(temp_host->action_url != NULL) {
					printf("<td align=center valign=center>");
					printf("<a href='");
					process_macros_r(mac, temp_host->action_url, &processed_string, 0);
					printf("%s", processed_string);
					free(processed_string);
					printf("' TARGET='%s'>", (action_url_target == NULL) ? "_blank" : action_url_target);
					printf("<IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", url_images_path, ACTION_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "执行主机扩展动作", "执行主机扩展动作");
					printf("</a>");
					printf("</td>\n");
					}
				if(temp_host->icon_image != NULL) {
					printf("<td align=center valign=center>");
					printf("<a href='%s?type=%d&host=%s'>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name));
					printf("<IMG SRC='%s", url_logo_images_path);
					process_macros_r(mac, temp_host->icon_image, &processed_string, 0);
					printf("%s", processed_string);
					free(processed_string);
					printf("' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, (temp_host->icon_image_alt == NULL) ? "" : temp_host->icon_image_alt, (temp_host->icon_image_alt == NULL) ? "" : temp_host->icon_image_alt);
					printf("</a>");
					printf("</td>\n");
					}
				printf("</tr>\n");
				printf("</table>\n");
				printf("</td>\n");
				printf("</tr>\n");
				printf("</table>\n");
				}
			else
				printf("<td>");
			printf("</td>\n");

			/* grab macros */
			grab_service_macros_r(mac, temp_service);

			/* service name column */
			printf("<td class='status%s'>", status_bg_class);
			printf("<table border=0 WIDTH='100%%' cellspacing=0 cellpadding=0>");
			printf("<tr>");
			printf("<td align='left'>");
			printf("<table border=0 cellspacing=0 cellpadding=0>\n");
			printf("<tr>\n");
			printf("<td align='left' valign=center class='status%s'><a href='%s?type=%d&host=%s", status_bg_class, EXTINFO_CGI, DISPLAY_SERVICE_INFO, url_encode(temp_status->host_name));
			printf("&service=%s'>", url_encode(temp_status->description));
			printf("%s</a></td>", temp_status->description);
			printf("</tr>\n");
			printf("</table>\n");
			printf("</td>\n");
			printf("<td ALIGN=RIGHT class='status%s'>\n", status_bg_class);
			printf("<table border=0 cellspacing=0 cellpadding=0>\n");
			printf("<tr>\n");
			total_comments = number_of_service_comments(temp_service->host_name, temp_service->description);
			/* only show comments if this is a non-read-only user */
			if(is_authorized_for_read_only(&current_authdata) == FALSE) {
				if(total_comments > 0) {
					printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s", EXTINFO_CGI, DISPLAY_SERVICE_INFO, url_encode(temp_status->host_name));
					printf("&service=%s#comments'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该服务有%d相关注释' TITLE='该服务有%d相关注释'></a></td>", url_encode(temp_status->description), url_images_path, COMMENT_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, total_comments, (total_comments == 1) ? "" : "s", total_comments, (total_comments == 1) ? "" : "s");
					}
				}
			if(temp_status->problem_has_been_acknowledged == TRUE) {
				printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s", EXTINFO_CGI, DISPLAY_SERVICE_INFO, url_encode(temp_status->host_name));
				printf("&service=%s#comments'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该服务的问题已经确认' TITLE='该服务的问题已经确认'></a></td>", url_encode(temp_status->description), url_images_path, ACKNOWLEDGEMENT_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
				}
			if(temp_status->checks_enabled == FALSE && temp_status->accept_passive_checks == FALSE) {
				printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s", EXTINFO_CGI, DISPLAY_SERVICE_INFO, url_encode(temp_status->host_name));
				printf("&service=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该服务的主动和被动检查已被禁用' TITLE='该服务的主动和被动检查已被禁用'></a></td>", url_encode(temp_status->description), url_images_path, DISABLED_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
				}
			else if(temp_status->checks_enabled == FALSE) {
				printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s", EXTINFO_CGI, DISPLAY_SERVICE_INFO, url_encode(temp_status->host_name));
				printf("&service=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该服务已禁用主动检查只接受被动检查' TITLE='该服务已禁用主动检查只接受被动检查'></a></td>", url_encode(temp_status->description), url_images_path, PASSIVE_ONLY_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
				}
			if(temp_status->notifications_enabled == FALSE) {
				printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s", EXTINFO_CGI, DISPLAY_SERVICE_INFO, url_encode(temp_status->host_name));
				printf("&service=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该服务的通知服务被禁用' TITLE='该服务的通知服务被禁用'></a></td>", url_encode(temp_status->description), url_images_path, NOTIFICATIONS_DISABLED_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
				}
			if(temp_status->is_flapping == TRUE) {
				printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s", EXTINFO_CGI, DISPLAY_SERVICE_INFO, url_encode(temp_status->host_name));
				printf("&service=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该服务处于抖动状态中' TITLE='该服务处于抖动状态中'></a></td>", url_encode(temp_status->description), url_images_path, FLAPPING_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
				}
			if(temp_status->scheduled_downtime_depth > 0) {
				printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s", EXTINFO_CGI, DISPLAY_SERVICE_INFO, url_encode(temp_status->host_name));
				printf("&service=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该服务当前处于宕机设置的时间段中' TITLE='该服务当前处于宕机设置的时间段中'></a></td>", url_encode(temp_status->description), url_images_path, SCHEDULED_DOWNTIME_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
				}
			if(temp_service->notes_url != NULL) {
				printf("<td align=center valign=center>");
				printf("<a href='");
				process_macros_r(mac, temp_service->notes_url, &processed_string, 0);
				printf("%s", processed_string);
				free(processed_string);
				printf("' TARGET='%s'>", (notes_url_target == NULL) ? "_blank" : notes_url_target);
				printf("<IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", url_images_path, NOTES_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "查看服务备注信息", "查看服务备注信息");
				printf("</a>");
				printf("</td>\n");
				}
			if(temp_service->action_url != NULL) {
				printf("<td align=center valign=center>");
				printf("<a href='");
				process_macros_r(mac, temp_service->action_url, &processed_string, 0);
				printf("%s", processed_string);
				free(processed_string);
				printf("' TARGET='%s'>", (action_url_target == NULL) ? "_blank" : action_url_target);
				printf("<IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", url_images_path, ACTION_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "执行服务扩展动作", "执行服务扩展动作");
				printf("</a>");
				printf("</td>\n");
				}
			if(temp_service->icon_image != NULL) {
				printf("<td ALIGN=center valign=center>");
				printf("<a href='%s?type=%d&host=%s", EXTINFO_CGI, DISPLAY_SERVICE_INFO, url_encode(temp_service->host_name));
				printf("&service=%s'>", url_encode(temp_service->description));
				printf("<IMG SRC='%s", url_logo_images_path);
				process_macros_r(mac, temp_service->icon_image, &processed_string, 0);
				printf("%s", processed_string);
				free(processed_string);
				printf("' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, (temp_service->icon_image_alt == NULL) ? "" : temp_service->icon_image_alt, (temp_service->icon_image_alt == NULL) ? "" : temp_service->icon_image_alt);
				printf("</a>");
				printf("</td>\n");
				}
			if(enable_splunk_integration == TRUE) {
				printf("<td ALIGN=center valign=center>");
				display_splunk_service_url(temp_service);
				printf("</td>\n");
				}
			printf("</tr>\n");
			printf("</table>\n");
			printf("</td>\n");
			printf("</tr>");
			printf("</table>");
			printf("</td>\n");

			/* state duration calculation... */
			t = 0;
			duration_error = FALSE;
			if(temp_status->last_state_change == (time_t)0) {
				if(program_start > current_time)
					duration_error = TRUE;
				else
					t = current_time - program_start;
				}
			else {
				if(temp_status->last_state_change > current_time)
					duration_error = TRUE;
				else
					t = current_time - temp_status->last_state_change;
				}
			get_time_breakdown((unsigned long)t, &days, &hours, &minutes, &seconds);
			if(duration_error == TRUE)
				snprintf(state_duration, sizeof(state_duration) - 1, "???");
			else
				snprintf(state_duration, sizeof(state_duration) - 1, "%2d日%2d时%2d分%2d秒%s", days, hours, minutes, seconds, (temp_status->last_state_change == (time_t)0) ? "+" : "");
			state_duration[sizeof(state_duration) - 1] = '\x0';

			/* the rest of the columns... */
			printf("<td class='status%s'>%s</td>\n", status_class, status);
			printf("<td class='status%s' nowrap>%s</td>\n", status_bg_class, date_time);
			printf("<td class='status%s' nowrap>%s</td>\n", status_bg_class, state_duration);
			printf("<td class='status%s'>%d/%d</td>\n", status_bg_class, temp_status->current_attempt, temp_status->max_attempts);
			printf("<td class='status%s' valign='center'>", status_bg_class);
			printf("%s&nbsp;", (temp_status->plugin_output == NULL) ? "" : html_encode(temp_status->plugin_output, TRUE));
			/*
			if(enable_splunk_integration==TRUE)
				display_splunk_service_url(temp_service);
			*/
			printf("</td>\n");

			printf("</tr>\n");

			/* mod to account for paging */
			if(visible_entries != 0)
				last_host = temp_status->host_name;
			}

		}

	printf("</table>\n");

	/* if user couldn't see anything, print out some helpful info... */
	if(user_has_seen_something == FALSE) {

		if(servicestatus_list != NULL) {
			printf("<P><div class='errorMessage'>看起来像是你没有权限查看你请求服务的任何信息...</div></P>\n");
			printf("<P><div class='errorDescription'>如果你认为这是一个错误，请检查HTTP服务器访问CGI的身份验证要求<br>");
			printf("并在你的CGI的配置文件中检查授权选项。</div></P>\n");
			}
		else {
			printf("<p><div class='infoMessage'>日志中不存在任何服务状态信息...<br><br>\n");
			printf("请确保Nagios程序正常运行，并且配置文件中状态日志设置正确。</div></p>\n");
			}
		}
	else {
		/* do page numbers if applicable */
		create_pagenumbers(total_entries,temp_url,TRUE);
		}

	return;
	}




/* display a detailed listing of the status of all hosts... */
void show_host_detail(void) {
	time_t t;
	char date_time[MAX_DATETIME_LENGTH];
	char state_duration[48];
	char status[MAX_INPUT_BUFFER];
	char temp_buffer[MAX_INPUT_BUFFER];
	char temp_url[MAX_INPUT_BUFFER];
	char *processed_string = NULL;
	const char *status_class = "";
	const char *status_bg_class = "";
	hoststatus *temp_status = NULL;
	hostgroup *temp_hostgroup = NULL;
	host *temp_host = NULL;
	hostsort *temp_hostsort = NULL;
	int odd = 0;
	int total_comments = 0;
	int user_has_seen_something = FALSE;
	int use_sort = FALSE;
	int result = OK;
	int first_entry = TRUE;
	int days;
	int hours;
	int minutes;
	int seconds;
	int duration_error = FALSE;
	int total_entries = 0;
	int visible_entries = 0;
//	int show_host = FALSE;


	/* sort the host list if necessary */
	if(sort_type != SORT_NONE) {
		result = sort_hosts(sort_type, sort_option);
		if(result == ERROR)
			use_sort = FALSE;
		else
			use_sort = TRUE;
		}
	else
		use_sort = FALSE;


//	printf("<P>\n");


	printf("<table class='pageTitle' border='0' width='100%%'>\n");
	printf("<tr>\n");

	printf("<td valign=top align=left width=33%%>\n");

	if(display_header == TRUE)
		show_filters();

	printf("</td>");

	printf("<td valign=top align=center width=33%%>\n");

	printf("<div align='center' class='statusTitle'>");
	if(show_all_hostgroups == TRUE)
		printf("所有主机组");
	else
		printf("主机组 '%s' ", hostgroup_name);
	printf("的主机状态详细</div>\n");

	if(use_sort == TRUE) {
		printf("<div align='center' class='statusSort'>排序: <b>");
		if(sort_option == SORT_HOSTNAME)
			printf("主机名");
		else if(sort_option == SORT_HOSTSTATUS)
			printf("主机状态");
		else if(sort_option == SORT_HOSTURGENCY)
			printf("主机紧急");
		else if(sort_option == SORT_LASTCHECKTIME)
			printf("最近检查时间");
		else if(sort_option == SORT_CURRENTATTEMPT)
			printf("尝试次数");
		else if(sort_option == SORT_STATEDURATION)
			printf("持续时间");
		printf("</b> (%s)\n", (sort_type == SORT_ASCENDING) ? "升序" : "降序");
		printf("</div>\n");
		}

	printf("<br>");

	printf("</td>\n");

	printf("<td valign=top align=right width=33%%></td>\n");

	printf("</tr>\n");
	printf("</table>\n");





	snprintf(temp_url, sizeof(temp_url) - 1, "%s?", STATUS_CGI);
	temp_url[sizeof(temp_url) - 1] = '\x0';
	snprintf(temp_buffer, sizeof(temp_buffer) - 1, "hostgroup=%s&style=hostdetail", url_encode(hostgroup_name));
	temp_buffer[sizeof(temp_buffer) - 1] = '\x0';
	strncat(temp_url, temp_buffer, sizeof(temp_url) - strlen(temp_url) - 1);
	temp_url[sizeof(temp_url) - 1] = '\x0';
	if(service_status_types != all_service_status_types) {
		snprintf(temp_buffer, sizeof(temp_buffer) - 1, "&servicestatustypes=%d", service_status_types);
		temp_buffer[sizeof(temp_buffer) - 1] = '\x0';
		strncat(temp_url, temp_buffer, sizeof(temp_url) - strlen(temp_url) - 1);
		temp_url[sizeof(temp_url) - 1] = '\x0';
		}
	if(host_status_types != all_host_status_types) {
		snprintf(temp_buffer, sizeof(temp_buffer) - 1, "&hoststatustypes=%d", host_status_types);
		temp_buffer[sizeof(temp_buffer) - 1] = '\x0';
		strncat(temp_url, temp_buffer, sizeof(temp_url) - strlen(temp_url) - 1);
		temp_url[sizeof(temp_url) - 1] = '\x0';
		}
	if(service_properties != 0) {
		snprintf(temp_buffer, sizeof(temp_buffer) - 1, "&serviceprops=%lu", service_properties);
		temp_buffer[sizeof(temp_buffer) - 1] = '\x0';
		strncat(temp_url, temp_buffer, sizeof(temp_url) - strlen(temp_url) - 1);
		temp_url[sizeof(temp_url) - 1] = '\x0';
		}
	if(host_properties != 0) {
		snprintf(temp_buffer, sizeof(temp_buffer) - 1, "&hostprops=%lu", host_properties);
		temp_buffer[sizeof(temp_buffer) - 1] = '\x0';
		strncat(temp_url, temp_buffer, sizeof(temp_url) - strlen(temp_url) - 1);
		temp_url[sizeof(temp_url) - 1] = '\x0';
		}
	/*
	if(temp_result_limit) {
		snprintf(temp_buffer, sizeof(temp_buffer) - 1, "&limit=%i", temp_result_limit);
		temp_buffer[sizeof(temp_buffer) - 1] = '\x0';
		strncat(temp_url, temp_buffer, sizeof(temp_url) - strlen(temp_url) - 1);
		temp_url[sizeof(temp_url) - 1] = '\x0';
		}
	*/

	/* GET input can override cgi.cfg */
	if(limit_results==TRUE)
		result_limit = temp_result_limit ? temp_result_limit : result_limit;
	else
		result_limit = 0;
	/* select box to set result limit */
	create_page_limiter(result_limit,temp_url);


	/* the main list of hosts */
	printf("<div align='center'>\n");
	printf("<table border=0 class='status' width='100%%'>\n");
	printf("<tr>\n");

	printf("<th class='status'>主机&nbsp;<a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='主机名排序(升序)' TITLE='主机名排序(升序)'></a><a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='主机名排序(降序)' TITLE='主机名排序(降序)'></a></th>", temp_url, SORT_ASCENDING, SORT_HOSTNAME, url_images_path, UP_ARROW_ICON, temp_url, SORT_DESCENDING, SORT_HOSTNAME, url_images_path, DOWN_ARROW_ICON);

	printf("<th class='status'>状态&nbsp;<a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='主机状态排序(升序)' TITLE='主机状态排序(升序)'></a><a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='主机状态排序(降序)' TITLE='主机状态排序(降序)'></a></th>", temp_url, SORT_ASCENDING, SORT_HOSTSTATUS, url_images_path, UP_ARROW_ICON, temp_url, SORT_DESCENDING, SORT_HOSTSTATUS, url_images_path, DOWN_ARROW_ICON);

	printf("<th class='status'>最近检查时间&nbsp;<a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='最近检查时间排序(升序)' TITLE='最近检查时间排序(升序)'></a><a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='最近检查时间排序(降序)' TITLE='最近检查时间排序(降序)'></a></th>", temp_url, SORT_ASCENDING, SORT_LASTCHECKTIME, url_images_path, UP_ARROW_ICON, temp_url, SORT_DESCENDING, SORT_LASTCHECKTIME, url_images_path, DOWN_ARROW_ICON);

	printf("<th class='status'>持续时间&nbsp;<a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='持续时间排序(升序)' TITLE='持续时间排序(升序)'></a><a href='%s&sorttype=%d&sortoption=%d'><IMG SRC='%s%s' border=0 ALT='Sort by state duration time (descending)' TITLE='Sort by state duration time (descending)'></a></th>", temp_url, SORT_ASCENDING, SORT_STATEDURATION, url_images_path, UP_ARROW_ICON, temp_url, SORT_DESCENDING, SORT_STATEDURATION, url_images_path, DOWN_ARROW_ICON);

	printf("<th class='status'>状态信息</th>\n");
	printf("</tr>\n");


	/* check all hosts... */
	while(1) {

		/* get the next service to display */
		if(use_sort == TRUE) {
			if(first_entry == TRUE)
				temp_hostsort = hostsort_list;
			else
				temp_hostsort = temp_hostsort->next;
			if(temp_hostsort == NULL)
				break;
			temp_status = temp_hostsort->hststatus;
			}
		else {
			if(first_entry == TRUE)
				temp_status = hoststatus_list;
			else
				temp_status = temp_status->next;
			}

		if(temp_status == NULL)
			break;

		first_entry = FALSE;

		/* find the host  */
		temp_host = find_host(temp_status->host_name);

		/* if we couldn't find the host, go to the next status entry */
		if(temp_host == NULL)
			continue;

		/* make sure user has rights to see this... */
		if(is_authorized_for_host(temp_host, &current_authdata) == FALSE)
			continue;

		user_has_seen_something = TRUE;

		/* see if we should display services for hosts with this type of status */
		if(!(host_status_types & temp_status->status))
			continue;

		/* check host properties filter */
		if(passes_host_properties_filter(temp_status) == FALSE)
			continue;


		/* see if this host is a member of the hostgroup */
		if(show_all_hostgroups == FALSE) {
			temp_hostgroup = find_hostgroup(hostgroup_name);
			if(temp_hostgroup == NULL)
				continue;
			if(is_host_member_of_hostgroup(temp_hostgroup, temp_host) == FALSE)
				continue;
			}



		total_entries++;

		/* final checks for display visibility, add to total results.  Used for page numbers */
		if(result_limit == 0)
			limit_results = FALSE;

		if( (limit_results == TRUE) && ( (total_entries < page_start) || (total_entries >= (page_start + result_limit)) )  ) {
			continue;
			}

		visible_entries++;


		/* grab macros */
		grab_host_macros_r(mac, temp_host);


		if(display_type == DISPLAY_HOSTGROUPS) {

			if(odd)
				odd = 0;
			else
				odd = 1;


			/* get the last host check time */
			t = temp_status->last_check;
			get_time_string(&t, date_time, (int)sizeof(date_time), SHORT_DATE_TIME);
			if((unsigned long)temp_status->last_check == 0L)
				strcpy(date_time, "N/A");

			if(temp_status->status == HOST_PENDING) {
				strncpy(status, "未决", sizeof(status));
				status_class = "PENDING";
				status_bg_class = (odd) ? "Even" : "Odd";
				}
			else if(temp_status->status == SD_HOST_UP) {
				strncpy(status, "运行", sizeof(status));
				status_class = "HOSTUP";
				status_bg_class = (odd) ? "Even" : "Odd";
				}
			else if(temp_status->status == SD_HOST_DOWN) {
				strncpy(status, "宕机", sizeof(status));
				status_class = "HOSTDOWN";
				if(temp_status->problem_has_been_acknowledged == TRUE)
					status_bg_class = "BGDOWNACK";
				else if(temp_status->scheduled_downtime_depth > 0)
					status_bg_class = "BGDOWNSCHED";
				else
					status_bg_class = "BGDOWN";
				}
			else if(temp_status->status == SD_HOST_UNREACHABLE) {
				strncpy(status, "不可达", sizeof(status));
				status_class = "HOSTUNREACHABLE";
				if(temp_status->problem_has_been_acknowledged == TRUE)
					status_bg_class = "BGUNREACHABLEACK";
				else if(temp_status->scheduled_downtime_depth > 0)
					status_bg_class = "BGUNREACHABLESCHED";
				else
					status_bg_class = "BGUNREACHABLE";
				}
			status[sizeof(status) - 1] = '\x0';


			printf("<tr>\n");


			/**** host name column ****/

			printf("<td class='status%s'>", status_class);

			printf("<table border=0 WIDTH='100%%' cellpadding=0 cellspacing=0>\n");
			printf("<tr>\n");
			printf("<td align='left'>\n");
			printf("<table border=0 cellpadding=0 cellspacing=0>\n");
			printf("<tr>\n");
			printf("<td align=left valign=center class='status%s'><a href='%s?type=%d&host=%s' title='%s'>%s</a>&nbsp;</td>\n", status_class, EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name), temp_host->address, temp_status->host_name);
			printf("</tr>\n");
			printf("</table>\n");
			printf("</td>\n");
			printf("<td align=right valign=center>\n");
			printf("<table border=0 cellpadding=0 cellspacing=0>\n");
			printf("<tr>\n");
			total_comments = number_of_host_comments(temp_host->name);
			if(temp_status->problem_has_been_acknowledged == TRUE) {
				printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s#comments'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该主机的问题已经确认' TITLE='该主机的问题已经确认'></a></td>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name), url_images_path, ACKNOWLEDGEMENT_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
				}
			if(total_comments > 0)
				printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s#comments'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该主机有%d相关注释' TITLE='该主机有%d相关注释'></a></td>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name), url_images_path, COMMENT_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, total_comments, (total_comments == 1) ? "" : "s", total_comments, (total_comments == 1) ? "" : "s");
			if(temp_status->notifications_enabled == FALSE) {
				printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该主机的通知已被禁用' TITLE='该主机的通知已被禁用'></a></td>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name), url_images_path, NOTIFICATIONS_DISABLED_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
				}
			if(temp_status->checks_enabled == FALSE) {
				printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该主机的检查已禁用' TITLE='该主机的检查已禁用'></a></td>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name), url_images_path, DISABLED_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
				}
			if(temp_status->is_flapping == TRUE) {
				printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该主机处于抖动状态中' TITLE='该主机处于抖动状态中'></a></td>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name), url_images_path, FLAPPING_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
				}
			if(temp_status->scheduled_downtime_depth > 0) {
				printf("<td ALIGN=center valign=center><a href='%s?type=%d&host=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='该主机当前处于宕机设置的时间段中' TITLE='该主机当前处于宕机设置的时间段中'></a></td>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name), url_images_path, SCHEDULED_DOWNTIME_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT);
				}
			if(temp_host->notes_url != NULL) {
				printf("<td align=center valign=center>");
				printf("<a href='");
				process_macros_r(mac, temp_host->notes_url, &processed_string, 0);
				printf("%s", processed_string);
				free(processed_string);
				printf("' TARGET='%s'>", (notes_url_target == NULL) ? "_blank" : notes_url_target);
				printf("<IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", url_images_path, NOTES_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "查看主机备注信息", "查看主机备注信息");
				printf("</a>");
				printf("</td>\n");
				}
			if(temp_host->action_url != NULL) {
				printf("<td align=center valign=center>");
				printf("<a href='");
				process_macros_r(mac, temp_host->action_url, &processed_string, 0);
				printf("%s", processed_string);
				free(processed_string);
				printf("' TARGET='%s'>", (action_url_target == NULL) ? "_blank" : action_url_target);
				printf("<IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", url_images_path, ACTION_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "执行主机扩展动作", "执行主机扩展动作");
				printf("</a>");
				printf("</td>\n");
				}
			if(temp_host->icon_image != NULL) {
				printf("<td align=center valign=center>");
				printf("<a href='%s?type=%d&host=%s'>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_status->host_name));
				printf("<IMG SRC='%s", url_logo_images_path);
				process_macros_r(mac, temp_host->icon_image, &processed_string, 0);
				printf("%s", processed_string);
				free(processed_string);
				printf("' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, (temp_host->icon_image_alt == NULL) ? "" : temp_host->icon_image_alt, (temp_host->icon_image_alt == NULL) ? "" : temp_host->icon_image_alt);
				printf("</a>");
				printf("</td>\n");
				}
			if(enable_splunk_integration == TRUE) {
				printf("<td ALIGN=center valign=center>");
				display_splunk_host_url(temp_host);
				printf("</td>\n");
				}
			printf("<td>");
			printf("<a href='%s?host=%s'><img src='%s%s' border=0 alt='该主机的详细服务状态' title='该主机的详细服务状态'></a>", STATUS_CGI, url_encode(temp_status->host_name), url_images_path, STATUS_DETAIL_ICON);
			printf("</td>\n");
			printf("</tr>\n");
			printf("</table>\n");
			printf("</td>\n");
			printf("</tr>\n");
			printf("</table>\n");

			printf("</td>\n");


			/* state duration calculation... */
			t = 0;
			duration_error = FALSE;
			if(temp_status->last_state_change == (time_t)0) {
				if(program_start > current_time)
					duration_error = TRUE;
				else
					t = current_time - program_start;
				}
			else {
				if(temp_status->last_state_change > current_time)
					duration_error = TRUE;
				else
					t = current_time - temp_status->last_state_change;
				}
			get_time_breakdown((unsigned long)t, &days, &hours, &minutes, &seconds);
			if(duration_error == TRUE)
				snprintf(state_duration, sizeof(state_duration) - 1, "???");
			else
				snprintf(state_duration, sizeof(state_duration) - 1, "%2d日%2d时%2d分%2d秒%s", days, hours, minutes, seconds, (temp_status->last_state_change == (time_t)0) ? "+" : "");
			state_duration[sizeof(state_duration) - 1] = '\x0';

			/* the rest of the columns... */
			printf("<td class='status%s'>%s</td>\n", status_class, status);
			printf("<td class='status%s' nowrap>%s</td>\n", status_bg_class, date_time);
			printf("<td class='status%s' nowrap>%s</td>\n", status_bg_class, state_duration);
			printf("<td class='status%s' valign='center'>", status_bg_class);
			printf("%s&nbsp;", (temp_status->plugin_output == NULL) ? "" : html_encode(temp_status->plugin_output, TRUE));
			/*
			if(enable_splunk_integration==TRUE)
				display_splunk_host_url(temp_host);
			*/
			printf("</td>\n");

			printf("</tr>\n");
			}

		}

	printf("</table>\n");
	printf("</div>\n");

	/* if user couldn't see anything, print out some helpful info... */
	if(user_has_seen_something == FALSE) {

		if(hoststatus_list != NULL) {
			printf("<P><div class='errorMessage'>看起来像是你没有权限查看你请求主机的任何信息...</div></P>\n");
			printf("<P><div class='errorDescription'>如果你认为这是一个错误，请检查HTTP服务器访问CGI的身份验证要求<br>");
			printf("并在你的CGI的配置文件中检查授权选项。</div></P>\n");
			}
		else {
			printf("<P><div class='infoMessage'>日志中不存在任何主机状态信息...<br><br>\n");
			printf("请确保Nagios程序正常运行，并且配置文件中状态日志设置正确。</div></P>\n");
			}
		}

	else {
		/* do page numbers if applicable */
		create_pagenumbers(total_entries,temp_url,FALSE);
		}
	return;
	}




/* show an overview of servicegroup(s)... */
void show_servicegroup_overviews(void) {
	servicegroup *temp_servicegroup = NULL;
	int current_column;
	int user_has_seen_something = FALSE;
	int servicegroup_error = FALSE;


	//printf("<P>\n");

	printf("<table border=0 width=100%%>\n");
	printf("<tr>\n");

	printf("<td valign=top align=left width=33%%>\n");

	show_filters();

	printf("</td>");

	printf("<td valign=top align=center width=33%%>\n");

	printf("<div align='center' class='statusTitle'>");
	if(show_all_servicegroups == TRUE)
		printf("所有服务组");
	else
		printf("服务组 '%s' ", servicegroup_name);
	printf("的服务概要</div>\n");

	printf("<br>");

	printf("</td>\n");

	printf("<td valign=top align=right width=33%%></td>\n");

	printf("</tr>\n");
	printf("</table>\n");

	//printf("</P>\n");


	/* display status overviews for all servicegroups */
	if(show_all_servicegroups == TRUE) {


		printf("<div ALIGN=center>\n");
		printf("<table border=0 cellpadding=10>\n");

		current_column = 1;

		/* loop through all servicegroups... */
		for(temp_servicegroup = servicegroup_list; temp_servicegroup != NULL; temp_servicegroup = temp_servicegroup->next) {

			/* make sure the user is authorized to view at least one host in this servicegroup */
			if(is_authorized_for_servicegroup(temp_servicegroup, &current_authdata) == FALSE)
				continue;

			if(current_column == 1)
				printf("<tr>\n");
			printf("<td VALIGN=top ALIGN=center>\n");

			show_servicegroup_overview(temp_servicegroup);

			user_has_seen_something = TRUE;

			printf("</td>\n");
			if(current_column == overview_columns)
				printf("</tr>\n");

			if(current_column < overview_columns)
				current_column++;
			else
				current_column = 1;
			}

		if(current_column != 1) {

			for(; current_column <= overview_columns; current_column++)
				printf("<td></td>\n");
			printf("</tr>\n");
			}

		printf("</table>\n");
		printf("</div>\n");
		}

	/* else display overview for just a specific servicegroup */
	else {

		temp_servicegroup = find_servicegroup(servicegroup_name);
		if(temp_servicegroup != NULL) {

			//printf("<P>\n");
			printf("<div align='center'>\n");
			printf("<table border=0 cellpadding=0 cellspacing=0><tr><td align='center'>\n");

			if(is_authorized_for_servicegroup(temp_servicegroup, &current_authdata) == TRUE) {

				show_servicegroup_overview(temp_servicegroup);

				user_has_seen_something = TRUE;
				}

			printf("</td></tr></table>\n");
			printf("</div>\n");
			//printf("</P>\n");
			}
		else {
			printf("<div class='errorMessage'>抱歉，服务组'%s'似乎不存在...</div>", servicegroup_name);
			servicegroup_error = TRUE;
			}
		}

	/* if user couldn't see anything, print out some helpful info... */
	if(user_has_seen_something == FALSE && servicegroup_error == FALSE) {

		//printf("<p>\n");
		printf("<div align='center'>\n");

		if(servicegroup_list != NULL) {
			printf("<div class='errorMessage'>看起来像是你没有权限查看你请求主机的任何信息...</div>\n");
			printf("<div class='errorDescription'>如果你认为这是一个错误，请检查HTTP服务器访问CGI的身份验证要求<br>");
			printf("并在你的CGI的配置文件中检查授权选项。</div>\n");
			}
		else {
			printf("<div class='errorMessage'>服务组不存在。</div>\n");
			}

		printf("</div>\n");
		//printf("</p>\n");
		}

	return;
	}



/* shows an overview of a specific servicegroup... */
void show_servicegroup_overview(servicegroup *temp_servicegroup) {
	servicesmember *temp_member;
	host *temp_host;
	host *last_host;
	hoststatus *temp_hoststatus = NULL;
	int odd = 0;


	printf("<div class='status'>\n");
	printf("<a href='%s?servicegroup=%s&style=detail'>%s</a>", STATUS_CGI, url_encode(temp_servicegroup->group_name), temp_servicegroup->alias);
	printf(" (<a href='%s?type=%d&servicegroup=%s'>%s</a>)", EXTINFO_CGI, DISPLAY_SERVICEGROUP_INFO, url_encode(temp_servicegroup->group_name), temp_servicegroup->group_name);
	printf("</div>\n");

	printf("<div class='status'>\n");
	printf("<table class='status'>\n");

	printf("<tr>\n");
	printf("<th class='status'>主机</th><th class='status'>状态</th><th class='status'>服务</th><th class='status'>动作</th>\n");
	printf("</tr>\n");

	/* find all hosts that have services that are members of the servicegroup */
	last_host = NULL;
	for(temp_member = temp_servicegroup->members; temp_member != NULL; temp_member = temp_member->next) {

		/* find the host */
		temp_host = find_host(temp_member->host_name);
		if(temp_host == NULL)
			continue;

		/* make sure user has rights to view this host */
		if(is_authorized_for_host(temp_host, &current_authdata) == FALSE)
			continue;

		/* skip this if it isn't a new host... */
		if(temp_host == last_host)
			continue;

		/* find the host status */
		temp_hoststatus = find_hoststatus(temp_host->name);
		if(temp_hoststatus == NULL)
			continue;

		/* make sure we only display hosts of the specified status levels */
		if(!(host_status_types & temp_hoststatus->status))
			continue;

		/* make sure we only display hosts that have the desired properties */
		if(passes_host_properties_filter(temp_hoststatus) == FALSE)
			continue;

		if(odd)
			odd = 0;
		else
			odd = 1;

		show_servicegroup_hostgroup_member_overview(temp_hoststatus, odd, temp_servicegroup);

		last_host = temp_host;
		}

	printf("</table>\n");
	printf("</div>\n");

	return;
	}



/* show a summary of servicegroup(s)... */
void show_servicegroup_summaries(void) {
	servicegroup *temp_servicegroup = NULL;
	int user_has_seen_something = FALSE;
	int servicegroup_error = FALSE;
	int odd = 0;


	printf("<P>\n");

	printf("<table border=0 width=100%%>\n");
	printf("<tr>\n");

	printf("<td valign=top align=left width=33%%>\n");

	show_filters();

	printf("</td>");

	printf("<td valign=top align=center width=33%%>\n");

	printf("<div align='center' class='statusTitle'>");
	if(show_all_servicegroups == TRUE)
		printf("所有服务组");
	else
		printf("服务组 '%s' ", servicegroup_name);
	printf("的状态汇总</div>\n");

	printf("<br>");

	printf("</td>\n");

	printf("<td valign=top align=right width=33%%></td>\n");

	printf("</tr>\n");
	printf("</table>\n");

	printf("</P>\n");


	printf("<div ALIGN=center>\n");
	printf("<table class='status'>\n");

	printf("<tr>\n");
	printf("<th class='status'>服务组</th><th class='status'>主机状态汇总</th><th class='status'>服务状态汇总</th>\n");
	printf("</tr>\n");

	/* display status summary for all servicegroups */
	if(show_all_servicegroups == TRUE) {

		/* loop through all servicegroups... */
		for(temp_servicegroup = servicegroup_list; temp_servicegroup != NULL; temp_servicegroup = temp_servicegroup->next) {

			/* make sure the user is authorized to view at least one host in this servicegroup */
			if(is_authorized_for_servicegroup(temp_servicegroup, &current_authdata) == FALSE)
				continue;

			if(odd == 0)
				odd = 1;
			else
				odd = 0;

			/* show summary for this servicegroup */
			show_servicegroup_summary(temp_servicegroup, odd);

			user_has_seen_something = TRUE;
			}

		}

	/* else just show summary for a specific servicegroup */
	else {
		temp_servicegroup = find_servicegroup(servicegroup_name);
		if(temp_servicegroup == NULL)
			servicegroup_error = TRUE;
		else {
			show_servicegroup_summary(temp_servicegroup, 1);
			user_has_seen_something = TRUE;
			}
		}

	printf("</table>\n");
	printf("</div>\n");

	/* if user couldn't see anything, print out some helpful info... */
	if(user_has_seen_something == FALSE && servicegroup_error == FALSE) {

		printf("<P><div align='center'>\n");

		if(servicegroup_list != NULL) {
			printf("<div class='errorMessage'>看起来像是你没有权限查看你请求主机的任何信息...</div>\n");
			printf("<div class='errorDescription'>如果你认为这是一个错误，请检查HTTP服务器访问CGI的身份验证要求<br>");
			printf("并在你的CGI的配置文件中检查授权选项。</div>\n");
			}
		else {
			printf("<div class='errorMessage'>服务组不存在。</div>\n");
			}

		printf("</div></P>\n");
		}

	/* we couldn't find the servicegroup */
	else if(servicegroup_error == TRUE) {
		printf("<P><div align='center'>\n");
		printf("<div class='errorMessage'>抱歉，服务组'%s'似乎不存在...</div>\n", servicegroup_name);
		printf("</div></P>\n");
		}

	return;
	}



/* displays status summary information for a specific servicegroup */
void show_servicegroup_summary(servicegroup *temp_servicegroup, int odd) {
	const char *status_bg_class = "";

	if(odd == 1)
		status_bg_class = "Even";
	else
		status_bg_class = "Odd";

	printf("<tr class='status%s'><td class='status%s'>\n", status_bg_class, status_bg_class);
	printf("<a href='%s?servicegroup=%s&style=overview'>%s</a> ", STATUS_CGI, url_encode(temp_servicegroup->group_name), temp_servicegroup->alias);
	printf("(<a href='%s?type=%d&servicegroup=%s'>%s</a>)", EXTINFO_CGI, DISPLAY_SERVICEGROUP_INFO, url_encode(temp_servicegroup->group_name), temp_servicegroup->group_name);
	printf("</td>");

	printf("<td class='status%s' align='center' Valign='center'>", status_bg_class);
	show_servicegroup_host_totals_summary(temp_servicegroup);
	printf("</td>");

	printf("<td class='status%s' align='center' Valign='center'>", status_bg_class);
	show_servicegroup_service_totals_summary(temp_servicegroup);
	printf("</td>");

	printf("</tr>\n");

	return;
	}



/* shows host total summary information for a specific servicegroup */
void show_servicegroup_host_totals_summary(servicegroup *temp_servicegroup) {
	servicesmember *temp_member;
	int hosts_up = 0;
	int hosts_down = 0;
	int hosts_unreachable = 0;
	int hosts_pending = 0;
	int hosts_down_scheduled = 0;
	int hosts_down_acknowledged = 0;
	int hosts_down_disabled = 0;
	int hosts_down_unacknowledged = 0;
	int hosts_unreachable_scheduled = 0;
	int hosts_unreachable_acknowledged = 0;
	int hosts_unreachable_disabled = 0;
	int hosts_unreachable_unacknowledged = 0;
	hoststatus *temp_hoststatus = NULL;
	host *temp_host = NULL;
	host *last_host = NULL;
	int problem = FALSE;

	/* find all the hosts that belong to the servicegroup */
	for(temp_member = temp_servicegroup->members; temp_member != NULL; temp_member = temp_member->next) {

		/* find the host... */
		temp_host = find_host(temp_member->host_name);
		if(temp_host == NULL)
			continue;

		/* make sure user has rights to view this host */
		if(is_authorized_for_host(temp_host, &current_authdata) == FALSE)
			continue;

		/* skip this if it isn't a new host... */
		if(temp_host == last_host)
			continue;

		/* find the host status */
		temp_hoststatus = find_hoststatus(temp_host->name);
		if(temp_hoststatus == NULL)
			continue;

		/* make sure we only display hosts of the specified status levels */
		if(!(host_status_types & temp_hoststatus->status))
			continue;

		/* make sure we only display hosts that have the desired properties */
		if(passes_host_properties_filter(temp_hoststatus) == FALSE)
			continue;

		problem = TRUE;

		if(temp_hoststatus->status == SD_HOST_UP)
			hosts_up++;

		else if(temp_hoststatus->status == SD_HOST_DOWN) {
			if(temp_hoststatus->scheduled_downtime_depth > 0) {
				hosts_down_scheduled++;
				problem = FALSE;
				}
			if(temp_hoststatus->problem_has_been_acknowledged == TRUE) {
				hosts_down_acknowledged++;
				problem = FALSE;
				}
			if(temp_hoststatus->checks_enabled == FALSE) {
				hosts_down_disabled++;
				problem = FALSE;
				}
			if(problem == TRUE)
				hosts_down_unacknowledged++;
			hosts_down++;
			}

		else if(temp_hoststatus->status == SD_HOST_UNREACHABLE) {
			if(temp_hoststatus->scheduled_downtime_depth > 0) {
				hosts_unreachable_scheduled++;
				problem = FALSE;
				}
			if(temp_hoststatus->problem_has_been_acknowledged == TRUE) {
				hosts_unreachable_acknowledged++;
				problem = FALSE;
				}
			if(temp_hoststatus->checks_enabled == FALSE) {
				hosts_unreachable_disabled++;
				problem = FALSE;
				}
			if(problem == TRUE)
				hosts_unreachable_unacknowledged++;
			hosts_unreachable++;
			}

		else
			hosts_pending++;

		last_host = temp_host;
		}

	printf("<table border='0'>\n");

	if(hosts_up > 0) {
		printf("<tr>");
		printf("<td class='miniStatusUP'><a href='%s?servicegroup=%s&style=detail&&hoststatustypes=%d&hostprops=%lu'>运行状态 %d 个<BR>(UP)</a></td>", STATUS_CGI, url_encode(temp_servicegroup->group_name), SD_HOST_UP, host_properties, hosts_up);
		printf("</tr>\n");
		}

	if(hosts_down > 0) {
		printf("<tr>\n");
		printf("<td class='miniStatusDOWN'><table border='0'>\n");
		printf("<tr>\n");

		printf("<td class='miniStatusDOWN'><a href='%s?servicegroup=%s&style=detail&hoststatustypes=%d&hostprops=%lu'>宕机状态 %d 个<BR>(DOWN)</a>&nbsp;:</td>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SD_HOST_DOWN, host_properties, hosts_down);

		printf("<td><table border='0'>\n");

		if(hosts_down_unacknowledged > 0)
			printf("<tr><td width=100%% class='hostImportantProblem'><a href='%s?servicegroup=%s&style=detail&hoststatustypes=%d&hostprops=%d'>尚未解决 %d 个<BR>(Unhandled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SD_HOST_DOWN, HOST_NO_SCHEDULED_DOWNTIME | HOST_STATE_UNACKNOWLEDGED | HOST_CHECKS_ENABLED, hosts_down_unacknowledged);

		if(hosts_down_scheduled > 0)
			printf("<tr><td width=100%% class='hostUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&hoststatustypes=%d&hostprops=%d'>计划内非重要故障 %d 个<BR>(Scheduled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SD_HOST_DOWN, HOST_SCHEDULED_DOWNTIME, hosts_down_scheduled);

		if(hosts_down_acknowledged > 0)
			printf("<tr><td width=100%% class='hostUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&hoststatustypes=%d&hostprops=%d'>已确认非重要故障 %d 个<BR>(Acknowledged)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SD_HOST_DOWN, HOST_STATE_ACKNOWLEDGED, hosts_down_acknowledged);

		if(hosts_down_disabled > 0)
			printf("<tr><td width=100%% class='hostUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&hoststatustypes=%d&hostprops=%d'>关闭的非重要故障 %d 个<BR>(Disabled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SD_HOST_DOWN, HOST_CHECKS_DISABLED, hosts_down_disabled);

		printf("</table></td>\n");

		printf("</tr>\n");
		printf("</table></td>\n");
		printf("</tr>\n");
		}

	if(hosts_unreachable > 0) {
		printf("<tr>\n");
		printf("<td class='miniStatusUNREACHABLE'><table border='0'>\n");
		printf("<tr>\n");

		printf("<td class='miniStatusUNREACHABLE'><a href='%s?servicegroup=%s&style=detail&hoststatustypes=%d&hostprops=%lu'>状态不可达 %d 个<BR>(UNREACHABLE)</a>&nbsp;:</td>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SD_HOST_UNREACHABLE, host_properties, hosts_unreachable);

		printf("<td><table border='0'>\n");

		if(hosts_unreachable_unacknowledged > 0)
			printf("<tr><td width=100%% class='hostImportantProblem'><a href='%s?servicegroup=%s&style=detail&hoststatustypes=%d&hostprops=%d'>尚未解决 %d 个<BR>(Unhandled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SD_HOST_UNREACHABLE, HOST_NO_SCHEDULED_DOWNTIME | HOST_STATE_UNACKNOWLEDGED | HOST_CHECKS_ENABLED, hosts_unreachable_unacknowledged);

		if(hosts_unreachable_scheduled > 0)
			printf("<tr><td width=100%% class='hostUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&hoststatustypes=%d&hostprops=%d'>计划内非重要故障 %d 个<BR>(Scheduled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SD_HOST_UNREACHABLE, HOST_SCHEDULED_DOWNTIME, hosts_unreachable_scheduled);

		if(hosts_unreachable_acknowledged > 0)
			printf("<tr><td width=100%% class='hostUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&hoststatustypes=%d&hostprops=%d'>已确认非重要故障 %d 个<BR>(Acknowledged)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SD_HOST_UNREACHABLE, HOST_STATE_ACKNOWLEDGED, hosts_unreachable_acknowledged);

		if(hosts_unreachable_disabled > 0)
			printf("<tr><td width=100%% class='hostUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&hoststatustypes=%d&hostprops=%d'>关闭的非重要故障 %d 个<BR>(Disabled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SD_HOST_UNREACHABLE, HOST_CHECKS_DISABLED, hosts_unreachable_disabled);

		printf("</table></td>\n");

		printf("</tr>\n");
		printf("</table></td>\n");
		printf("</tr>\n");
		}

	if(hosts_pending > 0)
		printf("<tr><td class='miniStatusPENDING'><a href='%s?servicegroup=%s&style=detail&hoststatustypes=%d&hostprops=%lu'>状态保持 %d 个<BR>(PENDING)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), HOST_PENDING, host_properties, hosts_pending);

	printf("</table>\n");

	if((hosts_up + hosts_down + hosts_unreachable + hosts_pending) == 0)
		printf("无符合条件的主机");

	return;
	return;
	}



/* shows service total summary information for a specific servicegroup */
void show_servicegroup_service_totals_summary(servicegroup *temp_servicegroup) {
	int services_ok = 0;
	int services_warning = 0;
	int services_unknown = 0;
	int services_critical = 0;
	int services_pending = 0;
	int services_warning_host_problem = 0;
	int services_warning_scheduled = 0;
	int services_warning_acknowledged = 0;
	int services_warning_disabled = 0;
	int services_warning_unacknowledged = 0;
	int services_unknown_host_problem = 0;
	int services_unknown_scheduled = 0;
	int services_unknown_acknowledged = 0;
	int services_unknown_disabled = 0;
	int services_unknown_unacknowledged = 0;
	int services_critical_host_problem = 0;
	int services_critical_scheduled = 0;
	int services_critical_acknowledged = 0;
	int services_critical_disabled = 0;
	int services_critical_unacknowledged = 0;
	servicesmember *temp_member = NULL;
	servicestatus *temp_servicestatus = NULL;
	hoststatus *temp_hoststatus = NULL;
	service *temp_service = NULL;
	service *last_service = NULL;
	int problem = FALSE;


	/* find all the services that belong to the servicegroup */
	for(temp_member = temp_servicegroup->members; temp_member != NULL; temp_member = temp_member->next) {

		/* find the service */
		temp_service = find_service(temp_member->host_name, temp_member->service_description);
		if(temp_service == NULL)
			continue;

		/* make sure user has rights to view this service */
		if(is_authorized_for_service(temp_service, &current_authdata) == FALSE)
			continue;

		/* skip this if it isn't a new service... */
		if(temp_service == last_service)
			continue;

		/* find the service status */
		temp_servicestatus = find_servicestatus(temp_service->host_name, temp_service->description);
		if(temp_servicestatus == NULL)
			continue;

		/* find the status of the associated host */
		temp_hoststatus = find_hoststatus(temp_servicestatus->host_name);
		if(temp_hoststatus == NULL)
			continue;

		/* make sure we only display hosts of the specified status levels */
		if(!(host_status_types & temp_hoststatus->status))
			continue;

		/* make sure we only display hosts that have the desired properties */
		if(passes_host_properties_filter(temp_hoststatus) == FALSE)
			continue;

		/* make sure we only display services of the specified status levels */
		if(!(service_status_types & temp_servicestatus->status))
			continue;

		/* make sure we only display services that have the desired properties */
		if(passes_service_properties_filter(temp_servicestatus) == FALSE)
			continue;

		problem = TRUE;

		if(temp_servicestatus->status == SERVICE_OK)
			services_ok++;

		else if(temp_servicestatus->status == SERVICE_WARNING) {
			temp_hoststatus = find_hoststatus(temp_servicestatus->host_name);
			if(temp_hoststatus != NULL && (temp_hoststatus->status == SD_HOST_DOWN || temp_hoststatus->status == SD_HOST_UNREACHABLE)) {
				services_warning_host_problem++;
				problem = FALSE;
				}
			if(temp_servicestatus->scheduled_downtime_depth > 0) {
				services_warning_scheduled++;
				problem = FALSE;
				}
			if(temp_servicestatus->problem_has_been_acknowledged == TRUE) {
				services_warning_acknowledged++;
				problem = FALSE;
				}
			if(temp_servicestatus->checks_enabled == FALSE) {
				services_warning_disabled++;
				problem = FALSE;
				}
			if(problem == TRUE)
				services_warning_unacknowledged++;
			services_warning++;
			}

		else if(temp_servicestatus->status == SERVICE_UNKNOWN) {
			temp_hoststatus = find_hoststatus(temp_servicestatus->host_name);
			if(temp_hoststatus != NULL && (temp_hoststatus->status == SD_HOST_DOWN || temp_hoststatus->status == SD_HOST_UNREACHABLE)) {
				services_unknown_host_problem++;
				problem = FALSE;
				}
			if(temp_servicestatus->scheduled_downtime_depth > 0) {
				services_unknown_scheduled++;
				problem = FALSE;
				}
			if(temp_servicestatus->problem_has_been_acknowledged == TRUE) {
				services_unknown_acknowledged++;
				problem = FALSE;
				}
			if(temp_servicestatus->checks_enabled == FALSE) {
				services_unknown_disabled++;
				problem = FALSE;
				}
			if(problem == TRUE)
				services_unknown_unacknowledged++;
			services_unknown++;
			}

		else if(temp_servicestatus->status == SERVICE_CRITICAL) {
			temp_hoststatus = find_hoststatus(temp_servicestatus->host_name);
			if(temp_hoststatus != NULL && (temp_hoststatus->status == SD_HOST_DOWN || temp_hoststatus->status == SD_HOST_UNREACHABLE)) {
				services_critical_host_problem++;
				problem = FALSE;
				}
			if(temp_servicestatus->scheduled_downtime_depth > 0) {
				services_critical_scheduled++;
				problem = FALSE;
				}
			if(temp_servicestatus->problem_has_been_acknowledged == TRUE) {
				services_critical_acknowledged++;
				problem = FALSE;
				}
			if(temp_servicestatus->checks_enabled == FALSE) {
				services_critical_disabled++;
				problem = FALSE;
				}
			if(problem == TRUE)
				services_critical_unacknowledged++;
			services_critical++;
			}

		else if(temp_servicestatus->status == SERVICE_PENDING)
			services_pending++;

		last_service = temp_service;
		}


	printf("<table border=0>\n");

	if(services_ok > 0)
		printf("<tr><td class='miniStatusOK'><a href='%s?servicegroup=%s&style=detail&&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>正常状态 %d 个<BR>(OK)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_OK, host_status_types, service_properties, host_properties, services_ok);

	if(services_warning > 0) {
		printf("<tr>\n");
		printf("<td class='miniStatusWARNING'><table border='0'>\n");
		printf("<tr>\n");

		printf("<td class='miniStatusWARNING'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>警告状态 %d 个<BR>(WARNING)</a>&nbsp;:</td>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_WARNING, host_status_types, service_properties, host_properties, services_warning);

		printf("<td><table border='0'>\n");

		if(services_warning_unacknowledged > 0)
			printf("<tr><td width=100%% class='serviceImportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%d'>尚未解决 %d 个<BR>(Unhandled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_WARNING, SD_HOST_UP | HOST_PENDING, SERVICE_NO_SCHEDULED_DOWNTIME | SERVICE_STATE_UNACKNOWLEDGED | SERVICE_CHECKS_ENABLED, services_warning_unacknowledged);

		if(services_warning_host_problem > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d'>在主机上的问题 %d 个</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_WARNING, SD_HOST_DOWN | SD_HOST_UNREACHABLE, services_warning_host_problem);

		if(services_warning_scheduled > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>计划内非重要故障 %d 个<BR>(Scheduled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_WARNING, SERVICE_SCHEDULED_DOWNTIME, services_warning_scheduled);

		if(services_warning_acknowledged > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>已确认非重要故障 %d 个<BR>(Acknowledged)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_WARNING, SERVICE_STATE_ACKNOWLEDGED, services_warning_acknowledged);

		if(services_warning_disabled > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>关闭的非重要故障 %d 个<BR>(Disabled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_WARNING, SERVICE_CHECKS_DISABLED, services_warning_disabled);

		printf("</table></td>\n");

		printf("</tr>\n");
		printf("</table></td>\n");
		printf("</tr>\n");
		}

	if(services_unknown > 0) {
		printf("<tr>\n");
		printf("<td class='miniStatusUNKNOWN'><table border='0'>\n");
		printf("<tr>\n");

		printf("<td class='miniStatusUNKNOWN'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>未知状态 %d 个<BR>(UNKNOWN)</a>&nbsp;:</td>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_UNKNOWN, host_status_types, service_properties, host_properties, services_unknown);

		printf("<td><table border='0'>\n");

		if(services_unknown_unacknowledged > 0)
			printf("<tr><td width=100%% class='serviceImportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%d'>尚未解决 %d 个<BR>(Unhandled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_UNKNOWN, SD_HOST_UP | HOST_PENDING, SERVICE_NO_SCHEDULED_DOWNTIME | SERVICE_STATE_UNACKNOWLEDGED | SERVICE_CHECKS_ENABLED, services_unknown_unacknowledged);

		if(services_unknown_host_problem > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d'>在主机上的问题 %d 个</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_UNKNOWN, SD_HOST_DOWN | SD_HOST_UNREACHABLE, services_unknown_host_problem);

		if(services_unknown_scheduled > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>计划内非重要故障 %d 个<BR>(Scheduled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_UNKNOWN, SERVICE_SCHEDULED_DOWNTIME, services_unknown_scheduled);

		if(services_unknown_acknowledged > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>已确认非重要故障 %d 个<BR>(Acknowledged)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_UNKNOWN, SERVICE_STATE_ACKNOWLEDGED, services_unknown_acknowledged);

		if(services_unknown_disabled > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>关闭的非重要故障 %d 个<BR>(Disabled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_UNKNOWN, SERVICE_CHECKS_DISABLED, services_unknown_disabled);

		printf("</table></td>\n");

		printf("</tr>\n");
		printf("</table></td>\n");
		printf("</tr>\n");
		}

	if(services_critical > 0) {
		printf("<tr>\n");
		printf("<td class='miniStatusCRITICAL'><table border='0'>\n");
		printf("<tr>\n");

		printf("<td class='miniStatusCRITICAL'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>紧急状态 %d 个<BR>(CRITICAL)</a>&nbsp:</td>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_CRITICAL, host_status_types, service_properties, host_properties, services_critical);

		printf("<td><table border='0'>\n");

		if(services_critical_unacknowledged > 0)
			printf("<tr><td width=100%% class='serviceImportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%d'>尚未解决 %d 个<BR>(Unhandled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_CRITICAL, SD_HOST_UP | HOST_PENDING, SERVICE_NO_SCHEDULED_DOWNTIME | SERVICE_STATE_UNACKNOWLEDGED | SERVICE_CHECKS_ENABLED, services_critical_unacknowledged);

		if(services_critical_host_problem > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d'>在主机上的问题 %d 个</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_CRITICAL, SD_HOST_DOWN | SD_HOST_UNREACHABLE, services_critical_host_problem);

		if(services_critical_scheduled > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>计划内非重要故障 %d 个<BR>(Scheduled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_CRITICAL, SERVICE_SCHEDULED_DOWNTIME, services_critical_scheduled);

		if(services_critical_acknowledged > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>已确认非重要故障 %d 个<BR>(Acknowledged)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_CRITICAL, SERVICE_STATE_ACKNOWLEDGED, services_critical_acknowledged);

		if(services_critical_disabled > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>关闭的非重要故障 %d 个<BR>(Disabled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_CRITICAL, SERVICE_CHECKS_DISABLED, services_critical_disabled);

		printf("</table></td>\n");

		printf("</tr>\n");
		printf("</table></td>\n");
		printf("</tr>\n");
		}

	if(services_pending > 0)
		printf("<tr><td class='miniStatusPENDING'><a href='%s?servicegroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>状态保持 %d 个<BR>(PENDING)</a></td></tr>\n", STATUS_CGI, url_encode(temp_servicegroup->group_name), SERVICE_PENDING, host_status_types, service_properties, host_properties, services_pending);

	printf("</table>\n");

	if((services_ok + services_warning + services_unknown + services_critical + services_pending) == 0)
		printf("无符合条件的服务");

	return;
	}



/* show a grid layout of servicegroup(s)... */
void show_servicegroup_grids(void) {
	servicegroup *temp_servicegroup = NULL;
	int user_has_seen_something = FALSE;
	int servicegroup_error = FALSE;
	int odd = 0;


	printf("<P>\n");

	printf("<table border=0 width=100%%>\n");
	printf("<tr>\n");

	printf("<td valign=top align=left width=33%%>\n");

	show_filters();

	printf("</td>");

	printf("<td valign=top align=center width=33%%>\n");

	printf("<div align='center' class='statusTitle'>");
	if(show_all_servicegroups == TRUE)
		printf("所有服务组");
	else
		printf("服务组 '%s' ", servicegroup_name);
	printf("的状态表格</div>\n");

	printf("<br>");

	printf("</td>\n");

	printf("<td valign=top align=right width=33%%></td>\n");

	printf("</tr>\n");
	printf("</table>\n");

	printf("</P>\n");


	/* display status grids for all servicegroups */
	if(show_all_servicegroups == TRUE) {

		/* loop through all servicegroups... */
		for(temp_servicegroup = servicegroup_list; temp_servicegroup != NULL; temp_servicegroup = temp_servicegroup->next) {

			/* make sure the user is authorized to view at least one host in this servicegroup */
			if(is_authorized_for_servicegroup(temp_servicegroup, &current_authdata) == FALSE)
				continue;

			if(odd == 0)
				odd = 1;
			else
				odd = 0;

			/* show grid for this servicegroup */
			show_servicegroup_grid(temp_servicegroup);

			user_has_seen_something = TRUE;
			}

		}

	/* else just show grid for a specific servicegroup */
	else {
		temp_servicegroup = find_servicegroup(servicegroup_name);
		if(temp_servicegroup == NULL)
			servicegroup_error = TRUE;
		else {
			show_servicegroup_grid(temp_servicegroup);
			user_has_seen_something = TRUE;
			}
		}

	/* if user couldn't see anything, print out some helpful info... */
	if(user_has_seen_something == FALSE && servicegroup_error == FALSE) {

		printf("<P><div align='center'>\n");

		if(servicegroup_list != NULL) {
			printf("<div class='errorMessage'>看起来像是你没有权限查看你请求主机的任何信息...</div>\n");
			printf("<div class='errorDescription'>如果你认为这是一个错误，请检查HTTP服务器访问CGI的身份验证要求<br>");
			printf("并在你的CGI的配置文件中检查授权选项。</div>\n");
			}
		else {
			printf("<div class='errorMessage'>服务组不存在。</div>\n");
			}

		printf("</div></P>\n");
		}

	/* we couldn't find the servicegroup */
	else if(servicegroup_error == TRUE) {
		printf("<P><div align='center'>\n");
		printf("<div class='errorMessage'>抱歉，服务组'%s'似乎不存在...</div>\n", servicegroup_name);
		printf("</div></P>\n");
		}

	return;
	}


/* displays status grid for a specific servicegroup */
void show_servicegroup_grid(servicegroup *temp_servicegroup) {
	const char *status_bg_class = "";
	const char *host_status_class = "";
	const char *service_status_class = "";
	char *processed_string = NULL;
	servicesmember *temp_member;
	servicesmember *temp_member2;
	host *temp_host;
	host *last_host;
	hoststatus *temp_hoststatus;
	servicestatus *temp_servicestatus;
	int odd = 0;
	int current_item;


	printf("<P>\n");
	printf("<div align='center'>\n");

	printf("<div class='status'><a href='%s?servicegroup=%s&style=detail'>%s</a>", STATUS_CGI, url_encode(temp_servicegroup->group_name), temp_servicegroup->alias);
	printf(" (<a href='%s?type=%d&servicegroup=%s'>%s</a>)</div>", EXTINFO_CGI, DISPLAY_SERVICEGROUP_INFO, url_encode(temp_servicegroup->group_name), temp_servicegroup->group_name);

	printf("<table class='status' align='center'>\n");
	printf("<tr><th class='status'>主机</th><th class='status'>服务</a></th><th class='status'>动作</th></tr>\n");

	/* find all hosts that have services that are members of the servicegroup */
	last_host = NULL;
	for(temp_member = temp_servicegroup->members; temp_member != NULL; temp_member = temp_member->next) {

		/* find the host */
		temp_host = find_host(temp_member->host_name);
		if(temp_host == NULL)
			continue;

		/* make sure user has rights to view this host */
		if(is_authorized_for_host(temp_host, &current_authdata) == FALSE)
			continue;

		/* get the status of the host */
		temp_hoststatus = find_hoststatus(temp_host->name);
		if(temp_hoststatus == NULL)
			continue;

		/* skip this if it isn't a new host... */
		if(temp_host == last_host)
			continue;

		if(odd == 1) {
			status_bg_class = "Even";
			odd = 0;
			}
		else {
			status_bg_class = "Odd";
			odd = 1;
			}

		printf("<tr class='status%s'>\n", status_bg_class);

		if(temp_hoststatus->status == SD_HOST_DOWN)
			host_status_class = "HOStdOWN";
		else if(temp_hoststatus->status == SD_HOST_UNREACHABLE)
			host_status_class = "HOSTUNREACHABLE";
		else
			host_status_class = status_bg_class;

		printf("<td class='status%s'>", host_status_class);

		printf("<table border=0 WIDTH='100%%' cellpadding=0 cellspacing=0>\n");
		printf("<tr>\n");
		printf("<td align='left'>\n");
		printf("<table border=0 cellpadding=0 cellspacing=0>\n");
		printf("<tr>\n");
		printf("<td align=left valign=center class='status%s'>", host_status_class);
		printf("<a href='%s?type=%d&host=%s'>%s</a>\n", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_host->name), temp_host->name);
		printf("</td>\n");
		printf("</tr>\n");
		printf("</table>\n");
		printf("</td>\n");
		printf("<td align=right valign=center nowrap>\n");
		printf("<table border=0 cellpadding=0 cellspacing=0>\n");
		printf("<tr>\n");

		if(temp_host->icon_image != NULL) {
			printf("<td align=center valign=center>");
			printf("<a href='%s?type=%d&host=%s'>\n", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_host->name));
			printf("<IMG SRC='%s", url_logo_images_path);
			process_macros_r(mac, temp_host->icon_image, &processed_string, 0);
			printf("%s", processed_string);
			free(processed_string);
			printf("' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, (temp_host->icon_image_alt == NULL) ? "" : temp_host->icon_image_alt, (temp_host->icon_image_alt == NULL) ? "" : temp_host->icon_image_alt);
			printf("</a>");
			printf("<td>\n");
			}

		printf("</tr>\n");
		printf("</table>\n");
		printf("</td>\n");
		printf("</tr>\n");
		printf("</table>\n");

		printf("</td>\n");

		printf("<td class='status%s'>", host_status_class);

		/* display all services on the host that are part of the hostgroup */
		current_item = 1;
		for(temp_member2 = temp_member; temp_member2 != NULL; temp_member2 = temp_member2->next) {

			/* bail out if we've reached the end of the services that are associated with this servicegroup */
			if(strcmp(temp_member2->host_name, temp_host->name))
				break;

			if(current_item > max_grid_width && max_grid_width > 0) {
				printf("<BR>\n");
				current_item = 1;
				}

			/* get the status of the service */
			temp_servicestatus = find_servicestatus(temp_member2->host_name, temp_member2->service_description);
			if(temp_servicestatus == NULL)
				service_status_class = "NULL";
			else if(temp_servicestatus->status == SERVICE_OK)
				service_status_class = "OK";
			else if(temp_servicestatus->status == SERVICE_WARNING)
				service_status_class = "WARNING";
			else if(temp_servicestatus->status == SERVICE_UNKNOWN)
				service_status_class = "UNKNOWN";
			else if(temp_servicestatus->status == SERVICE_CRITICAL)
				service_status_class = "CRITICAL";
			else
				service_status_class = "PENDING";

			printf("<a href='%s?type=%d&host=%s", EXTINFO_CGI, DISPLAY_SERVICE_INFO, url_encode(temp_servicestatus->host_name));
			printf("&service=%s' class='status%s'>%s</a>&nbsp;", url_encode(temp_servicestatus->description), service_status_class, temp_servicestatus->description);

			current_item++;
			}

		/* actions */
		printf("<td class='status%s'>", host_status_class);

		/* grab macros */
		grab_host_macros_r(mac, temp_host);

		printf("<a href='%s?type=%d&host=%s'>\n", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_host->name));
		printf("<IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", url_images_path, DETAIL_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "查看该主机的扩展信息", "查看该主机的扩展信息");
		printf("</a>");

		if(temp_host->notes_url != NULL) {
			printf("<a href='");
			process_macros_r(mac, temp_host->notes_url, &processed_string, 0);
			printf("%s", processed_string);
			free(processed_string);
			printf("' TARGET='%s'>", (notes_url_target == NULL) ? "_blank" : notes_url_target);
			printf("<IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", url_images_path, NOTES_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "查看主机备注信息", "查看主机备注信息");
			printf("</a>");
			}
		if(temp_host->action_url != NULL) {
			printf("<a href='");
			process_macros_r(mac, temp_host->action_url, &processed_string, 0);
			printf("%s", processed_string);
			free(processed_string);
			printf("' TARGET='%s'>", (action_url_target == NULL) ? "blank" : action_url_target);
			printf("<IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", url_images_path, ACTION_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "执行主机扩展动作", "执行主机扩展动作");
			printf("</a>");
			}

		printf("<a href='%s?host=%s'><img src='%s%s' border=0 alt='该主机的详细服务状态' title='该主机的详细服务状态'></a>\n", STATUS_CGI, url_encode(temp_host->name), url_images_path, STATUS_DETAIL_ICON);

#ifdef USE_STATUSMAP
		printf("<a href='%s?host=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'></a>", STATUSMAP_CGI, url_encode(temp_host->name), url_images_path, STATUSMAP_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "在拓扑图上定位主机", "在拓扑图上定位主机");
#endif
		printf("</td>\n");
		printf("</tr>\n");

		last_host = temp_host;
		}

	printf("</table>\n");
	printf("</div>\n");
	printf("</P>\n");

	return;
	}



/* show an overview of hostgroup(s)... */
void show_hostgroup_overviews(void) {
	hostgroup *temp_hostgroup = NULL;
	int current_column;
	int user_has_seen_something = FALSE;
	int hostgroup_error = FALSE;


	printf("<P>\n");

	printf("<table border=0 width=100%%>\n");
	printf("<tr>\n");

	printf("<td valign=top align=left width=33%%>\n");

	show_filters();

	printf("</td>");

	printf("<td valign=top align=center width=33%%>\n");

	printf("<div align='center' class='statusTitle'>");
	if(show_all_hostgroups == TRUE)
		printf("所有主机组");
	else
		printf("主机组 '%s' ", hostgroup_name);
	printf("的服务概要</div>\n");

	printf("<br>");

	printf("</td>\n");

	printf("<td valign=top align=right width=33%%></td>\n");

	printf("</tr>\n");
	printf("</table>\n");

	printf("</P>\n");


	/* display status overviews for all hostgroups */
	if(show_all_hostgroups == TRUE) {


		printf("<div ALIGN=center>\n");
		printf("<table border=0 cellpadding=10>\n");

		current_column = 1;

		/* loop through all hostgroups... */
		for(temp_hostgroup = hostgroup_list; temp_hostgroup != NULL; temp_hostgroup = temp_hostgroup->next) {

			/* make sure the user is authorized to view this hostgroup */
			if(is_authorized_for_hostgroup(temp_hostgroup, &current_authdata) == FALSE)
				continue;

			if(current_column == 1)
				printf("<tr>\n");
			printf("<td VALIGN=top ALIGN=center>\n");

			show_hostgroup_overview(temp_hostgroup);

			user_has_seen_something = TRUE;

			printf("</td>\n");
			if(current_column == overview_columns)
				printf("</tr>\n");

			if(current_column < overview_columns)
				current_column++;
			else
				current_column = 1;
			}

		if(current_column != 1) {

			for(; current_column <= overview_columns; current_column++)
				printf("<td></td>\n");
			printf("</tr>\n");
			}

		printf("</table>\n");
		printf("</div>\n");
		}

	/* else display overview for just a specific hostgroup */
	else {

		temp_hostgroup = find_hostgroup(hostgroup_name);
		if(temp_hostgroup != NULL) {

			printf("<P>\n");
			printf("<div align='center'>\n");
			printf("<table border=0 cellpadding=0 cellspacing=0><tr><td align='center'>\n");

			if(is_authorized_for_hostgroup(temp_hostgroup, &current_authdata) == TRUE) {

				show_hostgroup_overview(temp_hostgroup);

				user_has_seen_something = TRUE;
				}

			printf("</td></tr></table>\n");
			printf("</div>\n");
			printf("</P>\n");
			}
		else {
			printf("<div class='errorMessage'>抱歉，主机组'%s'似乎不存在...</div>", hostgroup_name);
			hostgroup_error = TRUE;
			}
		}

	/* if user couldn't see anything, print out some helpful info... */
	if(user_has_seen_something == FALSE && hostgroup_error == FALSE) {

		printf("<p>\n");
		printf("<div align='center'>\n");

		if(hoststatus_list != NULL) {
			printf("<div class='errorMessage'>看起来像是你没有权限查看你请求主机的任何信息...</div>\n");
			printf("<div class='errorDescription'>如果你认为这是一个错误，请检查HTTP服务器访问CGI的身份验证要求<br>");
			printf("并在你的CGI的配置文件中检查授权选项。</div>\n");
			}
		else {
			printf("<div class='infoMessage'>日志中不存在任何主机状态信息...<br><br>\n");
			printf("请确保Nagios程序正常运行，并且配置文件中状态日志设置正确。</div>\n");
			}

		printf("</div>\n");
		printf("</p>\n");
		}

	return;
	}



/* shows an overview of a specific hostgroup... */
void show_hostgroup_overview(hostgroup *hstgrp) {
	hostsmember *temp_member = NULL;
	host *temp_host = NULL;
	hoststatus *temp_hoststatus = NULL;
	int odd = 0;

	/* make sure the user is authorized to view this hostgroup */
	if(is_authorized_for_hostgroup(hstgrp, &current_authdata) == FALSE)
		return;

	printf("<div class='status'>\n");
	printf("<a href='%s?hostgroup=%s&style=detail'>%s</a>", STATUS_CGI, url_encode(hstgrp->group_name), hstgrp->alias);
	printf(" (<a href='%s?type=%d&hostgroup=%s'>%s</a>)", EXTINFO_CGI, DISPLAY_HOSTGROUP_INFO, url_encode(hstgrp->group_name), hstgrp->group_name);
	printf("</div>\n");

	printf("<div class='status'>\n");
	printf("<table class='status'>\n");

	printf("<tr>\n");
	printf("<th class='status'>主机</th><th class='status'>状态</th><th class='status'>服务</th><th class='status'>动作</th>\n");
	printf("</tr>\n");

	/* find all the hosts that belong to the hostgroup */
	for(temp_member = hstgrp->members; temp_member != NULL; temp_member = temp_member->next) {

		/* find the host... */
		temp_host = find_host(temp_member->host_name);
		if(temp_host == NULL)
			continue;

		/* find the host status */
		temp_hoststatus = find_hoststatus(temp_host->name);
		if(temp_hoststatus == NULL)
			continue;

		/* make sure we only display hosts of the specified status levels */
		if(!(host_status_types & temp_hoststatus->status))
			continue;

		/* make sure we only display hosts that have the desired properties */
		if(passes_host_properties_filter(temp_hoststatus) == FALSE)
			continue;

		if(odd)
			odd = 0;
		else
			odd = 1;

		show_servicegroup_hostgroup_member_overview(temp_hoststatus, odd, NULL);
		}

	printf("</table>\n");
	printf("</div>\n");

	return;
	}



/* shows a host status overview... */
void show_servicegroup_hostgroup_member_overview(hoststatus *hststatus, int odd, void *data) {
	char status[MAX_INPUT_BUFFER];
	const char *status_bg_class = "";
	const char *status_class = "";
	host *temp_host = NULL;
	char *processed_string = NULL;

	temp_host = find_host(hststatus->host_name);

	/* grab macros */
	grab_host_macros_r(mac, temp_host);

	if(hststatus->status == HOST_PENDING) {
		strncpy(status, "未决", sizeof(status));
		status_class = "HOSTPENDING";
		status_bg_class = (odd) ? "Even" : "Odd";
		}
	else if(hststatus->status == SD_HOST_UP) {
		strncpy(status, "运行", sizeof(status));
		status_class = "HOSTUP";
		status_bg_class = (odd) ? "Even" : "Odd";
		}
	else if(hststatus->status == SD_HOST_DOWN) {
		strncpy(status, "宕机", sizeof(status));
		status_class = "HOStdOWN";
		status_bg_class = "HOStdOWN";
		}
	else if(hststatus->status == SD_HOST_UNREACHABLE) {
		strncpy(status, "不可达", sizeof(status));
		status_class = "HOSTUNREACHABLE";
		status_bg_class = "HOSTUNREACHABLE";
		}

	status[sizeof(status) - 1] = '\x0';

	printf("<tr class='status%s'>\n", status_bg_class);

	printf("<td class='status%s'>\n", status_bg_class);

	printf("<table border=0 WIDTH=100%% cellpadding=0 cellspacing=0>\n");
	printf("<tr class='status%s'>\n", status_bg_class);
	printf("<td class='status%s'><a href='%s?host=%s&style=detail' title='%s'>%s</a></td>\n", status_bg_class, STATUS_CGI, url_encode(hststatus->host_name), temp_host->address, hststatus->host_name);

	if(temp_host->icon_image != NULL) {
		printf("<td class='status%s' WIDTH=5></td>\n", status_bg_class);
		printf("<td class='status%s' ALIGN=right>", status_bg_class);
		printf("<a href='%s?type=%d&host=%s'>", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(hststatus->host_name));
		printf("<IMG SRC='%s", url_logo_images_path);
		process_macros_r(mac, temp_host->icon_image, &processed_string, 0);
		printf("%s", processed_string);
		free(processed_string);
		printf("' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, (temp_host->icon_image_alt == NULL) ? "" : temp_host->icon_image_alt, (temp_host->icon_image_alt == NULL) ? "" : temp_host->icon_image_alt);
		printf("</a>");
		printf("</td>\n");
		}
	printf("</tr>\n");
	printf("</table>\n");
	printf("</td>\n");

	printf("<td class='status%s'>%s</td>\n", status_class, status);

	printf("<td class='status%s'>\n", status_bg_class);
	show_servicegroup_hostgroup_member_service_status_totals(hststatus->host_name, data);
	printf("</td>\n");

	printf("<td valign=center class='status%s'>", status_bg_class);
	printf("<a href='%s?type=%d&host=%s'><img src='%s%s' border=0 alt='查看该主机的扩展信息' title='查看该主机的扩展信息'></a>\n", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(hststatus->host_name), url_images_path, DETAIL_ICON);
	if(temp_host->notes_url != NULL) {
		printf("<a href='");
		process_macros_r(mac, temp_host->notes_url, &processed_string, 0);
		printf("%s", processed_string);
		free(processed_string);
		printf("' TARGET='%s'>", (notes_url_target == NULL) ? "_blank" : notes_url_target);
		printf("<IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", url_images_path, NOTES_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "查看主机备注信息", "查看主机备注信息");
		printf("</a>");
		}
	if(temp_host->action_url != NULL) {
		printf("<a href='");
		process_macros_r(mac, temp_host->action_url, &processed_string, 0);
		printf("%s", processed_string);
		free(processed_string);
		printf("' TARGET='%s'>", (action_url_target == NULL) ? "_blank" : action_url_target);
		printf("<IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", url_images_path, ACTION_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "执行主机扩展动作", "执行主机扩展动作");
		printf("</a>");
		}
	printf("<a href='%s?host=%s'><img src='%s%s' border=0 alt='该主机的详细服务状态' title='该主机的详细服务状态'></a>\n", STATUS_CGI, url_encode(hststatus->host_name), url_images_path, STATUS_DETAIL_ICON);
#ifdef USE_STATUSMAP
	printf("<a href='%s?host=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'></a>", STATUSMAP_CGI, url_encode(hststatus->host_name), url_images_path, STATUSMAP_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "在拓扑图上定位主机", "在拓扑图上定位主机");
#endif
	printf("</td>");

	printf("</tr>\n");

	return;
	}



void show_servicegroup_hostgroup_member_service_status_totals(char *hst_name, void *data) {
	int total_ok = 0;
	int total_warning = 0;
	int total_unknown = 0;
	int total_critical = 0;
	int total_pending = 0;
	servicestatus *temp_servicestatus;
	service *temp_service;
	servicegroup *temp_servicegroup = NULL;
	char temp_buffer[MAX_INPUT_BUFFER];


	if(display_type == DISPLAY_SERVICEGROUPS)
		temp_servicegroup = (servicegroup *)data;

	/* check all services... */
	for(temp_servicestatus = servicestatus_list; temp_servicestatus != NULL; temp_servicestatus = temp_servicestatus->next) {

		if(!strcmp(hst_name, temp_servicestatus->host_name)) {

			/* make sure the user is authorized to see this service... */
			temp_service = find_service(temp_servicestatus->host_name, temp_servicestatus->description);
			if(is_authorized_for_service(temp_service, &current_authdata) == FALSE)
				continue;

			if(display_type == DISPLAY_SERVICEGROUPS) {

				/* is this service a member of the servicegroup? */
				if(is_service_member_of_servicegroup(temp_servicegroup, temp_service) == FALSE)
					continue;
				}

			/* make sure we only display services of the specified status levels */
			if(!(service_status_types & temp_servicestatus->status))
				continue;

			/* make sure we only display services that have the desired properties */
			if(passes_service_properties_filter(temp_servicestatus) == FALSE)
				continue;

			if(temp_servicestatus->status == SERVICE_CRITICAL)
				total_critical++;
			else if(temp_servicestatus->status == SERVICE_WARNING)
				total_warning++;
			else if(temp_servicestatus->status == SERVICE_UNKNOWN)
				total_unknown++;
			else if(temp_servicestatus->status == SERVICE_OK)
				total_ok++;
			else if(temp_servicestatus->status == SERVICE_PENDING)
				total_pending++;
			else
				total_ok++;
			}
		}


	printf("<table border=0 WIDTH=100%%>\n");

	if(display_type == DISPLAY_SERVICEGROUPS)
		snprintf(temp_buffer, sizeof(temp_buffer) - 1, "servicegroup=%s&style=detail", url_encode(temp_servicegroup->group_name));
	else
		snprintf(temp_buffer, sizeof(temp_buffer) - 1, "host=%s", url_encode(hst_name));
	temp_buffer[sizeof(temp_buffer) - 1] = '\x0';

	if(total_ok > 0)
		printf("<tr><td class='miniStatusOK'><a href='%s?%s&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>正常状态 %d 个<BR>(OK)</a></td></tr>\n", STATUS_CGI, temp_buffer, SERVICE_OK, host_status_types, service_properties, host_properties, total_ok);
	if(total_warning > 0)
		printf("<tr><td class='miniStatusWARNING'><a href='%s?%s&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>警告状态 %d 个<BR>(WARNING)</a></td></tr>\n", STATUS_CGI, temp_buffer, SERVICE_WARNING, host_status_types, service_properties, host_properties, total_warning);
	if(total_unknown > 0)
		printf("<tr><td class='miniStatusUNKNOWN'><a href='%s?%s&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>未知状态 %d 个<BR>(UNKNOWN)</a></td></tr>\n", STATUS_CGI, temp_buffer, SERVICE_UNKNOWN, host_status_types, service_properties, host_properties, total_unknown);
	if(total_critical > 0)
		printf("<tr><td class='miniStatusCRITICAL'><a href='%s?%s&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>紧急状态 %d 个<BR>(CRITICAL)</a></td></tr>\n", STATUS_CGI, temp_buffer, SERVICE_CRITICAL, host_status_types, service_properties, host_properties, total_critical);
	if(total_pending > 0)
		printf("<tr><td class='miniStatusPENDING'><a href='%s?%s&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>状态保持 %d 个<BR>(PENDING)</a></td></tr>\n", STATUS_CGI, temp_buffer, SERVICE_PENDING, host_status_types, service_properties, host_properties, total_pending);

	printf("</table>\n");

	if((total_ok + total_warning + total_unknown + total_critical + total_pending) == 0)
		printf("无符合条件的服务");

	return;
	}



/* show a summary of hostgroup(s)... */
void show_hostgroup_summaries(void) {
	hostgroup *temp_hostgroup = NULL;
	int user_has_seen_something = FALSE;
	int hostgroup_error = FALSE;
	int odd = 0;


	printf("<P>\n");

	printf("<table border=0 width=100%%>\n");
	printf("<tr>\n");

	printf("<td valign=top align=left width=33%%>\n");

	show_filters();

	printf("</td>");

	printf("<td valign=top align=center width=33%%>\n");

	printf("<div align='center' class='statusTitle'>");
	if(show_all_hostgroups == TRUE)
		printf("所有主机组");
	else
		printf("主机组 '%s' ", hostgroup_name);
	printf("的状态汇总</div>\n");

	printf("<br>");

	printf("</td>\n");

	printf("<td valign=top align=right width=33%%></td>\n");

	printf("</tr>\n");
	printf("</table>\n");

	printf("</P>\n");


	printf("<div ALIGN=center>\n");
	printf("<table class='status'>\n");

	printf("<tr>\n");
	printf("<th class='status'>主机组</th><th class='status'>主机状态汇总</th><th class='status'>服务状态汇总</th>\n");
	printf("</tr>\n");

	/* display status summary for all hostgroups */
	if(show_all_hostgroups == TRUE) {

		/* loop through all hostgroups... */
		for(temp_hostgroup = hostgroup_list; temp_hostgroup != NULL; temp_hostgroup = temp_hostgroup->next) {

			/* make sure the user is authorized to view this hostgroup */
			if(is_authorized_for_hostgroup(temp_hostgroup, &current_authdata) == FALSE)
				continue;

			if(odd == 0)
				odd = 1;
			else
				odd = 0;

			/* show summary for this hostgroup */
			show_hostgroup_summary(temp_hostgroup, odd);

			user_has_seen_something = TRUE;
			}

		}

	/* else just show summary for a specific hostgroup */
	else {
		temp_hostgroup = find_hostgroup(hostgroup_name);
		if(temp_hostgroup == NULL)
			hostgroup_error = TRUE;
		else {
			show_hostgroup_summary(temp_hostgroup, 1);
			user_has_seen_something = TRUE;
			}
		}

	printf("</table>\n");
	printf("</div>\n");

	/* if user couldn't see anything, print out some helpful info... */
	if(user_has_seen_something == FALSE && hostgroup_error == FALSE) {

		printf("<P><div align='center'>\n");

		if(hoststatus_list != NULL) {
			printf("<div class='errorMessage'>看起来像是你没有权限查看你请求主机的任何信息...</div>\n");
			printf("<div class='errorDescription'>如果你认为这是一个错误，请检查HTTP服务器访问CGI的身份验证要求<br>");
			printf("并在你的CGI的配置文件中检查授权选项。</div>\n");
			}
		else {
			printf("<div class='infoMessage'>日志中不存在任何主机状态信息...<br><br>\n");
			printf("请确保Nagios程序正常运行，并且配置文件中状态日志设置正确。</div>\n");
			}

		printf("</div></P>\n");
		}

	/* we couldn't find the hostgroup */
	else if(hostgroup_error == TRUE) {
		printf("<P><div align='center'>\n");
		printf("<div class='errorMessage'>抱歉，主机组'%s'似乎不存在...</div>\n", hostgroup_name);
		printf("</div></P>\n");
		}

	return;
	}



/* displays status summary information for a specific hostgroup */
void show_hostgroup_summary(hostgroup *temp_hostgroup, int odd) {
	const char *status_bg_class = "";

	if(odd == 1)
		status_bg_class = "Even";
	else
		status_bg_class = "Odd";

	printf("<tr class='status%s'><td class='status%s'>\n", status_bg_class, status_bg_class);
	printf("<a href='%s?hostgroup=%s&style=overview'>%s</a> ", STATUS_CGI, url_encode(temp_hostgroup->group_name), temp_hostgroup->alias);
	printf("(<a href='%s?type=%d&hostgroup=%s'>%s</a>)", EXTINFO_CGI, DISPLAY_HOSTGROUP_INFO, url_encode(temp_hostgroup->group_name), temp_hostgroup->group_name);
	printf("</td>");

	printf("<td class='status%s' align='center' Valign='center'>", status_bg_class);
	show_hostgroup_host_totals_summary(temp_hostgroup);
	printf("</td>");

	printf("<td class='status%s' align='center' Valign='center'>", status_bg_class);
	show_hostgroup_service_totals_summary(temp_hostgroup);
	printf("</td>");

	printf("</tr>\n");

	return;
	}



/* shows host total summary information for a specific hostgroup */
void show_hostgroup_host_totals_summary(hostgroup *temp_hostgroup) {
	hostsmember *temp_member;
	int hosts_up = 0;
	int hosts_down = 0;
	int hosts_unreachable = 0;
	int hosts_pending = 0;
	int hosts_down_scheduled = 0;
	int hosts_down_acknowledged = 0;
	int hosts_down_disabled = 0;
	int hosts_down_unacknowledged = 0;
	int hosts_unreachable_scheduled = 0;
	int hosts_unreachable_acknowledged = 0;
	int hosts_unreachable_disabled = 0;
	int hosts_unreachable_unacknowledged = 0;
	hoststatus *temp_hoststatus;
	host *temp_host;
	int problem = FALSE;

	/* find all the hosts that belong to the hostgroup */
	for(temp_member = temp_hostgroup->members; temp_member != NULL; temp_member = temp_member->next) {

		/* find the host... */
		temp_host = find_host(temp_member->host_name);
		if(temp_host == NULL)
			continue;

		/* find the host status */
		temp_hoststatus = find_hoststatus(temp_host->name);
		if(temp_hoststatus == NULL)
			continue;

		/* make sure we only display hosts of the specified status levels */
		if(!(host_status_types & temp_hoststatus->status))
			continue;

		/* make sure we only display hosts that have the desired properties */
		if(passes_host_properties_filter(temp_hoststatus) == FALSE)
			continue;

		problem = TRUE;

		if(temp_hoststatus->status == SD_HOST_UP)
			hosts_up++;

		else if(temp_hoststatus->status == SD_HOST_DOWN) {
			if(temp_hoststatus->scheduled_downtime_depth > 0) {
				hosts_down_scheduled++;
				problem = FALSE;
				}
			if(temp_hoststatus->problem_has_been_acknowledged == TRUE) {
				hosts_down_acknowledged++;
				problem = FALSE;
				}
			if(temp_hoststatus->checks_enabled == FALSE) {
				hosts_down_disabled++;
				problem = FALSE;
				}
			if(problem == TRUE)
				hosts_down_unacknowledged++;
			hosts_down++;
			}

		else if(temp_hoststatus->status == SD_HOST_UNREACHABLE) {
			if(temp_hoststatus->scheduled_downtime_depth > 0) {
				hosts_unreachable_scheduled++;
				problem = FALSE;
				}
			if(temp_hoststatus->problem_has_been_acknowledged == TRUE) {
				hosts_unreachable_acknowledged++;
				problem = FALSE;
				}
			if(temp_hoststatus->checks_enabled == FALSE) {
				hosts_unreachable_disabled++;
				problem = FALSE;
				}
			if(problem == TRUE)
				hosts_unreachable_unacknowledged++;
			hosts_unreachable++;
			}

		else
			hosts_pending++;
		}

	printf("<table border='0'>\n");

	if(hosts_up > 0) {
		printf("<tr>");
		printf("<td class='miniStatusUP'><a href='%s?hostgroup=%s&style=hostdetail&&hoststatustypes=%d&hostprops=%lu'>运行状态 %d 个<BR>(UP)</a></td>", STATUS_CGI, url_encode(temp_hostgroup->group_name), SD_HOST_UP, host_properties, hosts_up);
		printf("</tr>\n");
		}

	if(hosts_down > 0) {
		printf("<tr>\n");
		printf("<td class='miniStatusDOWN'><table border='0'>\n");
		printf("<tr>\n");

		printf("<td class='miniStatusDOWN'><a href='%s?hostgroup=%s&style=hostdetail&hoststatustypes=%d&hostprops=%lu'>宕机状态 %d 个<BR>(DOWN)</a>&nbsp;:</td>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SD_HOST_DOWN, host_properties, hosts_down);

		printf("<td><table border='0'>\n");

		if(hosts_down_unacknowledged > 0)
			printf("<tr><td width=100%% class='hostImportantProblem'><a href='%s?hostgroup=%s&style=hostdetail&hoststatustypes=%d&hostprops=%d'>尚未解决 %d 个<BR>(Unhandled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SD_HOST_DOWN, HOST_NO_SCHEDULED_DOWNTIME | HOST_STATE_UNACKNOWLEDGED | HOST_CHECKS_ENABLED, hosts_down_unacknowledged);

		if(hosts_down_scheduled > 0)
			printf("<tr><td width=100%% class='hostUnimportantProblem'><a href='%s?hostgroup=%s&style=hostdetail&hoststatustypes=%d&hostprops=%d'>计划内非重要故障 %d 个<BR>(Scheduled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SD_HOST_DOWN, HOST_SCHEDULED_DOWNTIME, hosts_down_scheduled);

		if(hosts_down_acknowledged > 0)
			printf("<tr><td width=100%% class='hostUnimportantProblem'><a href='%s?hostgroup=%s&style=hostdetail&hoststatustypes=%d&hostprops=%d'>已确认非重要故障 %d 个<BR>(Acknowledged)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SD_HOST_DOWN, HOST_STATE_ACKNOWLEDGED, hosts_down_acknowledged);

		if(hosts_down_disabled > 0)
			printf("<tr><td width=100%% class='hostUnimportantProblem'><a href='%s?hostgroup=%s&style=hostdetail&hoststatustypes=%d&hostprops=%d'>关闭的非重要故障 %d 个<BR>(Disabled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SD_HOST_DOWN, HOST_CHECKS_DISABLED, hosts_down_disabled);

		printf("</table></td>\n");

		printf("</tr>\n");
		printf("</table></td>\n");
		printf("</tr>\n");
		}

	if(hosts_unreachable > 0) {
		printf("<tr>\n");
		printf("<td class='miniStatusUNREACHABLE'><table border='0'>\n");
		printf("<tr>\n");

		printf("<td class='miniStatusUNREACHABLE'><a href='%s?hostgroup=%s&style=hostdetail&hoststatustypes=%d&hostprops=%lu'>状态不可达 %d 个<BR>(UNREACHABLE)</a>&nbsp;:</td>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SD_HOST_UNREACHABLE, host_properties, hosts_unreachable);

		printf("<td><table border='0'>\n");

		if(hosts_unreachable_unacknowledged > 0)
			printf("<tr><td width=100%% class='hostImportantProblem'><a href='%s?hostgroup=%s&style=hostdetail&hoststatustypes=%d&hostprops=%d'>尚未解决 %d 个<BR>(Unhandled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SD_HOST_UNREACHABLE, HOST_NO_SCHEDULED_DOWNTIME | HOST_STATE_UNACKNOWLEDGED | HOST_CHECKS_ENABLED, hosts_unreachable_unacknowledged);

		if(hosts_unreachable_scheduled > 0)
			printf("<tr><td width=100%% class='hostUnimportantProblem'><a href='%s?hostgroup=%s&style=hostdetail&hoststatustypes=%d&hostprops=%d'>计划内非重要故障 %d 个<BR>(Scheduled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SD_HOST_UNREACHABLE, HOST_SCHEDULED_DOWNTIME, hosts_unreachable_scheduled);

		if(hosts_unreachable_acknowledged > 0)
			printf("<tr><td width=100%% class='hostUnimportantProblem'><a href='%s?hostgroup=%s&style=hostdetail&hoststatustypes=%d&hostprops=%d'>已确认非重要故障 %d 个<BR>(Acknowledged)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SD_HOST_UNREACHABLE, HOST_STATE_ACKNOWLEDGED, hosts_unreachable_acknowledged);

		if(hosts_unreachable_disabled > 0)
			printf("<tr><td width=100%% class='hostUnimportantProblem'><a href='%s?hostgroup=%s&style=hostdetail&hoststatustypes=%d&hostprops=%d'>关闭的非重要故障 %d 个<BR>(Disabled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SD_HOST_UNREACHABLE, HOST_CHECKS_DISABLED, hosts_unreachable_disabled);

		printf("</table></td>\n");

		printf("</tr>\n");
		printf("</table></td>\n");
		printf("</tr>\n");
		}

	if(hosts_pending > 0)
		printf("<tr><td class='miniStatusPENDING'><a href='%s?hostgroup=%s&style=hostdetail&hoststatustypes=%d&hostprops=%lu'>状态保持 %d 个<BR>(PENDING)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), HOST_PENDING, host_properties, hosts_pending);

	printf("</table>\n");

	if((hosts_up + hosts_down + hosts_unreachable + hosts_pending) == 0)
		printf("无符合条件的主机");

	return;
	}



/* shows service total summary information for a specific hostgroup */
void show_hostgroup_service_totals_summary(hostgroup *temp_hostgroup) {
	int services_ok = 0;
	int services_warning = 0;
	int services_unknown = 0;
	int services_critical = 0;
	int services_pending = 0;
	int services_warning_host_problem = 0;
	int services_warning_scheduled = 0;
	int services_warning_acknowledged = 0;
	int services_warning_disabled = 0;
	int services_warning_unacknowledged = 0;
	int services_unknown_host_problem = 0;
	int services_unknown_scheduled = 0;
	int services_unknown_acknowledged = 0;
	int services_unknown_disabled = 0;
	int services_unknown_unacknowledged = 0;
	int services_critical_host_problem = 0;
	int services_critical_scheduled = 0;
	int services_critical_acknowledged = 0;
	int services_critical_disabled = 0;
	int services_critical_unacknowledged = 0;
	servicestatus *temp_servicestatus = NULL;
	hoststatus *temp_hoststatus = NULL;
	host *temp_host = NULL;
	int problem = FALSE;


	/* check all services... */
	for(temp_servicestatus = servicestatus_list; temp_servicestatus != NULL; temp_servicestatus = temp_servicestatus->next) {

		/* find the host this service is associated with */
		temp_host = find_host(temp_servicestatus->host_name);
		if(temp_host == NULL)
			continue;

		/* see if this service is associated with a host in the specified hostgroup */
		if(is_host_member_of_hostgroup(temp_hostgroup, temp_host) == FALSE)
			continue;

		/* find the status of the associated host */
		temp_hoststatus = find_hoststatus(temp_servicestatus->host_name);
		if(temp_hoststatus == NULL)
			continue;

		/* find the status of the associated host */
		temp_hoststatus = find_hoststatus(temp_servicestatus->host_name);
		if(temp_hoststatus == NULL)
			continue;

		/* make sure we only display hosts of the specified status levels */
		if(!(host_status_types & temp_hoststatus->status))
			continue;

		/* make sure we only display hosts that have the desired properties */
		if(passes_host_properties_filter(temp_hoststatus) == FALSE)
			continue;

		/* make sure we only display services of the specified status levels */
		if(!(service_status_types & temp_servicestatus->status))
			continue;

		/* make sure we only display services that have the desired properties */
		if(passes_service_properties_filter(temp_servicestatus) == FALSE)
			continue;

		problem = TRUE;

		if(temp_servicestatus->status == SERVICE_OK)
			services_ok++;

		else if(temp_servicestatus->status == SERVICE_WARNING) {
			temp_hoststatus = find_hoststatus(temp_servicestatus->host_name);
			if(temp_hoststatus != NULL && (temp_hoststatus->status == SD_HOST_DOWN || temp_hoststatus->status == SD_HOST_UNREACHABLE)) {
				services_warning_host_problem++;
				problem = FALSE;
				}
			if(temp_servicestatus->scheduled_downtime_depth > 0) {
				services_warning_scheduled++;
				problem = FALSE;
				}
			if(temp_servicestatus->problem_has_been_acknowledged == TRUE) {
				services_warning_acknowledged++;
				problem = FALSE;
				}
			if(temp_servicestatus->checks_enabled == FALSE) {
				services_warning_disabled++;
				problem = FALSE;
				}
			if(problem == TRUE)
				services_warning_unacknowledged++;
			services_warning++;
			}

		else if(temp_servicestatus->status == SERVICE_UNKNOWN) {
			temp_hoststatus = find_hoststatus(temp_servicestatus->host_name);
			if(temp_hoststatus != NULL && (temp_hoststatus->status == SD_HOST_DOWN || temp_hoststatus->status == SD_HOST_UNREACHABLE)) {
				services_unknown_host_problem++;
				problem = FALSE;
				}
			if(temp_servicestatus->scheduled_downtime_depth > 0) {
				services_unknown_scheduled++;
				problem = FALSE;
				}
			if(temp_servicestatus->problem_has_been_acknowledged == TRUE) {
				services_unknown_acknowledged++;
				problem = FALSE;
				}
			if(temp_servicestatus->checks_enabled == FALSE) {
				services_unknown_disabled++;
				problem = FALSE;
				}
			if(problem == TRUE)
				services_unknown_unacknowledged++;
			services_unknown++;
			}

		else if(temp_servicestatus->status == SERVICE_CRITICAL) {
			temp_hoststatus = find_hoststatus(temp_servicestatus->host_name);
			if(temp_hoststatus != NULL && (temp_hoststatus->status == SD_HOST_DOWN || temp_hoststatus->status == SD_HOST_UNREACHABLE)) {
				services_critical_host_problem++;
				problem = FALSE;
				}
			if(temp_servicestatus->scheduled_downtime_depth > 0) {
				services_critical_scheduled++;
				problem = FALSE;
				}
			if(temp_servicestatus->problem_has_been_acknowledged == TRUE) {
				services_critical_acknowledged++;
				problem = FALSE;
				}
			if(temp_servicestatus->checks_enabled == FALSE) {
				services_critical_disabled++;
				problem = FALSE;
				}
			if(problem == TRUE)
				services_critical_unacknowledged++;
			services_critical++;
			}

		else if(temp_servicestatus->status == SERVICE_PENDING)
			services_pending++;
		}


	printf("<table border=0>\n");

	if(services_ok > 0)
		printf("<tr><td class='miniStatusOK'><a href='%s?hostgroup=%s&style=detail&&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>正常状态 %d 个<BR>(OK)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_OK, host_status_types, service_properties, host_properties, services_ok);

	if(services_warning > 0) {
		printf("<tr>\n");
		printf("<td class='miniStatusWARNING'><table border='0'>\n");
		printf("<tr>\n");

		printf("<td class='miniStatusWARNING'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>警告状态 %d 个<BR>(WARNING)</a>&nbsp;:</td>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_WARNING, host_status_types, service_properties, host_properties, services_warning);

		printf("<td><table border='0'>\n");

		if(services_warning_unacknowledged > 0)
			printf("<tr><td width=100%% class='serviceImportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%d'>尚未解决 %d 个<BR>(Unhandled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_WARNING, SD_HOST_UP | HOST_PENDING, SERVICE_NO_SCHEDULED_DOWNTIME | SERVICE_STATE_UNACKNOWLEDGED | SERVICE_CHECKS_ENABLED, services_warning_unacknowledged);

		if(services_warning_host_problem > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d'>在主机上的问题 %d 个</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_WARNING, SD_HOST_DOWN | SD_HOST_UNREACHABLE, services_warning_host_problem);

		if(services_warning_scheduled > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>计划内非重要故障 %d 个<BR>(Scheduled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_WARNING, SERVICE_SCHEDULED_DOWNTIME, services_warning_scheduled);

		if(services_warning_acknowledged > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>已确认非重要故障 %d 个<BR>(Acknowledged)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_WARNING, SERVICE_STATE_ACKNOWLEDGED, services_warning_acknowledged);

		if(services_warning_disabled > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>关闭的非重要故障 %d 个<BR>(Disabled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_WARNING, SERVICE_CHECKS_DISABLED, services_warning_disabled);

		printf("</table></td>\n");

		printf("</tr>\n");
		printf("</table></td>\n");
		printf("</tr>\n");
		}

	if(services_unknown > 0) {
		printf("<tr>\n");
		printf("<td class='miniStatusUNKNOWN'><table border='0'>\n");
		printf("<tr>\n");

		printf("<td class='miniStatusUNKNOWN'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>未知状态 %d 个<BR>(UNKNOWN)</a>&nbsp;:</td>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_UNKNOWN, host_status_types, service_properties, host_properties, services_unknown);

		printf("<td><table border='0'>\n");

		if(services_unknown_unacknowledged > 0)
			printf("<tr><td width=100%% class='serviceImportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%d'>尚未解决 %d 个<BR>(Unhandled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_UNKNOWN, SD_HOST_UP | HOST_PENDING, SERVICE_NO_SCHEDULED_DOWNTIME | SERVICE_STATE_UNACKNOWLEDGED | SERVICE_CHECKS_ENABLED, services_unknown_unacknowledged);

		if(services_unknown_host_problem > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d'>在主机上的问题 %d 个</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_UNKNOWN, SD_HOST_DOWN | SD_HOST_UNREACHABLE, services_unknown_host_problem);

		if(services_unknown_scheduled > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>计划内非重要故障 %d 个<BR>(Scheduled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_UNKNOWN, SERVICE_SCHEDULED_DOWNTIME, services_unknown_scheduled);

		if(services_unknown_acknowledged > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>已确认非重要故障 %d 个<BR>(Acknowledged)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_UNKNOWN, SERVICE_STATE_ACKNOWLEDGED, services_unknown_acknowledged);

		if(services_unknown_disabled > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>关闭的非重要故障 %d 个<BR>(Disabled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_UNKNOWN, SERVICE_CHECKS_DISABLED, services_unknown_disabled);

		printf("</table></td>\n");

		printf("</tr>\n");
		printf("</table></td>\n");
		printf("</tr>\n");
		}

	if(services_critical > 0) {
		printf("<tr>\n");
		printf("<td class='miniStatusCRITICAL'><table border='0'>\n");
		printf("<tr>\n");

		printf("<td class='miniStatusCRITICAL'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>紧急状态 %d 个<BR>(CRITICAL)</a>&nbsp;:</td>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_CRITICAL, host_status_types, service_properties, host_properties, services_critical);

		printf("<td><table border='0'>\n");

		if(services_critical_unacknowledged > 0)
			printf("<tr><td width=100%% class='serviceImportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%d'>尚未解决 %d 个<BR>(Unhandled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_CRITICAL, SD_HOST_UP | HOST_PENDING, SERVICE_NO_SCHEDULED_DOWNTIME | SERVICE_STATE_UNACKNOWLEDGED | SERVICE_CHECKS_ENABLED, services_critical_unacknowledged);

		if(services_critical_host_problem > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d'>在主机上的问题 %d 个</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_CRITICAL, SD_HOST_DOWN | SD_HOST_UNREACHABLE, services_critical_host_problem);

		if(services_critical_scheduled > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>计划内非重要故障 %d 个<BR>(Scheduled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_CRITICAL, SERVICE_SCHEDULED_DOWNTIME, services_critical_scheduled);

		if(services_critical_acknowledged > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>已确认非重要故障 %d 个<BR>(Acknowledged)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_CRITICAL, SERVICE_STATE_ACKNOWLEDGED, services_critical_acknowledged);

		if(services_critical_disabled > 0)
			printf("<tr><td width=100%% class='serviceUnimportantProblem'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&serviceprops=%d'>关闭的非重要故障 %d 个<BR>(Disabled)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_CRITICAL, SERVICE_CHECKS_DISABLED, services_critical_disabled);

		printf("</table></td>\n");

		printf("</tr>\n");
		printf("</table></td>\n");
		printf("</tr>\n");
		}

	if(services_pending > 0)
		printf("<tr><td class='miniStatusPENDING'><a href='%s?hostgroup=%s&style=detail&servicestatustypes=%d&hoststatustypes=%d&serviceprops=%lu&hostprops=%lu'>状态保持 %d 个<BR>(PENDING)</a></td></tr>\n", STATUS_CGI, url_encode(temp_hostgroup->group_name), SERVICE_PENDING, host_status_types, service_properties, host_properties, services_pending);

	printf("</table>\n");

	if((services_ok + services_warning + services_unknown + services_critical + services_pending) == 0)
		printf("无符合条件的服务");

	return;
	}



/* show a grid layout of hostgroup(s)... */
void show_hostgroup_grids(void) {
	hostgroup *temp_hostgroup = NULL;
	int user_has_seen_something = FALSE;
	int hostgroup_error = FALSE;
	int odd = 0;


	printf("<P>\n");

	printf("<table border=0 width=100%%>\n");
	printf("<tr>\n");

	printf("<td valign=top align=left width=33%%>\n");

	show_filters();

	printf("</td>");

	printf("<td valign=top align=center width=33%%>\n");

	printf("<div align='center' class='statusTitle'>");
	if(show_all_hostgroups == TRUE)
		printf("所有主机组");
	else
		printf("主机组 '%s' ", hostgroup_name);
	printf("的状态表格</div>\n");

	printf("<br>");

	printf("</td>\n");

	printf("<td valign=top align=right width=33%%></td>\n");

	printf("</tr>\n");
	printf("</table>\n");

	printf("</P>\n");


	/* display status grids for all hostgroups */
	if(show_all_hostgroups == TRUE) {

		/* loop through all hostgroups... */
		for(temp_hostgroup = hostgroup_list; temp_hostgroup != NULL; temp_hostgroup = temp_hostgroup->next) {

			/* make sure the user is authorized to view this hostgroup */
			if(is_authorized_for_hostgroup(temp_hostgroup, &current_authdata) == FALSE)
				continue;

			if(odd == 0)
				odd = 1;
			else
				odd = 0;

			/* show grid for this hostgroup */
			show_hostgroup_grid(temp_hostgroup);

			user_has_seen_something = TRUE;
			}

		}

	/* else just show grid for a specific hostgroup */
	else {
		temp_hostgroup = find_hostgroup(hostgroup_name);
		if(temp_hostgroup == NULL)
			hostgroup_error = TRUE;
		else {
			show_hostgroup_grid(temp_hostgroup);
			user_has_seen_something = TRUE;
			}
		}

	/* if user couldn't see anything, print out some helpful info... */
	if(user_has_seen_something == FALSE && hostgroup_error == FALSE) {

		printf("<P><div align='center'>\n");

		if(hoststatus_list != NULL) {
			printf("<div class='errorMessage'>看起来像是你没有权限查看你请求主机的任何信息...</div>\n");
			printf("<div class='errorDescription'>如果你认为这是一个错误，请检查HTTP服务器访问CGI的身份验证要求<br>");
			printf("并在你的CGI的配置文件中检查授权选项。</div>\n");
			}
		else {
			printf("<div class='infoMessage'>日志中不存在任何主机状态信息...<br><br>\n");
			printf("请确保Nagios程序正常运行，并且配置文件中状态日志设置正确。</div>\n");
			}

		printf("</div></P>\n");
		}

	/* we couldn't find the hostgroup */
	else if(hostgroup_error == TRUE) {
		printf("<P><div align='center'>\n");
		printf("<div class='errorMessage'>抱歉，主机组'%s'似乎不存在...</div>\n", hostgroup_name);
		printf("</div></P>\n");
		}

	return;
	}


/* displays status grid for a specific hostgroup */
void show_hostgroup_grid(hostgroup *temp_hostgroup) {
	hostsmember *temp_member;
	const char *status_bg_class = "";
	const char *host_status_class = "";
	const char *service_status_class = "";
	host *temp_host;
	service *temp_service;
	hoststatus *temp_hoststatus;
	servicestatus *temp_servicestatus;
	char *processed_string = NULL;
	int odd = 0;
	int current_item;


	printf("<P>\n");
	printf("<div align='center'>\n");

	printf("<div class='status'><a href='%s?hostgroup=%s&style=detail'>%s</a>", STATUS_CGI, url_encode(temp_hostgroup->group_name), temp_hostgroup->alias);
	printf(" (<a href='%s?type=%d&hostgroup=%s'>%s</a>)</div>", EXTINFO_CGI, DISPLAY_HOSTGROUP_INFO, url_encode(temp_hostgroup->group_name), temp_hostgroup->group_name);

	printf("<table class='status' align='center'>\n");
	printf("<tr><th class='status'>主机</th><th class='status'>服务</a></th><th class='status'>动作</th></tr>\n");

	/* find all the hosts that belong to the hostgroup */
	for(temp_member = temp_hostgroup->members; temp_member != NULL; temp_member = temp_member->next) {

		/* find the host... */
		temp_host = find_host(temp_member->host_name);
		if(temp_host == NULL)
			continue;

		/* grab macros */
		grab_host_macros_r(mac, temp_host);

		/* find the host status */
		temp_hoststatus = find_hoststatus(temp_host->name);
		if(temp_hoststatus == NULL)
			continue;

		if(odd == 1) {
			status_bg_class = "Even";
			odd = 0;
			}
		else {
			status_bg_class = "Odd";
			odd = 1;
			}

		printf("<tr class='status%s'>\n", status_bg_class);

		/* get the status of the host */
		if(temp_hoststatus->status == SD_HOST_DOWN)
			host_status_class = "HOStdOWN";
		else if(temp_hoststatus->status == SD_HOST_UNREACHABLE)
			host_status_class = "HOSTUNREACHABLE";
		else
			host_status_class = status_bg_class;

		printf("<td class='status%s'>", host_status_class);

		printf("<table border=0 WIDTH='100%%' cellpadding=0 cellspacing=0>\n");
		printf("<tr>\n");
		printf("<td align='left'>\n");
		printf("<table border=0 cellpadding=0 cellspacing=0>\n");
		printf("<tr>\n");
		printf("<td align=left valign=center class='status%s'>", host_status_class);
		printf("<a href='%s?type=%d&host=%s'>%s</a>\n", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_host->name), temp_host->name);
		printf("</td>\n");
		printf("</tr>\n");
		printf("</table>\n");
		printf("</td>\n");
		printf("<td align=right valign=center nowrap>\n");
		printf("<table border=0 cellpadding=0 cellspacing=0>\n");
		printf("<tr>\n");

		if(temp_host->icon_image != NULL) {
			printf("<td align=center valign=center>");
			printf("<a href='%s?type=%d&host=%s'>\n", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_host->name));
			printf("<IMG SRC='%s", url_logo_images_path);
			process_macros_r(mac, temp_host->icon_image, &processed_string, 0);
			printf("%s", processed_string);
			free(processed_string);
			printf("' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, (temp_host->icon_image_alt == NULL) ? "" : temp_host->icon_image_alt, (temp_host->icon_image_alt == NULL) ? "" : temp_host->icon_image_alt);
			printf("</a>");
			printf("<td>\n");
			}
		printf("<td>\n");

		printf("</tr>\n");
		printf("</table>\n");
		printf("</td>\n");
		printf("</tr>\n");
		printf("</table>\n");

		printf("</td>\n");

		printf("<td class='status%s'>", host_status_class);

		/* display all services on the host */
		current_item = 1;
		for(temp_service = service_list; temp_service; temp_service = temp_service->next) {

			/* skip this service if it's not associate with the host */
			if(strcmp(temp_service->host_name, temp_host->name))
				continue;

			if(current_item > max_grid_width && max_grid_width > 0) {
				printf("<BR>\n");
				current_item = 1;
				}

			/* grab macros */
			grab_service_macros_r(mac, temp_service);

			/* get the status of the service */
			temp_servicestatus = find_servicestatus(temp_service->host_name, temp_service->description);
			if(temp_servicestatus == NULL)
				service_status_class = "NULL";
			else if(temp_servicestatus->status == SERVICE_OK)
				service_status_class = "OK";
			else if(temp_servicestatus->status == SERVICE_WARNING)
				service_status_class = "WARNING";
			else if(temp_servicestatus->status == SERVICE_UNKNOWN)
				service_status_class = "UNKNOWN";
			else if(temp_servicestatus->status == SERVICE_CRITICAL)
				service_status_class = "CRITICAL";
			else
				service_status_class = "PENDING";

			printf("<a href='%s?type=%d&host=%s", EXTINFO_CGI, DISPLAY_SERVICE_INFO, url_encode(temp_servicestatus->host_name));
			printf("&service=%s' class='status%s'>%s</a>&nbsp;", url_encode(temp_servicestatus->description), service_status_class, temp_servicestatus->description);

			current_item++;
			}

		printf("</td>\n");

		/* actions */
		printf("<td class='status%s'>", host_status_class);

		printf("<a href='%s?type=%d&host=%s'>\n", EXTINFO_CGI, DISPLAY_HOST_INFO, url_encode(temp_host->name));
		printf("<IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", url_images_path, DETAIL_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "查看该主机的扩展信息", "查看该主机的扩展信息");
		printf("</a>");

		if(temp_host->notes_url != NULL) {
			printf("<a href='");
			process_macros_r(mac, temp_host->notes_url, &processed_string, 0);
			printf("%s", processed_string);
			free(processed_string);
			printf("' TARGET='%s'>", (notes_url_target == NULL) ? "_blank" : notes_url_target);
			printf("<IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", url_images_path, NOTES_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "查看主机备注信息", "查看主机备注信息");
			printf("</a>");
			}
		if(temp_host->action_url != NULL) {
			printf("<a href='");
			process_macros_r(mac, temp_host->action_url, &processed_string, 0);
			printf("%s", processed_string);
			free(processed_string);
			printf("' TARGET='%s'>", (action_url_target == NULL) ? "_blank" : action_url_target);
			printf("<IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'>", url_images_path, ACTION_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "执行主机扩展动作", "执行主机扩展动作");
			printf("</a>");
			}

		printf("<a href='%s?host=%s'><img src='%s%s' border=0 alt='该主机的详细服务状态' title='该主机的详细服务状态'></a>\n", STATUS_CGI, url_encode(temp_host->name), url_images_path, STATUS_DETAIL_ICON);
#ifdef USE_STATUSMAP
		printf("<a href='%s?host=%s'><IMG SRC='%s%s' border=0 WIDTH=%d HEIGHT=%d ALT='%s' TITLE='%s'></a>", STATUSMAP_CGI, url_encode(temp_host->name), url_images_path, STATUSMAP_ICON, STATUS_ICON_WIDTH, STATUS_ICON_HEIGHT, "在拓扑图上定位主机", "在拓扑图上定位主机");
#endif
		printf("</td>\n");

		printf("</tr>\n");
		}

	printf("</table>\n");
	printf("</div>\n");
	printf("</P>\n");

	return;
	}




/******************************************************************/
/**********  SERVICE SORTING & FILTERING FUNCTIONS  ***************/
/******************************************************************/


/* sorts the service list */
int sort_services(int s_type, int s_option) {
	servicesort *new_servicesort;
	servicesort *last_servicesort;
	servicesort *temp_servicesort;
	servicestatus *temp_svcstatus;

	if(s_type == SORT_NONE)
		return ERROR;

	if(servicestatus_list == NULL)
		return ERROR;

	/* sort all services status entries */
	for(temp_svcstatus = servicestatus_list; temp_svcstatus != NULL; temp_svcstatus = temp_svcstatus->next) {

		/* allocate memory for a new sort structure */
		new_servicesort = (servicesort *)malloc(sizeof(servicesort));
		if(new_servicesort == NULL)
			return ERROR;

		new_servicesort->svcstatus = temp_svcstatus;

		last_servicesort = servicesort_list;
		for(temp_servicesort = servicesort_list; temp_servicesort != NULL; temp_servicesort = temp_servicesort->next) {

			if(compare_servicesort_entries(s_type, s_option, new_servicesort, temp_servicesort) == TRUE) {
				new_servicesort->next = temp_servicesort;
				if(temp_servicesort == servicesort_list)
					servicesort_list = new_servicesort;
				else
					last_servicesort->next = new_servicesort;
				break;
				}
			else
				last_servicesort = temp_servicesort;
			}

		if(servicesort_list == NULL) {
			new_servicesort->next = NULL;
			servicesort_list = new_servicesort;
			}
		else if(temp_servicesort == NULL) {
			new_servicesort->next = NULL;
			last_servicesort->next = new_servicesort;
			}
		}

	return OK;
	}


int compare_servicesort_entries(int s_type, int s_option, servicesort *new_servicesort, servicesort *temp_servicesort) {
	servicestatus *new_svcstatus;
	servicestatus *temp_svcstatus;
	time_t nt;
	time_t tt;

	new_svcstatus = new_servicesort->svcstatus;
	temp_svcstatus = temp_servicesort->svcstatus;

	if(s_type == SORT_ASCENDING) {

		if(s_option == SORT_LASTCHECKTIME) {
			if(new_svcstatus->last_check < temp_svcstatus->last_check)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_CURRENTATTEMPT) {
			if(new_svcstatus->current_attempt < temp_svcstatus->current_attempt)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_SERVICESTATUS) {
			if(new_svcstatus->status <= temp_svcstatus->status)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_HOSTNAME) {
			if(strcasecmp(new_svcstatus->host_name, temp_svcstatus->host_name) < 0)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_SERVICENAME) {
			if(strcasecmp(new_svcstatus->description, temp_svcstatus->description) < 0)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_STATEDURATION) {
			if(new_svcstatus->last_state_change == (time_t)0)
				nt = (program_start > current_time) ? 0 : (current_time - program_start);
			else
				nt = (new_svcstatus->last_state_change > current_time) ? 0 : (current_time - new_svcstatus->last_state_change);
			if(temp_svcstatus->last_state_change == (time_t)0)
				tt = (program_start > current_time) ? 0 : (current_time - program_start);
			else
				tt = (temp_svcstatus->last_state_change > current_time) ? 0 : (current_time - temp_svcstatus->last_state_change);
			if(nt < tt)
				return TRUE;
			else
				return FALSE;
			}
		}
	else {
		if(s_option == SORT_LASTCHECKTIME) {
			if(new_svcstatus->last_check > temp_svcstatus->last_check)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_CURRENTATTEMPT) {
			if(new_svcstatus->current_attempt > temp_svcstatus->current_attempt)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_SERVICESTATUS) {
			if(new_svcstatus->status > temp_svcstatus->status)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_HOSTNAME) {
			if(strcasecmp(new_svcstatus->host_name, temp_svcstatus->host_name) > 0)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_SERVICENAME) {
			if(strcasecmp(new_svcstatus->description, temp_svcstatus->description) > 0)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_STATEDURATION) {
			if(new_svcstatus->last_state_change == (time_t)0)
				nt = (program_start > current_time) ? 0 : (current_time - program_start);
			else
				nt = (new_svcstatus->last_state_change > current_time) ? 0 : (current_time - new_svcstatus->last_state_change);
			if(temp_svcstatus->last_state_change == (time_t)0)
				tt = (program_start > current_time) ? 0 : (current_time - program_start);
			else
				tt = (temp_svcstatus->last_state_change > current_time) ? 0 : (current_time - temp_svcstatus->last_state_change);
			if(nt > tt)
				return TRUE;
			else
				return FALSE;
			}
		}

	return TRUE;
	}



/* sorts the host list */
int sort_hosts(int s_type, int s_option) {
	hostsort *new_hostsort;
	hostsort *last_hostsort;
	hostsort *temp_hostsort;
	hoststatus *temp_hststatus;

	if(s_type == SORT_NONE)
		return ERROR;

	if(hoststatus_list == NULL)
		return ERROR;

	/* sort all hosts status entries */
	for(temp_hststatus = hoststatus_list; temp_hststatus != NULL; temp_hststatus = temp_hststatus->next) {

		/* allocate memory for a new sort structure */
		new_hostsort = (hostsort *)malloc(sizeof(hostsort));
		if(new_hostsort == NULL)
			return ERROR;

		new_hostsort->hststatus = temp_hststatus;

		last_hostsort = hostsort_list;
		for(temp_hostsort = hostsort_list; temp_hostsort != NULL; temp_hostsort = temp_hostsort->next) {

			if(compare_hostsort_entries(s_type, s_option, new_hostsort, temp_hostsort) == TRUE) {
				new_hostsort->next = temp_hostsort;
				if(temp_hostsort == hostsort_list)
					hostsort_list = new_hostsort;
				else
					last_hostsort->next = new_hostsort;
				break;
				}
			else
				last_hostsort = temp_hostsort;
			}

		if(hostsort_list == NULL) {
			new_hostsort->next = NULL;
			hostsort_list = new_hostsort;
			}
		else if(temp_hostsort == NULL) {
			new_hostsort->next = NULL;
			last_hostsort->next = new_hostsort;
			}
		}

	return OK;
	}


int compare_hostsort_entries(int s_type, int s_option, hostsort *new_hostsort, hostsort *temp_hostsort) {
	hoststatus *new_hststatus;
	hoststatus *temp_hststatus;
	time_t nt;
	time_t tt;

	new_hststatus = new_hostsort->hststatus;
	temp_hststatus = temp_hostsort->hststatus;

	if(s_type == SORT_ASCENDING) {

		if(s_option == SORT_LASTCHECKTIME) {
			if(new_hststatus->last_check < temp_hststatus->last_check)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_HOSTSTATUS) {
			if(new_hststatus->status <= temp_hststatus->status)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_HOSTURGENCY) {
			if(HOST_URGENCY(new_hststatus->status) <= HOST_URGENCY(temp_hststatus->status))
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_HOSTNAME) {
			if(strcasecmp(new_hststatus->host_name, temp_hststatus->host_name) < 0)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_STATEDURATION) {
			if(new_hststatus->last_state_change == (time_t)0)
				nt = (program_start > current_time) ? 0 : (current_time - program_start);
			else
				nt = (new_hststatus->last_state_change > current_time) ? 0 : (current_time - new_hststatus->last_state_change);
			if(temp_hststatus->last_state_change == (time_t)0)
				tt = (program_start > current_time) ? 0 : (current_time - program_start);
			else
				tt = (temp_hststatus->last_state_change > current_time) ? 0 : (current_time - temp_hststatus->last_state_change);
			if(nt < tt)
				return TRUE;
			else
				return FALSE;
			}
		}
	else {
		if(s_option == SORT_LASTCHECKTIME) {
			if(new_hststatus->last_check > temp_hststatus->last_check)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_HOSTSTATUS) {
			if(new_hststatus->status > temp_hststatus->status)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_HOSTURGENCY) {
			if(HOST_URGENCY(new_hststatus->status) > HOST_URGENCY(temp_hststatus->status))
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_HOSTNAME) {
			if(strcasecmp(new_hststatus->host_name, temp_hststatus->host_name) > 0)
				return TRUE;
			else
				return FALSE;
			}
		else if(s_option == SORT_STATEDURATION) {
			if(new_hststatus->last_state_change == (time_t)0)
				nt = (program_start > current_time) ? 0 : (current_time - program_start);
			else
				nt = (new_hststatus->last_state_change > current_time) ? 0 : (current_time - new_hststatus->last_state_change);
			if(temp_hststatus->last_state_change == (time_t)0)
				tt = (program_start > current_time) ? 0 : (current_time - program_start);
			else
				tt = (temp_hststatus->last_state_change > current_time) ? 0 : (current_time - temp_hststatus->last_state_change);
			if(nt > tt)
				return TRUE;
			else
				return FALSE;
			}
		}

	return TRUE;
	}



/* free all memory allocated to the servicesort structures */
void free_servicesort_list(void) {
	servicesort *this_servicesort;
	servicesort *next_servicesort;

	/* free memory for the servicesort list */
	for(this_servicesort = servicesort_list; this_servicesort != NULL; this_servicesort = next_servicesort) {
		next_servicesort = this_servicesort->next;
		free(this_servicesort);
		}

	return;
	}


/* free all memory allocated to the hostsort structures */
void free_hostsort_list(void) {
	hostsort *this_hostsort;
	hostsort *next_hostsort;

	/* free memory for the hostsort list */
	for(this_hostsort = hostsort_list; this_hostsort != NULL; this_hostsort = next_hostsort) {
		next_hostsort = this_hostsort->next;
		free(this_hostsort);
		}

	return;
	}



/* check host properties filter */
int passes_host_properties_filter(hoststatus *temp_hoststatus) {

	if((host_properties & HOST_SCHEDULED_DOWNTIME) && temp_hoststatus->scheduled_downtime_depth <= 0)
		return FALSE;

	if((host_properties & HOST_NO_SCHEDULED_DOWNTIME) && temp_hoststatus->scheduled_downtime_depth > 0)
		return FALSE;

	if((host_properties & HOST_STATE_ACKNOWLEDGED) && temp_hoststatus->problem_has_been_acknowledged == FALSE)
		return FALSE;

	if((host_properties & HOST_STATE_UNACKNOWLEDGED) && temp_hoststatus->problem_has_been_acknowledged == TRUE)
		return FALSE;

	if((host_properties & HOST_CHECKS_DISABLED) && temp_hoststatus->checks_enabled == TRUE)
		return FALSE;

	if((host_properties & HOST_CHECKS_ENABLED) && temp_hoststatus->checks_enabled == FALSE)
		return FALSE;

	if((host_properties & HOST_EVENT_HANDLER_DISABLED) && temp_hoststatus->event_handler_enabled == TRUE)
		return FALSE;

	if((host_properties & HOST_EVENT_HANDLER_ENABLED) && temp_hoststatus->event_handler_enabled == FALSE)
		return FALSE;

	if((host_properties & HOST_FLAP_DETECTION_DISABLED) && temp_hoststatus->flap_detection_enabled == TRUE)
		return FALSE;

	if((host_properties & HOST_FLAP_DETECTION_ENABLED) && temp_hoststatus->flap_detection_enabled == FALSE)
		return FALSE;

	if((host_properties & HOST_IS_FLAPPING) && temp_hoststatus->is_flapping == FALSE)
		return FALSE;

	if((host_properties & HOST_IS_NOT_FLAPPING) && temp_hoststatus->is_flapping == TRUE)
		return FALSE;

	if((host_properties & HOST_NOTIFICATIONS_DISABLED) && temp_hoststatus->notifications_enabled == TRUE)
		return FALSE;

	if((host_properties & HOST_NOTIFICATIONS_ENABLED) && temp_hoststatus->notifications_enabled == FALSE)
		return FALSE;

	if((host_properties & HOST_PASSIVE_CHECKS_DISABLED) && temp_hoststatus->accept_passive_checks == TRUE)
		return FALSE;

	if((host_properties & HOST_PASSIVE_CHECKS_ENABLED) && temp_hoststatus->accept_passive_checks == FALSE)
		return FALSE;

	if((host_properties & HOST_PASSIVE_CHECK) && temp_hoststatus->check_type == CHECK_TYPE_ACTIVE)
		return FALSE;

	if((host_properties & HOST_ACTIVE_CHECK) && temp_hoststatus->check_type == CHECK_TYPE_PASSIVE)
		return FALSE;

	if((host_properties & HOST_HARD_STATE) && temp_hoststatus->state_type == SOFT_STATE)
		return FALSE;

	if((host_properties & HOST_SOFT_STATE) && temp_hoststatus->state_type == HARD_STATE)
		return FALSE;

	return TRUE;
	}



/* check service properties filter */
int passes_service_properties_filter(servicestatus *temp_servicestatus) {

	if((service_properties & SERVICE_SCHEDULED_DOWNTIME) && temp_servicestatus->scheduled_downtime_depth <= 0)
		return FALSE;

	if((service_properties & SERVICE_NO_SCHEDULED_DOWNTIME) && temp_servicestatus->scheduled_downtime_depth > 0)
		return FALSE;

	if((service_properties & SERVICE_STATE_ACKNOWLEDGED) && temp_servicestatus->problem_has_been_acknowledged == FALSE)
		return FALSE;

	if((service_properties & SERVICE_STATE_UNACKNOWLEDGED) && temp_servicestatus->problem_has_been_acknowledged == TRUE)
		return FALSE;

	if((service_properties & SERVICE_CHECKS_DISABLED) && temp_servicestatus->checks_enabled == TRUE)
		return FALSE;

	if((service_properties & SERVICE_CHECKS_ENABLED) && temp_servicestatus->checks_enabled == FALSE)
		return FALSE;

	if((service_properties & SERVICE_EVENT_HANDLER_DISABLED) && temp_servicestatus->event_handler_enabled == TRUE)
		return FALSE;

	if((service_properties & SERVICE_EVENT_HANDLER_ENABLED) && temp_servicestatus->event_handler_enabled == FALSE)
		return FALSE;

	if((service_properties & SERVICE_FLAP_DETECTION_DISABLED) && temp_servicestatus->flap_detection_enabled == TRUE)
		return FALSE;

	if((service_properties & SERVICE_FLAP_DETECTION_ENABLED) && temp_servicestatus->flap_detection_enabled == FALSE)
		return FALSE;

	if((service_properties & SERVICE_IS_FLAPPING) && temp_servicestatus->is_flapping == FALSE)
		return FALSE;

	if((service_properties & SERVICE_IS_NOT_FLAPPING) && temp_servicestatus->is_flapping == TRUE)
		return FALSE;

	if((service_properties & SERVICE_NOTIFICATIONS_DISABLED) && temp_servicestatus->notifications_enabled == TRUE)
		return FALSE;

	if((service_properties & SERVICE_NOTIFICATIONS_ENABLED) && temp_servicestatus->notifications_enabled == FALSE)
		return FALSE;

	if((service_properties & SERVICE_PASSIVE_CHECKS_DISABLED) && temp_servicestatus->accept_passive_checks == TRUE)
		return FALSE;

	if((service_properties & SERVICE_PASSIVE_CHECKS_ENABLED) && temp_servicestatus->accept_passive_checks == FALSE)
		return FALSE;

	if((service_properties & SERVICE_PASSIVE_CHECK) && temp_servicestatus->check_type == CHECK_TYPE_ACTIVE)
		return FALSE;

	if((service_properties & SERVICE_ACTIVE_CHECK) && temp_servicestatus->check_type == CHECK_TYPE_PASSIVE)
		return FALSE;

	if((service_properties & SERVICE_HARD_STATE) && temp_servicestatus->state_type == SOFT_STATE)
		return FALSE;

	if((service_properties & SERVICE_SOFT_STATE) && temp_servicestatus->state_type == HARD_STATE)
		return FALSE;

	return TRUE;
	}



/* shows service and host filters in use */
void show_filters(void) {
	int found = 0;

	/* show filters box if necessary */
	if(host_properties != 0L || service_properties != 0L || host_status_types != all_host_status_types || service_status_types != all_service_status_types) {

		printf("<table class='filter'>\n");
		printf("<tr><td class='filter'>\n");
		printf("<table border=0 cellspacing=2 cellpadding=0>\n");
		printf("<tr><td colspan=2 valign=top align=left class='filterTitle'>显示过滤:</td></tr>");
		printf("<tr><td valign=top align=left class='filterName'>主机状态类型:</td>");
		printf("<td valign=top align=left class='filterValue'>");
		if(host_status_types == all_host_status_types)
			printf("所有");
		else if(host_status_types == all_host_problems)
			printf("所有故障");
		else {
			found = 0;
			if(host_status_types & HOST_PENDING) {
				printf("未决状态");
				found = 1;
				}
			if(host_status_types & SD_HOST_UP) {
				printf("%s 运行状态", (found == 1) ? " |" : "");
				found = 1;
				}
			if(host_status_types & SD_HOST_DOWN) {
				printf("%s 宕机状态", (found == 1) ? " |" : "");
				found = 1;
				}
			if(host_status_types & SD_HOST_UNREACHABLE)
				printf("%s 不可达状态", (found == 1) ? " |" : "");
			}
		printf("</td></tr>");
		printf("<tr><td valign=top align=left class='filterName'>主机属性:</td>");
		printf("<td valign=top align=left class='filterValue'>");
		if(host_properties == 0)
			printf("任何");
		else {
			found = 0;
			if(host_properties & HOST_SCHEDULED_DOWNTIME) {
				printf("在安排的宕机时间中");
				found = 1;
				}
			if(host_properties & HOST_NO_SCHEDULED_DOWNTIME) {
				printf("%s 不在安排的宕机时间中", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_STATE_ACKNOWLEDGED) {
				printf("%s 问题已确认", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_STATE_UNACKNOWLEDGED) {
				printf("%s 问题未确认", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_CHECKS_DISABLED) {
				printf("%s 检查被禁用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_CHECKS_ENABLED) {
				printf("%s 检查已启用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_EVENT_HANDLER_DISABLED) {
				printf("%s 事件处理被禁用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_EVENT_HANDLER_ENABLED) {
				printf("%s 事件处理已启用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_FLAP_DETECTION_DISABLED) {
				printf("%s 抖动监测被禁用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_FLAP_DETECTION_ENABLED) {
				printf("%s 抖动监测已启用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_IS_FLAPPING) {
				printf("%s 处于抖动中", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_IS_NOT_FLAPPING) {
				printf("%s 不处于抖动中", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_NOTIFICATIONS_DISABLED) {
				printf("%s 通知被禁用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_NOTIFICATIONS_ENABLED) {
				printf("%s 通知已启用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_PASSIVE_CHECKS_DISABLED) {
				printf("%s 被动检查被禁用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_PASSIVE_CHECKS_ENABLED) {
				printf("%s 被动检查已启用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_PASSIVE_CHECK) {
				printf("%s 被动检查", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_ACTIVE_CHECK) {
				printf("%s 主动检查", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_HARD_STATE) {
				printf("%s 处于硬态状态", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(host_properties & HOST_SOFT_STATE) {
				printf("%s 处于软态状态", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			}
		printf("</td>");
		printf("</tr>\n");


		printf("<tr><td valign=top align=left class='filterName'>服务状态类型:</td>");
		printf("<td valign=top align=left class='filterValue'>");
		if(service_status_types == all_service_status_types)
			printf("所有");
		else if(service_status_types == all_service_problems)
			printf("所有故障");
		else {
			found = 0;
			if(service_status_types & SERVICE_PENDING) {
				printf("未决状态");
				found = 1;
				}
			if(service_status_types & SERVICE_OK) {
				printf("%s 正常状态", (found == 1) ? " |" : "");
				found = 1;
				}
			if(service_status_types & SERVICE_UNKNOWN) {
				printf("%s 未知状态", (found == 1) ? " |" : "");
				found = 1;
				}
			if(service_status_types & SERVICE_WARNING) {
				printf("%s 警告状态", (found == 1) ? " |" : "");
				found = 1;
				}
			if(service_status_types & SERVICE_CRITICAL) {
				printf("%s 紧急状态", (found == 1) ? " |" : "");
				found = 1;
				}
			}
		printf("</td></tr>");
		printf("<tr><td valign=top align=left class='filterName'>服务属性:</td>");
		printf("<td valign=top align=left class='filterValue'>");
		if(service_properties == 0)
			printf("任何");
		else {
			found = 0;
			if(service_properties & SERVICE_SCHEDULED_DOWNTIME) {
				printf("在安排的宕机时间中");
				found = 1;
				}
			if(service_properties & SERVICE_NO_SCHEDULED_DOWNTIME) {
				printf("%s 不在安排的宕机时间中", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_STATE_ACKNOWLEDGED) {
				printf("%s 问题已确认", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_STATE_UNACKNOWLEDGED) {
				printf("%s 问题未确认", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_CHECKS_DISABLED) {
				printf("%s Active Checks Disabled", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_CHECKS_ENABLED) {
				printf("%s Active Checks Enabled", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_EVENT_HANDLER_DISABLED) {
				printf("%s 事件处理被禁用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_EVENT_HANDLER_ENABLED) {
				printf("%s 事件处理已启用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_FLAP_DETECTION_DISABLED) {
				printf("%s 抖动监测被禁用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_FLAP_DETECTION_ENABLED) {
				printf("%s 抖动监测已启用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_IS_FLAPPING) {
				printf("%s 处于抖动中", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_IS_NOT_FLAPPING) {
				printf("%s 不处于抖动中", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_NOTIFICATIONS_DISABLED) {
				printf("%s 通知被禁用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_NOTIFICATIONS_ENABLED) {
				printf("%s 通知已启用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_PASSIVE_CHECKS_DISABLED) {
				printf("%s 被动检查被禁用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_PASSIVE_CHECKS_ENABLED) {
				printf("%s 被动检查已启用", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_PASSIVE_CHECK) {
				printf("%s 被动检查", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_ACTIVE_CHECK) {
				printf("%s 主动检查", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_HARD_STATE) {
				printf("%s 处于硬态状态", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			if(service_properties & SERVICE_SOFT_STATE) {
				printf("%s 处于软态状态", (found == 1) ? " &amp;" : "");
				found = 1;
				}
			}
		printf("</td></tr>");
		printf("</table>\n");

		printf("</td></tr>");
		printf("</table>\n");
		}

	return;
	}

void create_pagenumbers(int total_entries,char *temp_url,int type_service) {

	int pages = 1;
	int tmp_start;
	int i;
	int previous_page;

	/* do page numbers if applicable */
	if(result_limit > 0 && total_entries > result_limit) {
		pages = (total_entries / result_limit);
		previous_page = (page_start-result_limit) > 0 ? (page_start-result_limit) : 0;
		printf("<div id='bottom_page_numbers'>\n");
		printf("<div class='inner_numbers'>\n");
		printf("<a href='%s&start=0&limit=%i' class='pagenumber' title='首页'><img src='%s%s' height='15' width='15' alt='<<' /></a>\n",temp_url,result_limit,url_images_path,FIRST_PAGE_ICON);
		printf("<a href='%s&start=%i&limit=%i' class='pagenumber' title='前一页'><img src='%s%s' height='15' width='10' alt='<' /></a>\n",temp_url,previous_page,result_limit,url_images_path,PREVIOUS_PAGE_ICON);

		for(i = 0; i < (pages + 1); i++) {
			tmp_start = (i * result_limit);
			if(tmp_start == page_start)
				printf("<div class='pagenumber current_page'> %i </div>\n",(i+1));
			else
				printf("<a class='pagenumber' href='%s&start=%i&limit=%i' title='第 %i 页'> %i </a>\n",temp_url,tmp_start,result_limit,(i+1),(i+1));
			}

		printf("<a href='%s&start=%i&limit=%i' class='pagenumber' title='下一页'><img src='%s%s' height='15' width='10' alt='>' /></a>\n",temp_url,(page_start+result_limit),result_limit,url_images_path,NEXT_PAGE_ICON);
		printf("<a href='%s&start=%i&limit=%i' class='pagenumber' title='尾页'><img src='%s%s' height='15' width='15' alt='>>' /></a>\n",temp_url,((pages)*result_limit),result_limit,url_images_path,LAST_PAGE_ICON);
		printf("</div> <!-- end inner_page_numbers div -->\n");
		if(type_service == TRUE)
			printf("<br /><div class='itemTotalsTitle'>Results %i - %i of %d Matching Services</div>\n</div>\n",page_start,((page_start+result_limit) > total_entries ? total_entries :(page_start+result_limit) ),total_entries );
		else
			printf("<br /><div class='itemTotalsTitle'>Results %i - %i of %d Matching Hosts</div>\n\n",page_start,((page_start+result_limit) > total_entries ? total_entries :(page_start+result_limit) ),total_entries );

		printf("</div> <!-- end bottom_page_numbers div -->\n\n");
		}
	else {
		if(type_service == TRUE)
			printf("<br /><div class='itemTotalsTitle'>Results %i - %i of %d Matching Services</div>\n</div>\n",1,total_entries,total_entries);
		else
			printf("<br /><div class='itemTotalsTitle'>Results %i - %i of %d Matching Hosts</div>\n\n",1,total_entries,total_entries);

		}

	/* show total results displayed */
	//printf("<br /><div class='itemTotalsTitle'>Results %i - %i of %d Matching Services</div>\n</div>\n",page_start,((page_start+result_limit) > total_entries ? total_entries :(page_start+result_limit) ),total_entries );

	}

void create_page_limiter(int limit,char *temp_url) {

	/*  Result Limit Select Box   */
	printf("<div id='pagelimit'>\n<div id='result_limit'>\n");
	printf("<label for='limit'>每页显示: </label>\n");
	printf("<select onchange='set_limit(\"%s\")' name='limit' id='limit'>\n",temp_url);
	printf("<option %s value='50'>50</option>\n",( (limit==50) ? "selected='selected'" : "") );
	printf("<option %s value='100'>100</option>\n",( (limit==100) ? "selected='selected'" : "") );
	printf("<option %s value='250'>250</option>\n",( (limit==250) ? "selected='selected'" : "") );
	printf("<option %s value='1000'>1000</option>\n",( (limit==1000) ? "selected='selected'" : "") );
	printf("<option %s value='0'>全部</option>\n",(limit==0) ? "selected='selected'" : "");
	printf("</select></div>\n");
	printf("<div id='top_page_numbers'></div>\n</div>\n");
	//page numbers
	}
