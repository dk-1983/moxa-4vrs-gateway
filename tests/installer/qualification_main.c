#define _GNU_SOURCE
#include "installer/install_target.h"
#include "qualification_cut.h"
static unsigned int qualification_enabled;
static void qualification_target_init(install_context_t *c,install_target_t *t){
 install_target_init(c,t);
 if(qualification_enabled){c->boundary=qualification_cut;c->boundary_context=c;}
}
#define install_target_init qualification_target_init
#define main production_installer_main
#include "../../src/installer/main.c"
#undef main
#undef install_target_init
int main(int argc,char **argv){
 if(argc==1){fprintf(stderr,"qualification only: use --qualification-cut; not a release installer\n");return 2;}
 if(argc==2&&!strcmp(argv[1],"--qualification-cut")){
  qualification_enabled=1;argc=1;
 }
 return production_installer_main(argc,argv);
}
