#define _GNU_SOURCE
#include "installer/install_target.h"
#include "network/gateway_network_runtime.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Deliberately no --root, environment root override or fault injection in
 * this privileged entry point. Qualification links the orchestrator API. */
static install_context_t context;
static install_target_t target;
static install_package_t package;
static unsigned int recovery_only;
static const char version[] = INSTALL_RELEASE;
static int result_file(int result){
 char text[512];install_file_t f;int n;
 n=snprintf(text,sizeof(text),"version=%s\nresult=%d\nstage=%s\ndetail=%d\nfailure_stage=%s\nfailure_detail=%d\nutc=%lu\ngateway_sha256=%s\n",
 version,result,context.stage?context.stage:"unknown",context.detail,context.failure_stage?context.failure_stage:"none",context.failure_detail,(unsigned long)time(0),package.digest[1]);
 if(n<0||(size_t)n>=sizeof(text))return -1;
 f.kind=1;f.mode=0600;f.data=(unsigned char*)text;f.size=(size_t)n;
 return install_file_publish("/etc/4vrs-installer","result",&f);
}
static int work(void*unused){
 int r;(void)unused;context.stage="running";
 if(result_file(100))return 1;
 r=recovery_only?install_recover_only(&context):install_orchestrate(&context,&package);
 if(result_file(r))r=INSTALL_RECOVERY_REQUIRED;
 install_context_release(&context);install_package_free(&package);
 return r==INSTALL_COMPLETED||r==INSTALL_UNCHANGED||(recovery_only&&r==INSTALL_ROLLED_BACK)?0:1;
}
static int launch(int lock){pid_t child;
 if(install_process_detach(lock,work,0,&child)){close(lock);install_package_free(&package);return 1;}
 printf("operation started; reconnect and run /etc/4vrs-installer/recovery --status\nresult=/etc/4vrs-installer/result\n");
 close(lock);install_package_free(&package);return 0;
}
int main(int argc,char**argv){
 int lock,r,application=0;char cwd[1024];const char*stage="invocation";
 install_target_init(&context,&target);
 if(argc==2&&!strcmp(argv[1],"--status")){
  install_file_t f;
  if(install_file_read("/etc/4vrs-installer","result",&f)||f.kind!=1||f.size>512)return 1;
  fwrite(f.data,1,f.size,stdout);install_file_free(&f);return 0;
 }
 if(argc==2&&!strcmp(argv[1],"--recover")){
  install_process_t self;
  if(context.platform->detect(&target)||install_process_capture(getpid(),INSTALL_RECOVERY_EXECUTABLE,&self)||install_layout(&context,0))return 1;
  lock=install_target_lock();if(lock<0)return 1;
  recovery_only=1;return launch(lock);
 }
 if(argc==3&&(!strcmp(argv[1],"--network-entry")||!strcmp(argv[1],"--application-entry"))){
  unsigned int waited=0;
  application=!strcmp(argv[1],"--application-entry");
  if(context.platform->detect(&target)||install_layout(&context,0))return 1;
  lock=install_target_lock();if(lock<0)return 1;
  /* rcS need not stop on an S39 failure: S40 and the application entry
   * themselves are protected. Only application startup waits for late CF. */
  while(application&&!strcmp(argv[2],"start")&&context.platform->cf_available(&target)==0&&waited++<120U)sleep(1);
  r=install_recover_entry(&context,(unsigned int)application,argv[2]);
  fprintf(stderr,"4VRS installer entry result=%d stage=%s\n",r,context.stage);
  install_context_release(&context);close(lock);
  return r==0||(!application&&r==INSTALL_WAIT_CF)?0:1;
 }
 if(argc!=1){fprintf(stderr,"usage: ./4vrs-install | --status\n");return 2;}
 if(!getcwd(cwd,sizeof(cwd))||install_package_read(cwd,&package,&stage)||install_package_self(&package)){
  fprintf(stderr,"installer refused stage=%s/self\n",stage);return 1;
 }
 if(context.platform->detect(&target)||install_layout(&context,1)){
  fprintf(stderr,"installer refused stage=target/%s\n",context.stage);install_package_free(&package);return 1;
 }
 lock=install_target_lock();if(lock<0){fprintf(stderr,"installer refused stage=exclusive-lock\n");install_package_free(&package);return 1;}
 /* Detach BEFORE any cutover; the child retains the lock, not SSH fds.
  * All package bytes are already opened, hashed and held in private memory. */
 return launch(lock);
}
