/* Schedule the broker on each transport close: an EOF before SIGTERM is fatal.
 * Includes the actual Gateway stop function; no device or RNG state access. */
#define _GNU_SOURCE
#define kill stop_test_kill
#define close stop_test_close
#include GATEWAY_SOURCE
#undef kill
#undef close
static int signalled,early_eof,signals,closes;
int stop_test_kill(pid_t pid,int sig){if(pid!=123||sig!=SIGTERM)return -1;signalled=1;signals++;return 0;}
int stop_test_close(int fd){(void)fd;if(!signalled)early_eof=1;closes++;return 0;}
void rng_client_close(void){stop_test_close(101);}
void web_security_cancel(web_security_t *s){(void)s;}
int main(void){
 web_gateway_t w;memset(&w,0,sizeof(w));w.rng_pid=123;w.rng_web=102;w.rng_attaching=1;w.rng_pending_fd=103;
 rng_stop(&w);
 if(early_eof){puts("REPRODUCED broker EOF before stop signal");return 1;}
 if(signals!=1||closes!=3||w.rng_web!=-1||w.rng_pending_fd!=-1||w.rng_attaching||!w.rng_stopping)return 2;
 puts("PASS orderly signal precedes all broker channel closes");return 0;
}
