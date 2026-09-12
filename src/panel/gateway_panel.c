#include <stdio.h>
#include <string.h>

#include "diagnostics/gateway_diagnostics.h"
#include "panel/gateway_panel.h"
#include "version.h"

static const unsigned long baud_values[]={50UL,75UL,110UL,134UL,150UL,200UL,300UL,600UL,1200UL,1800UL,2400UL,4800UL,9600UL,19200UL,38400UL,57600UL,115200UL,230400UL,460800UL,921600UL};
static const char*menu_items[]={"Status","Ports","Configuration","Diagnostics","System","Shutdown","About"};
static const char*field_names[]={"Enabled","UART","Mode","Baud","Special baud","Data bits","Parity","Stop bits","Transport","Bind IPv4","TCP port","Apply","Discard edits"};
static void increment(unsigned int*v){if(*v!=0xffffffffU)++*v;}
static void clear_screen(gateway_panel_screen_t*s){unsigned int r;for(r=0;r<GATEWAY_PANEL_ROWS;++r){memset(s->row[r],' ',GATEWAY_PANEL_COLUMNS);s->row[r][GATEWAY_PANEL_COLUMNS]='\0';}}
static void row(gateway_panel_screen_t*s,unsigned int r,const char*t){size_t n;if(r>=GATEWAY_PANEL_ROWS)return;n=strlen(t);if(n>GATEWAY_PANEL_COLUMNS)n=GATEWAY_PANEL_COLUMNS;memset(s->row[r],' ',GATEWAY_PANEL_COLUMNS);memcpy(s->row[r],t,n);s->row[r][GATEWAY_PANEL_COLUMNS]='\0';}
static void number_row(gateway_panel_screen_t*s,unsigned int r,const char*label,unsigned long value){char b[32];snprintf(b,sizeof(b),"%s%lu",label,value);row(s,r,b);}
static const char*state_name(gateway_application_state_t s){static const char*n[]={"BOOT","LOAD CONFIG","SELECT CONFIG","VALIDATE","INIT CONTROLLER","START PORTS","CHECK HEALTH","READY","DEGRADED","SAFE MODE","SHUTTING DOWN","STOPPED","FATAL ERROR"};return(unsigned)s<13U?n[s]:"UNKNOWN";}
static const char*source_name(gateway_config_source_t s){static const char*n[]={"ACTIVE","BACKUP","DEFAULTS","SAFE MODE"};return(unsigned)s<4U?n[s]:"UNKNOWN";}
static const char*life_name(gateway_port_lifecycle_t s){static const char*n[]={"DISABLED","STARTING","READY","RECONFIG","ROLLBACK","STOPPING","DEGRADED","ERROR","WAIT NETWORK"};return(unsigned)s<9U?n[s]:"UNKNOWN";}
static const char*mode_name(serial_mode_t m){static const char*n[]={"RS232","RS485-2W","RS422","RS485-4W"};return(unsigned)m<4U?n[m]:"INVALID";}
static const char*parity_name(parity_mode_t p){static const char*n[]={"NONE","EVEN","ODD"};return(unsigned)p<3U?n[p]:"INVALID";}
static const char*transport_name(transport_type_t t){static const char*n[]={"MB TCP","MB UDP","RTU/UDP","RAW TCP","RAW UDP"};return(unsigned)t<5U?n[t]:"INVALID";}
static const char*ntp_state(const gateway_application_t*a);
static int terminal(gateway_application_state_t s){return s==GATEWAY_APP_READY||s==GATEWAY_APP_DEGRADED||s==GATEWAY_APP_SAFE_MODE||s==GATEWAY_APP_FATAL_ERROR;}
static void footer(gateway_panel_screen_t*s,const char*t){row(s,7,t);}
static void render_startup(gateway_panel_t*p,const gateway_diagnostic_snapshot_t*d){char b[24],bar[11];const char*result;unsigned int i,fill=d->application.coordinator.progress_percent/10U;clear_screen(&p->next);row(&p->next,0,FOURVRS_PRODUCT_NAME);row(&p->next,1,FOURVRS_VERSION);row(&p->next,3,state_name(d->application.coordinator.last_event.stage));for(i=0;i<10U;++i)bar[i]=i<fill?'#':'-';bar[10]='\0';snprintf(b,sizeof(b),"[%s]%u%%",bar,d->application.coordinator.progress_percent);row(&p->next,4,b);if(d->application.coordinator.last_event.port_index!=GATEWAY_NO_PORT){result=d->application.coordinator.last_event.result==GATEWAY_EVENT_SKIPPED?"OFF":(d->application.coordinator.last_event.result==GATEWAY_EVENT_FAILED?"FAILED":"READY");snprintf(b,sizeof(b),"P%u %s",d->application.coordinator.last_event.port_index+1U,result);row(&p->next,6,b);}if(terminal(d->application.coordinator.state))row(&p->next,6,state_name(d->application.coordinator.state));}
static void render_home(gateway_panel_t*p,const gateway_diagnostic_snapshot_t*d,const gateway_application_t*a){char b[32];clear_screen(&p->next);row(&p->next,0,FOURVRS_PRODUCT_NAME);row(&p->next,1,a->fatal_seen?"FATAL ERROR":state_name(d->application.coordinator.state));snprintf(b,sizeof(b),"Ports %u/%u ready",d->application.coordinator.ready_ports,d->application.coordinator.enabled_ports);row(&p->next,3,b);snprintf(b,sizeof(b),"Errors %u",d->application.coordinator.error_ports);row(&p->next,4,b);snprintf(b,sizeof(b),"Clients %u",d->application.coordinator.clients);row(&p->next,5,b);if(d->application.coordinator.error!=GATEWAY_APP_ERROR_NONE)number_row(&p->next,6,"App error ",(unsigned long)d->application.coordinator.error);else if(a->time.trust==GATEWAY_TIME_SYNCING)row(&p->next,6,"TIME SYNCING");else if(a->time.trust==GATEWAY_TIME_MANUAL)row(&p->next,6,"TIME MANUAL");else if(a->time.trust!=GATEWAY_TIME_SYNCED)row(&p->next,6,"TIME UNSYNC");footer(&p->next,"F1 Help F3 Menu");}
static void render_help(gateway_panel_t*p){clear_screen(&p->next);row(&p->next,0,p->help_page?"Button Help: 2/2":"Button Help: 1/2");if(!p->help_page){row(&p->next,1,"F1 Back/Cancel");row(&p->next,2,"F2 Previous");row(&p->next,3,"  or Decrease");row(&p->next,4,"F3 Select / OK");row(&p->next,6,"F4 Next page");}else{row(&p->next,1,"F4 Next");row(&p->next,2,"  or Increase");row(&p->next,3,"F5 Edit/Context");row(&p->next,5,"F2 Prev page");row(&p->next,6,"F1 Back");}}
static void render_menu(gateway_panel_t*p){unsigned int r;char b[24];clear_screen(&p->next);row(&p->next,0,"Main Menu:");for(r=0;r<7U;++r){snprintf(b,sizeof(b),"%c %.13s",r==p->selected?'>':' ',menu_items[r]);row(&p->next,r+1U,b);}}
static void list(gateway_panel_t*p,const char*title,const char*const*items,unsigned int count){unsigned int r,i;char b[24];clear_screen(&p->next);row(&p->next,0,title);if(p->selected<p->scroll)p->scroll=p->selected;if(p->selected>=p->scroll+GATEWAY_PANEL_MENU_VISIBLE)p->scroll=p->selected-GATEWAY_PANEL_MENU_VISIBLE+1U;for(r=0;r<GATEWAY_PANEL_MENU_VISIBLE;++r){i=p->scroll+r;if(i>=count)break;snprintf(b,sizeof(b),"%c %.13s",i==p->selected?'>':' ',items[i]);row(&p->next,r+1U,b);}footer(&p->next,"F3 Select");}
static void render_status(gateway_panel_t*p,const gateway_diagnostic_snapshot_t*d){char b[32];clear_screen(&p->next);row(&p->next,0,"Status:");row(&p->next,1,state_name(d->application.coordinator.state));snprintf(b,sizeof(b),"Cfg %s %u%%",source_name(d->application.coordinator.config_source),d->application.coordinator.progress_percent);row(&p->next,2,b);snprintf(b,sizeof(b),"Ports R%u E%u D%u",d->application.coordinator.ready_ports,d->application.coordinator.error_ports,d->application.coordinator.disabled_ports);row(&p->next,3,b);number_row(&p->next,4,"Clients ",d->application.coordinator.clients);number_row(&p->next,5,"Uptime s ",(unsigned long)(d->application.coordinator.uptime/1000UL));if(d->application.coordinator.error)number_row(&p->next,6,"Error ",d->application.coordinator.error);footer(&p->next,"F1 Back");}
static void render_ports(gateway_panel_t*p,const gateway_diagnostic_snapshot_t*d){unsigned int r,i;char b[32];clear_screen(&p->next);row(&p->next,0,"Ports:");if(p->selected<p->scroll)p->scroll=p->selected;if(p->selected>=p->scroll+5U)p->scroll=p->selected-4U;for(r=0;r<5U;++r){const gateway_port_health_t*q;i=p->scroll+r;if(i>=8U)break;q=&d->application.coordinator.gateway.port[i];snprintf(b,sizeof(b),"%cP%u %-8s %c",i==p->selected?'>':' ',i+1U,life_name(q->lifecycle),q->last_error?'!':' ');row(&p->next,r+1U,b);}footer(&p->next,"F3 Detail");}
static void render_port(gateway_panel_t*p,const gateway_diagnostic_snapshot_t*d){const gateway_port_health_t*q=&d->application.coordinator.gateway.port[p->port_index];char b[40];clear_screen(&p->next);snprintf(b,sizeof(b),"Port %u: %u/4",p->port_index+1U,p->detail_page+1U);row(&p->next,0,b);if(p->detail_page==0U){snprintf(b,sizeof(b),"ttyM%u %s",q->config.uart_index,mode_name(q->config.mode));row(&p->next,1,b);snprintf(b,sizeof(b),"%lu %u%s%u",q->config.baud,q->config.data_bits,parity_name(q->config.parity),q->config.stop_bits);row(&p->next,2,b);row(&p->next,3,transport_name(q->config.transport));row(&p->next,4,q->config.bind_address);number_row(&p->next,5,(q->config.transport==TRANSPORT_MODBUS_UDP||q->config.transport==TRANSPORT_RTU_UDP||q->config.transport==TRANSPORT_RAW_UDP)?"UDP ":"TCP ",q->config.endpoint_port);number_row(&p->next,6,"Clients ",q->connected_clients);}else if(p->detail_page==1U){number_row(&p->next,1,"Accepted ",q->transactions.accepted);number_row(&p->next,2,"Complete ",q->transactions.completed);number_row(&p->next,3,"Timeout ",q->transactions.timeouts);number_row(&p->next,4,"Recovery ",q->transactions.recoveries);number_row(&p->next,5,"Queue ",q->transactions.queue_depth);number_row(&p->next,6,"High water ",q->transactions.queue_high_water);}else if(p->detail_page==2U){number_row(&p->next,1,"CRC ",q->transport.crc_failures);number_row(&p->next,2,"Frame ",q->transport.framing_failures);number_row(&p->next,3,"Unit ",q->transport.unit_mismatches);number_row(&p->next,4,"Function ",q->transport.function_mismatches);number_row(&p->next,5,"Garbage ",q->transport.leading_garbage);number_row(&p->next,6,"Stale ",q->transactions.stale_responses);}else{number_row(&p->next,1,"Generation ",q->config_generation);number_row(&p->next,2,"Last ok ",(unsigned long)q->transactions.last_success_at);row(&p->next,4,q->runtime_last_error[0]?q->runtime_last_error:"No error");}footer(&p->next,"F2/F4 Page");}
static void render_config_ports(gateway_panel_t*p){const char*names[]={"P1","P2","P3","P4","P5","P6","P7","P8"};list(p,"Config:",names,8U);row(&p->next,6,"F5 Network");}
static void field_value(const gateway_port_config_t*c,unsigned int f,char*b,size_t n){switch(f){case 0:snprintf(b,n,"%s",c->enabled?"ENABLED":"DISABLED");break;case 1:snprintf(b,n,"ttyM%u",c->uart_index);break;case 2:snprintf(b,n,"%s",mode_name(c->mode));break;case 3:snprintf(b,n,"%lu",c->baud);break;case 4:snprintf(b,n,"%s %lu",c->special_baud_enabled?"ON":"OFF",c->special_baud);break;case 5:snprintf(b,n,"%u",c->data_bits);break;case 6:snprintf(b,n,"%s",parity_name(c->parity));break;case 7:snprintf(b,n,"%u",c->stop_bits);break;case 8:snprintf(b,n,"%s",transport_name(c->transport));break;case 9:snprintf(b,n,"%s",c->bind_address);break;case 10:snprintf(b,n,"%u",c->endpoint_port);break;default:snprintf(b,n,"%s",f==11U?"Save & Apply":"Cancel changes");break;}}
static void render_fields(gateway_panel_t*p){const gateway_port_config_t*c=&p->candidate.ports[p->port_index];char v[48],b[48];clear_screen(&p->next);snprintf(b,sizeof(b),"Config P%u:",p->port_index+1U);row(&p->next,0,b);row(&p->next,2,field_names[p->field_index]);field_value(c,p->field_index,v,sizeof(v));row(&p->next,3,v);if(p->field_index==1U)row(&p->next,5,"Fixed at runtime");else number_row(&p->next,5,"Field ",p->field_index+1U);footer(&p->next,p->field_index<11U&&p->field_index!=1U?"F5 Edit":"F3 Select");}
static void render_edit(gateway_panel_t*p){char b[48];clear_screen(&p->next);row(&p->next,0,"Edit Field:");row(&p->next,1,field_names[p->field_index]);field_value(&p->candidate.ports[p->port_index],p->field_index,b,sizeof(b));row(&p->next,3,b);if(p->field_index==9U){snprintf(b,sizeof(b),"Octet %u",p->edit_octet+1U);row(&p->next,5,b);}footer(&p->next,"F3 OK F1 Cancel");}
static void render_confirm(gateway_panel_t*p){clear_screen(&p->next);row(&p->next,0,"Confirm:");row(&p->next,1,"Apply changes?");row(&p->next,2,p->network_change?"Network changes":"Port settings");row(&p->next,4,"F3 YES");row(&p->next,5,"F1 CANCEL");}
static void render_result(gateway_panel_t*p,const gateway_application_t*a){gateway_configuration_transaction_state_t s=gateway_application_configuration_state(a);clear_screen(&p->next);row(&p->next,0,"Config Result:");if(p->local_validation_failed){row(&p->next,2,"INVALID");row(&p->next,3,"NOT APPLIED");number_row(&p->next,5,"Error ",p->local_validation_failed);}else if(s==GATEWAY_CONFIG_TX_SUCCEEDED)row(&p->next,2,"APPLIED");else if(s==GATEWAY_CONFIG_TX_UNCHANGED)row(&p->next,2,"NO CHANGES");else if(s==GATEWAY_CONFIG_TX_DURABILITY_UNCERTAIN){row(&p->next,2,"APPLIED");row(&p->next,3,"DURABILITY?");}else if(s==GATEWAY_CONFIG_TX_FAILED){row(&p->next,2,"FAILED");row(&p->next,3,a->configuration_transaction.rollback_failed?"ROLLBACK FAILED":"ROLLED BACK");}else row(&p->next,2,"APPLYING...");if(!p->local_validation_failed)number_row(&p->next,5,"Result ",gateway_application_configuration_result(a));footer(&p->next,"F1 Back");}
static void render_diagnostics(gateway_panel_t*p,const gateway_diagnostic_snapshot_t*d){clear_screen(&p->next);row(&p->next,0,"Diagnostics:");number_row(&p->next,1,"Accepted ",d->application.coordinator.accepted);number_row(&p->next,2,"Complete ",d->application.coordinator.completed);number_row(&p->next,3,"Timeout ",d->application.coordinator.timeouts);number_row(&p->next,4,"Recovery ",d->application.coordinator.recoveries);number_row(&p->next,5,"Stale ",d->application.coordinator.stale_responses);number_row(&p->next,6,"Q high ",d->queue_high_water);footer(&p->next,"F3 Events");}
static void render_events(gateway_panel_t*p,const gateway_diagnostic_snapshot_t*d){unsigned int i=p->startup_event_page;char b[40];const gateway_startup_event_t*e;clear_screen(&p->next);row(&p->next,0,"Startup Events:");if(i>=d->startup_event_count){row(&p->next,3,"No event");}else{e=&d->startup_events[i];number_row(&p->next,1,"Sequence ",e->sequence);row(&p->next,2,state_name(e->stage));snprintf(b,sizeof(b),"Progress %u%%",e->progress_percent);row(&p->next,3,b);if(e->port_index!=GATEWAY_NO_PORT)number_row(&p->next,4,"Port ",e->port_index+1U);number_row(&p->next,5,"Result ",e->result);number_row(&p->next,6,"Error ",e->error);}footer(&p->next,"F2/F4 Page");}
static void render_system(gateway_panel_t*p,const gateway_diagnostic_snapshot_t*d,gateway_application_t*a){char b[48];core_tick_t uptime=d->application.coordinator.uptime;clear_screen(&p->next);if(p->system_page>=2U){row(&p->next,0,"System:");row(&p->next,2,p->system_page==2U?"> Display":"> Web Server");footer(&p->next,"F3 Open F1 Back");return;}if(!p->have_displayed_time||core_elapsed(p->time_refresh_at,uptime)<0x80000000U){p->time_result=gateway_application_system_time_get(a,&p->displayed_time);p->have_displayed_time=p->time_result==GATEWAY_SYSTEM_TIME_OK;p->time_refresh_at=uptime+1000UL;}if(p->system_page==0U){row(&p->next,0,"Date & Time:");if(p->have_displayed_time){snprintf(b,sizeof(b),"%04u-%02u-%02u",p->displayed_time.year,p->displayed_time.month,p->displayed_time.day);row(&p->next,1,b);snprintf(b,sizeof(b),"%02u:%02u:%02u",p->displayed_time.hour,p->displayed_time.minute,p->displayed_time.second);row(&p->next,2,b);}else row(&p->next,1,"Time unavailable");snprintf(b,sizeof(b),"Time: %s",a->time.trust==GATEWAY_TIME_SYNCED?"Synced":a->time.trust==GATEWAY_TIME_SYNCING?"Syncing":a->time.trust==GATEWAY_TIME_MANUAL?"Manual":a->time.trust==GATEWAY_TIME_ERROR?"Error":a->time.trust==GATEWAY_TIME_HOLDOVER?"Holdover":"Unsynced");row(&p->next,3,b);snprintf(b,sizeof(b),"RTC: %s",a->time.rtc==GATEWAY_RTC_WRITE_FAILED?"Write fail":gateway_rtc_state_name(a->time.rtc));row(&p->next,4,b);row(&p->next,5,"F5 Set Time");row(&p->next,6,"F4 Platform");footer(&p->next,"F1 Back");}else{row(&p->next,0,"Platform:");row(&p->next,1,d->application.platform.model);row(&p->next,2,d->application.platform.kernel);row(&p->next,3,d->application.platform.architecture);snprintf(b,sizeof(b),"%u-bit %s endian",d->application.platform.word_bits,d->application.platform.big_endian?"big":"little");row(&p->next,4,b);number_row(&p->next,5,"Uptime s ",(unsigned long)(uptime/1000UL));row(&p->next,6,"F4 Disp F5 NTP");footer(&p->next,"F2 Date F1 Back");}}
static const char*time_field_name(unsigned int f){static const char*n[]={"Year","Month","Day","Hour","Minute","Second"};return n[f<6U?f:0U];}
static void render_time_edit(gateway_panel_t*p){char b[32];clear_screen(&p->next);row(&p->next,0,"Set Time:");snprintf(b,sizeof(b),"%04u-%02u-%02u",p->time_candidate.year,p->time_candidate.month,p->time_candidate.day);row(&p->next,1,b);snprintf(b,sizeof(b),"%02u:%02u:%02u",p->time_candidate.hour,p->time_candidate.minute,p->time_candidate.second);row(&p->next,2,b);snprintf(b,sizeof(b),"Edit %s",time_field_name(p->time_field));row(&p->next,3,b);row(&p->next,4,"F2 - / F4 +");row(&p->next,5,"F3 Next");footer(&p->next,"F1 Cancel");}
static void render_time_confirm(gateway_panel_t*p){char b[32];clear_screen(&p->next);row(&p->next,0,"Confirm:");row(&p->next,1,"Set system time?");snprintf(b,sizeof(b),"%04u-%02u-%02u",p->time_candidate.year,p->time_candidate.month,p->time_candidate.day);row(&p->next,3,b);snprintf(b,sizeof(b),"%02u:%02u:%02u",p->time_candidate.hour,p->time_candidate.minute,p->time_candidate.second);row(&p->next,4,b);row(&p->next,6,"F3 YES");footer(&p->next,"F1 CANCEL");}
static void render_time_result(gateway_panel_t*p){clear_screen(&p->next);row(&p->next,0,"Time Result:");if(p->time_result==GATEWAY_SYSTEM_TIME_OK){row(&p->next,2,"SYSTEM TIME SET");row(&p->next,3,"RTC SYNC OK");}else if(p->time_result==GATEWAY_SYSTEM_TIME_RTC_UNSUPPORTED){row(&p->next,2,"SYSTEM TIME SET");row(&p->next,3,"RTC NOT SYNCED");}else if(p->time_result==GATEWAY_SYSTEM_TIME_RTC_SYNC_FAILED){row(&p->next,2,"SYSTEM TIME SET");row(&p->next,3,"RTC SYNC FAILED");}else if(p->time_result==GATEWAY_SYSTEM_TIME_INVALID)row(&p->next,2,"INVALID DATE");else row(&p->next,2,"SET FAILED");footer(&p->next,"F1 Back");}
static const char*ntp_state(const gateway_application_t*a){switch(a->time.last_ntp_result){case GATEWAY_NTP_IN_PROGRESS:return"Syncing";case GATEWAY_NTP_SUCCEEDED:return"Synced";case GATEWAY_NTP_FAILED:return"Failed";case GATEWAY_NTP_TIMED_OUT:return"Timed out";case GATEWAY_NTP_UNSUITABLE:return"Unsuitable";case GATEWAY_NTP_NOT_ATTEMPTED:return"Pending";default:return"Disabled";}}
static void render_ntp_settings(gateway_panel_t*p,const gateway_application_t*a){clear_screen(&p->next);row(&p->next,0,"Network Time:");row(&p->next,1,a->time.ntp_enabled?"Enabled: Yes":"Enabled: No");{char b[32];snprintf(b,sizeof(b),"Every: %u h",a->time.ntp_interval_hours);row(&p->next,2,b);}row(&p->next,3,a->time.ntp_server[0]?a->time.ntp_server:"(empty)");{char b[32];snprintf(b,sizeof(b),"State: %s",ntp_state(a));row(&p->next,4,b);}if(a->time.ntp_test_active){char b[32];snprintf(b,sizeof(b),"Test 1m: %u/3",a->time.ntp_test_attempts);row(&p->next,5,b);}else number_row(&p->next,5,"Attempts ",a->time.ntp_attempts);row(&p->next,6,a->time.ntp_test_active?"F4 Stop F5 Edit":a->time.ntp_enabled?"F4 Test F5 Edit":"F5 Edit");footer(&p->next,"F1 Back");}
static void render_ntp_edit(gateway_panel_t*p){char b[32];clear_screen(&p->next);row(&p->next,0,"Edit NTP:");row(&p->next,1,p->candidate.settings.ntp_enabled?"Enabled: Yes":"Enabled: No");row(&p->next,2,"Server:");row(&p->next,3,p->candidate.settings.ntp_server[0]?p->candidate.settings.ntp_server:"(empty)");if(p->ntp_field==0U)row(&p->next,4,"Edit Enabled");else if(p->ntp_field==2U){snprintf(b,sizeof(b),"Every: %u h",p->candidate.settings.ntp_interval_hours);row(&p->next,4,b);}else{snprintf(b,sizeof(b),"Edit char %u",p->ntp_cursor+1U);row(&p->next,4,b);}row(&p->next,5,"F2 - F4 +");row(&p->next,6,p->ntp_field==2U?"F3 Back F5 Done":p->ntp_field?"F3 Next F5 Rate":"F3 Host F5 Rate");footer(&p->next,"F1 Cancel");}
static void render_ntp_confirm(gateway_panel_t*p){clear_screen(&p->next);row(&p->next,0,"Confirm:");row(&p->next,1,"Apply NTP setup?");row(&p->next,2,p->candidate.settings.ntp_enabled?"Enabled: Yes":"Enabled: No");row(&p->next,3,p->candidate.settings.ntp_server[0]?p->candidate.settings.ntp_server:"(empty)");{char b[32];snprintf(b,sizeof(b),"Every: %u h",p->candidate.settings.ntp_interval_hours);row(&p->next,4,b);}row(&p->next,5,"F3 YES");footer(&p->next,"F1 CANCEL");}
static void render_ntp_result(gateway_panel_t*p,const gateway_application_t*a){clear_screen(&p->next);row(&p->next,0,"NTP Result:");if(p->local_validation_failed)row(&p->next,2,"INVALID SETTINGS");else if(gateway_application_configuration_state(a)==GATEWAY_CONFIG_TX_SUCCEEDED)row(&p->next,2,"APPLIED");else if(gateway_application_configuration_state(a)==GATEWAY_CONFIG_TX_UNCHANGED)row(&p->next,2,"NO CHANGES");else if(gateway_application_configuration_state(a)==GATEWAY_CONFIG_TX_FAILED)row(&p->next,2,"FAILED");else row(&p->next,2,"APPLYING...");row(&p->next,4,"Gateway running");footer(&p->next,"F1 Back");}
static void change_time_field(gateway_panel_t*p,int direction){unsigned int*v=0,min=0,max=0;switch(p->time_field){case 0:v=&p->time_candidate.year;min=GATEWAY_SYSTEM_TIME_YEAR_MIN;max=GATEWAY_SYSTEM_TIME_YEAR_MAX;break;case 1:v=&p->time_candidate.month;min=1;max=12;break;case 2:v=&p->time_candidate.day;min=1;max=31;break;case 3:v=&p->time_candidate.hour;max=23;break;case 4:v=&p->time_candidate.minute;max=59;break;default:v=&p->time_candidate.second;max=59;break;}if(direction>0)*v=*v>=max?min:*v+1U;else*v=*v<=min?max:*v-1U;}
static void render_about(gateway_panel_t*p){clear_screen(&p->next);row(&p->next,0,"About:");row(&p->next,1,FOURVRS_PRODUCT_NAME);row(&p->next,2,FOURVRS_VERSION);row(&p->next,4,"Built-in panel");row(&p->next,5,"16x8 text LCM");footer(&p->next,"F1 Back");}
static void render_stop(gateway_panel_t*p){char b[17];clear_screen(&p->next);
 if(p->view==GATEWAY_PANEL_STOPPING){row(&p->next,0,"Stopping Gateway");row(&p->next,2,"Please wait...");row(&p->next,4,"Do not power off");}
 else if(p->view==GATEWAY_PANEL_STOP_FAILED){row(&p->next,0,"Shutdown failed");snprintf(b,sizeof(b),"Error: %u",p->shutdown_error);row(&p->next,2,b);row(&p->next,3,p->shutdown_error==1?"Settings changed":p->shutdown_error==2?"Ports / network":"Web / RNG stop");row(&p->next,4,"See diagnostics");row(&p->next,5,"Do not power off");footer(&p->next,"F1 Back");}
 else {row(&p->next,0,"Gateway stopped");row(&p->next,2,"App closed");row(&p->next,4,"OS still running");row(&p->next,5,"Shut down OS");row(&p->next,6,"before power off");}
}
void gateway_panel_stop_result(gateway_panel_t*p,unsigned int error){if(!p)return;p->shutdown_error=error;p->view=error?GATEWAY_PANEL_STOP_FAILED:GATEWAY_PANEL_STOPPED;p->dirty=1;key_repeat_init(&p->repeat);p->repeat_armed=0;}
static void render_shutdown(gateway_panel_t*p){clear_screen(&p->next);row(&p->next,0,"Confirm:");row(&p->next,1,"Stop gateway?");row(&p->next,2,"Graceful shutdown");row(&p->next,4,"F3 YES");row(&p->next,5,"F1 CANCEL");row(&p->next,6,"Reboot unavailable");}

