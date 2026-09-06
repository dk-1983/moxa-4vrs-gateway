#define _GNU_SOURCE
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/rtc.h>

#include "app/gateway_application.h"


/* Official UC-7420 /dev/lcm on=5, off=6; command acknowledgment only. */
static int backlight_set_real(void*x,unsigned int on){int fd,r,e;(void)x;if(on>1U)return EINVAL;fd=open("/dev/lcm",O_RDWR);if(fd<0)return errno?errno:EIO;r=ioctl(fd,on?5:6,0);e=r<0?(errno?errno:EIO):r?EIO:0;if(close(fd)&&!e)e=errno?errno:EIO;return e;}
static const gateway_product_metadata_t product={FOURVRS_PRODUCT_NAME,FOURVRS_VERSION};
static void copy_text(char*out,size_t capacity,const char*input)
{size_t n;if(!out||capacity==0U)return;n=strlen(input);if(n>=capacity)n=capacity-1U;memcpy(out,input,n);out[n]='\0';}

const gateway_product_metadata_t *gateway_product_metadata(void){return &product;}
const char *gateway_rtc_state_name(gateway_rtc_state_t state)
{
    static const char *names[]={"Unknown","Unavailable","Invalid","Read failed","Unverified","Saved","Write failed"};
    return (unsigned int)state<7U?names[state]:"Unknown";
}

int gateway_platform_info_default(void*context,gateway_platform_info_t*i)
{struct utsname u;(void)context;if(!i)return-1;memset(i,0,sizeof(*i));if(uname(&u)!=0)return-1;
#if defined(__arm__)
copy_text(i->model,sizeof(i->model),"Moxa UC-7420 Plus");copy_text(i->architecture,sizeof(i->architecture),"ARM XScale");
#else
copy_text(i->model,sizeof(i->model),"Host development");copy_text(i->architecture,sizeof(i->architecture),u.machine);
#endif
copy_text(i->kernel,sizeof(i->kernel),u.release);i->word_bits=(unsigned int)(sizeof(void*)*8U);
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
i->big_endian=1U;
#elif defined(__arm__) && defined(__ARMEB__)
i->big_endian=1U;
#else
i->big_endian=0U;
#endif
return 0;}

core_tick_t gateway_application_monotonic_ms(void*context)
{struct timespec t;(void)context;if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return 0;return(core_tick_t)((unsigned long)t.tv_sec*1000UL+(unsigned long)t.tv_nsec/1000000UL);}
static unsigned int leap_year(unsigned int y){return(y%4U==0U&&y%100U!=0U)||y%400U==0U;}
int gateway_system_time_validate(const gateway_system_time_t*v)
{static const unsigned int days[]={31U,28U,31U,30U,31U,30U,31U,31U,30U,31U,30U,31U};unsigned int limit;if(!v||v->year<GATEWAY_SYSTEM_TIME_YEAR_MIN||v->year>GATEWAY_SYSTEM_TIME_YEAR_MAX||v->month<1U||v->month>12U||v->hour>23U||v->minute>59U||v->second>59U)return-1;limit=days[v->month-1U];if(v->month==2U&&leap_year(v->year))++limit;return v->day>=1U&&v->day<=limit?0:-1;}
gateway_system_time_result_t gateway_system_time_get_default(void*context,gateway_system_time_t*v)
{time_t now;struct tm*t;(void)context;if(!v)return GATEWAY_SYSTEM_TIME_INVALID;now=time(0);if(now==(time_t)-1)return GATEWAY_SYSTEM_TIME_READ_FAILED;t=localtime(&now);if(!t)return GATEWAY_SYSTEM_TIME_READ_FAILED;v->year=(unsigned int)(t->tm_year+1900);v->month=(unsigned int)(t->tm_mon+1);v->day=(unsigned int)t->tm_mday;v->hour=(unsigned int)t->tm_hour;v->minute=(unsigned int)t->tm_min;v->second=(unsigned int)t->tm_sec;return gateway_system_time_validate(v)==0?GATEWAY_SYSTEM_TIME_OK:GATEWAY_SYSTEM_TIME_READ_FAILED;}
gateway_system_time_result_t gateway_system_time_set_default(void*context,const gateway_system_time_t*v)
{struct tm tmv;struct timespec ts;time_t converted;(void)context;if(gateway_system_time_validate(v)!=0)return GATEWAY_SYSTEM_TIME_INVALID;memset(&tmv,0,sizeof(tmv));tmv.tm_year=(int)v->year-1900;tmv.tm_mon=(int)v->month-1;tmv.tm_mday=(int)v->day;tmv.tm_hour=(int)v->hour;tmv.tm_min=(int)v->minute;tmv.tm_sec=(int)v->second;tmv.tm_isdst=-1;converted=mktime(&tmv);if(converted==(time_t)-1)return GATEWAY_SYSTEM_TIME_SET_FAILED;if((unsigned int)(tmv.tm_year+1900)!=v->year||(unsigned int)(tmv.tm_mon+1)!=v->month||(unsigned int)tmv.tm_mday!=v->day||(unsigned int)tmv.tm_hour!=v->hour||(unsigned int)tmv.tm_min!=v->minute||(unsigned int)tmv.tm_sec!=v->second)return GATEWAY_SYSTEM_TIME_INVALID;ts.tv_sec=converted;ts.tv_nsec=0;if(clock_settime(CLOCK_REALTIME,&ts)!=0)return GATEWAY_SYSTEM_TIME_SET_FAILED;return GATEWAY_SYSTEM_TIME_RTC_UNSUPPORTED;}
gateway_system_time_result_t gateway_application_system_time_get(gateway_application_t*a,gateway_system_time_t*v)
{if(!a||!v)return GATEWAY_SYSTEM_TIME_INVALID;return(a->dependencies.system_time_get?a->dependencies.system_time_get:gateway_system_time_get_default)(a->dependencies.system_time_context,v);}
static void rtc_save_trusted(gateway_application_t *a)
{
    if (!a->dependencies.rtc_save) return;
    a->time.rtc = a->dependencies.rtc_save(a->dependencies.rtc_context);
    if (a->time.rtc == GATEWAY_RTC_SAVED)
        a->time.last_rtc_save = a->dependencies.clock(a->dependencies.clock_context);
    else if (a->time.rtc_save_failures != 0xffffffffU)
        ++a->time.rtc_save_failures;
}

