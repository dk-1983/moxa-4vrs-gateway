#include <stdio.h>
#include <string.h>
#include "network/gateway_network_document.h"

/* A line is bounded independently of the whole file. Shell continuations and
 * interface mappings cannot be interpreted safely as this typed model. */
#define LINE_MAX_BYTES 512U
typedef struct line {
    const char *begin;
    size_t size;
    char words[LINE_MAX_BYTES];
    char *key, *value;
} line_t;

static int next_line(const char *input, size_t length, size_t *offset, line_t *l)
{
    size_t end = *offset, n;
    char *p;
    if (end == length) return 0;
    while (end < length && input[end] != '\n') ++end;
    n = end - *offset;
    if (n >= sizeof(l->words)) return -1;
    l->begin = input + *offset;
    l->size = n + (end < length ? 1U : 0U);
    memcpy(l->words, l->begin, n);
    l->words[n] = '\0';
    if (memchr(l->words, '\0', n)) return -1;
    *offset += l->size;
    p = l->words;
    while (*p == ' ' || *p == '\t') ++p;
    l->key = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r') ++p;
    if (*p) *p++ = '\0';
    while (*p == ' ' || *p == '\t') ++p;
    l->value = p;
    n = strlen(p);
    while (n && (p[n-1U] == ' ' || p[n-1U] == '\t' || p[n-1U] == '\r')) p[--n] = '\0';
    return 1;
}

static int copy_ip(char *out, const char *value)
{
    unsigned long ip;
    if (gateway_ipv4_parse(value, &ip)) return -1;
    strcpy(out, value);
    return 0;
}

static int iface(const line_t *l, int *owned, gateway_lan_mode_t *mode)
{
    char name[32], family[32], method[32], extra[2];
    int n;
    if (strcmp(l->key, "iface")) return 0;
    n = sscanf(l->value, "%31s %31s %31s %1s", name, family, method, extra);
    if (n != 3) return -1;
    *owned = !strcmp(name,"eth0") ? 0 : !strcmp(name,"eth1") ? 1 : -1;
    if (*owned < 0) return 1;
    if (strcmp(family,"inet")) return -1;
    if (!strcmp(method,"static")) *mode = GATEWAY_LAN_STATIC;
    else if (!strcmp(method,"dhcp")) *mode = GATEWAY_LAN_DHCP_CLIENT;
    else return -1;
    return 1;
}

/* Vendor singular/plural forms both contain one or two literal IPv4 values.
 * Never evaluate lease data, shell syntax or per-interface DNS hooks. */
static int dns_line(const char *value,char dns[2][GATEWAY_IPV4_TEXT_MAX])
{
    char extra[2];int count;unsigned int i;unsigned long ip;
    memset(dns,0,2U*GATEWAY_IPV4_TEXT_MAX);
    count=sscanf(value,"%15s %15s %1s",dns[0],dns[1],extra);
    if(count<1||count>2)return -1;
    for(i=0;i<(unsigned int)count;++i)
        if(gateway_ipv4_parse(dns[i],&ip)||!ip||(ip>>24)==127UL||(ip>>24)>=224UL)return -1;
    if(count==2&&!strcmp(dns[0],dns[1]))return -1;
    return 0;
}
static int dns_key(const char *key)
{return !strcmp(key,"dns-nameserver")||!strcmp(key,"dns-nameservers");}

