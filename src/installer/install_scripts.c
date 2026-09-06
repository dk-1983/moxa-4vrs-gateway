#include "installer/install_scripts.h"
#include <stdlib.h>
#include <string.h>
static int text(const install_file_t*f){size_t i;if(f->kind!=1||f->size<10||f->size>16384||(memcmp(f->data,"#!/bin/sh\n",10)&&(f->size<11||memcmp(f->data,"#! /bin/sh\n",11)))||f->data[f->size-1]!='\n')return -1;for(i=0;i<f->size;i++)if(!f->data[i]||f->data[i]=='\r'||f->data[i]>127)return -1;return 0;}
static int allocate(const install_file_t*in,install_file_t*out){memset(out,0,sizeof(*out));if(text(in))return -1;out->data=calloc(1,32769);if(!out->data)return -1;out->kind=1;out->mode=in->mode;return 0;}
int install_vendor_script(const install_file_t*in,install_file_t*out){
 size_t at=0,n=0,start,end,z,first;unsigned int up=0,down=0;const char *replace;
 if(allocate(in,out))return -1;
 while(at<in->size){first=start=at;while(at<in->size&&in->data[at]!='\n')at++;end=at++;while(start<end&&(in->data[start]==' '||in->data[start]=='\t'))start++;while(end>start&&(in->data[end-1]==' '||in->data[end-1]=='\t'))end--;
  replace=0;if((size_t)(end-start)==strlen("/sbin/ifdown -a")&&!memcmp(in->data+start,"/sbin/ifdown -a",strlen("/sbin/ifdown -a"))){replace="/etc/4vrs-network/gateway-network-recovery --network-vendor-down || exit $?";down++;}
  if((size_t)(end-start)==strlen("/sbin/ifup -a")&&!memcmp(in->data+start,"/sbin/ifup -a",strlen("/sbin/ifup -a"))){replace="/etc/4vrs-network/gateway-network-recovery --network-vendor-up || exit $?";up++;}
  /* Preserve indentation, trailing whitespace and every unrelated byte. */
  if(replace){memcpy(out->data+n,in->data+first,start-first);n+=start-first;z=strlen(replace);memcpy(out->data+n,replace,z);n+=z;memcpy(out->data+n,in->data+end,at-end);n+=at-end;}
  else{memcpy(out->data+n,in->data+first,at-first);n+=at-first;}
 }
 if(up!=2||down!=2){install_file_free(out);return -1;}out->size=n;return 0;
}
/* Historical top-level guards may follow only comments/blank lines (halt also
 * permits its vendor PATH assignment). A guard inside a branch/comment is not
 * proof of clock ownership. */
static int preamble(const char*s,const char*end,unsigned int halt){
 const char*p=strchr(s,'\n');if(!p)return 0;p++;
 while(p<end){const char*e=strchr(p,'\n'),*q=p;if(!e||e>=end)return 0;while(q<e&&(*q==' '||*q=='\t'))q++;
  if(q<e&&*q!='#'&&!(halt&&(size_t)(e-q)==strlen("PATH=/sbin:/bin:/usr/sbin:/usr/bin")&&!memcmp(q,"PATH=/sbin:/bin:/usr/sbin:/usr/bin",(size_t)(e-q))))return 0;
  p=e+1;
 }return p==end;
}
static int writers(const char*s,const char*guard,size_t length,unsigned int halt,unsigned int top){
 const char*p=s,*limit=top?0:strstr(guard+length,";;");
 while(*p){const char*e=strchr(p,'\n'),*q=p,*w;if(!e)return 0;while(q<e&&(*q==' '||*q=='\t'))q++;
  if(q<e&&*q!='#'){
   w=strstr(q,halt?"hwclock":"/ntpdate");
   if(w&&w<e&&!(!halt&&(size_t)(e-q)==strlen("test -f /usr/sbin/ntpdate || exit 0")&&!memcmp(q,"test -f /usr/sbin/ntpdate || exit 0",(size_t)(e-q)))){if(halt){if(w<guard||w>=guard+length)return 0;}
    else if(w<guard+length||(!top&&(!limit||w>=limit)))return 0;}
  }p=e+1;
 }return 1;
}
int install_clock_script(const install_file_t*in,unsigned int halt,install_file_t*out){
 const char*needle=halt?"hwclock --systohc\n":"start)\n";
 const char*replacement=halt?"if test ! -e /etc/4vrs-clock-managed; then\n  hwclock --systohc\nfi\n":"start)\n  test ! -e /etc/4vrs-clock-managed || exit 0\n";
 const char*found,*again;size_t prefix,n;int error=INSTALL_CLOCK_GUARD;
 if(allocate(in,out))return -INSTALL_CLOCK_FORMAT;
 if(strstr((char*)in->data,"/etc/4vrs-clock-managed")){
  /* Only an exact guard is accepted as managed, never marker substring alone.
   * Historical checked guards may have a comment before the guard. */
  const char *guard=halt?"if test ! -e /etc/4vrs-clock-managed; then\n  hwclock --systohc\nfi\n":"test ! -e /etc/4vrs-clock-managed || exit 0\n";
  unsigned int top;
  found=strstr((char*)in->data,guard);
  if(!found){guard=halt?"if ! test -f /etc/4vrs-clock-managed; then\n    hwclock --systohc\nfi\n":"test -f /etc/4vrs-clock-managed && exit 0\n";found=strstr((char*)in->data,guard);}
  if(!found||strstr(found+strlen(guard),"/etc/4vrs-clock-managed")||strstr((char*)in->data,"/etc/4vrs-clock-managed")!=strstr(found,"/etc/4vrs-clock-managed"))goto fail;
  if(halt&&(strstr(found+strlen(guard),"hwclock --systohc")||strstr((char*)in->data,"hwclock --systohc")!=strstr(found,"hwclock --systohc")))goto fail;
  top=(unsigned int)preamble((char*)in->data,found,halt);
  if(!top){const char*line=found;while(line>(char*)in->data&&line[-1]!='\n')line--;
   if(halt||line==found||found-line!=2||memcmp(line,"  ",2)||line-(char*)in->data<7||memcmp(line-7,"start)\n",7)){error=INSTALL_CLOCK_POSITION;goto fail;}
  }
  if(!writers((char*)in->data,found,strlen(guard),halt,top)){error=INSTALL_CLOCK_WRITER;goto fail;}
  memcpy(out->data,in->data,in->size);out->size=in->size;return 0;
 }
 found=strstr((char*)in->data,needle);if(!found)goto fail;
 again=strstr(found+strlen(needle),needle);if(again)goto fail;
 prefix=(size_t)(found-(char*)in->data);n=strlen(replacement);
 memcpy(out->data,in->data,prefix);memcpy(out->data+prefix,replacement,n);
 memcpy(out->data+prefix+n,found+strlen(needle),in->size-prefix-strlen(needle));out->size=in->size+n-strlen(needle);
 {install_file_t checked;int result=install_clock_script(out,halt,&checked);if(result){install_file_free(out);return result;}install_file_free(&checked);}return 0;
 fail:install_file_free(out);return -error;
}