gateway_system_time_result_t gateway_application_system_time_set(gateway_application_t*a,const gateway_system_time_t*v)
{
    gateway_system_time_result_t r;
    if (!a || gateway_system_time_validate(v) != 0) return GATEWAY_SYSTEM_TIME_INVALID;
    /* Never race a helper that can still change CLOCK_REALTIME. */
    if (a->time.trust == GATEWAY_TIME_SYNCING || a->ntp_terminating ||
        a->run_control.stop_requested || a->process_state == GATEWAY_PROCESS_STOPPED)
        return GATEWAY_SYSTEM_TIME_SET_FAILED;
    r=(a->dependencies.system_time_set?a->dependencies.system_time_set:gateway_system_time_set_default)(a->dependencies.system_time_context,v);
    if(r==GATEWAY_SYSTEM_TIME_OK||r==GATEWAY_SYSTEM_TIME_RTC_SYNC_FAILED||r==GATEWAY_SYSTEM_TIME_RTC_UNSUPPORTED) {
        a->time.trust=GATEWAY_TIME_MANUAL;
        if (a->dependencies.rtc_save) {
            rtc_save_trusted(a);
            r = a->time.rtc == GATEWAY_RTC_SAVED ? GATEWAY_SYSTEM_TIME_OK :
                a->time.rtc == GATEWAY_RTC_UNAVAILABLE ? GATEWAY_SYSTEM_TIME_RTC_UNSUPPORTED : GATEWAY_SYSTEM_TIME_RTC_SYNC_FAILED;
        }
    }
    return r;
}

