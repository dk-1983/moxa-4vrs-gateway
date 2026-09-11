#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>

#include "config/gateway_persistence.h"

/* The UC-7420 GLIBC 2.4 headers omit this POSIX prototype in strict C99. */
extern int fsync(int file_descriptor);

static unsigned long crc32_bytes(const unsigned char *p,size_t n)
{unsigned long c=0xffffffffUL;size_t i;unsigned int b;for(i=0;i<n;++i){c^=p[i];for(b=0;b<8U;++b)c=(c&1UL)?(c>>1)^0xedb88320UL:c>>1;}return c^0xffffffffUL;}
static int append(char*b,size_t cap,size_t*used,const char*fmt,unsigned int port,unsigned long value)
{int n=snprintf(b+*used,cap-*used,fmt,port,value);if(n<0||(size_t)n>=cap-*used)return-1;*used+=(size_t)n;return 0;}
static int path_join(char*out,const char*dir,const char*name)
{int n;if(!out||!dir||!name)return-1;n=snprintf(out,GATEWAY_CONFIG_PATH_MAX,"%s/%s",dir,name);return n>0&&(unsigned)n<GATEWAY_CONFIG_PATH_MAX?0:-1;}

void gateway_persistent_defaults(gateway_persistent_config_t*c)
{if(!c)return;memset(c,0,sizeof(*c));c->schema_version=GATEWAY_CONFIG_SCHEMA_VERSION;c->settings.ntp_interval_hours=1U;c->settings.backlight_on=1U;c->settings.web_protocol=1U;gateway_configuration_defaults(c->ports);}
void gateway_persistent_safe_mode(gateway_persistent_config_t*c)
{unsigned int i;gateway_persistent_defaults(c);for(i=0;i<GATEWAY_PORT_COUNT;++i)c->ports[i].enabled=0;}

int gateway_product_settings_validate(const gateway_product_settings_t*s)
{size_t i,n;if(!s||s->web_protocol>1U||s->web_enabled>1U||s->web_interface>2U||s->backlight_on>1U|| (s->ntp_interval_hours!=1U&&s->ntp_interval_hours!=6U&&s->ntp_interval_hours!=24U)||(s->ntp_enabled!=0&&s->ntp_enabled!=1))return-1;n=strlen(s->ntp_server);if(n>=GATEWAY_NTP_SERVER_MAX||(s->ntp_enabled&&n==0U))return-1;for(i=0;i<n;++i)if(!(isalnum((unsigned char)s->ntp_server[i])||s->ntp_server[i]=='.'||s->ntp_server[i]=='-'))return-1;return 0;}

