#include "panel/gateway_panel.c"
#include <assert.h>
int main(void){assert(!strcmp(web_error_label(2),"LAN unavailable"));assert(!strcmp(web_error_label(60),"Net data pending"));assert(!strcmp(web_error_label(61),"Net data error"));assert(strlen(web_error_label(60))<=GATEWAY_PANEL_COLUMNS);assert(strlen(web_error_label(61))<=GATEWAY_PANEL_COLUMNS);puts("PASS distinct LCD snapshot/selected-LAN labels fit 16 columns");return 0;}
