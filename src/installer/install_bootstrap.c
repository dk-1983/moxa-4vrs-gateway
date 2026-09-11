#include "installer/install_bootstrap.h"
#include "installer/install_digest.h"
#include <string.h>
int install_bootstrap_prepare(const char*root,const install_file_t*executable){
 install_file_t actual;int equal;
 if(!executable||executable->kind!=1||executable->mode!=0755||!executable->size)return -1;
 if(install_file_read(root,INSTALL_RECOVERY_DIRECTORY "/recovery",&actual))return -1;
 equal=install_file_equal(&actual,executable);install_file_free(&actual);
 if(!equal&&install_file_publish(root,INSTALL_RECOVERY_DIRECTORY "/recovery",executable))return -1;
 if(install_file_read(root,INSTALL_RECOVERY_DIRECTORY "/recovery",&actual))return -1;
 equal=install_file_equal(&actual,executable);install_file_free(&actual);return equal?0:-1;
}
int install_bootstrap_add_gate(install_plan_t*p,const char*root,const char*path,unsigned int application){
 static const char network[]="#!/bin/sh\nexec " INSTALL_RECOVERY_EXECUTABLE " --network-entry \"$@\"\n";
 static const char app[]="#!/bin/sh\nexec " INSTALL_RECOVERY_EXECUTABLE " --application-entry \"$@\"\n";
 install_file_t f;f.kind=1;f.mode=0755;f.data=(unsigned char*)(application?app:network);f.size=strlen((char*)f.data);
 return install_plan_add(p,root,path,INSTALL_BOOT_GATE,&f);
}
