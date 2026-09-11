#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "network/modbus_udp_listener.h"
#include "modbus/modbus_crc.h"

static void inc(unsigned int *v) { if (*v != 0xffffffffU) ++*v; }
static unsigned int be16(const unsigned char *p) { return ((unsigned int)p[0] << 8) | p[1]; }
static void crc_append(unsigned char *p, unsigned int n)
{
    unsigned short crc = modbus_crc16(p, n);
    p[n] = (unsigned char)crc; p[n+1U] = (unsigned char)(crc >> 8);
}

/* Validate supported request shapes before any request can reach a UART. */
static int request_valid(const unsigned char *r, unsigned int n)
{
    unsigned int fc, count, bytes, address;
    if (n < 8U || r[0] == 0U || r[0] > 247U) return 0;
    fc = r[1]; address = be16(r+2); count = be16(r+4);
    if (fc >= 1U && fc <= 4U)
        return n == 8U && count > 0U && count <= (fc <= 2U ? 2000U : 125U) && address+count <= 65536U;
    if (fc == 5U) return n == 8U && (count == 0U || count == 0xff00U);
    if (fc == 6U) return n == 8U;
    if (fc != 15U && fc != 16U) return 0;
    if (n < 10U || count == 0U || count > (fc == 15U ? 1968U : 123U) || address+count > 65536U) return 0;
    bytes = fc == 15U ? (count+7U)/8U : count*2U;
    return r[6] == bytes && n == bytes+9U;
}

static void send_response(modbus_udp_listener_t *l, modbus_udp_peer_t *p)
{
    ssize_t sent;
    if (!p->response_length || l->fd < 0) return;
    sent = sendto(l->fd, p->response, p->response_length, 0,
                  (const struct sockaddr *)&p->address, sizeof(p->address));
    if (sent == (ssize_t)p->response_length) inc(&l->stats.sent);
    else inc(&l->stats.send_errors); /* Atomic datagram, never a partial retry. */
}

static void finish(modbus_udp_listener_t *l, modbus_udp_peer_t *p)
{
    p->pending = p->submitted = 0;
    p->cached = !l->rtu_mode;
    p->completed_at = p->last_seen = l->now;
    send_response(l, p);
}

static void completion(void *context, const core_transaction_t *t,
                       core_transaction_status_t status,
                       const unsigned char *response, unsigned int n)
{
    modbus_udp_listener_t *l = context;
    modbus_udp_peer_t *p;
    unsigned char mbap[MBAP_ADU_MAX];
    unsigned int size = 0;
    int result;
    if (l->fd < 0 || t->owner_kind != MODBUS_OWNER_UDP || t->owner_id >= MODBUS_UDP_PEERS) return;
    p = &l->peers[t->owner_id];
    if (!p->used || !p->submitted || p->epoch != t->transport_metadata[MODBUS_META_CLIENT_EPOCH] ||
        p->transaction_id != t->id || (t->generation && t->generation != l->port->generation)) {
        inc(&l->stats.stale); return;
    }
    p->response_length = 0;
    if (status == CORE_TX_COMPLETED) {
        result = modbus_tcp_build_response(t, response, n, mbap, sizeof(mbap), &size);
        if (result == 0) {
            unsigned int frame_length = 0;
            if (modbus_rtu_frame_probe(0,t->payload,t->payload_length,response,n,&frame_length) != UART_FRAME_COMPLETE)
                result = -5;
            else if (!(response[1] & 0x80U)) {
                unsigned int fc=t->payload[1], count=be16(t->payload+4);
                if (fc<=4U) {
                    unsigned int bytes=fc<=2U?(count+7U)/8U:count*2U;
                    if (response[2]!=bytes) result=-5;
                } else if (memcmp(response+2,t->payload+2,4U)) result=-5;
            }
        }
        if (result == 0) {
            memcpy(p->response, l->rtu_mode ? response : mbap, l->rtu_mode ? n : size);
            p->response_length = l->rtu_mode ? n : size;
        } else {
            inc(&l->stats.invalid_responses);
            if (result == -2) inc(&l->stats.crc_errors);
        }
    } else {
        if (status == CORE_TX_TIMED_OUT) inc(&l->stats.timeouts);
        else inc(&l->stats.failures);
        if (!l->rtu_mode && status != CORE_TX_CANCELLED)
            modbus_tcp_build_gateway_exception(t, t->request_transmitted ? 0x0bU : 0x0aU,
                p->response, sizeof(p->response), &p->response_length);
    }
    finish(l, p);
}

