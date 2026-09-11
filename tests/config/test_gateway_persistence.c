#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "config/gateway_persistence.h"

static unsigned int checks,failed;
const unsigned char persistence_config_layout[sizeof(gateway_persistent_config_t)]={0};
#define CHECK(x) do{++checks;if(!(x)){++failed;printf("FAIL line %u: %s\n",(unsigned)__LINE__,#x);}}while(0)
static void path(char*out,const char*d,const char*n){sprintf(out,"%s/%s",d,n);}
static int write_text(const char*p,const char*s){FILE*f=fopen(p,"wb");if(!f)return-1;if(fputs(s,f)<0){fclose(f);return-1;}return fclose(f);}
static void remove_files(const char*d){char p[256];path(p,d,GATEWAY_CONFIG_ACTIVE_NAME);unlink(p);path(p,d,GATEWAY_CONFIG_BACKUP_NAME);unlink(p);path(p,d,GATEWAY_CONFIG_STAGED_NAME);unlink(p);}
static unsigned long test_crc32(const unsigned char*p,size_t n){unsigned long c=0xffffffffUL;size_t i;unsigned int j;for(i=0;i<n;++i){c^=p[i];for(j=0;j<8U;++j)c=(c&1UL)?(c>>1)^0xedb88320UL:c>>1;}return c^0xffffffffUL;}
static size_t replace_value(char*b,size_t n,const char*old,const char*replacement){char*p=strstr(b,old);size_t a=strlen(old),z=strlen(replacement),tail;if(!p)return 0;tail=n-(size_t)(p-b)-a;memmove(p+z,p+a,tail+1U);memcpy(p,replacement,z);return n-a+z;}
static void rechecksum(char*b,size_t n){char*p=strstr(b,"crc32=");unsigned long crc;CHECK(p!=0);if(!p)return;crc=test_crc32((const unsigned char*)b,(size_t)(p-b));sprintf(p,"crc32=%08lX\n",crc);CHECK(strlen(b)==n);}

static void round_trips(void)
{gateway_persistent_config_t a,b;char text[GATEWAY_CONFIG_MAX_BYTES],bad[GATEWAY_CONFIG_MAX_BYTES];size_t n;unsigned int i;gateway_persistent_defaults(&a);CHECK(gateway_config_encode(&a,text,sizeof(text),&n)==GATEWAY_CONFIG_OK);CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_OK);CHECK(memcmp(&a,&b,sizeof(a))==0);for(i=0;i<8U;++i){a.ports[i].enabled=(i&1U)==0;a.ports[i].mode=(serial_mode_t)(i%SERIAL_MODE_COUNT);a.ports[i].baud=baud_value((int)(i+10U));a.ports[i].data_bits=7U+(i&1U);a.ports[i].parity=(parity_mode_t)(i%PARITY_COUNT);a.ports[i].stop_bits=1U+(i&1U);a.ports[i].endpoint_port=1502U+i;a.ports[i].revision=20U+i;}strcpy(a.ports[0].bind_address,"127.0.0.1");CHECK(gateway_config_encode(&a,text,sizeof(text),&n)==GATEWAY_CONFIG_OK);CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_OK&&memcmp(&a,&b,sizeof(a))==0);
memcpy(bad,text,n+1U);bad[0]='X';CHECK(gateway_config_decode(bad,n,&b)==GATEWAY_CONFIG_INVALID);CHECK(gateway_config_decode(text,n/2U,&b)==GATEWAY_CONFIG_INVALID);CHECK(gateway_config_decode("",0,&b)==GATEWAY_CONFIG_INVALID);memcpy(bad,text,n+1U);{char*p=strstr(bad,"schema=1");CHECK(p!=0);if(p)p[7]='4';}CHECK(gateway_config_decode(bad,n,&b)==GATEWAY_CONFIG_UNSUPPORTED_VERSION);memcpy(bad,text,n+1U);{char*p=strstr(bad,"port.1.enabled=");CHECK(p!=0);if(p)p[15]='x';}CHECK(gateway_config_decode(bad,n,&b)==GATEWAY_CONFIG_INVALID);
}

