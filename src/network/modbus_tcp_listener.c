#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "network/modbus_tcp_listener.h"
#include "modbus/modbus_crc.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

typedef struct admission_context {
    modbus_tcp_listener_t *listener;
    modbus_tcp_client_t *client;
} admission_context_t;

static void increment(unsigned int *value){if(*value!=0xffffffffU)++*value;}

static int nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ? -1 : 0;
}

static void client_reset(modbus_tcp_client_t *client)
{
    memset(client, 0, sizeof(*client));
    client->fd = -1;
}

static void close_client(modbus_tcp_listener_t *listener,
                         modbus_tcp_client_t *client)
{
    if (client->fd >= 0) {
        modbus_dispatcher_disconnect(&listener->dispatcher, client->id,
                                     client->epoch);
        close(client->fd);
        increment(&listener->stats.closed);
    }
    client_reset(client);
}

static modbus_tcp_client_t *current_client(modbus_tcp_listener_t *listener,
                                           unsigned int id,
                                           unsigned int epoch)
{
    unsigned int i;
    for (i=0;i<MODBUS_LISTENER_CLIENT_MAX;++i)
        if (listener->clients[i].fd >= 0 && listener->clients[i].id == id &&
            listener->clients[i].epoch == epoch)
            return &listener->clients[i];
    return 0;
}

static int admit(void *context, const mbap_adu_t *adu)
{
    admission_context_t *a=(admission_context_t *)context;
    modbus_tcp_request_t request;
    memset(&request,0,sizeof(request));
    request.client_id=a->client->id;
    request.client_epoch=a->client->epoch;
    request.adu=*adu;
    return modbus_dispatcher_enqueue(&a->listener->dispatcher,&request)==0 ? 0 : 1;
}

static void completion(void *context, const core_transaction_t *transaction,
                       core_transaction_status_t status,
                       const unsigned char *response, unsigned int response_length)
{
    modbus_tcp_listener_t *listener=(modbus_tcp_listener_t *)context;
    modbus_tcp_client_t *client;
    unsigned int length;
    int result;
    unsigned char output[MBAP_ADU_MAX];
    if (status == CORE_TX_TIMED_OUT && transaction->request_transmitted) {
        increment(&listener->stats.gateway_target_no_response);
        client=current_client(listener,transaction->owner_id,
            transaction->transport_metadata[MODBUS_META_CLIENT_EPOCH]);
        if(client==0) {
            increment(&listener->stats.timeout_exceptions_obsolete_epoch);
            increment(&listener->stats.stale_completions);
            return;
        }
        if(modbus_tcp_build_gateway_exception(transaction,
            MODBUS_EXCEPTION_GATEWAY_TARGET_NO_RESPONSE,output,sizeof(output),
            &length)!=0) {
            increment(&listener->stats.timeout_exception_output_failures);
            return;
        }
        result=modbus_tcp_listener_queue_response(listener,client->id,
            client->epoch,output,length);
        if(result==0) increment(&listener->stats.timeout_exceptions_queued);
        else increment(&listener->stats.timeout_exception_output_failures);
        if(listener->trace_enabled)fprintf(stderr,"MODBUS_TIMEOUT_EXCEPTION core_id=%u generation=%u tid=%04X unit=%u function=%u queued=%u\n",transaction->id,transaction->generation,transaction->transport_metadata[MODBUS_META_TRANSACTION_ID],output[6],output[7],result==0?1U:0U);
        return;
    }
    if (status != CORE_TX_COMPLETED) {
        if(status==CORE_TX_MALFORMED) {
            unsigned int metadata=transaction->transport_metadata[MODBUS_META_UNIT_FUNCTION];
            if(response_length>=1U&&response[0]!=(unsigned char)(metadata>>8)) {
                if(response_length>=2U&&response[1]==(unsigned char)(metadata>>8))
                    increment(&listener->stats.downstream_leading_garbage);
                increment(&listener->stats.downstream_unit_mismatches);
            } else if(response_length>=2U&&response[1]!=(unsigned char)(metadata&0xffU)&&
                    response[1]!=(unsigned char)((metadata&0xffU)|0x80U))
                increment(&listener->stats.downstream_function_mismatches);
            else if(response_length>=4U) {
                unsigned short actual=(unsigned short)(response[response_length-2U]|
                    ((unsigned short)response[response_length-1U]<<8));
                if(modbus_crc16(response,response_length-2U)!=actual)
                    increment(&listener->stats.downstream_crc_failures);
                else increment(&listener->stats.downstream_framing_failures);
            } else increment(&listener->stats.downstream_framing_failures);
        }
        if(listener->trace_enabled)fprintf(stderr,"MODBUS_COMPLETE core_id=%u generation=%u tid=%04X status=%u length=%u\n",transaction->id,transaction->generation,transaction->transport_metadata[MODBUS_META_TRANSACTION_ID],(unsigned int)status,response_length);
        return;
    }
    client=current_client(listener,transaction->owner_id,
                          transaction->transport_metadata[MODBUS_META_CLIENT_EPOCH]);
    if (client==0) { increment(&listener->stats.stale_completions); return; }
    result=modbus_tcp_build_response(transaction,response,response_length,
                                  output,sizeof(output),&length);
    if (result!=0) {
        increment(&listener->stats.invalid_responses);
        if(result==-2) increment(&listener->stats.downstream_crc_failures);
        else if(result==-3) increment(&listener->stats.downstream_unit_mismatches);
        else if(result==-4) increment(&listener->stats.downstream_function_mismatches);
        else increment(&listener->stats.downstream_framing_failures);
        return;
    }
    if(listener->trace_enabled)fprintf(stderr,"MODBUS_COMPLETE core_id=%u generation=%u tid=%04X unit=%u function=%u crc=valid rtu_length=%u mbap_length=%u\n",transaction->id,transaction->generation,transaction->transport_metadata[MODBUS_META_TRANSACTION_ID],response[0],response[1],response_length,length);
    modbus_tcp_listener_queue_response(listener,client->id,client->epoch,output,length);
}