void modbus_udp_listener_init(modbus_udp_listener_t *l, port_runtime_t *port, int rtu)
{
    memset(l, 0, sizeof(*l)); l->fd = -1; l->port = port;
    l->rtu_mode = rtu ? 1U : 0U; l->next_epoch = 1U;
    port_runtime_set_completion(port, completion, l);
}

int modbus_udp_listener_open(modbus_udp_listener_t *l, const char *address, unsigned short port)
{
    struct sockaddr_in sa;
    int fd, flags;
    if (!l || !address || l->fd >= 0) return -1;
    memset(&sa, 0, sizeof(sa)); sa.sin_family = AF_INET; sa.sin_port = htons(port);
    sa.sin_addr.s_addr = inet_addr(address);
    if (sa.sin_addr.s_addr == INADDR_NONE) return -1;
    fd = socket(AF_INET, SOCK_DGRAM, 0); if (fd < 0) return -1;
    flags = fcntl(fd, F_GETFL, 0);
    /* Deliberately no SO_REUSEADDR: two modes must never share a UDP endpoint. */
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
        bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) { close(fd); return -1; }
    l->fd = fd; return 0;
}

int modbus_udp_listener_receive(modbus_udp_listener_t *l, const struct sockaddr_in *source,
                                const unsigned char *data, unsigned int n, core_tick_t now)
{
    unsigned char rtu[MODBUS_RTU_ADU_MAX];
    unsigned int len, i, free_slot = MODBUS_UDP_PEERS;
    unsigned short tid = 0;
    modbus_udp_peer_t *p = 0;
    if (!l || !source || !data || l->fd < 0 || source->sin_family != AF_INET) return -1;
    l->now = now; inc(&l->stats.received);
    if (l->rtu_mode) {
        if (n < 4U || n > MODBUS_RTU_ADU_MAX) goto malformed;
        if (modbus_crc16(data, n-2U) != (unsigned short)(data[n-2U] | ((unsigned int)data[n-1U] << 8))) {
            inc(&l->stats.crc_errors); goto malformed;
        }
        memcpy(rtu, data, n); len = n;
    } else {
        if (n < 8U || n > MBAP_ADU_MAX || data[2] || data[3] || be16(data+4) != n-6U) goto malformed;
        tid = (unsigned short)be16(data); len = n-4U;
        memcpy(rtu, data+6, n-6U); crc_append(rtu, len-2U);
    }
    if (!request_valid(rtu, len)) goto malformed;
    for (i=0; i<MODBUS_UDP_PEERS; ++i) {
        modbus_udp_peer_t *q = &l->peers[i];
        if (q->used && !q->pending && core_elapsed(q->last_seen, now) >= MODBUS_UDP_IDLE_TIMEOUT)
            memset(q, 0, sizeof(*q));
        if (!q->used) { if (free_slot == MODBUS_UDP_PEERS) free_slot = i; continue; }
        if (q->address.sin_addr.s_addr == source->sin_addr.s_addr && q->address.sin_port == source->sin_port) p = q;
    }
    if (!p) {
        if (free_slot == MODBUS_UDP_PEERS) { inc(&l->stats.no_peer); return -2; }
        p = &l->peers[free_slot]; memset(p, 0, sizeof(*p)); p->used = 1U;
        p->address = *source; p->epoch = l->next_epoch++;
        if (!l->next_epoch) l->next_epoch = 1U;
    }
    if (p->pending || (!l->rtu_mode && p->cached && p->tid == tid && core_elapsed(p->completed_at, now) < MODBUS_UDP_CACHE_TIMEOUT)) {
        if (p->tid == tid && p->rtu_length == len && !memcmp(p->rtu, rtu, len)) {
            inc(&l->stats.duplicates);
            if (!p->pending) send_response(l, p);
        } else if (!l->rtu_mode && p->tid == tid) inc(&l->stats.conflicts);
        else inc(&l->stats.busy);
        return 1;
    }
    p->tid = tid; memcpy(p->rtu, rtu, len); p->rtu_length = len;
    p->pending = 1U; p->submitted = p->cached = p->response_length = 0;
    p->admitted_at = p->last_seen = now; inc(&l->stats.accepted); return 0;
malformed:
    inc(&l->stats.malformed); return -1;
}