gateway_config_result_t gateway_config_encode(const gateway_persistent_config_t*c,char*out,size_t cap,size_t*length)
{size_t u=0;unsigned int i;int z;gateway_error_t errors[GATEWAY_PORT_COUNT];unsigned long crc;if(!c||!out||!length||cap>GATEWAY_CONFIG_MAX_BYTES||cap<64U)return GATEWAY_CONFIG_INVALID;if(c->schema_version!=GATEWAY_CONFIG_SCHEMA_VERSION&&c->schema_version!=GATEWAY_CONFIG_WEB_SCHEMA_VERSION&&c->schema_version!=GATEWAY_CONFIG_PROTOCOL_SCHEMA_VERSION)return GATEWAY_CONFIG_UNSUPPORTED_VERSION;if((c->schema_version<3U&&c->settings.web_protocol!=1U)||(c->schema_version==1U&&(c->settings.web_enabled||c->settings.web_interface))||gateway_product_settings_validate(&c->settings)!=0||gateway_configuration_validate(c->ports,errors)!=0)return GATEWAY_CONFIG_INVALID;z=snprintf(out,cap,"4VRS_GATEWAY_CONFIG\nschema=%lu\ntime.ntp_enabled=%u\ntime.ntp_server=%s\ntime.ntp_interval_hours=%u\n",(unsigned long)c->schema_version,(unsigned)c->settings.ntp_enabled,c->settings.ntp_server,c->settings.ntp_interval_hours);if(z<0||(size_t)z>=cap)return GATEWAY_CONFIG_INVALID;u=(size_t)z;if(!c->settings.backlight_on){z=snprintf(out+u,cap-u,"display.backlight_on=0\n");if(z<0||(size_t)z>=cap-u)return GATEWAY_CONFIG_INVALID;u+=(size_t)z;}if(c->schema_version>=2U){z=snprintf(out+u,cap-u,"web.enabled=%u\nweb.interface=%u\n",c->settings.web_enabled,c->settings.web_interface);if(z<0||(size_t)z>=cap-u)return GATEWAY_CONFIG_INVALID;u+=(size_t)z;}if(c->schema_version==3U){z=snprintf(out+u,cap-u,"web.protocol=%u\n",c->settings.web_protocol);if(z<0||(size_t)z>=cap-u)return GATEWAY_CONFIG_INVALID;u+=(size_t)z;}for(i=0;i<GATEWAY_PORT_COUNT;++i){const gateway_port_config_t*p=&c->ports[i];
#define FIELD(name,fmt,val) if(append(out,cap,&u,"port.%u." name "=" fmt "\n",i+1U,(unsigned long)(val)))return GATEWAY_CONFIG_INVALID
FIELD("enabled","%lu",p->enabled);FIELD("uart","%lu",p->uart_index);FIELD("mode","%lu",p->mode);FIELD("baud","%lu",p->baud);FIELD("data_bits","%lu",p->data_bits);FIELD("parity","%lu",p->parity);FIELD("stop_bits","%lu",p->stop_bits);FIELD("transport","%lu",p->transport);
{int n=snprintf(out+u,cap-u,"port.%u.bind_address=%s\n",i+1U,p->bind_address);if(n<0||(size_t)n>=cap-u)return GATEWAY_CONFIG_INVALID;u+=(size_t)n;}
if(p->bind_policy){FIELD("bind_policy","%lu",p->bind_policy);}
FIELD("endpoint_port","%lu",p->endpoint_port);FIELD("special_baud_enabled","%lu",p->special_baud_enabled);FIELD("special_baud","%lu",p->special_baud);FIELD("revision","%lu",p->revision);
#undef FIELD
}crc=crc32_bytes((const unsigned char*)out,u);z=snprintf(out+u,cap-u,"crc32=%08lX\n",crc);if(z<0||(size_t)z>=cap-u)return GATEWAY_CONFIG_INVALID;u+=(size_t)z;out[u]='\0';*length=u;return GATEWAY_CONFIG_OK;}