static void strict_parser_matrix(void)
{gateway_persistent_config_t c,out;char text[GATEWAY_CONFIG_MAX_BYTES],bad[GATEWAY_CONFIG_MAX_BYTES];size_t n,m;gateway_persistent_defaults(&c);CHECK(gateway_config_encode(&c,text,sizeof(text),&n)==GATEWAY_CONFIG_OK);
#define INVALID_REPLACE(old,repl) do{memcpy(bad,text,n+1U);m=replace_value(bad,n,old,repl);CHECK(m>0U);if(m){rechecksum(bad,m);CHECK(gateway_config_decode(bad,m,&out)==GATEWAY_CONFIG_INVALID);}}while(0)
INVALID_REPLACE("port.1.uart=0","port.1.junk=0");
INVALID_REPLACE("port.1.uart=0","port.1.mode=0");
INVALID_REPLACE("port.1.mode=0","port.1.mode=9");
INVALID_REPLACE("port.1.baud=9600","port.1.baud=x");
INVALID_REPLACE("port.1.baud=9600","port.1.baud=42949672960");
INVALID_REPLACE("port.1.bind_address=0.0.0.0","port.1.bind_address=1111111111111111111111111111111111111111");
INVALID_REPLACE("port.1.endpoint_port=502","port.1.endpoint_port=0");
#undef INVALID_REPLACE
memcpy(bad,text,n+1U);bad[n-10U]^=1;CHECK(gateway_config_decode(bad,n,&out)==GATEWAY_CONFIG_INVALID);CHECK(gateway_config_decode(text,GATEWAY_CONFIG_MAX_BYTES+1U,&out)==GATEWAY_CONFIG_INVALID);}

static void invalid_typed(void)
{gateway_persistent_config_t c;char b[4096];size_t n;gateway_persistent_defaults(&c);c.ports[1].uart_index=0;CHECK(gateway_config_encode(&c,b,sizeof(b),&n)==GATEWAY_CONFIG_INVALID);gateway_persistent_defaults(&c);c.ports[1].endpoint_port=502;CHECK(gateway_config_encode(&c,b,sizeof(b),&n)==GATEWAY_CONFIG_INVALID);gateway_persistent_defaults(&c);c.ports[0].data_bits=9;CHECK(gateway_config_encode(&c,b,sizeof(b),&n)==GATEWAY_CONFIG_INVALID);gateway_persistent_defaults(&c);c.ports[0].endpoint_port=0;CHECK(gateway_config_encode(&c,b,sizeof(b),&n)==GATEWAY_CONFIG_INVALID);gateway_persistent_defaults(&c);strcpy(c.ports[0].bind_address,"invalid");CHECK(gateway_config_encode(&c,b,sizeof(b),&n)==GATEWAY_CONFIG_INVALID);c.schema_version=4;CHECK(gateway_config_encode(&c,b,sizeof(b),&n)==GATEWAY_CONFIG_UNSUPPORTED_VERSION);}

static void filesystem_cases(const char*d)
{gateway_persistent_config_t a,b;gateway_config_source_t source;char p[256];remove_files(d);CHECK(gateway_config_startup_select(d,&b,&source)==GATEWAY_CONFIG_ABSENT&&source==GATEWAY_CONFIG_SOURCE_DEFAULTS&&b.ports[0].enabled);gateway_persistent_defaults(&a);CHECK(gateway_config_save(d,&a,GATEWAY_SAVE_FAIL_NONE)==GATEWAY_CONFIG_OK);CHECK(gateway_config_save(d,&a,GATEWAY_SAVE_FAIL_NONE)==GATEWAY_CONFIG_UNCHANGED);a.ports[0].endpoint_port=1502;a.ports[0].revision=2;CHECK(gateway_config_save(d,&a,GATEWAY_SAVE_FAIL_NONE)==GATEWAY_CONFIG_OK);CHECK(gateway_config_startup_select(d,&b,&source)==GATEWAY_CONFIG_OK&&source==GATEWAY_CONFIG_SOURCE_ACTIVE&&b.ports[0].endpoint_port==1502);path(p,d,GATEWAY_CONFIG_ACTIVE_NAME);CHECK(write_text(p,"corrupt")==0);CHECK(gateway_config_startup_select(d,&b,&source)==GATEWAY_CONFIG_OK&&source==GATEWAY_CONFIG_SOURCE_BACKUP&&b.ports[0].endpoint_port==502);path(p,d,GATEWAY_CONFIG_BACKUP_NAME);CHECK(write_text(p,"bad")==0);CHECK(gateway_config_startup_select(d,&b,&source)==GATEWAY_CONFIG_INVALID&&source==GATEWAY_CONFIG_SOURCE_SAFE_MODE&&!b.ports[0].enabled&&!b.ports[7].enabled);remove_files(d);
gateway_persistent_defaults(&a);CHECK(gateway_config_save(d,&a,GATEWAY_SAVE_FAIL_WRITE)==GATEWAY_CONFIG_IO_ERROR);CHECK(gateway_config_save(d,&a,GATEWAY_SAVE_FAIL_FLUSH)==GATEWAY_CONFIG_IO_ERROR);CHECK(gateway_config_save(d,&a,GATEWAY_SAVE_FAIL_FILE_SYNC)==GATEWAY_CONFIG_IO_ERROR);CHECK(gateway_config_load_file("/definitely/not/a/real/path",&b)==GATEWAY_CONFIG_ABSENT);remove_files(d);}