static void render_network(gateway_panel_t *p,gateway_application_t *a)
{
    char b[48];unsigned int i,affected=0;gateway_network_runtime_t *n=&a->network;
    unsigned int lan=p->network_page==1U?0U:1U;
    static const char *pages[]={"Network / LAN2","Network / LAN1","Default route","Global DNS","Observed LAN1","Observed LAN2"};
    clear_screen(&p->next);
    if(p->view==GATEWAY_PANEL_NETWORK){
        row(&p->next,0,pages[p->network_page]);
        row(&p->next,1,n->available?"Confirmed policy":"Not enrolled");
        if(p->network_page>=4U){
            const gateway_lan_observation_t *live=&n->observed.lan[p->network_page-4U];
            row(&p->next,1,n->observation_error?"Observe error":n->observed_valid?"Observed live":"Observing...");
            row(&p->next,2,live->address);row(&p->next,3,live->netmask);
            row(&p->next,4,live->link?"Link up":"Link down");
            {unsigned int index=p->network_page-4U;
                if(n->confirmed.settings.lan[index].mode==GATEWAY_LAN_DHCP_CLIENT){
                    static const char *states[]={"DHCP off","DHCP selecting","DHCP requesting","DHCP probing","DHCP bound","DHCP renewing","DHCP rebinding","DHCP backoff"};
                    row(&p->next,5,n->dhcp_state[index]<8U?states[n->dhcp_state[index]]:"DHCP error");
                    if(n->lease_valid[index]){core_tick_t now=a->dependencies.clock(a->dependencies.clock_context);
                        core_tick_t left=n->lease_expiry[index]-now;if(left>=0x80000000U)left=0;
                        snprintf(b,sizeof(b),"Lease %lus",(unsigned long)(left/1000U));row(&p->next,6,b);
                    }else row(&p->next,6,"No valid lease");
                }else row(&p->next,5,"Static observed");
            }
            footer(&p->next,"F1 Back F2/F4");return;
        }
        if(p->network_page<2U){
            row(&p->next,2,n->confirmed.settings.lan[lan].address);
            row(&p->next,3,n->confirmed.settings.lan[lan].netmask);
            row(&p->next,4,n->confirmed.settings.lan[lan].mode==GATEWAY_LAN_STATIC?"Static IPv4":"DHCP client");
        }else if(p->network_page==2U){
            snprintf(b,sizeof(b),"Interface: %s",n->confirmed.settings.default_lan==1U?"LAN1":n->confirmed.settings.default_lan==2U?"LAN2":"none");
            row(&p->next,2,b);
            if(n->confirmed.settings.default_lan)row(&p->next,3,n->confirmed.settings.lan[n->confirmed.settings.default_lan-1U].gateway);
        }else{
            if(n->confirmed.settings.automatic_dns){snprintf(b,sizeof(b),"DHCP DNS LAN%u",n->confirmed.settings.dns_lan?n->confirmed.settings.dns_lan:n->confirmed.settings.default_lan);row(&p->next,2,b);}else row(&p->next,2,"Manual DNS");
            row(&p->next,3,n->confirmed.settings.dns[0]);row(&p->next,4,n->confirmed.settings.dns[1]);
        }
        row(&p->next,5,"F2/F4 Pages");row(&p->next,6,"F5 Edit");footer(&p->next,"F1 Back");return;
    }
    if(p->view==GATEWAY_PANEL_NETWORK_EDIT){
        row(&p->next,0,pages[p->network_page]);
        if(p->network_page<2U){
            row(&p->next,1,p->lan2_candidate.lan[lan].address);row(&p->next,2,p->lan2_candidate.lan[lan].netmask);
            if(p->lan2_cursor==8U)snprintf(b,sizeof(b),"Mode: %s",p->lan2_candidate.lan[lan].mode==GATEWAY_LAN_STATIC?"Static":"DHCP");
            else snprintf(b,sizeof(b),"%s octet %u",p->lan2_cursor<4U?"IP":"Mask",p->lan2_cursor%4U+1U);
        }else if(p->network_page==2U){
            unsigned int route=p->lan2_candidate.default_lan;
            snprintf(b,sizeof(b),"Via: %s",route==1U?"LAN1":route==2U?"LAN2":"none");row(&p->next,1,b);
            if(route)row(&p->next,2,p->lan2_candidate.lan[route-1U].gateway);
            if(!p->lan2_cursor)strcpy(b,"Choose interface");else snprintf(b,sizeof(b),"Gateway octet %u",p->lan2_cursor);
        }else{
            row(&p->next,1,p->lan2_candidate.dns[0]);row(&p->next,2,p->lan2_candidate.dns[1]);
            if(p->lan2_cursor==8U){if(p->lan2_candidate.automatic_dns)snprintf(b,sizeof(b),"DNS: DHCP LAN%u",p->lan2_candidate.dns_lan?p->lan2_candidate.dns_lan:p->lan2_candidate.default_lan);else strcpy(b,"DNS: Manual");}
            else snprintf(b,sizeof(b),"DNS%u octet %u",p->lan2_cursor/4U+1U,p->lan2_cursor%4U+1U);
        }
        row(&p->next,3,b);row(&p->next,4,"F2 - / F4 +");row(&p->next,5,"F3 Next field");
        row(&p->next,6,"F5 Review");footer(&p->next,"F1 Cancel");return;
    }
    if(p->view==GATEWAY_PANEL_NETWORK_CONFIRM){
        row(&p->next,0,"Apply network?");row(&p->next,1,pages[p->network_page]);
        row(&p->next,2,"Keep within 60s");row(&p->next,3,"or auto revert");
        row(&p->next,4,"Check connection");
        for(i=0;i<8U;++i){const gateway_port_config_t *port=&a->coordinator.selected.ports[i];
            unsigned int affinity=port->bind_policy?(port->bind_policy>1U?port->bind_policy-1U:0U):n->confirmed.affinity[i];
            if(!affinity&&!port->bind_policy&&!strcmp(port->bind_address,n->confirmed.settings.lan[0].address))affinity=1;
            if(port->enabled&&affinity&&(p->lan2_candidate.lan[affinity-1U].mode!=n->confirmed.settings.lan[affinity-1U].mode||
                strcmp(p->lan2_candidate.lan[affinity-1U].address,n->confirmed.settings.lan[affinity-1U].address)))++affected;
        }
        snprintf(b,sizeof(b),"Rebind ports: %u",affected);row(&p->next,5,b);
        row(&p->next,6,"F3 Apply");footer(&p->next,"F1 Cancel");return;
    }
    row(&p->next,0,"Network change");
    row(&p->next,1,p->local_validation_failed?"Invalid / busy":gateway_network_state_name(n->status.state));
    if(n->status.state==GATEWAY_NETWORK_WAIT_CONFIRM){
        core_tick_t now=a->dependencies.clock(a->dependencies.clock_context);
        core_tick_t remaining=core_elapsed(now,n->status.deadline);
        if(remaining>=0x80000000U)remaining=0;
        snprintf(b,sizeof(b),"Revert in %lus",(unsigned long)((remaining+999U)/1000U));row(&p->next,3,b);
        row(&p->next,5,"F3 Keep (save)");
    }
    row(&p->next,6,"F1 Revert/Back");
    footer(&p->next,gateway_network_runtime_busy(n)?"F1 Revert":"F1 Back");
}
static int network_key(gateway_panel_t *p,gateway_application_t *a,unsigned int k)
{
    if(p->view==GATEWAY_PANEL_CONFIG_PORTS&&k==GATEWAY_PANEL_KEY_F5){p->network_page=0;p->view=GATEWAY_PANEL_NETWORK;return 1;}
    if(p->view==GATEWAY_PANEL_NETWORK){
        if(k==GATEWAY_PANEL_KEY_F1)p->view=GATEWAY_PANEL_CONFIG_PORTS;
        else if(k==GATEWAY_PANEL_KEY_F2||k==GATEWAY_PANEL_KEY_F4)p->network_page=(p->network_page+(k==GATEWAY_PANEL_KEY_F4?1U:5U))%6U;
        else if(k==GATEWAY_PANEL_KEY_F5&&p->network_page<4U&&a->network.available&&!gateway_network_runtime_busy(&a->network)){
            p->lan2_candidate=a->network.confirmed.settings;p->lan2_cursor=0;p->view=GATEWAY_PANEL_NETWORK_EDIT;
        }return 1;
    }
    if(p->view==GATEWAY_PANEL_NETWORK_EDIT){
        if(k==GATEWAY_PANEL_KEY_F1)p->view=GATEWAY_PANEL_NETWORK;
        else if(k==GATEWAY_PANEL_KEY_F3)p->lan2_cursor=(p->lan2_cursor+1U)%(p->network_page==2U?5U:9U);
        else if(k==GATEWAY_PANEL_KEY_F2||k==GATEWAY_PANEL_KEY_F4){
            unsigned long address,shift,value;unsigned int lan=p->network_page==1U?0U:1U,octet=p->lan2_cursor%4U;
            char *text=0;
            if(p->network_page<2U){
                if(p->lan2_cursor==8U)p->lan2_candidate.lan[lan].mode=p->lan2_candidate.lan[lan].mode==GATEWAY_LAN_STATIC?GATEWAY_LAN_DHCP_CLIENT:GATEWAY_LAN_STATIC;
                else text=p->lan2_cursor<4U?p->lan2_candidate.lan[lan].address:p->lan2_candidate.lan[lan].netmask;
            }else if(p->network_page==2U){
                if(!p->lan2_cursor)p->lan2_candidate.default_lan=(p->lan2_candidate.default_lan+(k==GATEWAY_PANEL_KEY_F4?1U:2U))%3U;
                else if(p->lan2_candidate.default_lan){text=p->lan2_candidate.lan[p->lan2_candidate.default_lan-1U].gateway;octet=p->lan2_cursor-1U;}
            }else if(p->lan2_cursor==8U){
                unsigned int source=p->lan2_candidate.automatic_dns?(p->lan2_candidate.dns_lan?p->lan2_candidate.dns_lan:p->lan2_candidate.default_lan):0U;
                source=(source+(k==GATEWAY_PANEL_KEY_F4?1U:2U))%3U;p->lan2_candidate.automatic_dns=source?1U:0U;p->lan2_candidate.dns_lan=source;
            }
            else text=p->lan2_candidate.dns[p->lan2_cursor/4U];
            if(text){
                if(!text[0])strcpy(text,"0.0.0.0");
                if(!gateway_ipv4_parse(text,&address)){
                    shift=(3U-octet)*8U;value=(address>>shift)&255UL;
                    value=(value+(k==GATEWAY_PANEL_KEY_F4?1UL:255UL))&255UL;
                    address=(address&~(255UL<<shift))|(value<<shift);gateway_ipv4_format(address,text);
                }
            }
        }else if(k==GATEWAY_PANEL_KEY_F5){
            unsigned int i;
            for(i=0;i<2U;++i)if(!strcmp(p->lan2_candidate.dns[i],"0.0.0.0"))memset(p->lan2_candidate.dns[i],0,16);
            p->local_validation_failed=gateway_network_settings_validate(&p->lan2_candidate,0)!=0;
            p->view=p->local_validation_failed?GATEWAY_PANEL_NETWORK_RESULT:GATEWAY_PANEL_NETWORK_CONFIRM;
        }return 1;
    }
    if(p->view==GATEWAY_PANEL_NETWORK_CONFIRM){
        if(k==GATEWAY_PANEL_KEY_F1)p->view=GATEWAY_PANEL_NETWORK_EDIT;
        else if(k==GATEWAY_PANEL_KEY_F3){p->local_validation_failed=gateway_application_revision_check(a,p->draft_token)||gateway_application_network_apply(a,&p->lan2_candidate)!=0;p->network_operation=p->local_validation_failed?0U:a->network_operation;p->view=GATEWAY_PANEL_NETWORK_RESULT;}
        return 1;
    }
    if(p->view==GATEWAY_PANEL_NETWORK_RESULT){
        if(k==GATEWAY_PANEL_KEY_F1){if(gateway_network_runtime_busy(&a->network)){if(p->network_operation==a->network_operation)(void)gateway_application_network_revert(a);}else p->view=GATEWAY_PANEL_NETWORK;}
        else if(k==GATEWAY_PANEL_KEY_F3&&p->network_operation==a->network_operation)(void)gateway_application_network_keep(a);
        return 1;
    }return 0;
}

