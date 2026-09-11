#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "web/web_control.h"
int web_control_set(gateway_application_t *a, const gateway_persistent_config_t *expected,
                    unsigned int enabled, unsigned int interface, int admin)
{
    gateway_persistent_config_t candidate;
    char current[GATEWAY_CONFIG_MAX_BYTES], previous[GATEWAY_CONFIG_MAX_BYTES];
    size_t cn, pn;
    if (!admin) return -3;
    if (!a || !expected || enabled > 1U || interface > 2U) return -1;
    if (gateway_config_encode(&a->coordinator.selected,current,sizeof(current),&cn) ||
        gateway_config_encode(expected,previous,sizeof(previous),&pn)) return -1;
    if (cn != pn || memcmp(current,previous,cn)) return -2;
    candidate = a->coordinator.selected;
    if (candidate.settings.web_enabled != enabled || candidate.settings.web_interface != interface)
        candidate.schema_version = candidate.schema_version < 2U ? 2U : candidate.schema_version;
    candidate.settings.web_enabled = enabled;
    candidate.settings.web_interface = interface;
    return gateway_application_request_configuration(a, &candidate);
}
void web_control_status(const gateway_application_t *a, web_actual_t actual, int error, web_status_t *out)
{
    out->saved_enabled = a->coordinator.selected.settings.web_enabled;
    out->interface = a->coordinator.selected.settings.web_interface;
    out->actual = actual; out->error = error;
    out->transaction = gateway_application_configuration_state(a);
}
static int address_valid(const char *address)
{
    struct in_addr in; unsigned long host;
    if (!address || strlen(address)>15U || inet_pton(AF_INET,address,&in)!=1) return 0;
    host = ntohl(in.s_addr);
    return host && host!=0xffffffffUL && (host>>24)!=127U && (host>>24)<224U;
}
int web_control_addresses(unsigned int interface, const char *eth0, const char *eth1, char out[2][16])
{
    unsigned int i, count=0; const char *in[2]; in[0]=eth0;in[1]=eth1;
    memset(out,0,32);
    if (interface>2U) return -1;
    for(i=0;i<2U;++i) if(interface==2U||interface==i) {
        if(!address_valid(in[i])) { memset(out,0,32); return -1; }
        if(count && !strcmp(out[0],in[i])) { memset(out,0,32); return -1; }
        strcpy(out[count++],in[i]);
    }
    return (int)count;
}
int web_control_redirect(const char *local, char *out, size_t n)
{
    int r;
    if(!out||!n) return -1;
    out[0]=0;
    if(!address_valid(local)) return -1;
    r=snprintf(out,n,"https://%s/",local);
    if(r<0||(size_t)r>=n) { out[0]=0;return -1; }
    return 0;
}