static int rtc_calendar_valid(const struct rtc_time *r)
{
    gateway_system_time_t v;
    if(r->tm_year<100||r->tm_year>137||r->tm_mon<0||r->tm_mon>11)return 0;
    v.year=(unsigned int)(r->tm_year+1900);v.month=(unsigned int)(r->tm_mon+1);
    v.day=(unsigned int)r->tm_mday;v.hour=(unsigned int)r->tm_hour;
    v.minute=(unsigned int)r->tm_min;v.second=(unsigned int)r->tm_sec;
    return gateway_system_time_validate(&v)==0;
}
static gateway_rtc_state_t rtc_open_error(void)
{
    return errno==ENOENT||errno==ENODEV||errno==ENXIO ?
        GATEWAY_RTC_UNAVAILABLE : GATEWAY_RTC_READ_FAILED;
}
static gateway_rtc_state_t rtc_probe_real(void *context)
{
    int fd, result;struct rtc_time r;(void)context;
    fd=open("/dev/rtc",O_RDONLY);if(fd<0)return rtc_open_error();
    memset(&r,0,sizeof(r));result=ioctl(fd,RTC_RD_TIME,&r);close(fd);
    if(result<0)return GATEWAY_RTC_READ_FAILED;
    return rtc_calendar_valid(&r)?GATEWAY_RTC_UNVERIFIED:GATEWAY_RTC_INVALID;
}
static gateway_rtc_state_t rtc_save_real(void *context)
{
    int fd,result;time_t now;struct tm *t;struct rtc_time r;(void)context;
    now=time(0);if(now==(time_t)-1)return GATEWAY_RTC_WRITE_FAILED;
    t=gmtime(&now);if(!t)return GATEWAY_RTC_WRITE_FAILED;
    memset(&r,0,sizeof(r));r.tm_year=t->tm_year;r.tm_mon=t->tm_mon;
    r.tm_mday=t->tm_mday;r.tm_hour=t->tm_hour;r.tm_min=t->tm_min;
    r.tm_sec=t->tm_sec;r.tm_wday=t->tm_wday;r.tm_yday=t->tm_yday;r.tm_isdst=0;
    if(!rtc_calendar_valid(&r))return GATEWAY_RTC_WRITE_FAILED;
    fd=open("/dev/rtc",O_RDWR);
    if(fd<0)return errno==ENOENT||errno==ENODEV||errno==ENXIO?GATEWAY_RTC_UNAVAILABLE:GATEWAY_RTC_WRITE_FAILED;
    result=ioctl(fd,RTC_SET_TIME,&r);close(fd);
    return result==0?GATEWAY_RTC_SAVED:GATEWAY_RTC_WRITE_FAILED;
}
static int ntp_start_real(void *context,const char *server)
{
    gateway_application_t *a=(gateway_application_t *)context;
    pid_t pid;int fd,p[2],flags;
    if(!a||!server||!server[0]||a->ntp_helper_pid>0||pipe(p)!=0)return -1;
    flags=fcntl(p[0],F_GETFL,0);
    if(flags<0||fcntl(p[0],F_SETFL,flags|O_NONBLOCK)<0){close(p[0]);close(p[1]);return -1;}
    pid=fork();
    if(pid<0){close(p[0]);close(p[1]);return -1;}
    if(pid==0){
        DIR *directory;struct dirent *entry;int directory_fd;
        close(p[0]);fd=open("/dev/null",O_RDONLY);
        if(fd<0||dup2(fd,0)<0||dup2(p[1],1)<0||dup2(p[1],2)<0)_exit(127);
        /* The helper must never retain ownership of UARTs or listeners. */
        /* Enumerate actual descriptors, including those above a lowered
         * RLIMIT_NOFILE. Fail closed if procfs cannot be inspected. */
        directory=opendir("/proc/self/fd");if(!directory)_exit(127);
        directory_fd=dirfd(directory);
        for(;;){
            char *end;long number;
            errno=0;entry=readdir(directory);
            if(!entry){if(errno)_exit(127);break;}
            number=strtol(entry->d_name,&end,10);
            if(*end=='\0'&&number>=3&&number!=directory_fd)close((int)number);
        }
        closedir(directory);
        execl("/usr/sbin/ntpdate","ntpdate",server,(char *)0);_exit(127);
    }
    close(p[1]);a->ntp_output_fd=p[0];a->ntp_output_used=0;
    a->ntp_output[0]='\0';a->ntp_helper_pid=(int)pid;return 0;
}
static void ntp_read_output(gateway_application_t *a)
{
    char b[256];ssize_t n;size_t keep,space;
    if(a->ntp_output_fd<0)return;
    n=read(a->ntp_output_fd,b,sizeof(b));
    if(n<=0)return;
    /* Drain even after the diagnostic buffer fills, keeping work bounded. */
    space=sizeof(a->ntp_output)-a->ntp_output_used-1U;
    keep=(size_t)n<space?(size_t)n:space;
    memcpy(a->ntp_output+a->ntp_output_used,b,keep);
    a->ntp_output_used+=(unsigned int)keep;a->ntp_output[a->ntp_output_used]='\0';
}
static int ntp_poll_real(void *context)
{
    gateway_application_t *a=(gateway_application_t *)context;int status;pid_t r;
    if(!a||a->ntp_helper_pid<=0)return -1;
    ntp_read_output(a);r=waitpid((pid_t)a->ntp_helper_pid,&status,WNOHANG);
    if(r==0||(r<0&&errno==EINTR))return 0;
    ntp_read_output(a);
    if(a->ntp_output_fd>=0)close(a->ntp_output_fd);
    a->ntp_output_fd=-1;a->ntp_helper_pid=0;
    if(r<0)return -1;
    if(WIFEXITED(status)&&WEXITSTATUS(status)==0)return 1;
    if(strstr(a->ntp_output,"stratum 16")||strstr(a->ntp_output,"strata too high")||strstr(a->ntp_output,"no server suitable")){
        a->time.last_ntp_result=GATEWAY_NTP_UNSUITABLE;return -2;
    }
    return -1;
}
static void ntp_cancel_real(void*context){gateway_application_t*a=(gateway_application_t*)context;if(a&&a->ntp_helper_pid>0)kill((pid_t)a->ntp_helper_pid,SIGKILL);}
void gateway_application_time_health(const gateway_application_t*a,gateway_time_health_t*h){if(a&&h)*h=a->time;}
void gateway_application_wait_default(void*context,unsigned int us)
{struct timeval t;(void)context;t.tv_sec=(long)(us/1000000U);t.tv_usec=(long)(us%1000000U);while(select(0,0,0,0,&t)<0&&errno==EINTR)break;}
static gateway_config_result_t stage_real(void*x,const char*d,const gateway_persistent_config_t*c)
{(void)x;return gateway_config_stage(d,c,GATEWAY_SAVE_FAIL_NONE);}
static gateway_config_result_t promote_real(void*x,const char*d)
{(void)x;return gateway_config_promote(d,GATEWAY_SAVE_FAIL_NONE);}
static void discard_real(void*x,const char*d){(void)x;gateway_config_discard_staged(d);}

void gateway_application_dependencies_default(gateway_application_dependencies_t*d)
{if(!d)return;memset(d,0,sizeof(*d));d->configuration_directory=GATEWAY_CONFIG_TARGET_DIRECTORY;d->platform_provider=gateway_platform_info_default;d->clock=gateway_application_monotonic_ms;d->wait=gateway_application_wait_default;d->system_time_get=gateway_system_time_get_default;d->system_time_set=gateway_system_time_set_default;d->ntp_server="";d->ntp_start=ntp_start_real;d->ntp_poll=ntp_poll_real;d->ntp_cancel=ntp_cancel_real;d->stage_configuration=stage_real;d->promote_configuration=promote_real;d->discard_configuration=discard_real;}

static int real_prepare(void*context,const gateway_port_config_t*p,gateway_port_binding_t*b)
{gateway_application_t*a=(gateway_application_t*)context;unsigned int i;if(!a)return-1;a->prepared_adapters=0;memset(a->adapters,0,sizeof(a->adapters));for(i=0;i<GATEWAY_PORT_COUNT;++i){if(gateway_real_adapter_init(&a->adapters[i],p[i].uart_index,&p[i],uart_posix_syscalls(),0,gateway_modbus_listener_driver(),0)!=0)return-1;b[i]=gateway_real_adapter_binding(&a->adapters[i]);a->prepared_adapters|=1U<<i;}return 0;}
static int real_attach(void*context,gateway_controller_t*c)
{gateway_application_t*a=(gateway_application_t*)context;unsigned int i;if(!a||!c)return-1;for(i=0;i<GATEWAY_PORT_COUNT;++i)if(a->prepared_adapters&(1U<<i))gateway_real_adapter_attach(&a->adapters[i],&c->ports[i].runtime);return 0;}