void modbus_tcp_listener_set_trace(modbus_tcp_listener_t *listener,int enabled)
{if(listener!=0)listener->trace_enabled=enabled?1:0;}

int modbus_tcp_listener_queue_response(modbus_tcp_listener_t *listener,
                                       unsigned int client_id,unsigned int epoch,
                                       const unsigned char *data,unsigned int length)
{
    modbus_tcp_client_t *client;modbus_tx_slot_t *slot;
    if(listener==0||data==0||length==0||length>MBAP_ADU_MAX)return -1;
    client=current_client(listener,client_id,epoch);
    if(client==0){increment(&listener->stats.stale_completions);return -2;}
    if(client->tx_count>=MODBUS_TX_QUEUE_MAX){increment(&listener->stats.tx_overflow);close_client(listener,client);return -3;}
    slot=&client->tx[(client->tx_head+client->tx_count)%MODBUS_TX_QUEUE_MAX];
    memcpy(slot->data,data,length);slot->length=length;
    if(client->tx_count++==0)client->write_started_at=listener->now;
    return 0;
}

void modbus_tcp_listener_init(modbus_tcp_listener_t *listener,
                              port_runtime_t *port)
{
    unsigned int i;
    memset(listener,0,sizeof(*listener)); listener->listen_fd=-1;
    listener->next_epoch=1; listener->port=port;
    for (i=0;i<MODBUS_LISTENER_CLIENT_MAX;++i) client_reset(&listener->clients[i]);
    modbus_dispatcher_init(&listener->dispatcher);
    port_runtime_set_completion(port,completion,listener);
}