static int line_value(const char*line,const char*expected,unsigned long*value)
{char*end;if(strncmp(line,expected,strlen(expected))!=0)return-1;errno=0;*value=strtoul(line+strlen(expected),&end,10);return errno==0&&*value<=0xffffffffUL&&end!=line+strlen(expected)&&*end=='\0'?0:-1;}
gateway_config_result_t gateway_config_decode(const char*input,size_t length,gateway_persistent_config_t*out)
{char b[GATEWAY_CONFIG_MAX_BYTES+1U],*line,*next,*crc_line=0,*schema;gateway_persistent_config_t c;unsigned int i;unsigned long v,got_crc;size_t body_len;gateway_error_t errors[GATEWAY_PORT_COUNT];if(!input||!out||length==0||length>GATEWAY_CONFIG_MAX_BYTES)return GATEWAY_CONFIG_INVALID;memcpy(b,input,length);b[length]='\0';schema=strstr(b,"\nschema=");if(schema){char*end;errno=0;v=strtoul(schema+8,&end,10);if(errno==0&&end!=schema+8&&*end=='\n'&&v!=GATEWAY_CONFIG_SCHEMA_VERSION&&v!=GATEWAY_CONFIG_WEB_SCHEMA_VERSION&&v!=GATEWAY_CONFIG_PROTOCOL_SCHEMA_VERSION)return GATEWAY_CONFIG_UNSUPPORTED_VERSION;}crc_line=strstr(b,"crc32=");if(!crc_line||crc_line==b||strchr(crc_line,'\n')==0||crc_line+15U!=b+length)return GATEWAY_CONFIG_INVALID;body_len=(size_t)(crc_line-b);if(sscanf(crc_line,"crc32=%8lX\n",&got_crc)!=1||got_crc!=crc32_bytes((const unsigned char*)b,body_len))return GATEWAY_CONFIG_INVALID;*crc_line='\0';memset(&c,0,sizeof(c));c.settings.ntp_interval_hours=1U;c.settings.backlight_on=1U;c.settings.web_protocol=1U;line=b;next=strchr(line,'\n');if(!next)return GATEWAY_CONFIG_INVALID;*next++='\0';if(strcmp(line,"4VRS_GATEWAY_CONFIG")!=0)return GATEWAY_CONFIG_INVALID;line=next;next=strchr(line,'\n');if(!next)return GATEWAY_CONFIG_INVALID;*next++='\0';if(line_value(line,"schema=",&v))return GATEWAY_CONFIG_INVALID;c.schema_version=(unsigned int)v;if(strncmp(next,"time.ntp_enabled=",17U)==0){line=next;next=strchr(line,'\n');if(!next)return GATEWAY_CONFIG_INVALID;*next++='\0';if(line_value(line,"time.ntp_enabled=",&v))return GATEWAY_CONFIG_INVALID;c.settings.ntp_enabled=(int)v;line=next;next=strchr(line,'\n');if(!next)return GATEWAY_CONFIG_INVALID;*next++='\0';if(strncmp(line,"time.ntp_server=",16U)!=0||strlen(line+16U)>=GATEWAY_NTP_SERVER_MAX)return GATEWAY_CONFIG_INVALID;strcpy(c.settings.ntp_server,line+16U);}
if(strncmp(next,"time.ntp_interval_hours=",sizeof("time.ntp_interval_hours=")-1U)==0){line=next;next=strchr(line,'\n');if(!next)return GATEWAY_CONFIG_INVALID;*next++='\0';if(line[sizeof("time.ntp_interval_hours=")-1U]<'0'||line[sizeof("time.ntp_interval_hours=")-1U]>'9'||line_value(line,"time.ntp_interval_hours=",&v)|| (v!=1UL&&v!=6UL&&v!=24UL))return GATEWAY_CONFIG_INVALID;c.settings.ntp_interval_hours=(unsigned int)v;}
if(strncmp(next,"display.backlight_on=",21U)==0){line=next;next=strchr(line,'\n');if(!next)return GATEWAY_CONFIG_INVALID;*next++='\0';if(strcmp(line,"display.backlight_on=0")&&strcmp(line,"display.backlight_on=1"))return GATEWAY_CONFIG_INVALID;c.settings.backlight_on=(unsigned int)(line[21]-'0');}
if(c.schema_version>=2U){
 const char *keys[2]={"web.enabled=","web.interface="};unsigned int w;
 for(w=0;w<2U;++w){size_t k=strlen(keys[w]);line=next;next=strchr(line,'\n');if(!next)return GATEWAY_CONFIG_INVALID;*next++=0;
 if(strncmp(line,keys[w],k)||strlen(line)!=k+1U||line[k]<'0'||line[k]>(w?'2':'1'))return GATEWAY_CONFIG_INVALID;
 if(w)c.settings.web_interface=(unsigned int)(line[k]-'0');else c.settings.web_enabled=(unsigned int)(line[k]-'0');
 }
}if(c.schema_version==3U){line=next;next=strchr(line,'\n');if(!next)return GATEWAY_CONFIG_INVALID;*next++=0;if(strcmp(line,"web.protocol=0")&&strcmp(line,"web.protocol=1"))return GATEWAY_CONFIG_INVALID;c.settings.web_protocol=(unsigned int)(line[13]-'0');}for(i=0;i<GATEWAY_PORT_COUNT;++i){gateway_port_config_t*p=&c.ports[i];char key[64];unsigned int f;for(f=0;f<13U;++f){line=next;next=strchr(line,'\n');if(!next)return GATEWAY_CONFIG_INVALID;*next++='\0';if(f==8U){snprintf(key,sizeof(key),"port.%u.bind_address=",i+1U);if(strncmp(line,key,strlen(key))||strlen(line+strlen(key))>=GATEWAY_BIND_ADDRESS_LENGTH)return GATEWAY_CONFIG_INVALID;strcpy(p->bind_address,line+strlen(key));
snprintf(key,sizeof(key),"port.%u.bind_policy=",i+1U);
if(!strncmp(next,key,strlen(key))){line=next;next=strchr(line,'\n');if(!next)return GATEWAY_CONFIG_INVALID;*next++=0;if(line[strlen(key)]<'0'||line[strlen(key)]>'3'||line[strlen(key)+1U]||line_value(line,key,&v)||v>3UL)return GATEWAY_CONFIG_INVALID;p->bind_policy=(unsigned int)v;}
continue;}snprintf(key,sizeof(key),"port.%u.%s=",i+1U,(const char*[]){"enabled","uart","mode","baud","data_bits","parity","stop_bits","transport","","endpoint_port","special_baud_enabled","special_baud","revision"}[f]);if(line_value(line,key,&v))return GATEWAY_CONFIG_INVALID;switch(f){case 0:p->enabled=(int)v;break;case 1:p->uart_index=(unsigned int)v;break;case 2:p->mode=(serial_mode_t)v;break;case 3:p->baud=v;break;case 4:p->data_bits=(unsigned int)v;break;case 5:p->parity=(parity_mode_t)v;break;case 6:p->stop_bits=(unsigned int)v;break;case 7:p->transport=(transport_type_t)v;break;case 9:p->endpoint_port=(unsigned int)v;break;case 10:p->special_baud_enabled=(int)v;break;case 11:p->special_baud=v;break;default:p->revision=(unsigned int)v;break;}}}if(*next!='\0'||gateway_product_settings_validate(&c.settings)!=0||gateway_configuration_validate(c.ports,errors)!=0)return GATEWAY_CONFIG_INVALID;*out=c;return GATEWAY_CONFIG_OK;}