void gateway_application_dependencies_production(gateway_application_dependencies_t*d,gateway_application_t*a,const char*directory)
{gateway_application_dependencies_default(d);if(directory)d->configuration_directory=directory;d->provide_bindings=real_prepare;d->attach_bindings=real_attach;d->binding_context=a;d->ntp_context=a;d->backlight_set=backlight_set_real;d->rtc_probe=rtc_probe_real;d->rtc_save=rtc_save_real;
#if defined(__arm__)
if(!strcmp(d->configuration_directory,GATEWAY_CONFIG_TARGET_DIRECTORY)&&access("/etc/4vrs-network/enabled",F_OK)==0)d->network_environment=gateway_network_environment_production();
#endif
}

int gateway_application_init(gateway_application_t*a,const gateway_application_dependencies_t*d)
{core_tick_t now;size_t server_length;if(!a||!d||!d->configuration_directory||!d->provide_bindings||!d->clock||!d->wait||!d->stage_configuration||!d->promote_configuration||!d->discard_configuration)return-1;memset(a,0,sizeof(*a));a->dependencies=*d;if(gateway_network_runtime_init(&a->network,d->network_environment))return-1;a->ntp_output_fd=-1;a->process_state=GATEWAY_PROCESS_STARTING;a->exit_status=GATEWAY_EXIT_CLEAN;a->configuration_transaction.state=GATEWAY_CONFIG_TX_IDLE;a->time.ntp_interval_hours=1U;a->time.trust=GATEWAY_TIME_UNSYNCED;a->time.last_ntp_result=GATEWAY_NTP_NOT_CONFIGURED;if(d->ntp_server&&d->ntp_server[0]){server_length=strlen(d->ntp_server);if(server_length>=sizeof(a->time.ntp_server))server_length=sizeof(a->time.ntp_server)-1U;memcpy(a->time.ntp_server,d->ntp_server,server_length);a->time.last_ntp_result=GATEWAY_NTP_NOT_ATTEMPTED;}gateway_run_control_init(&a->run_control);if((d->platform_provider?d->platform_provider:gateway_platform_info_default)(d->platform_context,&a->platform)!=0){a->process_state=GATEWAY_PROCESS_STOPPED;a->exit_status=GATEWAY_EXIT_FATAL_STARTUP;return-1;}now=d->clock(d->clock_context);if(gateway_coordinator_init(&a->coordinator,d->configuration_directory,&a->run_control,d->select_configuration,d->select_context,d->provide_bindings,d->attach_bindings,d->binding_context,d->controller_driver,now)!=0){a->process_state=GATEWAY_PROCESS_STOPPED;a->exit_status=GATEWAY_EXIT_FATAL_STARTUP;return-1;}return 0;}

int gateway_application_init_production(gateway_application_t*a,const char*d)
{gateway_application_dependencies_t deps;gateway_application_dependencies_production(&deps,a,d);return gateway_application_init(a,&deps);}

void gateway_application_request_stop(gateway_application_t*a)
{if(!a)return;if(a->shutdown_requests!=0xffffffffU)++a->shutdown_requests;gateway_run_control_request_stop(&a->run_control);}