static void dispatch_one(modbus_udp_listener_t *l, core_tick_t now)
{
    unsigned int i, index, metadata[CORE_TRANSPORT_METADATA_WORDS];
    core_operation_t op;
    core_submit_result_t result;
    for (i=0; i<MODBUS_UDP_PEERS; ++i) {
        modbus_udp_peer_t *p;
        index = (l->next_peer+i)%MODBUS_UDP_PEERS; p = &l->peers[index];
        if (!p->pending || p->submitted) continue;
        if (core_elapsed(p->admitted_at, now) >= MODBUS_UDP_QUEUE_TIMEOUT) {
            inc(&l->stats.timeouts); p->response_length = 0;
            if (!l->rtu_mode) {
                unsigned char *o = p->response;
                o[0]=(unsigned char)(p->tid>>8); o[1]=(unsigned char)p->tid;
                o[2]=o[3]=o[4]=0; o[5]=3; o[6]=p->rtu[0]; o[7]=(unsigned char)(p->rtu[1]|0x80U); o[8]=0x0a;
                p->response_length=9;
            }
            finish(l, p); continue;
        }
        metadata[0]=p->tid; metadata[1]=((unsigned int)p->rtu[0]<<8)|p->rtu[1]; metadata[2]=p->epoch; metadata[3]=0;
        op.direction=CORE_OPERATION_TX_THEN_RX; op.response_policy=CORE_RESPONSE_BACKEND_FRAMED; op.expected_response_length=0;
        result=port_runtime_submit_operation(l->port,p->rtu,p->rtu_length,MODBUS_OWNER_UDP,index,metadata,&op,now,
            MODBUS_UDP_QUEUE_TIMEOUT-core_elapsed(p->admitted_at,now),MODBUS_UDP_RESPONSE_TIMEOUT,&p->transaction_id);
        if (result==CORE_ACCEPTED) p->submitted=1U;
        l->next_peer=(index+1U)%MODBUS_UDP_PEERS; return;
    }
}

void modbus_udp_listener_step_io(modbus_udp_listener_t *l, core_tick_t now)
{
    unsigned int i;
    if (!l || l->fd < 0) return;
    l->now=now;
    for (i=0; i<MODBUS_UDP_STEP_MAX; ++i) {
        /* One extra byte detects all oversize datagrams even without MSG_TRUNC. */
        unsigned char data[MBAP_ADU_MAX+1U];
        struct sockaddr_in source;
        socklen_t size=sizeof(source);
        ssize_t n=recvfrom(l->fd,data,sizeof(data),0,(struct sockaddr *)&source,&size);
        if (n<0) { if(errno==EINTR) continue; break; }
        if (size==sizeof(source)) modbus_udp_listener_receive(l,&source,data,(unsigned int)n,now);
    }
    dispatch_one(l,now);
    for (i=0; i<MODBUS_UDP_PEERS; ++i)
        if (l->peers[i].used && !l->peers[i].pending && core_elapsed(l->peers[i].last_seen,now)>=MODBUS_UDP_IDLE_TIMEOUT)
            memset(&l->peers[i],0,sizeof(l->peers[i]));
}

void modbus_udp_listener_close(modbus_udp_listener_t *l)
{
    if (!l) return;
    if (l->fd>=0) close(l->fd);
    l->fd=-1; memset(l->peers,0,sizeof(l->peers));
}

unsigned int modbus_udp_listener_peer_count(const modbus_udp_listener_t *l)
{
    unsigned int i,n=0;
    for(i=0;i<MODBUS_UDP_PEERS;++i) if(l->peers[i].used) ++n;
    return n;
}