static int import_document(const char *input, size_t size, const char *resolver,
                           size_t resolver_size, gateway_network_settings_t *out,int compare_dns)
{
    gateway_network_settings_t s;
    size_t off = 0;
    unsigned int seen = 0, fields[2] = {0,0}, dns_count = 0, i;
    char networks[2][16]={{0},{0}},vendor_dns[2][2][GATEWAY_IPV4_TEXT_MAX];
    unsigned int dns_seen[2]={0,0};
    int owned = -1, r;
    line_t l;
    gateway_lan_mode_t mode = GATEWAY_LAN_STATIC;
    if (!input || !resolver || !out || !size || size > GATEWAY_NETWORK_FILE_MAX ||
        resolver_size > GATEWAY_NETWORK_FILE_MAX) return -1;
    gateway_network_settings_init(&s);
    while ((r = next_line(input,size,&off,&l)) > 0) {
        unsigned int bit = 0;
        char *target = 0;
        if (!l.key[0] || l.key[0] == '#') continue;
        if (strchr(l.value,'\\') || !strcmp(l.key,"mapping") ||
            !strcmp(l.key,"source") || !strcmp(l.key,"source-directory")) return -1;
        r = iface(&l,&owned,&mode);
        if (r < 0) return -1;
        if (r) {
            if (owned >= 0) {
                if (seen & (1U << owned)) return -1;
                seen |= 1U << owned;
                s.lan[owned].mode = mode;
            }
            continue;
        }
        if (!strcmp(l.key,"auto") || !strcmp(l.key,"allow-hotplug")) { owned = -1; continue; }
        if (owned < 0) continue;
        if (!strcmp(l.key,"address")) { target=s.lan[owned].address; bit=1U; }
        else if (!strcmp(l.key,"netmask")) { target=s.lan[owned].netmask; bit=2U; }
        else if (!strcmp(l.key,"gateway")) { target=s.lan[owned].gateway; bit=4U; }
        else if (!strcmp(l.key,"network")) { target=networks[owned]; bit=8U; }
        if (target) {
            if ((fields[owned]&bit) || copy_ip(target,l.value)) return -1;
            fields[owned] |= bit;
            if (bit == 4U) {
                if (s.default_lan) return -1;
                s.default_lan = (unsigned int)owned + 1U;
            }
        } else if(dns_key(l.key)) {
            if(dns_seen[owned]++||dns_line(l.value,vendor_dns[owned]))return -1;
        } else if (strcmp(l.key,"network") && strcmp(l.key,"broadcast") &&
                   strcmp(l.key,"dns-nameserver") && strcmp(l.key,"dns-nameservers") &&
                   strcmp(l.key,"pre-up") && strcmp(l.key,"up") &&
                   strcmp(l.key,"down") && strcmp(l.key,"post-down")) {
            /* Do not silently reinterpret VLANs, point-to-point links,
             * metrics or vendor options as ordinary Ethernet settings. */
            return -1;
        }
    }
    if (r < 0 || seen != 3U) return -1;
    for(i=0;i<2U;++i)if(fields[i]&8U){
        unsigned long ip,mask,network;
        if(s.lan[i].mode!=GATEWAY_LAN_STATIC||gateway_ipv4_parse(s.lan[i].address,&ip)||
           gateway_ipv4_parse(s.lan[i].netmask,&mask)||gateway_ipv4_parse(networks[i],&network)||
           network!=(ip&mask))return -1;
    }
    off = 0;
    while ((r=next_line(resolver,resolver_size,&off,&l)) > 0) {
        if (strcmp(l.key,"nameserver")) continue;
        if (dns_count == 2U || copy_ip(s.dns[dns_count++],l.value)) return -1;
    }
    if (r < 0 || gateway_network_settings_validate(&s,0)) return -1;
    if(compare_dns)for(i=0;i<2U;++i)if(dns_seen[i]&&
        (strcmp(vendor_dns[i][0],s.dns[0])||strcmp(vendor_dns[i][1],s.dns[1])))return -1;
    *out = s;
    return 0;
}

int gateway_network_import(const char *input,size_t size,const char *resolver,
                           size_t resolver_size,gateway_network_settings_t *out)
{return import_document(input,size,resolver,resolver_size,out,1);}

static int append(char *out, size_t cap, size_t *used, const char *p, size_t n)
{
    if (*used >= cap || n >= cap - *used) return -1;
    memcpy(out+*used,p,n); *used += n; out[*used]='\0'; return 0;
}

static int render_lan(const gateway_network_settings_t *s, unsigned int lan,
                      char *out, size_t cap, size_t *used)
{
    char b[256];
    int n;
    unsigned long ip, mask, broadcast, network;
    const gateway_lan_settings_t *p = &s->lan[lan];
    n = snprintf(b,sizeof(b),"iface eth%u inet %s\n",lan,
                 p->mode == GATEWAY_LAN_STATIC ? "static" : "dhcp");
    if (n < 0 || (size_t)n >= sizeof(b) || append(out,cap,used,b,(size_t)n)) return -1;
    if (p->mode == GATEWAY_LAN_DHCP_CLIENT) return 0;
    if (gateway_ipv4_parse(p->address,&ip) || gateway_ipv4_parse(p->netmask,&mask)) return -1;
    network = ip & mask;
    broadcast = ip | ((~mask)&0xffffffffUL);
    n = snprintf(b,sizeof(b),"\taddress %s\n\tnetwork %lu.%lu.%lu.%lu\n\tnetmask %s\n\tbroadcast %lu.%lu.%lu.%lu\n",
                 p->address,network>>24,(network>>16)&255UL,(network>>8)&255UL,network&255UL,p->netmask,broadcast>>24,(broadcast>>16)&255UL,
                 (broadcast>>8)&255UL,broadcast&255UL);
    if (n < 0 || (size_t)n >= sizeof(b) || append(out,cap,used,b,(size_t)n)) return -1;
    if (s->default_lan == lan+1U) {
        n=snprintf(b,sizeof(b),"\tgateway %s\n",p->gateway);
        if (n < 0 || (size_t)n >= sizeof(b) || append(out,cap,used,b,(size_t)n)) return -1;
    }
    return 0;
}

