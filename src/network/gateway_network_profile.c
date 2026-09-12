#include "core/platform.h"
#include <stdio.h>
#include <string.h>
#include "network/gateway_network_profile.h"

static int documents_agree(const gateway_network_profile_t *p)
{
    gateway_network_settings_t imported;unsigned int i;
    if(gateway_network_import(p->interfaces,strlen(p->interfaces),p->resolver,strlen(p->resolver),&imported))return -1;
    for(i=0;i<2U;++i){
        const gateway_lan_settings_t *s=&p->settings.lan[i],*t=&imported.lan[i];
        if(s->mode!=t->mode)return -1;
        if(s->mode==GATEWAY_LAN_STATIC && (strcmp(s->address,t->address)||strcmp(s->netmask,t->netmask)))return -1;
        if(p->settings.default_lan==i+1U&&s->mode==GATEWAY_LAN_STATIC&&
           (imported.default_lan!=i+1U||strcmp(s->gateway,t->gateway)))return -1;
    }
    if(!p->settings.default_lan&&imported.default_lan)return -1;
    if(!p->settings.automatic_dns&&(strcmp(p->settings.dns[0],imported.dns[0])||
        strcmp(p->settings.dns[1],imported.dns[1])))return -1;
    return 0;
}

/* Metadata and vendor documents share one CRC-protected store payload. No
 * native padding, pointer, word size or byte order is persisted. */
int gateway_network_profile_encode(const gateway_network_profile_t *p,char *out,size_t cap,size_t *length)
{
    unsigned int i;size_t a,b,used;int n;
    if(length)*length=0;
    if(!p||!out||!length||gateway_network_settings_validate(&p->settings,0)||
       !memchr(p->interfaces,0,sizeof(p->interfaces))||!memchr(p->resolver,0,sizeof(p->resolver)))return -1;
    a=strlen(p->interfaces);b=strlen(p->resolver);
    if(documents_agree(p))return -1;
    if(p->settings.dns_lan)n=snprintf(out,cap,"4VRS_PROFILE_2 %u %u %u %lu %lu\n",p->settings.default_lan,p->settings.automatic_dns,
               p->settings.dns_lan,(unsigned long)a,(unsigned long)b);
    else n=snprintf(out,cap,"4VRS_PROFILE_1 %u %u %lu %lu\n",p->settings.default_lan,p->settings.automatic_dns,
               (unsigned long)a,(unsigned long)b);
    if(n<0||(size_t)n>=cap)return -1;
    used=(size_t)n;
    for(i=0;i<2U;++i){
        const gateway_lan_settings_t *s=&p->settings.lan[i];
        n=snprintf(out+used,cap-used,"LAN %u %s %s %s\n",(unsigned int)s->mode,
                   s->address[0]?s->address:"-",s->netmask[0]?s->netmask:"-",s->gateway[0]?s->gateway:"-");
        if(n<0||(size_t)n>=cap-used)return -1;
        used+=(size_t)n;
    }
    n=snprintf(out+used,cap-used,"DNS %s %s\n",p->settings.dns[0][0]?p->settings.dns[0]:"-",
               p->settings.dns[1][0]?p->settings.dns[1]:"-");
    if(n<0||(size_t)n>=cap-used)return -1;
    used+=(size_t)n;
    for(i=0;i<GATEWAY_NETWORK_BINDINGS;++i){
        unsigned long ip;
        if(p->affinity[i]>2U||gateway_ipv4_parse(p->original_bind[i],&ip))return -1;
        n=snprintf(out+used,cap-used,"%u %s\n",p->affinity[i],p->original_bind[i]);
        if(n<0||(size_t)n>=cap-used)return -1;
        used+=(size_t)n;
    }
    if(a>=cap-used||b>=cap-used-a)return -1;
    memcpy(out+used,p->interfaces,a);used+=a;
    memcpy(out+used,p->resolver,b);used+=b;out[used]=0;
    *length=used;return 0;
}

