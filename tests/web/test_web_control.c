/* Reuse the application fake drivers and its regression suite. */
#define main application_regressions
#include "../app/test_gateway_application.c"
#undef main
#include "web/web_control.h"
#include <stdlib.h>
int main(void)
{
    fixture_t f, reboot;
    gateway_persistent_config_t before, restored;
    web_status_t status;
    char addresses[2][16], url[64], encoded[GATEWAY_CONFIG_MAX_BYTES];
    size_t length; unsigned int i, calls;
    if(application_regressions()) return 1;
    prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);step_to_runtime(&f);
    before=f.application.coordinator.selected;
    CHECK(before.settings.web_enabled==0&&before.settings.web_interface==0);
    CHECK(web_control_set(&f.application,&before,1,0,0)==-3);
    CHECK(web_control_set(&f.application,&before,2,0,1)==-1);
    CHECK(web_control_set(&f.application,&before,0,0,1)==0&&f.stage_calls==0);
    CHECK(web_control_set(&f.application,&before,1,2,1)==0);
    CHECK(web_control_set(&f.application,&before,1,0,1)==-1); /* in-flight */
    for(i=0;i<40;++i)gateway_application_step(&f.application);
    web_control_status(&f.application,WEB_FAILED,98,&status);
    CHECK(status.saved_enabled==1&&status.interface==2&&status.actual==WEB_FAILED&&status.error==98);
    CHECK(f.application.process_state==GATEWAY_PROCESS_RUNNING);
    for(i=0;i<8;++i)CHECK(f.backend[i].opens==1&&f.backend[i].stops==0);
    CHECK(web_control_set(&f.application,&before,0,0,1)==-2);
    before=f.application.coordinator.selected;
    CHECK(gateway_config_encode(&before,encoded,sizeof(encoded),&length)==GATEWAY_CONFIG_OK);
    CHECK(gateway_config_decode(encoded,length,&restored)==GATEWAY_CONFIG_OK);
    CHECK(restored.settings.web_enabled==1&&restored.settings.web_interface==2&&restored.schema_version==2);
    prepare(&reboot,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);
    reboot.config=restored;step_to_runtime(&reboot);
    CHECK(reboot.application.coordinator.selected.settings.web_enabled==1);
    CHECK(reboot.application.coordinator.selected.settings.web_interface==2);
    calls=f.stage_calls;
    CHECK(web_control_set(&f.application,&before,1,2,1)==0&&f.stage_calls==calls);
    /* Concurrent panel edit invalidates the snapshot, even for another field. */
    f.application.coordinator.selected.settings.backlight_on=0;
    CHECK(web_control_set(&f.application,&before,0,0,1)==-2);
    before=f.application.coordinator.selected;
    f.promote_result=GATEWAY_CONFIG_IO_ERROR;
    CHECK(web_control_set(&f.application,&before,0,0,1)==0);
    for(i=0;i<40;++i)gateway_application_step(&f.application);
    CHECK(f.application.coordinator.selected.settings.web_enabled==1);
    f.promote_result=GATEWAY_CONFIG_OK;
    CHECK(web_control_set(&f.application,&before,0,1,1)==0);
    for(i=0;i<40;++i)gateway_application_step(&f.application);
    web_control_status(&f.application,WEB_STOPPED,0,&status);
    CHECK(!status.saved_enabled&&status.interface==1&&status.actual==WEB_STOPPED);
    CHECK(web_control_addresses(0,"10.0.2.1",NULL,addresses)==1&&!strcmp(addresses[0],"10.0.2.1"));
    CHECK(web_control_addresses(1,"10.0.2.1","10.0.3.1",addresses)==1&&!strcmp(addresses[0],"10.0.3.1"));
    CHECK(web_control_addresses(2,"10.0.2.1","10.0.3.1",addresses)==2);
    CHECK(web_control_addresses(2,"10.0.2.1","",addresses)==-1&&!addresses[0][0]);
    CHECK(web_control_addresses(2,"10.0.2.1","10.0.2.1",addresses)==-1);
    CHECK(web_control_addresses(0,"0.0.0.0","10.0.3.1",addresses)==-1);
    CHECK(web_control_addresses(0,"224.0.0.1","10.0.3.1",addresses)==-1);
    CHECK(web_control_redirect("10.0.3.1",url,sizeof(url))==0&&!strcmp(url,"https://10.0.3.1/"));
    CHECK(web_control_redirect("evil\r\nHost: foo",url,sizeof(url))==-1);
    CHECK(web_control_redirect("10.0.3.1",url,4)==-1&&!url[0]);
    printf("web control + application: checks=%u failed=%u\n",checks,failed);
    return failed?1:0;
}