gateway_config_result_t gateway_config_load_file(const char*path,gateway_persistent_config_t*c)
{FILE*f;char b[GATEWAY_CONFIG_MAX_BYTES+1U];size_t n;if(!path||!c)return GATEWAY_CONFIG_INVALID;f=fopen(path,"rb");if(!f)return errno==ENOENT?GATEWAY_CONFIG_ABSENT:GATEWAY_CONFIG_IO_ERROR;n=fread(b,1,sizeof(b),f);if(ferror(f)){fclose(f);return GATEWAY_CONFIG_IO_ERROR;}if(!feof(f)||n>GATEWAY_CONFIG_MAX_BYTES){fclose(f);return GATEWAY_CONFIG_INVALID;}if(fclose(f)!=0)return GATEWAY_CONFIG_IO_ERROR;return gateway_config_decode(b,n,c);}

gateway_config_result_t gateway_config_startup_select(const char*d,gateway_persistent_config_t*c,gateway_config_source_t*s)
{char a[GATEWAY_CONFIG_PATH_MAX],g[GATEWAY_CONFIG_PATH_MAX];gateway_config_result_t ar,br;if(path_join(a,d,GATEWAY_CONFIG_ACTIVE_NAME)||path_join(g,d,GATEWAY_CONFIG_BACKUP_NAME))return GATEWAY_CONFIG_INVALID;ar=gateway_config_load_file(a,c);if(ar==GATEWAY_CONFIG_OK){*s=GATEWAY_CONFIG_SOURCE_ACTIVE;return ar;}br=gateway_config_load_file(g,c);if(br==GATEWAY_CONFIG_OK){*s=GATEWAY_CONFIG_SOURCE_BACKUP;return GATEWAY_CONFIG_OK;}if(ar==GATEWAY_CONFIG_ABSENT&&br==GATEWAY_CONFIG_ABSENT){gateway_persistent_defaults(c);*s=GATEWAY_CONFIG_SOURCE_DEFAULTS;return GATEWAY_CONFIG_ABSENT;}gateway_persistent_safe_mode(c);*s=GATEWAY_CONFIG_SOURCE_SAFE_MODE;if(ar==GATEWAY_CONFIG_IO_ERROR||br==GATEWAY_CONFIG_IO_ERROR)return GATEWAY_CONFIG_IO_ERROR;if(ar==GATEWAY_CONFIG_UNSUPPORTED_VERSION)return ar;return GATEWAY_CONFIG_INVALID;}

