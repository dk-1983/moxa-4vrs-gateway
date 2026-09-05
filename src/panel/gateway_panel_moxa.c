#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "panel/gateway_panel_moxa.h"

#define MOXA_LCM_GOTO 1
#define MOXA_LCM_CLEAR 2
#define MOXA_LCM_BACKLIGHT_ON 5
#define MOXA_LCM_AUTOSCROLL_OFF 14
#define MOXA_KEYPAD_HAS_PRESS 1
#define MOXA_KEYPAD_GET_KEY 2

typedef struct moxa_lcm_xy { int x; int y; } moxa_lcm_xy_t;

static int display_open(void*x){gateway_panel_moxa_t*c=(gateway_panel_moxa_t*)x;c->display_fd=open("/dev/lcm",O_RDWR);if(c->display_fd<0)return errno?errno:-1;if(ioctl(c->display_fd,MOXA_LCM_AUTOSCROLL_OFF,0)<0||ioctl(c->display_fd,MOXA_LCM_BACKLIGHT_ON,0)<0){int e=errno;close(c->display_fd);c->display_fd=-1;return e?e:-1;}return 0;}
static int keypad_open(void*x){gateway_panel_moxa_t*c=(gateway_panel_moxa_t*)x;c->keypad_fd=open("/dev/keypad",O_RDWR);if(c->keypad_fd<0)return errno?errno:-1;return 0;}
static int draw(void*x,const gateway_panel_screen_t*s){gateway_panel_moxa_t*c=(gateway_panel_moxa_t*)x;unsigned int r;moxa_lcm_xy_t p;if(c->display_fd<0)return-1;if(ioctl(c->display_fd,MOXA_LCM_CLEAR,0)<0)return errno?errno:-1;for(r=0;r<GATEWAY_PANEL_ROWS;++r){p.x=0;p.y=(int)r;if(ioctl(c->display_fd,MOXA_LCM_GOTO,&p)<0)return errno?errno:-1;if(write(c->display_fd,s->row[r],GATEWAY_PANEL_COLUMNS)!=(ssize_t)GATEWAY_PANEL_COLUMNS)return errno?errno:-1;}return 0;}
static int poll_key(void*x,unsigned int*k){gateway_panel_moxa_t*c=(gateway_panel_moxa_t*)x;int n=0,v=-1;if(c->keypad_fd<0||!k)return-1;if(ioctl(c->keypad_fd,MOXA_KEYPAD_HAS_PRESS,&n)<0)return errno?errno:-1;if(n<=0)return 0;if(ioctl(c->keypad_fd,MOXA_KEYPAD_GET_KEY,&v)<0)return errno?errno:-1;if(v<0||v>4)return 0;*k=(unsigned int)v;return 1;}
static void display_close(void*x){gateway_panel_moxa_t*c=(gateway_panel_moxa_t*)x;if(c->display_fd>=0){close(c->display_fd);c->display_fd=-1;}}
static void keypad_close(void*x){gateway_panel_moxa_t*c=(gateway_panel_moxa_t*)x;if(c->keypad_fd>=0){close(c->keypad_fd);c->keypad_fd=-1;}}
static const gateway_panel_ops_t operations={display_open,keypad_open,draw,poll_key,display_close,keypad_close};
void gateway_panel_moxa_context_init(gateway_panel_moxa_t*c){if(c){memset(c,0,sizeof(*c));c->display_fd=-1;c->keypad_fd=-1;}}
const gateway_panel_ops_t *gateway_panel_moxa_ops(void){return &operations;}