static int config_equal(const gateway_port_config_t*a,const gateway_port_config_t*b)
{return memcmp(a,b,sizeof(*a))==0;}
static int config_terminal(const gateway_port_controller_t*p,const gateway_port_config_t*c)
{return c->enabled?p->lifecycle==GATEWAY_PORT_READY:p->lifecycle==GATEWAY_PORT_DISABLED;}
static void config_rollback_begin(gateway_application_t*a)
{a->dependencies.discard_configuration(a->dependencies.configuration_write_context,a->dependencies.configuration_directory);a->configuration_transaction.state=GATEWAY_CONFIG_TX_ROLLING_BACK;a->configuration_transaction.port_index=memcmp(a->configuration_transaction.candidate.ports,a->configuration_transaction.previous.ports,sizeof(a->configuration_transaction.candidate.ports))==0?GATEWAY_PORT_COUNT:0;a->configuration_transaction.waiting=0;}
static void config_transaction_step(gateway_application_t*a,core_tick_t now)
{gateway_configuration_transaction_t*t=&a->configuration_transaction;gateway_port_controller_t*p;const gateway_port_config_t*desired;gateway_persistent_config_t loaded;gateway_config_source_t source;gateway_config_result_t r;
 if((a->coordinator.state==GATEWAY_APP_SHUTTING_DOWN||a->coordinator.state==GATEWAY_APP_STOPPED||a->coordinator.state==GATEWAY_APP_FATAL_ERROR)&&(t->state==GATEWAY_CONFIG_TX_ACTIVATING||t->state==GATEWAY_CONFIG_TX_PROMOTING||t->state==GATEWAY_CONFIG_TX_ROLLING_BACK)){a->dependencies.discard_configuration(a->dependencies.configuration_write_context,a->dependencies.configuration_directory);t->state=GATEWAY_CONFIG_TX_FAILED;return;}
 if(t->state==GATEWAY_CONFIG_TX_ACTIVATING||t->state==GATEWAY_CONFIG_TX_ROLLING_BACK){
  if(t->port_index>=GATEWAY_PORT_COUNT){if(t->state==GATEWAY_CONFIG_TX_ROLLING_BACK){a->coordinator.selected=t->previous;t->state=GATEWAY_CONFIG_TX_FAILED;return;}t->state=GATEWAY_CONFIG_TX_PROMOTING;return;}
  desired=t->state==GATEWAY_CONFIG_TX_ACTIVATING?&t->candidate.ports[t->port_index]:&t->previous.ports[t->port_index];p=&a->coordinator.controller.ports[t->port_index];
  if(!t->waiting){if(config_equal(&p->current,desired)&&config_terminal(p,desired)){++t->port_index;return;}if(gateway_controller_reconfigure(&a->coordinator.controller,t->port_index,desired,now)!=0){if(t->state==GATEWAY_CONFIG_TX_ROLLING_BACK){t->rollback_failed=1;++t->port_index;}else config_rollback_begin(a);return;}t->waiting=1;return;}
  if(p->lifecycle==GATEWAY_PORT_STARTING||p->lifecycle==GATEWAY_PORT_RECONFIGURING||p->lifecycle==GATEWAY_PORT_ROLLING_BACK||p->lifecycle==GATEWAY_PORT_STOPPING)return;
  t->waiting=0;if(config_equal(&p->current,desired)&&config_terminal(p,desired)){++t->port_index;return;}if(t->state==GATEWAY_CONFIG_TX_ROLLING_BACK){t->rollback_failed=1;++t->port_index;}else config_rollback_begin(a);return;
 }
 if(t->state!=GATEWAY_CONFIG_TX_PROMOTING)return;
 r=a->dependencies.promote_configuration(a->dependencies.configuration_write_context,a->dependencies.configuration_directory);t->persistence_result=r;
 if(r==GATEWAY_CONFIG_OK||r==GATEWAY_CONFIG_UNCHANGED){a->coordinator.selected=t->candidate;a->coordinator.config_source=GATEWAY_CONFIG_SOURCE_ACTIVE;t->state=GATEWAY_CONFIG_TX_SUCCEEDED;return;}
 if(r==GATEWAY_CONFIG_DURABILITY_UNCERTAIN){source=GATEWAY_CONFIG_SOURCE_SAFE_MODE;r=a->coordinator.select_configuration(a->coordinator.select_context,a->dependencies.configuration_directory,&loaded,&source);gateway_application_network_map(a,&loaded,0);if((r==GATEWAY_CONFIG_OK||r==GATEWAY_CONFIG_ABSENT)&&memcmp(&loaded,&t->candidate,sizeof(loaded))==0){a->coordinator.selected=t->candidate;a->coordinator.config_source=source;t->state=GATEWAY_CONFIG_TX_DURABILITY_UNCERTAIN;return;}}
 config_rollback_begin(a);
}

int gateway_application_request_configuration(gateway_application_t*a,const gateway_persistent_config_t*c)
{gateway_error_t errors[GATEWAY_PORT_COUNT]={GATEWAY_ERROR_NONE};gateway_configuration_transaction_t*t;gateway_config_result_t r;gateway_persistent_config_t canonical;unsigned int i;if(!a||!c||gateway_network_runtime_busy(&a->network)||a->network.status.state==GATEWAY_NETWORK_DURABILITY_UNCERTAIN||a->network.status.state==GATEWAY_NETWORK_ROLLBACK_FAILED||!a->coordinator.controller_initialized||a->process_state!=GATEWAY_PROCESS_RUNNING)return-1;t=&a->configuration_transaction;if(t->state==GATEWAY_CONFIG_TX_ACTIVATING||t->state==GATEWAY_CONFIG_TX_PROMOTING||t->state==GATEWAY_CONFIG_TX_ROLLING_BACK)return-1;memset(t,0,sizeof(*t));t->state=GATEWAY_CONFIG_TX_FAILED;if(c->schema_version!=GATEWAY_CONFIG_SCHEMA_VERSION||gateway_product_settings_validate(&c->settings)!=0||gateway_configuration_validate(c->ports,errors)!=0){for(i=0;i<GATEWAY_PORT_COUNT;++i)if(errors[i]!=GATEWAY_ERROR_NONE){t->validation_error=errors[i];break;}t->persistence_result=GATEWAY_CONFIG_INVALID;return-1;}for(i=0;i<GATEWAY_PORT_COUNT;++i)if(c->ports[i].uart_index!=a->coordinator.selected.ports[i].uart_index){t->validation_error=GATEWAY_ERROR_INVALID_CONFIG;t->persistence_result=GATEWAY_CONFIG_INVALID;return-1;}if(memcmp(c,&a->coordinator.selected,sizeof(*c))==0){t->state=GATEWAY_CONFIG_TX_UNCHANGED;t->persistence_result=GATEWAY_CONFIG_UNCHANGED;return 0;}t->candidate=*c;t->previous=a->coordinator.selected;
for(i=0;i<8U;++i)if(strcmp(c->ports[i].bind_address,t->previous.ports[i].bind_address)){
    t->candidate.ports[i].bind_policy=1U;
    if(a->network.available){unsigned int lan;for(lan=0;lan<2U;++lan)
        if(strcmp(c->ports[i].bind_address,"0.0.0.0")){
            const char *address=a->network.confirmed.settings.lan[lan].address;
            if(a->network.confirmed.settings.lan[lan].mode==GATEWAY_LAN_DHCP_CLIENT)
                address=a->network.observed_valid&&!a->network.observation_error&&a->network.lease_valid[lan]?a->network.observed.lan[lan].address:"";
            if(!strcmp(c->ports[i].bind_address,address))t->candidate.ports[i].bind_policy=lan+2U;
        }
    }
}
canonical=t->candidate;gateway_application_network_map(a,&canonical,1);r=a->dependencies.stage_configuration(a->dependencies.configuration_write_context,a->dependencies.configuration_directory,&canonical);t->persistence_result=r;if(r!=GATEWAY_CONFIG_OK){a->dependencies.discard_configuration(a->dependencies.configuration_write_context,a->dependencies.configuration_directory);t->state=GATEWAY_CONFIG_TX_FAILED;return-1;}t->state=memcmp(t->candidate.ports,t->previous.ports,sizeof(t->candidate.ports))==0?GATEWAY_CONFIG_TX_PROMOTING:GATEWAY_CONFIG_TX_ACTIVATING;return 0;}
int gateway_application_backlight_set(gateway_application_t*a,unsigned int on)
{gateway_persistent_config_t c;int r;if(!a||on>1U)return -1;c=a->coordinator.selected;c.settings.backlight_on=on;r=gateway_application_request_configuration(a,&c);if(!r&&a->configuration_transaction.state==GATEWAY_CONFIG_TX_UNCHANGED)a->backlight.attempted=0;return r;}
static void backlight_step(gateway_application_t*a)
{gateway_backlight_health_t*h=&a->backlight;unsigned int on;
 if(!a->coordinator.controller_initialized||a->run_control.stop_requested)return;
 on=a->coordinator.selected.settings.backlight_on;
 if(h->attempted&&h->desired_on==on)return;
 h->desired_on=on;h->attempted=1;h->last_error=a->dependencies.backlight_set?a->dependencies.backlight_set(a->dependencies.backlight_context,on):ENOSYS;
 h->command_known=h->last_error==0;if(h->command_known)h->command_on=on;
}