int gateway_network_render(const char *input, size_t size,
                           const gateway_network_settings_t *s,
                           char *out, size_t cap, size_t *length)
{
    gateway_network_settings_t imported;
    size_t off=0,used=0;
    int owned=-1,r;
    unsigned int changed[2], route_changed[2], i;
    gateway_lan_mode_t mode=GATEWAY_LAN_STATIC;
    line_t l;
    if (length) *length=0;
    if (!out || !cap || !length || gateway_network_settings_validate(s,0) ||
        import_document(input,size,"",0,&imported,0)) return -1;
    for (i=0;i<2U;++i) {
        changed[i] = s->lan[i].mode != imported.lan[i].mode ||
            strcmp(s->lan[i].address,imported.lan[i].address) ||
            strcmp(s->lan[i].netmask,imported.lan[i].netmask);
        route_changed[i] =
            ((s->default_lan == i+1U) != (imported.default_lan == i+1U)) ||
            (s->default_lan == i+1U &&
             strcmp(s->lan[i].gateway,imported.lan[i].gateway));
    }
    while ((r=next_line(input,size,&off,&l)) > 0) {
        r=iface(&l,&owned,&mode);
        if (r < 0) return -1;
        if (r && owned >= 0 && changed[owned]) {
            if (render_lan(s,(unsigned int)owned,out,cap,&used)) return -1;
            continue;
        }
        /* A route-only edit must not normalize the address stanza: some
         * installed profiles intentionally retain a historical broadcast. */
        if (r && owned >= 0 && route_changed[owned]) {
            char gateway[48]; int n;
            if (append(out,cap,&used,l.begin,l.size)) return -1;
            if (s->default_lan == (unsigned int)owned+1U &&
                s->lan[owned].mode == GATEWAY_LAN_STATIC) {
                if (used && out[used-1U]!='\n' && append(out,cap,&used,"\n",1)) return -1;
                n=snprintf(gateway,sizeof(gateway),"\tgateway %s\n",s->lan[owned].gateway);
                if (n<0 || (size_t)n>=sizeof(gateway) || append(out,cap,&used,gateway,(size_t)n)) return -1;
            }
            continue;
        }
        if (!strcmp(l.key,"auto") || !strcmp(l.key,"allow-hotplug")) owned=-1;
        if(owned>=0&&dns_key(l.key)) {
            char old[2][GATEWAY_IPV4_TEXT_MAX],b[80];int n;
            if(dns_line(l.value,old))return -1;
            if(s->automatic_dns)continue;
            if(strcmp(old[0],s->dns[0])||strcmp(old[1],s->dns[1])) {
                if(!s->dns[0][0])continue;
                n=snprintf(b,sizeof(b),"\t%s %s%s%s\n",l.key,s->dns[0],
                           s->dns[1][0]?" ":"",s->dns[1]);
                if(n<0||(size_t)n>=sizeof(b)||append(out,cap,&used,b,(size_t)n))return -1;
                continue;
            }
            if(append(out,cap,&used,l.begin,l.size))return -1;
            continue;
        }
        if (owned >= 0 && route_changed[owned] && !strcmp(l.key,"gateway")) continue;
        if (owned >= 0 && changed[owned] && (!strcmp(l.key,"address") || !strcmp(l.key,"netmask") ||
            !strcmp(l.key,"network") || !strcmp(l.key,"broadcast") ||
            !strcmp(l.key,"gateway"))) continue;
        if (append(out,cap,&used,l.begin,l.size)) return -1;
    }
    if (r < 0) return -1;
    *length=used; return 0;
}

int gateway_network_resolver(const char *input, size_t size,
                             const char dns[2][GATEWAY_IPV4_TEXT_MAX],
                             char *out, size_t cap, size_t *length)
{
    line_t l;
    size_t off=0,used=0;
    int r;
    unsigned int i;
    if (length) *length=0;
    if (!input || size > GATEWAY_NETWORK_FILE_MAX || !dns || !out || !cap || !length) return -1;
    out[0]='\0';
    while ((r=next_line(input,size,&off,&l)) > 0) {
        if (!strcmp(l.key,"nameserver")) continue;
        if (append(out,cap,&used,l.begin,l.size)) return -1;
    }
    if (r < 0) return -1;
    if (used && out[used-1U]!='\n' && append(out,cap,&used,"\n",1)) return -1;
    for (i=0;i<2U;++i) if (dns[i][0]) {
        char b[40]; unsigned long ip; int n;
        if (gateway_ipv4_parse(dns[i],&ip) || !ip || (ip>>24)==127UL || (ip>>24)>=224UL) return -1;
        n=snprintf(b,sizeof(b),"nameserver %s\n",dns[i]);
        if (n < 0 || (size_t)n>=sizeof(b) || append(out,cap,&used,b,(size_t)n)) return -1;
    }
    *length=used; return 0;
}