static int sync_directory(const char*d){int fd=open(d,O_RDONLY);int r;if(fd<0)return-1;r=fsync(fd);close(fd);return r;}
gateway_config_result_t gateway_config_stage(const char*d,const gateway_persistent_config_t*c,gateway_save_failpoint_t fail)
{char t[GATEWAY_CONFIG_PATH_MAX],b[GATEWAY_CONFIG_MAX_BYTES];size_t n,w;FILE*f;gateway_config_result_t r;if(path_join(t,d,GATEWAY_CONFIG_STAGED_NAME))return GATEWAY_CONFIG_INVALID;r=gateway_config_encode(c,b,sizeof(b),&n);if(r!=GATEWAY_CONFIG_OK)return r;f=fopen(t,"wb");if(!f)return GATEWAY_CONFIG_IO_ERROR;w=fail==GATEWAY_SAVE_FAIL_WRITE?n/2U:fwrite(b,1,n,f);if(w!=n){fclose(f);return GATEWAY_CONFIG_IO_ERROR;}if(fail==GATEWAY_SAVE_FAIL_FLUSH||fflush(f)!=0){fclose(f);return GATEWAY_CONFIG_IO_ERROR;}if(fail==GATEWAY_SAVE_FAIL_FILE_SYNC||fsync(fileno(f))!=0){fclose(f);return GATEWAY_CONFIG_IO_ERROR;}if(fclose(f)!=0)return GATEWAY_CONFIG_IO_ERROR;return GATEWAY_CONFIG_OK;}
gateway_config_result_t gateway_config_promote(const char*d,gateway_save_failpoint_t fail)
{char a[GATEWAY_CONFIG_PATH_MAX],g[GATEWAY_CONFIG_PATH_MAX],t[GATEWAY_CONFIG_PATH_MAX];gateway_persistent_config_t staged,active;gateway_config_result_t ar;if(path_join(a,d,GATEWAY_CONFIG_ACTIVE_NAME)||path_join(g,d,GATEWAY_CONFIG_BACKUP_NAME)||path_join(t,d,GATEWAY_CONFIG_STAGED_NAME))return GATEWAY_CONFIG_INVALID;if(gateway_config_load_file(t,&staged)!=GATEWAY_CONFIG_OK)return GATEWAY_CONFIG_INVALID;ar=gateway_config_load_file(a,&active);if(ar==GATEWAY_CONFIG_OK&&memcmp(&active,&staged,sizeof(active))==0){unlink(t);return GATEWAY_CONFIG_UNCHANGED;}if(ar==GATEWAY_CONFIG_OK){if(fail==GATEWAY_SAVE_FAIL_BACKUP_RENAME||rename(a,g)!=0)return GATEWAY_CONFIG_IO_ERROR;}if(fail==GATEWAY_SAVE_FAIL_PROMOTE_RENAME||rename(t,a)!=0)return GATEWAY_CONFIG_IO_ERROR;if(fail==GATEWAY_SAVE_FAIL_DIRECTORY_SYNC||sync_directory(d)!=0)return GATEWAY_CONFIG_DURABILITY_UNCERTAIN;return GATEWAY_CONFIG_OK;}
void gateway_config_discard_staged(const char*d){char t[GATEWAY_CONFIG_PATH_MAX];if(path_join(t,d,GATEWAY_CONFIG_STAGED_NAME)==0)unlink(t);}
gateway_config_result_t gateway_config_save(const char*d,const gateway_persistent_config_t*c,gateway_save_failpoint_t fail)
{gateway_persistent_config_t old;char a[GATEWAY_CONFIG_PATH_MAX];gateway_config_result_t r;if(path_join(a,d,GATEWAY_CONFIG_ACTIVE_NAME))return GATEWAY_CONFIG_INVALID;r=gateway_config_load_file(a,&old);if(r==GATEWAY_CONFIG_OK&&memcmp(&old,c,sizeof(old))==0)return GATEWAY_CONFIG_UNCHANGED;r=gateway_config_stage(d,c,fail);if(r!=GATEWAY_CONFIG_OK)return r;return gateway_config_promote(d,fail);}
unsigned int gateway_persistent_config_memory_bytes(void){return(unsigned int)sizeof(gateway_persistent_config_t);}