gateway_configuration_transaction_state_t gateway_application_configuration_state(const gateway_application_t*a){return a?a->configuration_transaction.state:GATEWAY_CONFIG_TX_FAILED;}
gateway_config_result_t gateway_application_configuration_result(const gateway_application_t*a){return a?a->configuration_transaction.persistence_result:GATEWAY_CONFIG_INVALID;}

/* Diagnostic mode is RAM-only and bounded even when the server fails. */
int gateway_application_ntp_test(gateway_application_t *a,unsigned int enabled)
{
    core_tick_t now;
    if(!a||a->process_state!=GATEWAY_PROCESS_RUNNING||a->run_control.stop_requested||enabled>1U)return -1;
    if(enabled && (!a->time.ntp_enabled||!a->time.ntp_server[0]||a->ntp_terminating))return -1;
    if(enabled==a->time.ntp_test_active)return 0;
    a->time.ntp_test_active=enabled;
    if(enabled){a->time.ntp_test_attempts=0;a->ntp_normal_deadline=a->ntp_retry_deadline;}
    now=a->dependencies.clock(a->dependencies.clock_context);
    if(a->time.trust!=GATEWAY_TIME_SYNCING) {
        a->ntp_retry_deadline=enabled?core_deadline_after(now,60000U):a->ntp_normal_deadline;
        a->time.next_ntp_attempt=a->ntp_retry_deadline.at;
    }
    return 0;
}
static core_tick_t ntp_success_interval(gateway_application_t *a)
{
    if(a->time.ntp_test_active && a->time.ntp_test_attempts>=3U)a->time.ntp_test_active=0;
    return a->time.ntp_test_active?60000U:(core_tick_t)(a->time.ntp_interval_hours*3600000UL);
}
static core_tick_t ntp_retry_delay(unsigned int attempts){if(attempts<=1U)return GATEWAY_NTP_RETRY_INITIAL_MS;if(attempts==2U)return GATEWAY_NTP_RETRY_SECOND_MS;return GATEWAY_NTP_RETRY_MAX_MS;}
static void time_settings_step(gateway_application_t *a)
{
    const gateway_product_settings_t *s;
    if(a->coordinator.state!=GATEWAY_APP_READY && a->coordinator.state!=GATEWAY_APP_DEGRADED && a->coordinator.state!=GATEWAY_APP_SAFE_MODE)return;
    if(!a->rtc_checked) {
        if(a->dependencies.rtc_probe)a->time.rtc=a->dependencies.rtc_probe(a->dependencies.rtc_context);
        a->rtc_checked=1;
    }
    s=&a->coordinator.selected.settings;
    if(a->time_settings_applied && a->time.ntp_enabled==(unsigned)s->ntp_enabled && strcmp(a->time.ntp_server,s->ntp_server)==0) {
        if(a->time.ntp_interval_hours!=s->ntp_interval_hours) {
            a->time.ntp_interval_hours=s->ntp_interval_hours;
            /* Keep in-flight synchronization and failure backoff unchanged. */
            if(a->time.trust==GATEWAY_TIME_SYNCED) {
                core_tick_t now=a->dependencies.clock(a->dependencies.clock_context);
                core_tick_t interval=(core_tick_t)(s->ntp_interval_hours*3600000UL);
                core_tick_t elapsed=core_elapsed(a->time.last_sync,now);
                a->ntp_normal_deadline=core_deadline_after(now,elapsed>=interval?0U:interval-elapsed);
                if(!a->time.ntp_test_active){a->ntp_retry_deadline=a->ntp_normal_deadline;
                    a->time.next_ntp_attempt=a->ntp_retry_deadline.at;}
            }
        }
        return;
    }
    if(a->time.trust==GATEWAY_TIME_SYNCING) {
        if(a->dependencies.ntp_cancel)a->dependencies.ntp_cancel(a->dependencies.ntp_context);
        a->ntp_terminating=1;
        a->time.trust=a->trust_before_sync;
    }
    if(a->time.trust==GATEWAY_TIME_SYNCED)a->time.trust=GATEWAY_TIME_HOLDOVER;
    a->time.ntp_test_active=0;
    a->time.ntp_enabled=(unsigned)s->ntp_enabled;
    a->time.ntp_interval_hours=s->ntp_interval_hours;
    copy_text(a->time.ntp_server,sizeof(a->time.ntp_server),s->ntp_server);
    a->time.ntp_failures=0;a->time.next_ntp_attempt=0;a->ntp_retry_deadline.armed=0;
    a->ntp_normal_deadline.armed=0;
    a->time.last_ntp_result=s->ntp_enabled?GATEWAY_NTP_NOT_ATTEMPTED:GATEWAY_NTP_NOT_CONFIGURED;
    a->time_settings_applied=1;
}
static void time_sync_failed(gateway_application_t *a,core_tick_t now,gateway_ntp_result_t result)
{
    core_tick_t delay;
    if(a->time.ntp_failures!=0xffffffffU)++a->time.ntp_failures;
    delay=ntp_retry_delay(a->time.ntp_failures);
    a->ntp_normal_deadline=core_deadline_after(now,delay);
    if(a->time.ntp_test_active) {
        if(a->time.ntp_test_attempts>=3U)a->time.ntp_test_active=0;
        else delay=60000U;
    }
    if(result==GATEWAY_NTP_FAILED && a->time.last_ntp_result==GATEWAY_NTP_UNSUITABLE)result=GATEWAY_NTP_UNSUITABLE;
    a->time.last_ntp_result=result;
    a->time.trust=a->trust_before_sync==GATEWAY_TIME_MANUAL?GATEWAY_TIME_MANUAL:
        (a->trust_before_sync==GATEWAY_TIME_SYNCED || a->trust_before_sync==GATEWAY_TIME_HOLDOVER)?GATEWAY_TIME_HOLDOVER:GATEWAY_TIME_ERROR;
    a->ntp_retry_deadline=core_deadline_after(now,delay);a->time.next_ntp_attempt=now+delay;
}
static void time_sync_step(gateway_application_t *a,core_tick_t now)
{
    int r;
    unsigned int stopping=a->run_control.stop_requested || a->coordinator.state==GATEWAY_APP_SHUTTING_DOWN || a->coordinator.state==GATEWAY_APP_STOPPED || a->coordinator.state==GATEWAY_APP_FATAL_ERROR;
    a->time.ntp_network_deferred=0;
    if(!stopping)time_settings_step(a);
    else a->time.ntp_test_active=0;
    if(stopping && a->time.trust==GATEWAY_TIME_SYNCING && !a->ntp_terminating) {
        if(a->dependencies.ntp_cancel)a->dependencies.ntp_cancel(a->dependencies.ntp_context);
        a->ntp_terminating=1;a->time.trust=a->trust_before_sync;
        a->time.last_ntp_result=GATEWAY_NTP_NOT_ATTEMPTED;
    }
    /* Reap canceled children even when NTP was disabled or shutdown began. */
    if(a->ntp_terminating) {
        gateway_ntp_result_t saved_result=a->time.last_ntp_result;
        r=a->dependencies.ntp_poll?a->dependencies.ntp_poll(a->dependencies.ntp_context):-1;
        a->time.last_ntp_result=saved_result;
        if(r==0)return;
        a->ntp_terminating=0;
    }
    if(stopping || !a->time_settings_applied || !a->time.ntp_enabled || !a->time.ntp_server[0])return;
    if(a->time.trust==GATEWAY_TIME_SYNCING) {
        r=a->dependencies.ntp_poll?a->dependencies.ntp_poll(a->dependencies.ntp_context):-1;
        if(r>0) {
            gateway_system_time_t checked;core_tick_t interval;
            if(gateway_application_system_time_get(a,&checked)!=GATEWAY_SYSTEM_TIME_OK || gateway_system_time_validate(&checked)!=0) {
                /* An invalid current calendar cannot be described as holdover
                 * from an earlier valid synchronization. */
                a->trust_before_sync=GATEWAY_TIME_UNSYNCED;
                time_sync_failed(a,now,GATEWAY_NTP_FAILED);return;
            }
            a->time.trust=GATEWAY_TIME_SYNCED;a->time.last_ntp_result=GATEWAY_NTP_SUCCEEDED;
            a->time.last_sync=now;a->time.ntp_failures=0;
            a->ntp_normal_deadline=core_deadline_after(now,(core_tick_t)(a->time.ntp_interval_hours*3600000UL));
            interval=ntp_success_interval(a);
            a->ntp_retry_deadline=core_deadline_after(now,interval);
            a->time.next_ntp_attempt=now+interval;
            rtc_save_trusted(a);return;
        }
        if(r<0){time_sync_failed(a,now,GATEWAY_NTP_FAILED);return;}
        if(core_deadline_expired(a->ntp_deadline,now)) {
            if(a->dependencies.ntp_cancel)a->dependencies.ntp_cancel(a->dependencies.ntp_context);
            a->ntp_terminating=1;time_sync_failed(a,now,GATEWAY_NTP_TIMED_OUT);
        }
        return;
    }
    if(a->network.available&&a->network.observed_valid){
        const gateway_network_observation_t *o=&a->network.observed;unsigned long numeric;
        unsigned int i,usable=0;
        for(i=0;i<2U;++i)if(o->lan[i].up&&o->lan[i].link&&o->lan[i].address[0])usable=1;
        if(a->network.observation_error||!usable||
           (gateway_ipv4_parse(a->time.ntp_server,&numeric)&&!o->dns[0][0]&&!o->dns[1][0])){
            a->time.ntp_network_deferred=1;
            if(a->time.trust==GATEWAY_TIME_SYNCED&&core_deadline_expired(a->ntp_normal_deadline,now))
                a->time.trust=GATEWAY_TIME_HOLDOVER;
            return;
        }
    }
    if(a->ntp_retry_deadline.armed && !core_deadline_expired(a->ntp_retry_deadline,now))return;
    a->trust_before_sync=a->time.trust;
    if(a->time.ntp_attempts!=0xffffffffU)++a->time.ntp_attempts;
    a->time.last_ntp_attempt=now;
    if(a->time.ntp_test_active)++a->time.ntp_test_attempts;
    if(a->dependencies.ntp_start && a->dependencies.ntp_start(a->dependencies.ntp_context,a->time.ntp_server)==0) {
        a->time.trust=GATEWAY_TIME_SYNCING;a->time.last_ntp_result=GATEWAY_NTP_IN_PROGRESS;
        a->ntp_deadline=core_deadline_after(now,GATEWAY_NTP_ATTEMPT_TIMEOUT_MS);
    } else time_sync_failed(a,now,GATEWAY_NTP_FAILED);
}