static void staged_runtime_contract(const char*d)
{gateway_persistent_config_t active,candidate,loaded;gateway_config_source_t source;char p[256];remove_files(d);gateway_persistent_defaults(&active);CHECK(gateway_config_save(d,&active,GATEWAY_SAVE_FAIL_NONE)==GATEWAY_CONFIG_OK);candidate=active;candidate.ports[3].endpoint_port=1605;candidate.ports[3].revision=2;CHECK(gateway_config_stage(d,&candidate,GATEWAY_SAVE_FAIL_NONE)==GATEWAY_CONFIG_OK);/* simulated runtime activation failure */gateway_config_discard_staged(d);CHECK(gateway_config_startup_select(d,&loaded,&source)==GATEWAY_CONFIG_OK&&source==GATEWAY_CONFIG_SOURCE_ACTIVE&&loaded.ports[3].endpoint_port==505);CHECK(gateway_config_stage(d,&candidate,GATEWAY_SAVE_FAIL_NONE)==GATEWAY_CONFIG_OK);CHECK(gateway_config_promote(d,GATEWAY_SAVE_FAIL_PROMOTE_RENAME)==GATEWAY_CONFIG_IO_ERROR);CHECK(gateway_config_startup_select(d,&loaded,&source)==GATEWAY_CONFIG_OK&&source==GATEWAY_CONFIG_SOURCE_BACKUP&&loaded.ports[3].endpoint_port==505);path(p,d,GATEWAY_CONFIG_STAGED_NAME);CHECK(access(p,F_OK)==0);gateway_config_discard_staged(d);remove_files(d);CHECK(gateway_config_stage(d,&candidate,GATEWAY_SAVE_FAIL_NONE)==GATEWAY_CONFIG_OK);CHECK(gateway_config_promote(d,GATEWAY_SAVE_FAIL_DIRECTORY_SYNC)==GATEWAY_CONFIG_DURABILITY_UNCERTAIN);CHECK(gateway_config_load_file(p,&loaded)==GATEWAY_CONFIG_ABSENT);path(p,d,GATEWAY_CONFIG_ACTIVE_NAME);CHECK(gateway_config_load_file(p,&loaded)==GATEWAY_CONFIG_OK&&loaded.ports[3].endpoint_port==1605);remove_files(d);}

static void product_time_settings(void)
{gateway_persistent_config_t c,out;char text[GATEWAY_CONFIG_MAX_BYTES],old[GATEWAY_CONFIG_MAX_BYTES];char*first,*second;size_t n,oldn;gateway_persistent_defaults(&c);CHECK(c.settings.ntp_enabled==0&&c.settings.ntp_server[0]=='\0');c.settings.ntp_enabled=1;strcpy(c.settings.ntp_server,"10.0.0.10");CHECK(gateway_product_settings_validate(&c.settings)==0);CHECK(gateway_config_encode(&c,text,sizeof(text),&n)==GATEWAY_CONFIG_OK);CHECK(strstr(text,"time.ntp_enabled=1\ntime.ntp_server=10.0.0.10\n")!=0);CHECK(gateway_config_decode(text,n,&out)==GATEWAY_CONFIG_OK&&out.settings.ntp_enabled==1&&strcmp(out.settings.ntp_server,"10.0.0.10")==0);strcpy(c.settings.ntp_server,"bad server");CHECK(gateway_product_settings_validate(&c.settings)!=0&&gateway_config_encode(&c,text,sizeof(text),&n)==GATEWAY_CONFIG_INVALID);gateway_persistent_defaults(&c);CHECK(gateway_config_encode(&c,text,sizeof(text),&n)==GATEWAY_CONFIG_OK);first=strstr(text,"time.ntp_enabled=");second=strstr(text,"port.1.enabled=");CHECK(first!=0&&second!=0);if(first&&second){memcpy(old,text,(size_t)(first-text));oldn=(size_t)(first-text);memmove(old+oldn,second,n-(size_t)(second-text)+1U);oldn+=n-(size_t)(second-text);rechecksum(old,oldn);CHECK(gateway_config_decode(old,oldn,&out)==GATEWAY_CONFIG_OK&&out.settings.ntp_enabled==0&&out.settings.ntp_server[0]=='\0');}}