static void render_display(gateway_panel_t*p,const gateway_application_t*a){
 clear_screen(&p->next);row(&p->next,0,p->view==GATEWAY_PANEL_DISPLAY?"Display:":"Backlight:");
 if(p->view==GATEWAY_PANEL_DISPLAY){row(&p->next,2,"> Backlight");footer(&p->next,"F1 Back F3 Open");return;}
 row(&p->next,1,p->backlight_choice?"  On":"> On");row(&p->next,2,p->backlight_choice?"> Off":"  Off");
 row(&p->next,3,a->coordinator.selected.settings.backlight_on?"Stored: On":"Stored: Off");
 row(&p->next,4,a->backlight.last_error?"Command failed":!a->backlight.command_known?"Command unknown":a->backlight.command_on?"Command: On OK":"Command: Off OK");
 if(p->local_validation_failed||a->configuration_transaction.state==GATEWAY_CONFIG_TX_FAILED)row(&p->next,5,"Save failed");
 else if(a->configuration_transaction.state==GATEWAY_CONFIG_TX_DURABILITY_UNCERTAIN)row(&p->next,5,"Durability ?");
 else if(a->configuration_transaction.state==GATEWAY_CONFIG_TX_PROMOTING)row(&p->next,5,"Saving...");
 row(&p->next,6,"F2/F4 On/Off");footer(&p->next,"F1 Back F3 Save");
}
static const char *web_error_label(unsigned int error){
 switch(error){case 0:return "";case 1:return "Security storage";case 2:return "LAN unavailable";case 60:return "Net data pending";case 61:return "Net data error";case 3:return "Set trusted time";case 4:return "IPC socket error";case 5:case 127:return "Cannot start Web";case 6:return "Startup timeout";case 9:return "Web exited";case 20:return "Entropy / TLS";case 21:return "Port 80/443 busy";case 22:return "Privilege error";case 23:return "IPC unavailable";case 24:return "TLS write failed";case 25:return "Invalid TLS file";case 26:return "TLS time range";case 27:return "TLS setup failed";case 28:return "TLS renewal";case 31:return "RNG starting";case 33:return "RNG provision req";case 34:return "RNG state invalid";case 35:return "RNG policy req";case 36:return "RNG CF missing";case 37:return "Legacy RNG quota";case 40:return "RNG owner busy";case 38:return "RNG service req";case 39:return "RNG disconnected";case 50:case 51:case 52:case 53:case 56:return "RNG channel error";case 55:return "RNG write failed";case 157:return "Cannot start RNG";case 54:return "RNG trial limit";default:return "Web failure";}
}
static void web_transaction_result(gateway_panel_t *p,const gateway_application_t *a)
{
 const gateway_configuration_transaction_t *t=&a->configuration_transaction;
 if(!p->web_pending)return;
 if(p->view<GATEWAY_PANEL_WEB||p->view>GATEWAY_PANEL_WEB_CERT){p->web_pending=0;return;}
 if(strcmp(p->web_transaction_token,t->accepted_token)){
  p->web_pending=0;p->local_validation_failed=1;return;
 }
 if(t->state>=GATEWAY_CONFIG_TX_ACTIVATING&&t->state<=GATEWAY_CONFIG_TX_ROLLING_BACK)return;
 p->web_pending=0;
 p->web_save_failed=t->state!=GATEWAY_CONFIG_TX_SUCCEEDED&&t->state!=GATEWAY_CONFIG_TX_UNCHANGED;
 if(t->state==GATEWAY_CONFIG_TX_DURABILITY_UNCERTAIN||t->rollback_failed||
    gateway_application_revision_check(a,t->result_token)){
  p->local_validation_failed=1;return;
 }
 strcpy(p->draft_token,t->result_token);
}
static void render_web(gateway_panel_t*p,const gateway_application_t*a){
 char b[40];clear_screen(&p->next);row(&p->next,0,"Web Server:");
 if(p->view==GATEWAY_PANEL_WEB_RECOVER){row(&p->next,2,"Reset Web access?");row(&p->next,4,"Ports unchanged");footer(&p->next,"F1 No F3 Yes");return;}
 if(p->view==GATEWAY_PANEL_WEB_CODE){row(&p->next,2,"One-time code:");row(&p->next,3,a->web.code[0]?a->web.code:"Expired / used");snprintf(b,sizeof(b),"Expires %us",a->web.seconds_left);row(&p->next,4,b);footer(&p->next,"F1 Cancel / Back");return;}
 if(p->view==GATEWAY_PANEL_WEB_CERT){unsigned int i;row(&p->next,1,"SHA-256 certificate");for(i=0;i<4;i++){snprintf(b,sizeof(b),"%.16s",a->web.fingerprint+i*16);row(&p->next,i+2,b);}footer(&p->next,"F1 Back");return;}
 if(p->view==GATEWAY_PANEL_WEB_URL){row(&p->next,2,a->coordinator.selected.settings.web_protocol?"LAN1 HTTPS:":"LAN1 HTTP:");row(&p->next,3,a->web.urls[0][0]?a->web.urls[0]+(a->coordinator.selected.settings.web_protocol?8:7):"Unavailable");row(&p->next,4,a->coordinator.selected.settings.web_protocol?"LAN2 HTTPS:":"LAN2 HTTP:");row(&p->next,5,a->web.urls[1][0]?a->web.urls[1]+(a->coordinator.selected.settings.web_protocol?8:7):"Unavailable");footer(&p->next,a->coordinator.selected.settings.web_protocol?"F1 Back F5 Cert":"HTTP / No encryption");return;}
 snprintf(b,sizeof(b),"%c %s",p->selected==0?'>':' ',a->coordinator.selected.settings.web_enabled?"Disable":"Enable");row(&p->next,1,b);
 snprintf(b,sizeof(b),"%c Iface %s",p->selected==1?'>':' ',a->coordinator.selected.settings.web_interface==0?"LAN1":a->coordinator.selected.settings.web_interface==1?"LAN2":"Both");row(&p->next,2,b);
 snprintf(b,sizeof(b),"%c New code",p->selected==2?'>':' ');row(&p->next,3,b);snprintf(b,sizeof(b),"%c Recover access",p->selected==3?'>':' ');row(&p->next,4,b);
 snprintf(b,sizeof(b),"%c %s",p->selected==4?'>':' ',a->coordinator.selected.settings.web_protocol?"HTTPS not rec.":"HTTP cleartext");row(&p->next,5,b);row(&p->next,6,p->web_pending?"Saving...":p->local_validation_failed?"Config conflict":p->web_save_failed?"Save failed":a->web.error?web_error_label(a->web.error):a->web.state==2?"Running":a->web.state==1?"Starting / Stop":a->web.state==3?"Failed":"Stopped");footer(&p->next,"F3 Set F5 URLs");return;
}
static int web_key(gateway_panel_t*p,gateway_application_t*a,unsigned int k){
 gateway_persistent_config_t c;
 if(p->view!=GATEWAY_PANEL_WEB&&p->view!=GATEWAY_PANEL_WEB_CODE&&p->view!=GATEWAY_PANEL_WEB_RECOVER&&p->view!=GATEWAY_PANEL_WEB_URL&&p->view!=GATEWAY_PANEL_WEB_CERT)return 0;
 if(p->view==GATEWAY_PANEL_WEB_URL&&k==GATEWAY_PANEL_KEY_F5&&a->coordinator.selected.settings.web_protocol){p->view=GATEWAY_PANEL_WEB_CERT;return 1;}
 if(p->web_remote&&!p->web_prompt_ready)return 1;
 if(p->web_remote&&k==GATEWAY_PANEL_KEY_F1){if(a->web.local)a->web.local(a->web.context,0);p->view=p->web_return;p->web_remote=0;return 1;}
 if(k==GATEWAY_PANEL_KEY_F1){if(p->view==GATEWAY_PANEL_WEB_CODE&&a->web.code[0]&&a->web.local)a->web.local(a->web.context,0);p->view=p->view==GATEWAY_PANEL_WEB?GATEWAY_PANEL_SYSTEM:GATEWAY_PANEL_WEB;return 1;}
 if(p->view==GATEWAY_PANEL_WEB_RECOVER){if(k==GATEWAY_PANEL_KEY_F3&&a->web.local&&!a->web.local(a->web.context,2)){p->view=GATEWAY_PANEL_WEB_CODE;p->web_remote=0;}return 1;}
 if(p->view!=GATEWAY_PANEL_WEB)return 1;
 if(k==GATEWAY_PANEL_KEY_F2)p->selected=(p->selected+4U)%5U;else if(k==GATEWAY_PANEL_KEY_F4)p->selected=(p->selected+1U)%5U;
 else if(k==GATEWAY_PANEL_KEY_F5)p->view=GATEWAY_PANEL_WEB_URL;
 else if(k==GATEWAY_PANEL_KEY_F3){
  if(p->selected==3){p->view=GATEWAY_PANEL_WEB_RECOVER;return 1;}
  if(p->selected==2){if(a->coordinator.selected.settings.web_enabled&&a->web.local&&!a->web.local(a->web.context,1))p->view=GATEWAY_PANEL_WEB_CODE;return 1;}
  if(p->web_pending)return 1;
  c=a->coordinator.selected;c.schema_version=3;
  if(p->selected==0)c.settings.web_enabled=!c.settings.web_enabled;else if(p->selected==1)c.settings.web_interface=(c.settings.web_interface+1U)%3U;else c.settings.web_protocol=!c.settings.web_protocol;
  p->local_validation_failed=gateway_application_revision_check(a,p->draft_token)!=0;
  if(!p->local_validation_failed&&gateway_application_request_configuration(a,&c)!=0){p->web_save_failed=1;return 1;}
  if(!p->local_validation_failed){
   p->web_save_failed=0;
   if(a->configuration_transaction.accepted_token[0]){
    strcpy(p->web_transaction_token,a->configuration_transaction.accepted_token);p->web_pending=1;
   }
  }
  if(!p->local_validation_failed&&p->selected==0&&c.settings.web_enabled&&a->web.local&&!a->web.local(a->web.context,1)&&a->web.code[0])p->view=GATEWAY_PANEL_WEB_CODE;
 }
 return 1;
}
static void make_screen(gateway_panel_t*p,const gateway_diagnostic_snapshot_t*d,gateway_application_t*a){switch(p->view){case GATEWAY_PANEL_STOPPING:case GATEWAY_PANEL_STOP_FAILED:case GATEWAY_PANEL_STOPPED:render_stop(p);break;case GATEWAY_PANEL_WEB:case GATEWAY_PANEL_WEB_CODE:case GATEWAY_PANEL_WEB_RECOVER:case GATEWAY_PANEL_WEB_URL:case GATEWAY_PANEL_WEB_CERT:render_web(p,a);break;case GATEWAY_PANEL_DISPLAY:case GATEWAY_PANEL_BACKLIGHT:render_display(p,a);break;case GATEWAY_PANEL_NETWORK:case GATEWAY_PANEL_NETWORK_EDIT:case GATEWAY_PANEL_NETWORK_CONFIRM:case GATEWAY_PANEL_NETWORK_RESULT:render_network(p,a);break;case GATEWAY_PANEL_STARTUP:render_startup(p,d);break;case GATEWAY_PANEL_HOME:render_home(p,d,a);break;case GATEWAY_PANEL_HELP:render_help(p);break;case GATEWAY_PANEL_MENU:render_menu(p);break;case GATEWAY_PANEL_STATUS:render_status(p,d);break;case GATEWAY_PANEL_PORTS:render_ports(p,d);break;case GATEWAY_PANEL_PORT_DETAIL:render_port(p,d);break;case GATEWAY_PANEL_CONFIG_PORTS:render_config_ports(p);break;case GATEWAY_PANEL_CONFIG_FIELDS:render_fields(p);break;case GATEWAY_PANEL_EDIT_FIELD:render_edit(p);break;case GATEWAY_PANEL_APPLY_CONFIRM:render_confirm(p);break;case GATEWAY_PANEL_APPLY_RESULT:render_result(p,a);break;case GATEWAY_PANEL_DIAGNOSTICS:render_diagnostics(p,d);break;case GATEWAY_PANEL_STARTUP_EVENTS:render_events(p,d);break;case GATEWAY_PANEL_SYSTEM:render_system(p,d,a);break;case GATEWAY_PANEL_TIME_EDIT:render_time_edit(p);break;case GATEWAY_PANEL_TIME_CONFIRM:render_time_confirm(p);break;case GATEWAY_PANEL_TIME_RESULT:render_time_result(p);break;case GATEWAY_PANEL_NTP_SETTINGS:render_ntp_settings(p,a);break;case GATEWAY_PANEL_NTP_EDIT:render_ntp_edit(p);break;case GATEWAY_PANEL_NTP_CONFIRM:render_ntp_confirm(p);break;case GATEWAY_PANEL_NTP_RESULT:render_ntp_result(p,a);break;case GATEWAY_PANEL_SHUTDOWN_CONFIRM:render_shutdown(p);break;default:render_about(p);break;}}
static unsigned int baud_index(unsigned long b){unsigned int i;for(i=0;i<sizeof(baud_values)/sizeof(baud_values[0]);++i)if(baud_values[i]==b)return i;return 0;}
static void change_field(gateway_panel_t*p,int direction){gateway_port_config_t*c=&p->candidate.ports[p->port_index];unsigned int i;int octet[4];switch(p->field_index){case 0:c->enabled=!c->enabled;break;case 1:c->uart_index=(c->uart_index+(direction>0?1U:7U))%8U;break;case 2:c->mode=(serial_mode_t)(((unsigned)c->mode+(direction>0?1U:3U))%4U);break;case 3:i=baud_index(c->baud);i=(i+(direction>0?1U:(unsigned)(sizeof(baud_values)/sizeof(baud_values[0])-1U)))%(unsigned)(sizeof(baud_values)/sizeof(baud_values[0]));c->baud=baud_values[i];break;case 4:if(c->special_baud==0UL)c->special_baud=115200UL;else if(direction>0&&c->special_baud<999900UL)c->special_baud+=100UL;else if(direction<0&&c->special_baud>100UL)c->special_baud-=100UL;break;case 5:if(direction>0)c->data_bits=c->data_bits>=8U?5U:c->data_bits+1U;else c->data_bits=c->data_bits<=5U?8U:c->data_bits-1U;break;case 6:c->parity=(parity_mode_t)(((unsigned)c->parity+(direction>0?1U:2U))%3U);break;case 7:c->stop_bits=c->stop_bits==1U?2U:1U;break;case 8:c->transport=(transport_type_t)(((unsigned)c->transport+(direction>0?1U:4U))%5U);break;case 9:if(sscanf(c->bind_address,"%d.%d.%d.%d",&octet[0],&octet[1],&octet[2],&octet[3])!=4)octet[0]=octet[1]=octet[2]=octet[3]=0;octet[p->edit_octet]=(octet[p->edit_octet]+(direction>0?1:255))%256;snprintf(c->bind_address,sizeof(c->bind_address),"%d.%d.%d.%d",octet[0],octet[1],octet[2],octet[3]);break;case 10:if(direction>0)c->endpoint_port=c->endpoint_port>=65535U?1U:c->endpoint_port+1U;else c->endpoint_port=c->endpoint_port<=1U?65535U:c->endpoint_port-1U;break;default:break;}}
static int network_changed(const gateway_panel_t*p,const gateway_application_t*a){unsigned int i;for(i=0;i<8U;++i)if(strcmp(p->candidate.ports[i].bind_address,a->coordinator.selected.ports[i].bind_address)!=0||p->candidate.ports[i].endpoint_port!=a->coordinator.selected.ports[i].endpoint_port)return 1;return 0;}
static void load_candidate(gateway_panel_t*p,const gateway_application_t*a){gateway_application_revision(a,p->draft_token);p->candidate=a->coordinator.selected;p->candidate_loaded=1;p->local_validation_failed=0;}
static void select_menu(gateway_panel_t*p,const gateway_application_t*a){switch(p->selected){case 0:p->view=GATEWAY_PANEL_STATUS;break;case 1:p->view=GATEWAY_PANEL_PORTS;p->selected=p->scroll=0;break;case 2:p->view=GATEWAY_PANEL_CONFIG_PORTS;p->selected=p->scroll=0;load_candidate(p,a);break;case 3:p->view=GATEWAY_PANEL_DIAGNOSTICS;break;case 4:p->system_page=0U;p->have_displayed_time=0U;p->view=GATEWAY_PANEL_SYSTEM;break;case 5:p->view=GATEWAY_PANEL_SHUTDOWN_CONFIRM;break;default:p->view=GATEWAY_PANEL_ABOUT;break;}}
static void accept_field(gateway_panel_t*p,const gateway_application_t*a){gateway_port_config_t*c=&p->candidate.ports[p->port_index];if(memcmp(c,&p->edit_original,sizeof(*c))!=0)c->revision=a->coordinator.selected.ports[p->port_index].revision+1U;p->view=GATEWAY_PANEL_CONFIG_FIELDS;}
static void ntp_change_char(gateway_panel_t*p,int direction){static const char chars[]="0123456789.abcdefghijklmnopqrstuvwxyz-";char*c=&p->candidate.settings.ntp_server[p->ntp_cursor];const char*q;unsigned int i,n=(unsigned int)(sizeof(chars)-1U);if(*c=='\0')i=n;else{q=strchr(chars,*c);i=q?(unsigned int)(q-chars):0U;}if(direction>0)i=i>=n?0U:i+1U;else i=i==0U?n:i-1U;if(i==n){*c='\0';}else{*c=chars[i];if(p->ntp_cursor+1U<GATEWAY_NTP_SERVER_MAX&&p->candidate.settings.ntp_server[p->ntp_cursor+1U]=='\0')p->candidate.settings.ntp_server[p->ntp_cursor+1U]='\0';}}
static void ntp_change_interval(gateway_panel_t*p,int direction)
{unsigned int h=p->candidate.settings.ntp_interval_hours;p->candidate.settings.ntp_interval_hours=direction>0?(h==1U?6U:h==6U?24U:1U):(h==24U?6U:h==6U?1U:24U);}
static int ntp_key(gateway_panel_t*p,gateway_application_t*a,unsigned int k){if(p->view==GATEWAY_PANEL_NTP_SETTINGS){if(k==GATEWAY_PANEL_KEY_F1){p->system_page=1;p->view=GATEWAY_PANEL_SYSTEM;}else if(k==GATEWAY_PANEL_KEY_F4){(void)gateway_application_ntp_test(a,!a->time.ntp_test_active);}else if(k==GATEWAY_PANEL_KEY_F5){load_candidate(p,a);p->settings_original=p->candidate.settings;p->ntp_field=0;p->ntp_cursor=0;p->view=GATEWAY_PANEL_NTP_EDIT;}return 1;}if(p->view==GATEWAY_PANEL_NTP_EDIT){if(k==GATEWAY_PANEL_KEY_F1){p->candidate.settings=p->settings_original;p->view=GATEWAY_PANEL_NTP_SETTINGS;}else if(k==GATEWAY_PANEL_KEY_F2){if(p->ntp_field==0U)p->candidate.settings.ntp_enabled=!p->candidate.settings.ntp_enabled;else if(p->ntp_field==2U)ntp_change_interval(p,-1);else ntp_change_char(p,-1);}else if(k==GATEWAY_PANEL_KEY_F4){if(p->ntp_field==0U)p->candidate.settings.ntp_enabled=!p->candidate.settings.ntp_enabled;else if(p->ntp_field==2U)ntp_change_interval(p,1);else ntp_change_char(p,1);}else if(k==GATEWAY_PANEL_KEY_F3){if(p->ntp_field==2U)p->ntp_field=0U;else if(p->ntp_field==0U)p->ntp_field=1U;else if(p->ntp_cursor+1U<GATEWAY_NTP_SERVER_MAX-1U)++p->ntp_cursor;}else if(k==GATEWAY_PANEL_KEY_F5){if(p->ntp_field!=2U){p->ntp_field=2U;return 1;}p->view=gateway_product_settings_validate(&p->candidate.settings)==0?GATEWAY_PANEL_NTP_CONFIRM:GATEWAY_PANEL_NTP_RESULT;p->local_validation_failed=gateway_product_settings_validate(&p->candidate.settings)!=0;}return 1;}if(p->view==GATEWAY_PANEL_NTP_CONFIRM){if(k==GATEWAY_PANEL_KEY_F1)p->view=GATEWAY_PANEL_NTP_EDIT;else if(k==GATEWAY_PANEL_KEY_F3){if(gateway_application_revision_check(a,p->draft_token)){p->local_validation_failed=1;}else gateway_application_request_configuration(a,&p->candidate);p->view=GATEWAY_PANEL_NTP_RESULT;}return 1;}if(p->view==GATEWAY_PANEL_NTP_RESULT){if(k==GATEWAY_PANEL_KEY_F1)p->view=GATEWAY_PANEL_NTP_SETTINGS;return 1;}return 0;}
static int special_key(gateway_panel_t*p,gateway_application_t*a,unsigned int k){
 if(p->view==GATEWAY_PANEL_DISPLAY){if(k==GATEWAY_PANEL_KEY_F1)p->view=GATEWAY_PANEL_SYSTEM;else if(k==GATEWAY_PANEL_KEY_F3){p->backlight_choice=!a->coordinator.selected.settings.backlight_on;p->local_validation_failed=0;p->view=GATEWAY_PANEL_BACKLIGHT;}return 1;}
 if(p->view==GATEWAY_PANEL_BACKLIGHT){if(k==GATEWAY_PANEL_KEY_F1)p->view=GATEWAY_PANEL_DISPLAY;else if(k==GATEWAY_PANEL_KEY_F2||k==GATEWAY_PANEL_KEY_F4)p->backlight_choice=!p->backlight_choice;else if(k==GATEWAY_PANEL_KEY_F3)p->local_validation_failed=gateway_application_revision_check(a,p->draft_token)||gateway_application_backlight_set(a,!p->backlight_choice)!=0;return 1;}
if(p->view==GATEWAY_PANEL_SYSTEM){if(k==GATEWAY_PANEL_KEY_F1){p->view=GATEWAY_PANEL_MENU;return 1;}if(k==GATEWAY_PANEL_KEY_F2){p->system_page=p->system_page?p->system_page-1U:3U;return 1;}if(k==GATEWAY_PANEL_KEY_F4){p->system_page=(p->system_page+1U)%4U;return 1;}if(k==GATEWAY_PANEL_KEY_F3&&p->system_page==3U){p->view=GATEWAY_PANEL_WEB;p->selected=0;return 1;}if(k==GATEWAY_PANEL_KEY_F3&&p->system_page==2U){p->view=GATEWAY_PANEL_DISPLAY;return 1;}if(k==GATEWAY_PANEL_KEY_F5&&p->system_page==1U){p->view=GATEWAY_PANEL_NTP_SETTINGS;return 1;}}return ntp_key(p,a,k);}
static void key(gateway_panel_t*p,gateway_application_t*a,unsigned int k,const gateway_diagnostic_snapshot_t*d){if(p->view==GATEWAY_PANEL_STOP_FAILED){if(k==GATEWAY_PANEL_KEY_F1){p->view=GATEWAY_PANEL_MENU;p->selected=0;}return;}
 if(p->view==GATEWAY_PANEL_STOPPING||p->view==GATEWAY_PANEL_STOPPED)return;
 if(gateway_application_finished(a)&&p->shutdown_error&&p->view==GATEWAY_PANEL_MENU&&k==GATEWAY_PANEL_KEY_F3&&(p->selected==2U||p->selected==4U||p->selected==5U)){p->view=GATEWAY_PANEL_STOP_FAILED;return;}
 if(p->view==GATEWAY_PANEL_STARTUP){return;}if(p->view==GATEWAY_PANEL_HOME){if(k==GATEWAY_PANEL_KEY_F1){p->view=GATEWAY_PANEL_HELP;p->help_page=0;}else if(k==GATEWAY_PANEL_KEY_F3){p->view=GATEWAY_PANEL_MENU;p->selected=p->scroll=0;}return;}if(p->view==GATEWAY_PANEL_HELP){if(k==GATEWAY_PANEL_KEY_F1)p->view=GATEWAY_PANEL_HOME;else if(k==GATEWAY_PANEL_KEY_F2)p->help_page=0;else if(k==GATEWAY_PANEL_KEY_F4)p->help_page=1;return;}if(p->view==GATEWAY_PANEL_SYSTEM){if(k==GATEWAY_PANEL_KEY_F1)p->view=GATEWAY_PANEL_MENU;else if(k==GATEWAY_PANEL_KEY_F2)p->system_page=0;else if(k==GATEWAY_PANEL_KEY_F4)p->system_page=1;else if(k==GATEWAY_PANEL_KEY_F5&&p->system_page==0U&&gateway_application_system_time_get(a,&p->time_candidate)==GATEWAY_SYSTEM_TIME_OK){p->time_field=0;p->view=GATEWAY_PANEL_TIME_EDIT;}return;}if(p->view==GATEWAY_PANEL_TIME_EDIT){if(k==GATEWAY_PANEL_KEY_F1)p->view=GATEWAY_PANEL_SYSTEM;else if(k==GATEWAY_PANEL_KEY_F2)change_time_field(p,-1);else if(k==GATEWAY_PANEL_KEY_F4)change_time_field(p,1);else if(k==GATEWAY_PANEL_KEY_F3){if(p->time_field<5U)++p->time_field;else{p->time_result=gateway_system_time_validate(&p->time_candidate)==0?GATEWAY_SYSTEM_TIME_OK:GATEWAY_SYSTEM_TIME_INVALID;p->view=p->time_result==GATEWAY_SYSTEM_TIME_OK?GATEWAY_PANEL_TIME_CONFIRM:GATEWAY_PANEL_TIME_RESULT;}}return;}if(p->view==GATEWAY_PANEL_TIME_CONFIRM){if(k==GATEWAY_PANEL_KEY_F1)p->view=GATEWAY_PANEL_TIME_EDIT;else if(k==GATEWAY_PANEL_KEY_F3){p->time_result=gateway_application_revision_check(a,p->draft_token)?GATEWAY_SYSTEM_TIME_INVALID:gateway_application_system_time_set(a,&p->time_candidate);p->have_displayed_time=0;p->view=GATEWAY_PANEL_TIME_RESULT;}return;}if(p->view==GATEWAY_PANEL_TIME_RESULT){if(k==GATEWAY_PANEL_KEY_F1){p->system_page=0;p->view=GATEWAY_PANEL_SYSTEM;}return;}if(p->view==GATEWAY_PANEL_MENU){if(k==GATEWAY_PANEL_KEY_F1)p->view=GATEWAY_PANEL_HOME;else if(k==GATEWAY_PANEL_KEY_F2)p->selected=p->selected? p->selected-1U:6U;else if(k==GATEWAY_PANEL_KEY_F4)p->selected=(p->selected+1U)%7U;else if(k==GATEWAY_PANEL_KEY_F3)select_menu(p,a);return;}if(k==GATEWAY_PANEL_KEY_F1){if(p->view==GATEWAY_PANEL_EDIT_FIELD){p->candidate.ports[p->port_index]=p->edit_original;p->view=GATEWAY_PANEL_CONFIG_FIELDS;}else if(p->view==GATEWAY_PANEL_PORT_DETAIL)p->view=GATEWAY_PANEL_PORTS;else if(p->view==GATEWAY_PANEL_CONFIG_FIELDS)p->view=GATEWAY_PANEL_CONFIG_PORTS;else if(p->view==GATEWAY_PANEL_APPLY_CONFIRM)p->view=GATEWAY_PANEL_CONFIG_FIELDS;else if(p->view==GATEWAY_PANEL_STARTUP_EVENTS)p->view=GATEWAY_PANEL_DIAGNOSTICS;else p->view=GATEWAY_PANEL_MENU;return;}if(p->view==GATEWAY_PANEL_PORTS){if(k==GATEWAY_PANEL_KEY_F2)p->selected=p->selected?p->selected-1U:7U;else if(k==GATEWAY_PANEL_KEY_F4)p->selected=(p->selected+1U)%8U;else if(k==GATEWAY_PANEL_KEY_F3){p->port_index=p->selected;p->detail_page=0;p->view=GATEWAY_PANEL_PORT_DETAIL;}return;}if(p->view==GATEWAY_PANEL_PORT_DETAIL){if(k==GATEWAY_PANEL_KEY_F2)p->detail_page=p->detail_page?p->detail_page-1U:3U;else if(k==GATEWAY_PANEL_KEY_F4)p->detail_page=(p->detail_page+1U)%4U;return;}if(p->view==GATEWAY_PANEL_CONFIG_PORTS){if(k==GATEWAY_PANEL_KEY_F2)p->selected=p->selected?p->selected-1U:7U;else if(k==GATEWAY_PANEL_KEY_F4)p->selected=(p->selected+1U)%8U;else if(k==GATEWAY_PANEL_KEY_F3){p->port_index=p->selected;p->field_index=0;p->view=GATEWAY_PANEL_CONFIG_FIELDS;}return;}if(p->view==GATEWAY_PANEL_CONFIG_FIELDS){if(k==GATEWAY_PANEL_KEY_F2)p->field_index=p->field_index?p->field_index-1U:GATEWAY_PANEL_FIELD_COUNT-1U;else if(k==GATEWAY_PANEL_KEY_F4)p->field_index=(p->field_index+1U)%GATEWAY_PANEL_FIELD_COUNT;else if((k==GATEWAY_PANEL_KEY_F5)&&p->field_index<11U&&p->field_index!=1U){p->edit_original=p->candidate.ports[p->port_index];p->edit_octet=0;p->view=GATEWAY_PANEL_EDIT_FIELD;}else if(k==GATEWAY_PANEL_KEY_F3&&p->field_index==11U){gateway_error_t e[8];unsigned int i;p->local_validation_failed=0;if(gateway_configuration_validate(p->candidate.ports,e)!=0){for(i=0;i<8U;++i)if(e[i]!=GATEWAY_ERROR_NONE){p->local_validation_failed=(unsigned int)e[i];break;}p->view=GATEWAY_PANEL_APPLY_RESULT;}else{p->network_change=(unsigned int)network_changed(p,a);p->view=GATEWAY_PANEL_APPLY_CONFIRM;}}else if(k==GATEWAY_PANEL_KEY_F3&&p->field_index==12U){load_candidate(p,a);p->view=GATEWAY_PANEL_CONFIG_PORTS;}return;}if(p->view==GATEWAY_PANEL_EDIT_FIELD){if(k==GATEWAY_PANEL_KEY_F2)change_field(p,-1);else if(k==GATEWAY_PANEL_KEY_F4)change_field(p,1);else if(k==GATEWAY_PANEL_KEY_F3)accept_field(p,a);else if(k==GATEWAY_PANEL_KEY_F5&&p->field_index==9U)p->edit_octet=(p->edit_octet+1U)%4U;else if(k==GATEWAY_PANEL_KEY_F5&&p->field_index==4U)p->candidate.ports[p->port_index].special_baud_enabled=!p->candidate.ports[p->port_index].special_baud_enabled;return;}if(p->view==GATEWAY_PANEL_APPLY_CONFIRM&&k==GATEWAY_PANEL_KEY_F3){if(gateway_application_revision_check(a,p->draft_token)){p->local_validation_failed=1;}else gateway_application_request_configuration(a,&p->candidate);p->view=GATEWAY_PANEL_APPLY_RESULT;return;}if(p->view==GATEWAY_PANEL_DIAGNOSTICS&&k==GATEWAY_PANEL_KEY_F3){p->startup_event_page=0;p->view=GATEWAY_PANEL_STARTUP_EVENTS;return;}if(p->view==GATEWAY_PANEL_STARTUP_EVENTS){if(k==GATEWAY_PANEL_KEY_F2)p->startup_event_page=p->startup_event_page?p->startup_event_page-1U:(d->startup_event_count?d->startup_event_count-1U:0U);else if(k==GATEWAY_PANEL_KEY_F4&&d->startup_event_count)p->startup_event_page=(p->startup_event_page+1U)%d->startup_event_count;return;}if(p->view==GATEWAY_PANEL_SHUTDOWN_CONFIRM&&k==GATEWAY_PANEL_KEY_F3){if(!gateway_application_revision_check(a,p->draft_token)){p->shutdown_error=0;gateway_application_request_stop(a);p->view=GATEWAY_PANEL_STOPPING;}else gateway_panel_stop_result(p,1U);return;}}