int modbus_tcp_listener_open(modbus_tcp_listener_t *listener,
                             const char *address, unsigned short port)
{
    struct sockaddr_in sa; int yes=1; int fd;
    if (listener==0 || address==0 || listener->listen_fd>=0) return -1;
    fd=socket(AF_INET,SOCK_STREAM,0); if (fd<0) return -1;
    setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    memset(&sa,0,sizeof(sa)); sa.sin_family=AF_INET; sa.sin_port=htons(port);
    sa.sin_addr.s_addr=inet_addr(address);
    if (sa.sin_addr.s_addr==INADDR_NONE || nonblocking(fd)<0 ||
        bind(fd,(struct sockaddr *)&sa,sizeof(sa))<0 || listen(fd,8)<0) {
        close(fd); return -1;
    }
    listener->listen_fd=fd; return 0;
}

unsigned int modbus_tcp_listener_client_count(const modbus_tcp_listener_t *listener)
{
    unsigned int i,count=0;
    for(i=0;i<MODBUS_LISTENER_CLIENT_MAX;++i) if(listener->clients[i].fd>=0) ++count;
    return count;
}

static void accept_one(modbus_tcp_listener_t *listener, core_tick_t now)
{
    int fd; int yes=1; unsigned int i; modbus_tcp_client_t *client=0;
    fd=accept(listener->listen_fd,0,0);
    if(fd<0) return;
    setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));
    if(nonblocking(fd)<0) { close(fd); return; }
    for(i=0;i<MODBUS_LISTENER_CLIENT_MAX;++i)
        if(listener->clients[i].fd<0) { client=&listener->clients[i]; break; }
    if(client==0) { increment(&listener->stats.refused); close(fd); return; }
    client_reset(client); client->fd=fd; client->id=i+1U;
    client->epoch=listener->next_epoch++;
    if(listener->next_epoch==0) listener->next_epoch=1;
    client->connected_at=client->last_progress=now;
    mbap_stream_init(&client->rx);
    if(modbus_dispatcher_connect(&listener->dispatcher,client->id,client->epoch)<0) {
        close_client(listener,client); return;
    }
    increment(&listener->stats.accepted);
    i=modbus_tcp_listener_client_count(listener);
    if(i>listener->stats.max_clients) listener->stats.max_clients=i;
}

static int client_has_core_work(const modbus_tcp_listener_t *listener,
                                const modbus_tcp_client_t *client)
{
    unsigned int i,index;
    for(i=0;i<MODBUS_CLIENT_MAX;++i)
        if(listener->dispatcher.clients[i].connected &&
           listener->dispatcher.clients[i].id==client->id &&
           listener->dispatcher.clients[i].epoch==client->epoch &&
           listener->dispatcher.clients[i].count!=0)
            return 1;
    if(listener->port->has_active && listener->port->active.owner_id==client->id &&
       listener->port->active.transport_metadata[MODBUS_META_CLIENT_EPOCH]==client->epoch)
        return 1;
    for(i=0;i<listener->port->queue.count;++i) {
        index=(listener->port->queue.head+i)%CORE_QUEUE_CAPACITY;
        if(listener->port->queue.entries[index].owner_id==client->id &&
           listener->port->queue.entries[index].transport_metadata[MODBUS_META_CLIENT_EPOCH]==client->epoch)
            return 1;
    }
    return 0;
}

static void read_client(modbus_tcp_listener_t *listener,
                        modbus_tcp_client_t *client, core_tick_t now)
{
    unsigned char data[MODBUS_SOCKET_IO_STEP_MAX]; ssize_t got; mbap_result_t result;
    admission_context_t a;
    if(client->peer_eof) return;
    got=recv(client->fd,data,sizeof(data),0);
    if(got>0) {
        if(client->rx.count==0) client->frame_started_at=now;
        client->last_progress=now; listener->stats.rx_bytes+=(unsigned int)got;
        a.listener=listener;a.client=client;
        result=mbap_stream_feed(&client->rx,data,(unsigned int)got,admit,&a);
        if(result==MBAP_MALFORMED) { increment(&listener->stats.malformed); close_client(listener,client); }
        else if(result==MBAP_OVERFLOW) { increment(&listener->stats.rx_overflow); close_client(listener,client); }
        return;
    }
    if(got==0) { client->peer_eof=1; return; }
    if(errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) close_client(listener,client);
}