static void interval_settings(void)
{
 gateway_persistent_config_t a,b;char text[GATEWAY_CONFIG_MAX_BYTES];size_t n;unsigned int i;
 const unsigned int hours[]={1U,6U,24U};gateway_persistent_defaults(&a);
 CHECK(a.settings.ntp_interval_hours==1U);
 for(i=0;i<3U;++i){a.settings.ntp_interval_hours=hours[i];CHECK(gateway_config_encode(&a,text,sizeof(text),&n)==GATEWAY_CONFIG_OK);CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_OK&&b.settings.ntp_interval_hours==hours[i]);}
 n=replace_value(text,n,"time.ntp_interval_hours=24\n","");rechecksum(text,n);
 CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_OK&&b.settings.ntp_interval_hours==1U);
 a.settings.ntp_interval_hours=0U;CHECK(gateway_product_settings_validate(&a.settings)!=0);
 a.settings.ntp_interval_hours=2U;CHECK(gateway_product_settings_validate(&a.settings)!=0);
 a.settings.ntp_interval_hours=24U;CHECK(gateway_config_encode(&a,text,sizeof(text),&n)==GATEWAY_CONFIG_OK);
 n=replace_value(text,n,"time.ntp_interval_hours=24","time.ntp_interval_hours=0");rechecksum(text,n);
 CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_INVALID);
 n=replace_value(text,n,"time.ntp_interval_hours=0","time.ntp_interval_hours=-4294967295");rechecksum(text,n);
 CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_INVALID);
 n=replace_value(text,n,"time.ntp_interval_hours=-4294967295","time.ntp_interval_hours=+1");rechecksum(text,n);
 CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_INVALID);
}
static void interval_crc_and_rollback(const char *d)
{
 gateway_persistent_config_t a,b;gateway_config_source_t source;char text[GATEWAY_CONFIG_MAX_BYTES];size_t n;
 remove_files(d);gateway_persistent_defaults(&a);
 CHECK(gateway_config_save(d,&a,GATEWAY_SAVE_FAIL_NONE)==GATEWAY_CONFIG_OK);
 a.settings.ntp_interval_hours=6U;
 CHECK(gateway_config_stage(d,&a,GATEWAY_SAVE_FAIL_NONE)==GATEWAY_CONFIG_OK);
 CHECK(gateway_config_promote(d,GATEWAY_SAVE_FAIL_PROMOTE_RENAME)==GATEWAY_CONFIG_IO_ERROR);
 CHECK(gateway_config_startup_select(d,&b,&source)==GATEWAY_CONFIG_OK&&source==GATEWAY_CONFIG_SOURCE_BACKUP&&b.settings.ntp_interval_hours==1U);
 CHECK(gateway_config_encode(&a,text,sizeof(text),&n)==GATEWAY_CONFIG_OK);
 CHECK(strstr(text,"ntp_test")==0);
 n=replace_value(text,n,"time.ntp_interval_hours=6","time.ntp_interval_hours=1");
 CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_INVALID);
 rechecksum(text,n);CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_OK&&b.settings.ntp_interval_hours==1U);
 n=replace_value(text,n,"time.ntp_interval_hours=1","time.ntp_interval_hours=4294967295");rechecksum(text,n);
 CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_INVALID);
 n=replace_value(text,n,"time.ntp_interval_hours=4294967295","time.ntp_interval_hours=-4294967295");rechecksum(text,n);
 CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_INVALID);
 n=replace_value(text,n,"time.ntp_interval_hours=-4294967295","time.ntp_interval_hours=+1");rechecksum(text,n);
 CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_INVALID);remove_files(d);
}

static void web_protocol_roundtrip(void)
{
 gateway_persistent_config_t a,b;char text[GATEWAY_CONFIG_MAX_BYTES];size_t n;unsigned int schema,mode;
 for(schema=1;schema<=3;++schema)for(mode=0;mode<2;++mode){
  gateway_persistent_defaults(&a);a.schema_version=schema;a.settings.web_protocol=mode;
  if(schema<3&&!mode){CHECK(gateway_config_encode(&a,text,sizeof(text),&n)==GATEWAY_CONFIG_INVALID);continue;}
  CHECK(gateway_config_encode(&a,text,sizeof(text),&n)==GATEWAY_CONFIG_OK);
  CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_OK&&b.settings.web_protocol==mode);
  if(schema==3){n=replace_value(text,n,mode?"web.protocol=1":"web.protocol=0","web.protocol=2");rechecksum(text,n);CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_INVALID);}
 }
}