int gateway_panel_init(gateway_panel_t*p,const gateway_panel_ops_t*o,void*x){int r;if(!p||!o||!o->display_open||!o->keypad_open||!o->display_draw||!o->keypad_poll||!o->display_close||!o->keypad_close)return-1;memset(p,0,sizeof(*p));p->ops=o;p->ops_context=x;p->view=GATEWAY_PANEL_STARTUP;p->health.view=p->view;p->dirty=1;p->initialized=1;r=o->display_open(x);if(r==0)p->health.display_available=1;else{p->health.last_display_error=r;increment(&p->health.display_errors);}r=o->keypad_open(x);if(r==0)p->health.keypad_available=1;else{p->health.last_keypad_error=r;increment(&p->health.keypad_errors);}return 0;}
/* Drain queued PRESS records before accepting a newly displayed prompt.
 * This is an event-queue barrier, not a claim about held/released key state. */
static int web_drain(gateway_panel_t *p){unsigned int i,key;int r;for(i=0;i<64;i++){r=p->ops->keypad_poll(p->ops_context,&key);if(r<=0)return r;}return -1;}
static void web_prompt(gateway_panel_t *p,gateway_application_t *a){
 int safe=(p->view==GATEWAY_PANEL_HOME||p->view==GATEWAY_PANEL_MENU||p->view==GATEWAY_PANEL_SYSTEM||p->view==GATEWAY_PANEL_WEB)&&p->health.keypad_available&&p->health.display_available;
 if(a->network.status.state>=GATEWAY_NETWORK_APPLYING&&a->network.status.state<=GATEWAY_NETWORK_ROLLBACK_BINDINGS)safe=0;
 if(a->configuration_transaction.state>=GATEWAY_CONFIG_TX_ACTIVATING&&a->configuration_transaction.state<=GATEWAY_CONFIG_TX_ROLLING_BACK)safe=0;
 a->web.recovery_available=safe;
 if(p->web_remote&&!a->web.recovery_prompt){p->view=p->web_return;p->web_remote=0;}
 if(a->web.recovery_prompt==1){if(!safe||web_drain(p)){a->web.recovery_prompt=0;return;}p->web_return=p->view;p->view=GATEWAY_PANEL_WEB_RECOVER;p->web_remote=1;p->web_prompt_ready=0;a->web.recovery_prompt=2;}
 if(a->web.show_code&&safe){a->web.show_code=0;p->view=GATEWAY_PANEL_WEB_CODE;}
}
static unsigned int repeat_context(const gateway_panel_t*p){
 unsigned int field=0;
 if(p->view==GATEWAY_PANEL_EDIT_FIELD)field=(p->field_index<<8)|p->edit_octet;
 else if(p->view==GATEWAY_PANEL_TIME_EDIT)field=p->time_field;
 else if(p->view==GATEWAY_PANEL_NTP_EDIT)field=(p->ntp_field<<8)|p->ntp_cursor;
 else if(p->view==GATEWAY_PANEL_NETWORK_EDIT)field=(p->network_page<<8)|p->lan2_cursor;
 return ((unsigned int)p->view<<24)|field;
}
static int repeat_allowed(const gateway_panel_t*p){
 switch(p->view){
 case GATEWAY_PANEL_HELP:case GATEWAY_PANEL_SYSTEM:
 case GATEWAY_PANEL_MENU:case GATEWAY_PANEL_PORTS:case GATEWAY_PANEL_PORT_DETAIL:
 case GATEWAY_PANEL_CONFIG_PORTS:case GATEWAY_PANEL_CONFIG_FIELDS:case GATEWAY_PANEL_EDIT_FIELD:
 case GATEWAY_PANEL_STARTUP_EVENTS:case GATEWAY_PANEL_TIME_EDIT:case GATEWAY_PANEL_NTP_EDIT:
 case GATEWAY_PANEL_NETWORK:case GATEWAY_PANEL_NETWORK_EDIT:case GATEWAY_PANEL_WEB:
 case GATEWAY_PANEL_BACKLIGHT:return 1;
 default:return 0;
 }
}
static int repeat_numeric(const gateway_panel_t*p){
 if(p->view==GATEWAY_PANEL_EDIT_FIELD)return p->field_index==4U||p->field_index==9U||p->field_index==10U;
 if(p->view==GATEWAY_PANEL_TIME_EDIT)return 1;
 if(p->view==GATEWAY_PANEL_NTP_EDIT)return p->ntp_field==2U;
 if(p->view==GATEWAY_PANEL_NETWORK_EDIT)return p->network_page==2U?p->lan2_cursor>0U:p->lan2_cursor<8U;
 return 0;
}
static int panel_poll(gateway_panel_t*p,core_tick_t now,unsigned int*raw){
 unsigned int mask=0,direction,previous;int state,event=p->ops->keypad_poll(p->ops_context,raw),step;
 p->repeat_count=1;
 if(event<0||!p->key_state)return event;
 state=p->key_state(p->ops_context,&mask);
 if(state!=1){key_repeat_init(&p->repeat);p->repeat_armed=0;return event;}
 direction=(mask&~10U)?3U:((mask&2U)?1U:0U)|((mask&8U)?2U:0U);
 previous=p->repeat.mask;
 step=key_repeat_scaled(&p->repeat,now,direction,repeat_context(p),repeat_allowed(p),repeat_numeric(p));
 if(!direction||direction!=previous||p->repeat.blocked)p->repeat_armed=0;
 /* The vendor PRESS queue owns the first step, including taps entirely
  * between samples. Physical state supplies repeats only after that PRESS.
  * Never infer a held key from queue silence. */
 if(event>0){
  if(!p->repeat.blocked&&repeat_allowed(p)&&
     ((*raw==GATEWAY_PANEL_KEY_F2&&direction==1U)||
      (*raw==GATEWAY_PANEL_KEY_F4&&direction==2U)))p->repeat_armed=1;
  return event;
 }
 if(step&&p->repeat_armed){p->repeat_count=(unsigned int)(step<0?-step:step);/* Keep the physical key identity separate from signed step magnitude. */ *raw=direction==1U?GATEWAY_PANEL_KEY_F2:GATEWAY_PANEL_KEY_F4;return 1;}
 return 0;
}