int gateway_network_profile_decode(const char *data,size_t length,gateway_network_profile_t *p)
{
    char line[128],extra;size_t used=0,n;unsigned int i,route,dns,dns_lan=0;unsigned long a,b;
    gateway_network_profile_t result;
    gateway_network_settings_t imported;
    char canonical[GATEWAY_NETWORK_SNAPSHOT_MAX];size_t canonical_length;
    const char *end;
    if(!data||!p||!length||length>GATEWAY_NETWORK_SNAPSHOT_MAX||memchr(data,0,length))return -1;
    end=memchr(data,'\n',length);if(!end||(n=(size_t)(end-data))>=sizeof(line))return -1;
    memcpy(line,data,n);line[n]=0;
    if(!strncmp(line,"4VRS_PROFILE_2 ",15U)){
        if(sscanf(line,"4VRS_PROFILE_2 %u %u %u %lu %lu %c",&route,&dns,&dns_lan,&a,&b,&extra)!=5||!dns_lan||dns_lan>2U)return -1;
    }else if(sscanf(line,"4VRS_PROFILE_1 %u %u %lu %lu %c",&route,&dns,&a,&b,&extra)!=4)return -1;
    if(route>2U||dns>1U||a>=sizeof(result.interfaces)||b>=sizeof(result.resolver))return -1;
    used=n+1U;memset(&result,0,sizeof(result));
    for(i=0;i<3U;++i){
        end=memchr(data+used,'\n',length-used);
        if(!end||(n=(size_t)(end-data-used))>=sizeof(line))return -1;
        memcpy(line,data+used,n);line[n]=0;used+=n+1U;
        if(i<2U){unsigned int mode;gateway_lan_settings_t *s=&result.settings.lan[i];
            if(sscanf(line,"LAN %u %15s %15s %15s %c",&mode,s->address,s->netmask,s->gateway,&extra)!=4||mode>1U)return -1;
            s->mode=(gateway_lan_mode_t)mode;
            if(!strcmp(s->address,"-"))s->address[0]=0;
            if(!strcmp(s->netmask,"-"))s->netmask[0]=0;
            if(!strcmp(s->gateway,"-"))s->gateway[0]=0;
        }else{
            if(sscanf(line,"DNS %15s %15s %c",result.settings.dns[0],result.settings.dns[1],&extra)!=2)return -1;
            if(!strcmp(result.settings.dns[0],"-"))result.settings.dns[0][0]=0;
            if(!strcmp(result.settings.dns[1],"-"))result.settings.dns[1][0]=0;
        }
    }
    for(i=0;i<GATEWAY_NETWORK_BINDINGS;++i){
        unsigned long ip;
        end=memchr(data+used,'\n',length-used);
        if(!end||(n=(size_t)(end-data-used))>=sizeof(line))return -1;
        memcpy(line,data+used,n);line[n]=0;used+=n+1U;
        if(sscanf(line,"%u %15s %c",&result.affinity[i],result.original_bind[i],&extra)!=2||
           result.affinity[i]>2U||gateway_ipv4_parse(result.original_bind[i],&ip))return -1;
    }
    if(a>length-used||b!=length-used-a)return -1;
    memcpy(result.interfaces,data+used,(size_t)a);memcpy(result.resolver,data+used+a,(size_t)b);
    if(gateway_network_import(result.interfaces,(size_t)a,result.resolver,(size_t)b,&imported))return -1;
    result.settings.default_lan=route;result.settings.automatic_dns=dns;result.settings.dns_lan=dns_lan;
    if(gateway_network_settings_validate(&result.settings,0))return -1;
    /* Require a canonical numeric/text encoding, also on 32-bit strtoul/scanf
     * implementations. Signed wrapping forms and embedded whitespace cannot
     * sneak through the typed metadata. */
    if(gateway_network_profile_encode(&result,canonical,sizeof(canonical),&canonical_length)||
       canonical_length!=length||memcmp(canonical,data,length))return -1;
    *p=result;return 0;
}

static void clean_tail(char text[16])
{size_t n=strlen(text);memset(text+n,0,16U-n);}
static void clean_documents(gateway_network_profile_t *p)
{
    size_t n=strlen(p->interfaces);memset(p->interfaces+n,0,sizeof(p->interfaces)-n);
    n=strlen(p->resolver);memset(p->resolver+n,0,sizeof(p->resolver)-n);
}

int gateway_network_profile_candidate(const gateway_network_profile_t *p,
                                      const gateway_network_settings_t *s,gateway_network_profile_t *out)
{
    gateway_network_profile_t c;size_t n;unsigned int i;
    if(!p||!out||gateway_network_settings_validate(s,0))return -1;
    c=*p;c.settings=*s;
    for(i=0;i<2U;++i){
        clean_tail(c.settings.lan[i].address);clean_tail(c.settings.lan[i].netmask);
        clean_tail(c.settings.lan[i].gateway);clean_tail(c.settings.dns[i]);
    }
    if(gateway_network_render(p->interfaces,strlen(p->interfaces),s,c.interfaces,sizeof(c.interfaces),&n))return -1;
    /* Automatic DNS is populated from an accepted lease by the sole provider.
     * Until then preserve resolver search/options and remove stale servers. */
    if(!s->automatic_dns&&!p->settings.automatic_dns&&!memcmp(s->dns,p->settings.dns,sizeof(s->dns))) {
        /* A LAN-only change must not normalize unrelated resolver bytes. */
        clean_documents(&c);*out=c;return 0;
    }
    if(s->automatic_dns){const char empty[2][GATEWAY_IPV4_TEXT_MAX]={{0},{0}};
        if(gateway_network_resolver(p->resolver,strlen(p->resolver),empty,c.resolver,sizeof(c.resolver),&n))return -1;
    }else if(gateway_network_resolver(p->resolver,strlen(p->resolver),s->dns,c.resolver,sizeof(c.resolver),&n))return -1;
    clean_documents(&c);*out=c;return 0;
}

int gateway_network_profile_broadcast(const gateway_network_profile_t *p,unsigned int lan,char out[16])
{
    const char *s;int owned=0;unsigned int seen=0;unsigned long ip,mask,broadcast;
    if(!p||!out||lan>=2U)return -1;
    s=p->interfaces;
    if(gateway_ipv4_parse(p->settings.lan[lan].address,&ip)||gateway_ipv4_parse(p->settings.lan[lan].netmask,&mask))return -1;
    gateway_ipv4_format(ip|(~mask&0xffffffffUL),out);
    while(*s){char line[513],key[64],name[64],extra;size_t n=strcspn(s,"\n");
        if(n>=sizeof(line))return -1;
        memcpy(line,s,n);line[n]=0;s+=n;if(*s)++s;
        if(sscanf(line," %63s",key)!=1||key[0]=='#')continue;
        if(!strcmp(key,"iface")){if(sscanf(line," iface %63s",name)!=1)return -1;owned=!strcmp(name,lan?"" FOURVRS_LAN_PREFIX "1":"" FOURVRS_LAN_PREFIX "0");continue;}
        if(!strcmp(key,"auto")||!strcmp(key,"allow-hotplug")){owned=0;continue;}
        if(owned&&!strcmp(key,"broadcast")){
            if(seen++||sscanf(line," broadcast %15s %c",out,&extra)!=1||gateway_ipv4_parse(out,&broadcast)||
               (broadcast&mask)!=(ip&mask))return -1;
        }
    }
    return 0;
}