static void binding_policy_roundtrip(void)
{
    gateway_persistent_config_t a,b;char text[GATEWAY_CONFIG_MAX_BYTES];size_t n;unsigned int i;
    gateway_persistent_defaults(&a);
    for(i=0;i<8U;++i)a.ports[i].bind_policy=i%4U;
    CHECK(gateway_config_encode(&a,text,sizeof(text),&n)==GATEWAY_CONFIG_OK);
    CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_OK&&!memcmp(&a,&b,sizeof(a)));
    n=replace_value(text,n,"port.2.bind_policy=1","port.2.bind_policy=4");rechecksum(text,n);
    CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_INVALID);
    gateway_persistent_defaults(&a);CHECK(gateway_config_encode(&a,text,sizeof(text),&n)==GATEWAY_CONFIG_OK);
    CHECK(strstr(text,"bind_policy")==0);CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_OK&&!b.ports[0].bind_policy);
}
static void backlight_roundtrip(void){gateway_persistent_config_t a,b;char text[GATEWAY_CONFIG_MAX_BYTES];size_t n;gateway_persistent_defaults(&a);CHECK(a.settings.backlight_on==1);CHECK(gateway_config_encode(&a,text,sizeof(text),&n)==GATEWAY_CONFIG_OK);CHECK(!strstr(text,"display.backlight"));CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_OK&&b.settings.backlight_on==1);
 a.settings.backlight_on=0;CHECK(gateway_config_encode(&a,text,sizeof(text),&n)==GATEWAY_CONFIG_OK);CHECK(strstr(text,"display.backlight_on=0")!=0);CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_OK);CHECK(!memcmp(&a,&b,sizeof(a)));a.settings.backlight_on=2;CHECK(gateway_config_encode(&a,text,sizeof(text),&n)==GATEWAY_CONFIG_INVALID);
}
static void backlight_durable(const char*d){gateway_persistent_config_t a,b;char file[256],text[GATEWAY_CONFIG_MAX_BYTES];size_t n;gateway_config_source_t source;remove_files(d);gateway_persistent_defaults(&a);a.settings.backlight_on=0;
 CHECK(gateway_config_save(d,&a,GATEWAY_SAVE_FAIL_NONE)==GATEWAY_CONFIG_OK);CHECK(gateway_config_startup_select(d,&b,&source)==GATEWAY_CONFIG_OK&&!b.settings.backlight_on);
 a.settings.ntp_interval_hours=6;CHECK(gateway_config_save(d,&a,GATEWAY_SAVE_FAIL_NONE)==GATEWAY_CONFIG_OK);CHECK(gateway_config_startup_select(d,&b,&source)==GATEWAY_CONFIG_OK&&!b.settings.backlight_on&&b.settings.ntp_interval_hours==6);
 a.settings.backlight_on=1;CHECK(gateway_config_save(d,&a,GATEWAY_SAVE_FAIL_FILE_SYNC)==GATEWAY_CONFIG_IO_ERROR);CHECK(gateway_config_startup_select(d,&b,&source)==GATEWAY_CONFIG_OK&&!b.settings.backlight_on);
 CHECK(gateway_config_encode(&b,text,sizeof(text),&n)==GATEWAY_CONFIG_OK);n=replace_value(text,n,"display.backlight_on=0","display.backlight_on=2");rechecksum(text,n);CHECK(gateway_config_decode(text,n,&b)==GATEWAY_CONFIG_INVALID);
 path(file,d,GATEWAY_CONFIG_ACTIVE_NAME);(void)file;remove_files(d);
}
int main(int argc,char**argv){web_protocol_roundtrip();backlight_roundtrip();binding_policy_roundtrip();if(argc!=2)return 2;if(strcmp(argv[1],"--parser-only")==0){interval_settings();printf("interval parser checks=%u failed=%u word_bits=%u\n",checks,failed,(unsigned)(sizeof(unsigned long)*8U));return failed?1:0;}backlight_durable(argv[1]);interval_crc_and_rollback(argv[1]);interval_settings();round_trips();strict_parser_matrix();invalid_typed();filesystem_cases(argv[1]);staged_runtime_contract(argv[1]);product_time_settings();printf("persistence checks=%u failed=%u config=%u max_file=%u max_fds=2 steady_files=2\n",checks,failed,gateway_persistent_config_memory_bytes(),GATEWAY_CONFIG_MAX_BYTES);return failed?1:0;}