void gateway_panel_step(gateway_panel_t*p,gateway_application_t*a){gateway_diagnostic_snapshot_t d;unsigned int raw=0,repeats;int r;gateway_panel_view_t old_view;if(!p||!a||!p->initialized)return;if(gateway_diagnostics_capture(a,&d)!=0)return;if(p->view==GATEWAY_PANEL_STARTUP&&(terminal(d.application.coordinator.state)||a->fatal_seen))p->view=GATEWAY_PANEL_HOME;if(!a->run_control.stop_requested){web_transaction_result(p,a);web_prompt(p,a);}if(p->health.keypad_available){r=panel_poll(p,d.application.coordinator.uptime,&raw);if(r<0){p->health.keypad_available=0;p->health.last_keypad_error=r;increment(&p->health.keypad_errors);p->ops->keypad_close(p->ops_context);}else if(r>0&&raw<=4U){old_view=p->view;increment(&p->health.key_event_count);for(repeats=0;repeats<p->repeat_count;++repeats){if(a->run_control.stop_requested||(!web_key(p,a,raw)&&!network_key(p,a,raw)&&!special_key(p,a,raw)))key(p,a,raw,&d);if(p->view!=old_view)break;}
if(p->view!=old_view&&(p->view==GATEWAY_PANEL_BACKLIGHT||p->view==GATEWAY_PANEL_WEB||p->view==GATEWAY_PANEL_SHUTDOWN_CONFIRM||(p->view==GATEWAY_PANEL_TIME_EDIT&&old_view==GATEWAY_PANEL_SYSTEM)||(p->view==GATEWAY_PANEL_NETWORK_EDIT&&old_view==GATEWAY_PANEL_NETWORK)))gateway_application_revision(a,p->draft_token);
p->dirty=1;}}
 if(a->run_control.stop_requested&&!p->shutdown_error&&p->view!=GATEWAY_PANEL_STOPPED)p->view=GATEWAY_PANEL_STOPPING;
 make_screen(p,&d,a);if(!p->have_displayed||memcmp(&p->next,&p->displayed,sizeof(p->next))!=0)p->dirty=1;if(p->dirty&&p->health.display_available){r=p->ops->display_draw(p->ops_context,&p->next);if(r==0){p->displayed=p->next;p->have_displayed=1;p->dirty=0;increment(&p->health.render_count);if(p->web_remote&&!p->web_prompt_ready){if(web_drain(p)){a->web.recovery_prompt=0;}else p->web_prompt_ready=1;}}else{p->health.display_available=0;p->health.last_display_error=r;increment(&p->health.display_errors);p->ops->display_close(p->ops_context);}}else if(p->health.display_available)increment(&p->health.suppressed_render_count);p->health.view=p->view;}
void gateway_panel_shutdown(gateway_panel_t*p){if(!p||!p->initialized)return;if(p->health.keypad_available)p->ops->keypad_close(p->ops_context);if(p->health.display_available)p->ops->display_close(p->ops_context);p->health.keypad_available=0;p->health.display_available=0;p->initialized=0;}
void gateway_panel_health(const gateway_panel_t*p,gateway_panel_health_t*h){if(p&&h)*h=p->health;}
unsigned int gateway_panel_memory_bytes(void){return(unsigned int)sizeof(gateway_panel_t);}
unsigned int gateway_panel_screen_bytes(void){return(unsigned int)sizeof(gateway_panel_screen_t);}
