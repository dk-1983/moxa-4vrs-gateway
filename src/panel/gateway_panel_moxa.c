#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>

#include "panel/gateway_panel_moxa.h"

#define MOXA_LCM_GOTO 1
#define MOXA_LCM_CLEAR 2
#define MOXA_LCM_AUTOSCROLL_OFF 14
#define MOXA_KEYPAD_HAS_PRESS 1
#define MOXA_KEYPAD_GET_KEY 2

typedef struct moxa_lcm_xy { int x; int y; } moxa_lcm_xy_t;


static int display_open(void*x){gateway_panel_moxa_t*c=(gateway_panel_moxa_t*)x;c->display_fd=open("/dev/lcm",O_RDWR);if(c->display_fd<0)return errno?errno:-1;if(ioctl(c->display_fd,MOXA_LCM_AUTOSCROLL_OFF,0)<0){int e=errno;close(c->display_fd);c->display_fd=-1;return e?e:-1;}return 0;}
/* UC-7420-LX Plus software id 60040002: vendor misc driver reads one
 * active-low byte at 0x54000000. Mapping has no write permission and no
 * register stores. Unsupported hardware retains event-only operation. */
static void key_state_open(gateway_panel_moxa_t*c){
 char id[128];int fd,region=0;FILE*f=fopen("/proc/sw_id","r");void *mapped;
 if(!f)f=fopen("/proc/swid","r");
 if(!f)return;
 if(!fgets(id,sizeof(id),f)){fclose(f);return;}fclose(f);
 if(strcmp(id,"60040002\n")&&strcmp(id,"60040002"))return;
 f=fopen("/proc/iomem","r");if(!f)return;
 while(fgets(id,sizeof(id),f))if(strstr(id,"54000000-5400000f : Moxa DA-66X misc"))region=1;
 fclose(f);if(!region)return;
 fd=open("/dev/mem",O_RDONLY|O_SYNC);if(fd<0)return;
 mapped=mmap(0,4096,PROT_READ,MAP_SHARED,fd,0x54000000);close(fd);
 if(mapped!=MAP_FAILED)c->key_register=mapped;
}
int gateway_panel_moxa_key_state(void*x,unsigned int*mask){
 gateway_panel_moxa_t*c=x;if(!c||!mask)return -1;
 if(!c->key_register)return 0;
 *mask=(~*c->key_register)&31U;return 1;
}
static int keypad_open(void*x){gateway_panel_moxa_t*c=(gateway_panel_moxa_t*)x;c->keypad_fd=open("/dev/keypad",O_RDWR);if(c->keypad_fd<0)return errno?errno:-1;key_state_open(c);return 0;}
static int draw(void*x,const gateway_panel_screen_t*s){gateway_panel_moxa_t*c=(gateway_panel_moxa_t*)x;unsigned int r;moxa_lcm_xy_t p;if(c->display_fd<0)return-1;if(ioctl(c->display_fd,MOXA_LCM_CLEAR,0)<0)return errno?errno:-1;for(r=0;r<GATEWAY_PANEL_ROWS;++r){p.x=0;p.y=(int)r;if(ioctl(c->display_fd,MOXA_LCM_GOTO,&p)<0)return errno?errno:-1;if(write(c->display_fd,s->row[r],GATEWAY_PANEL_COLUMNS)!=(ssize_t)GATEWAY_PANEL_COLUMNS)return errno?errno:-1;}return 0;}
static int poll_key(void*x,unsigned int*k){gateway_panel_moxa_t*c=(gateway_panel_moxa_t*)x;int n=0,v=-1;if(c->keypad_fd<0||!k)return-1;if(ioctl(c->keypad_fd,MOXA_KEYPAD_HAS_PRESS,&n)<0)return errno?errno:-1;if(n<=0)return 0;if(ioctl(c->keypad_fd,MOXA_KEYPAD_GET_KEY,&v)<0)return errno?errno:-1;if(v<0||v>4)return 0;*k=(unsigned int)v;return 1;}
static void display_close(void*x){gateway_panel_moxa_t*c=(gateway_panel_moxa_t*)x;if(c->display_fd>=0){close(c->display_fd);c->display_fd=-1;}}
static void keypad_close(void*x){gateway_panel_moxa_t*c=(gateway_panel_moxa_t*)x;if(c->key_register){munmap((void*)c->key_register,4096);c->key_register=0;}if(c->keypad_fd>=0){close(c->keypad_fd);c->keypad_fd=-1;}}
static const gateway_panel_ops_t operations={display_open,keypad_open,draw,poll_key,display_close,keypad_close};
void gateway_panel_moxa_context_init(gateway_panel_moxa_t*c){if(c){memset(c,0,sizeof(*c));c->display_fd=-1;c->keypad_fd=-1;c->key_register=0;}}
const gateway_panel_ops_t *gateway_panel_moxa_ops(void){return &operations;}