void gateway_application_step(gateway_application_t*a)
{core_tick_t now;if(!a||a->process_state==GATEWAY_PROCESS_STOPPED)return;now=a->dependencies.clock(a->dependencies.clock_context);if(a->run_control.stop_requested&&a->cleanup_attempts==0U)++a->cleanup_attempts;gateway_coordinator_step(&a->coordinator,now);if(a->coordinator.state==GATEWAY_APP_SELECTING_CONFIGURATION)gateway_application_network_map(a,&a->coordinator.selected,0);gateway_application_network_step(a,now);config_transaction_step(a,now);backlight_step(a);time_sync_step(a,now);if(a->coordinator.state==GATEWAY_APP_FATAL_ERROR){a->fatal_seen=1;a->process_state=GATEWAY_PROCESS_STOPPING;if(a->cleanup_attempts==0U)++a->cleanup_attempts;gateway_coordinator_request_shutdown(&a->coordinator,now);}
else if(a->coordinator.state==GATEWAY_APP_READY||a->coordinator.state==GATEWAY_APP_DEGRADED){a->process_state=GATEWAY_PROCESS_RUNNING;}
else if(a->coordinator.state==GATEWAY_APP_SAFE_MODE){a->safe_mode_seen=1;a->process_state=GATEWAY_PROCESS_RUNNING;}
else if(a->coordinator.state==GATEWAY_APP_SHUTTING_DOWN){a->process_state=GATEWAY_PROCESS_STOPPING;}
else if(a->coordinator.state==GATEWAY_APP_STOPPED){a->process_state=(a->ntp_terminating||gateway_network_runtime_busy(&a->network))?GATEWAY_PROCESS_STOPPING:GATEWAY_PROCESS_STOPPED;if(a->network.status.state==GATEWAY_NETWORK_ROLLBACK_FAILED||a->network.status.state==GATEWAY_NETWORK_DURABILITY_UNCERTAIN||!a->coordinator.cleanup_complete||a->coordinator.error==GATEWAY_APP_ERROR_SHUTDOWN_INCOMPLETE)a->exit_status=GATEWAY_EXIT_CLEANUP_FAILURE;else if(a->fatal_seen)a->exit_status=GATEWAY_EXIT_FATAL_STARTUP;else if(a->safe_mode_seen)a->exit_status=GATEWAY_EXIT_SAFE_MODE;else a->exit_status=GATEWAY_EXIT_CLEAN;}}

