/* Separate qualification entry only. Never included by production main. */
#ifndef FOURVRS_QUALIFICATION_CUT_H
#define FOURVRS_QUALIFICATION_CUT_H
#include "installer/install_process.h"
#include "installer/install_orchestrator.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int qualification_cut(void *v,const char *point,unsigned int index){
 install_context_t *c=v;install_process_t self;install_file_t event;
 char text[384];int n;
 if(strcmp(point,"publish")||index>=c->plan.count||
    strcmp(c->plan.member[index].path,"etc/4vrs-installer/application"))return 0;
 /* The caller has already atomically published/fsynced this member. No
  * activation/completed marker has occurred. Failure to record is a normal
  * callback error (rollback), never a claimed successful interruption. */
 if(install_process_capture(getpid(),"/proc/self/exe",&self)||
    install_process_matches(&self)!=1)return -1;
 n=snprintf(text,sizeof(text),"qualification=publication-v1\nboundary=publish-application\npid=%ld\nstarttime=%llu\nexit=77\n",
            (long)self.pid,self.starttime);
 if(n<0||(size_t)n>=sizeof(text))return -1;
 event.kind=1;event.mode=0600;event.data=(unsigned char*)text;event.size=(size_t)n;
 if(install_file_publish(c->state_directory,"qualification-event",&event))return -1;
 /* Abrupt process exit, not power loss; only this freshly identified process.
  * Recovery invocations never enable this callback. */
 _exit(77);
}
#endif
