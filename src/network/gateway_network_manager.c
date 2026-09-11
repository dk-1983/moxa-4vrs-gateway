#include <string.h>
#include "network/gateway_network_manager.h"

unsigned int gateway_network_manager_busy(const gateway_network_manager_t *m)
{
    return m && (m->state==GATEWAY_NETWORK_APPLYING || m->state==GATEWAY_NETWORK_WAIT_BINDINGS ||
        m->state==GATEWAY_NETWORK_WAIT_CONFIRM || m->state==GATEWAY_NETWORK_COMMITTING ||
        m->state==GATEWAY_NETWORK_ROLLING_BACK || m->state==GATEWAY_NETWORK_ROLLBACK_BINDINGS);
}

int gateway_network_manager_init(gateway_network_manager_t *m,
                                 const gateway_network_manager_ops_t *ops,void *ctx)
{
    if(!m||!ops||!ops->stage||!ops->apply||!ops->poll||!ops->commit||!ops->restore||!ops->discard)return -1;
    memset(m,0,sizeof(*m));m->ops=ops;m->context=ctx;m->state=GATEWAY_NETWORK_IDLE;return 0;
}

static void rollback(gateway_network_manager_t *m,core_tick_t now)
{
    m->confirmation_requested=0;m->rollback_requested=0;m->bindings_ready=0;
    int r=m->ops->restore(m->context);
    if(r) {m->error_code=r;m->state=GATEWAY_NETWORK_ROLLBACK_FAILED;return;}
    m->state=GATEWAY_NETWORK_ROLLING_BACK;
    m->deadline=core_deadline_after(now,GATEWAY_NETWORK_ROLLBACK_MS);
}

int gateway_network_manager_apply(gateway_network_manager_t *m,
                                  const gateway_network_settings_t *candidate,core_tick_t now)
{
    int r;
    if(!m||!m->ops||gateway_network_manager_busy(m)||
        m->state==GATEWAY_NETWORK_ROLLBACK_FAILED||m->state==GATEWAY_NETWORK_DURABILITY_UNCERTAIN||
        gateway_network_settings_validate(candidate,0))return -1;
    m->confirmation_requested=0;m->rollback_requested=0;m->bindings_ready=0;m->client_alive=1;
    m->rollback_reason=GATEWAY_NETWORK_REASON_NONE;m->error_code=0;
    r=m->ops->stage(m->context,candidate);
    if(r) {m->rollback_reason=GATEWAY_NETWORK_REASON_STAGE;m->error_code=r;m->state=GATEWAY_NETWORK_FAILED;return -1;}
    ++m->generation;
    /* The durable candidate exists, but boot still uses confirmed. */
    r=m->ops->apply(m->context);
    if(r){m->rollback_reason=GATEWAY_NETWORK_REASON_APPLY;m->error_code=r;rollback(m,now);return -1;}
    m->state=GATEWAY_NETWORK_APPLYING;
    m->deadline=core_deadline_after(now,GATEWAY_NETWORK_APPLY_MS);return 0;
}

int gateway_network_manager_confirm(gateway_network_manager_t *m)
{
    if(!m||m->state!=GATEWAY_NETWORK_WAIT_CONFIRM||!m->client_alive||!m->bindings_ready)return -1;
    m->confirmation_requested=1;return 0;
}
void gateway_network_manager_revert(gateway_network_manager_t *m)
{if(m)m->rollback_requested=1;}

void gateway_network_manager_step(gateway_network_manager_t *m,core_tick_t now)
{
    int r;
    if(!m||!m->ops)return;
    if(m->state==GATEWAY_NETWORK_APPLYING||m->state==GATEWAY_NETWORK_WAIT_BINDINGS||
       m->state==GATEWAY_NETWORK_WAIT_CONFIRM||m->state==GATEWAY_NETWORK_COMMITTING){
        if(!m->client_alive||m->rollback_requested||core_deadline_expired(m->deadline,now)){
            m->rollback_reason=!m->client_alive?GATEWAY_NETWORK_REASON_PEER:
                m->rollback_requested?GATEWAY_NETWORK_REASON_REVERT:GATEWAY_NETWORK_REASON_DEADLINE;
            rollback(m,now);return;
        }
    }
    switch(m->state){
    case GATEWAY_NETWORK_APPLYING:
        r=m->ops->poll(m->context);
        if(r<0){m->rollback_reason=GATEWAY_NETWORK_REASON_OBSERVATION;m->error_code=r;rollback(m,now);}
        else if(r>0)m->state=GATEWAY_NETWORK_WAIT_BINDINGS;
        break;
    case GATEWAY_NETWORK_WAIT_BINDINGS:
        if(m->bindings_ready){m->state=GATEWAY_NETWORK_WAIT_CONFIRM;m->deadline=core_deadline_after(now,GATEWAY_NETWORK_CONFIRM_MS);}
        break;
    case GATEWAY_NETWORK_WAIT_CONFIRM:
        r=m->ops->poll(m->context);
        if(r!=1 || !m->bindings_ready){m->rollback_reason=GATEWAY_NETWORK_REASON_OBSERVATION;m->error_code=r;rollback(m,now);}
        else if(m->confirmation_requested){m->state=GATEWAY_NETWORK_COMMITTING;m->deadline=core_deadline_after(now,3000U);}
        break;
    case GATEWAY_NETWORK_COMMITTING:
        r=m->ops->commit(m->context);
        if(r==1)break; /* Isolated durable writer still pending. */
        if(r){m->rollback_reason=GATEWAY_NETWORK_REASON_COMMIT;m->error_code=r;}
        if(r==-2)m->state=GATEWAY_NETWORK_DURABILITY_UNCERTAIN;
        else if(r)rollback(m,now);
        else m->state=GATEWAY_NETWORK_KEPT;
        break;
    case GATEWAY_NETWORK_ROLLING_BACK:
        r=m->ops->poll(m->context);
        if(r==2){m->state=GATEWAY_NETWORK_KEPT;break;} /* Prior authorized Keep became durable before cancellation. */
        if(r<0 || core_deadline_expired(m->deadline,now)){m->error_code=r<0?r:-1;m->state=GATEWAY_NETWORK_ROLLBACK_FAILED;}
        else if(r>0){
            r=m->ops->discard(m->context);
            if(r){m->error_code=r;m->state=GATEWAY_NETWORK_ROLLBACK_FAILED;}
            else m->state=m->client_alive?GATEWAY_NETWORK_ROLLBACK_BINDINGS:GATEWAY_NETWORK_REVERTED;
        }
        break;
    case GATEWAY_NETWORK_ROLLBACK_BINDINGS:
        if(m->bindings_ready||!m->client_alive)m->state=GATEWAY_NETWORK_REVERTED;
        else if(core_deadline_expired(m->deadline,now)){m->error_code=-1;m->state=GATEWAY_NETWORK_ROLLBACK_FAILED;}
        break;
    default:break;
    }
}

const char *gateway_network_state_name(gateway_network_state_t state)
{
    static const char *names[]={"UNAVAILABLE","IDLE","APPLYING","REBINDING","KEEP / REVERT",
        "SAVING","RESTORING","RESTORE BINDINGS","KEPT","REVERTED","FAILED","ROLLBACK FAILED","SAVE UNCERTAIN"};
    return (unsigned int)state<sizeof(names)/sizeof(names[0])?names[state]:"INVALID";
}