static void drain_parser(modbus_tcp_listener_t *listener,
                         modbus_tcp_client_t *client)
{
    admission_context_t a; mbap_result_t result;
    a.listener=listener;a.client=client;
    result=mbap_stream_feed(&client->rx,0,0,admit,&a);
    if(result==MBAP_MALFORMED) { increment(&listener->stats.malformed); close_client(listener,client); }
}

static void write_client(modbus_tcp_listener_t *listener,
                         modbus_tcp_client_t *client, core_tick_t now)
{
    modbus_tx_slot_t *slot; unsigned int available; unsigned int amount; ssize_t sent;
    if(client->tx_count==0) return;
    slot=&client->tx[client->tx_head]; available=slot->length-client->tx_offset;
    amount=available>MODBUS_SOCKET_IO_STEP_MAX?MODBUS_SOCKET_IO_STEP_MAX:available;
    sent=send(client->fd,slot->data+client->tx_offset,amount,MSG_NOSIGNAL);
    if(sent>0) {
        client->tx_offset+=(unsigned int)sent;client->last_progress=now;
        listener->stats.tx_bytes+=(unsigned int)sent;
        if(client->tx_offset==slot->length) {
            client->tx_offset=0;client->tx_head=(client->tx_head+1U)%MODBUS_TX_QUEUE_MAX;
            --client->tx_count;if(client->tx_count)client->write_started_at=now;
        }
    } else if(sent<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR){increment(&listener->stats.output_peer_errors);close_client(listener,client);}
}

void modbus_tcp_listener_step_io(modbus_tcp_listener_t *listener, core_tick_t now)
{
    unsigned int i; modbus_tcp_client_t *c;
    if(listener==0) return;
    listener->now=now;
    ++listener->stats.scheduler_steps;
    if(listener->listen_fd>=0) accept_one(listener,now);
    for(i=0;i<MODBUS_LISTENER_CLIENT_MAX;++i) {
        c=&listener->clients[i];if(c->fd<0)continue;
        drain_parser(listener,c);if(c->fd<0)continue;
        read_client(listener,c,now);if(c->fd<0)continue;
        if(c->rx.count && core_elapsed(c->frame_started_at,now)>=MODBUS_INPUT_TIMEOUT)
            { close_client(listener,c);continue; }
        write_client(listener,c,now);if(c->fd<0)continue;
        if(c->tx_count && core_elapsed(c->write_started_at,now)>=MODBUS_WRITE_TIMEOUT)
            { increment(&listener->stats.write_deadline_expired);close_client(listener,c);continue; }
        if(c->peer_eof && c->tx_count==0 && !client_has_core_work(listener,c))
            { close_client(listener,c);continue; }
        if(!c->rx.count && !c->tx_count && !client_has_core_work(listener,c) &&
           core_elapsed(c->last_progress,now)>=MODBUS_IDLE_TIMEOUT)
            close_client(listener,c);
    }
    modbus_dispatcher_step(&listener->dispatcher,listener->port,now,1000,1000);
}

void modbus_tcp_listener_step(modbus_tcp_listener_t *listener, core_tick_t now)
{
    if(listener==0)return;
    modbus_tcp_listener_step_io(listener,now);
    port_runtime_step(listener->port,now);
}

void modbus_tcp_listener_close(modbus_tcp_listener_t *listener)
{
    unsigned int i;if(listener==0)return;
    for(i=0;i<MODBUS_LISTENER_CLIENT_MAX;++i)close_client(listener,&listener->clients[i]);
    if(listener->listen_fd>=0){close(listener->listen_fd);listener->listen_fd=-1;}
}

unsigned int modbus_tcp_listener_memory_bytes(void)
{
    return (unsigned int)sizeof(modbus_tcp_listener_t);
}