int gateway_application_finished(const gateway_application_t*a){return a&&a->process_state==GATEWAY_PROCESS_STOPPED;}
gateway_process_exit_t gateway_application_exit_status(const gateway_application_t*a){return a?a->exit_status:GATEWAY_EXIT_INVALID_INVOCATION;}
void gateway_application_health(const gateway_application_t*a,gateway_application_health_t*h)
{size_t n;if(!a||!h)return;memset(h,0,sizeof(*h));h->process_state=a->process_state;h->exit_status=a->exit_status;h->product=product;h->platform=a->platform;h->time=a->time;h->backlight=a->backlight;n=strlen(a->dependencies.configuration_directory);if(n>=sizeof(h->configuration_directory))n=sizeof(h->configuration_directory)-1U;memcpy(h->configuration_directory,a->dependencies.configuration_directory,n);gateway_coordinator_health(&a->coordinator,a->dependencies.clock(a->dependencies.clock_context),&h->coordinator);h->shutdown_requests=a->shutdown_requests;h->cleanup_attempts=a->cleanup_attempts;}
unsigned int gateway_application_memory_bytes(void){return(unsigned int)sizeof(gateway_application_t);}
unsigned int gateway_application_adapter_bytes(void){return(unsigned int)sizeof(((gateway_application_t*)0)->adapters);}
unsigned int gateway_application_max_fds(void){return(GATEWAY_PORT_COUNT*2U)+(GATEWAY_PORT_COUNT*MODBUS_LISTENER_CLIENT_MAX)+9U;}
