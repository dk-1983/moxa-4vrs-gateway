#define _GNU_SOURCE
#include "installer/install_package.h"
#include "core/platform.h"
#include "installer/install_digest.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
const char *const install_payload_names[INSTALL_PAYLOAD_COUNT]={"4vrs-install","4vrs-gateway","4vrs-gateway.init","4vrs-networking-wrapper","4vrs-web","4vrs-rng","4vrs-kdf"};
static unsigned int u32(const unsigned char*p){return((unsigned int)p[0]<<24)|((unsigned int)p[1]<<16)|((unsigned int)p[2]<<8)|p[3];}
static unsigned int u16(const unsigned char*p){return((unsigned int)p[0]<<8)|p[1];}
static int elf(const install_file_t*f){
 const unsigned char*p=f->data;unsigned int ph,n,i,load=0,interp=0,version=0;size_t z;
 if(f->size<52||memcmp(p,"\177ELF\1\2\1",7)||u16(p+16)!=2||u16(p+18)!=40||u32(p+20)!=1||u32(p+36)!=FOURVRS_ELF_FLAGS||u16(p+40)!=52||u16(p+42)!=32)return -1;
 ph=u32(p+28);n=u16(p+44);if(ph<52||ph>f->size||!n||n>128||n*32>f->size-ph)return -1;
 for(i=0;i<n;i++){const unsigned char*h=p+ph+i*32;unsigned int off=u32(h+4),size=u32(h+16);if(off>f->size||size>f->size-off)return -1;if(u32(h)==1){load++;if(size>u32(h+20))return -1;}if(u32(h)==3){interp++;if(size!=sizeof(FOURVRS_ELF_LOADER)||memcmp(p+off,FOURVRS_ELF_LOADER,size))return -1;}}
 for(z=0;z+sizeof(INSTALL_RELEASE)<=f->size;z++){
  size_t j,count;
  if(p[z]!='v')continue;
  for(j=z+1;j<z+5&&p[j]>='0'&&p[j]<='9';j++);
  if(j!=z+5||p[j++]!='.')continue;
  count=0;while(j<f->size&&p[j]>='0'&&p[j]<='9'){count++;j++;}
  if(count<2||j>=f->size||p[j++]!='.')continue;
  count=0;while(j<f->size&&p[j]>='0'&&p[j]<='9'){count++;j++;}
  if(count<2||j>=f->size||p[j])continue;
  if(j-z+1!=sizeof(INSTALL_RELEASE)||memcmp(p+z,INSTALL_RELEASE,sizeof(INSTALL_RELEASE)))return -1;
  version++;
 }
 return load&&interp==1&&version?0:-1;
}
static int script(const install_file_t*f){size_t i;if(f->size<10||f->size>65536||memcmp(f->data,"#!/bin/sh\n",10)||f->data[f->size-1]!='\n')return -1;for(i=0;i<f->size;i++)if(!f->data[i]||f->data[i]=='\r'||f->data[i]>127)return -1;return 0;}
void install_package_free(install_package_t*p){unsigned int i;for(i=0;i<INSTALL_PAYLOAD_COUNT;i++)install_file_free(&p->payload[i]);memset(p,0,sizeof(*p));}
int install_package_read(const char*root,install_package_t*p,const char**stage){
 install_file_t manifest,sums;char expected[4096],hash[65],checksums[1024];size_t at=0;int n;unsigned int i;
 memset(p,0,sizeof(*p));memset(&manifest,0,sizeof(manifest));memset(&sums,0,sizeof(sums));*stage="package-payload";
 for(i=0;i<INSTALL_PAYLOAD_COUNT;i++){
  if(install_file_read(root,install_payload_names[i],&p->payload[i])||p->payload[i].kind!=1||p->payload[i].mode!=0755||p->payload[i].size>8U*1024U*1024U||(i<2||i>=4?elf(&p->payload[i]):script(&p->payload[i])))goto fail;
  install_digest_hex(p->payload[i].data,p->payload[i].size,p->digest[i]);
 }
 *stage="package-manifest";
 if(install_file_read(root,"manifest.json",&manifest)||manifest.kind!=1||manifest.size>=sizeof(expected))goto fail;
 /* Format 3 deliberately accepts only the canonical maintainer serialization.
  * Reconstruct from the opened bytes: no generic JSON parser, duplicate keys,
  * alternate paths or command interpolation can acquire meaning. */
 n=snprintf(expected,sizeof(expected),"{\n  \"entrypoint\": \"4vrs-install\",\n  \"files\": [\n");if(n<0)goto fail;at=(size_t)n;
 for(i=0;i<INSTALL_PAYLOAD_COUNT;i++){
  n=snprintf(expected+at,sizeof(expected)-at,"    {\n      \"mode\": \"0755\",\n      \"name\": \"%s\",\n      \"sha256\": \"%s\",\n      \"size\": %lu\n    }%s\n",install_payload_names[i],p->digest[i],(unsigned long)p->payload[i].size,i==INSTALL_PAYLOAD_COUNT-1?"":",");
  if(n<0||(size_t)n>=sizeof(expected)-at)goto fail;
  at+=(size_t)n;
 }
 n=snprintf(expected+at,sizeof(expected)-at,"  ],\n  \"format\": 3,\n  \"product\": \"4VRS Gateway\",\n  \"version\": \"%s\"\n}\n",INSTALL_RELEASE);if(n<0||(size_t)n>=sizeof(expected)-at)goto fail;at+=(size_t)n;
 if(manifest.size!=at||memcmp(manifest.data,expected,at))goto fail;
 *stage="package-checksums";
 install_digest_hex(manifest.data,manifest.size,hash);
 n=snprintf(checksums,sizeof(checksums),"%s  4vrs-gateway\n%s  4vrs-gateway.init\n%s  4vrs-install\n%s  4vrs-kdf\n%s  4vrs-networking-wrapper\n%s  4vrs-rng\n%s  4vrs-web\n%s  manifest.json\n",p->digest[1],p->digest[2],p->digest[0],p->digest[6],p->digest[3],p->digest[5],p->digest[4],hash);
 if(n<0||(size_t)n>=sizeof(checksums)||install_file_read(root,"SHA256SUMS",&sums)||sums.kind!=1||sums.size!=(size_t)n||memcmp(sums.data,checksums,(size_t)n))goto fail;
 install_file_free(&manifest);install_file_free(&sums);*stage="package-ok";return 0;
 fail:install_file_free(&manifest);install_file_free(&sums);install_package_free(p);return -1;
}
int install_package_self(const install_package_t*p){
 int fd=open("/proc/self/exe",O_RDONLY);struct stat s;unsigned char b[4096],d[32];install_digest_t hash;size_t n=0;ssize_t z;char hex[65];unsigned int i;static const char digits[]="0123456789abcdef";
 if(fd<0)return -1;
 if(fstat(fd,&s)||!S_ISREG(s.st_mode)||s.st_size!=(off_t)p->payload[0].size){close(fd);return -1;}
 install_digest_init(&hash);
 while(n<p->payload[0].size){z=read(fd,b,sizeof(b));if(z<0&&errno==EINTR)continue;if(z<=0){close(fd);return -1;}n+=(size_t)z;if(n>p->payload[0].size){close(fd);return -1;}install_digest_update(&hash,b,(size_t)z);}
 if(read(fd,b,1)!=0){close(fd);return -1;}if(close(fd))return -1;
 install_digest_final(&hash,d);for(i=0;i<32;i++){hex[i*2]=digits[d[i]>>4];hex[i*2+1]=digits[d[i]&15];}hex[64]=0;
 return strcmp(hex,p->digest[0])? -1:0;
}
