#include "installer/install_scripts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{n++;if(!(x)){fprintf(stderr,"scripts line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned int n;
static install_file_t file(const char*s){install_file_t f;f.kind=1;f.mode=0755;f.size=strlen(s);f.data=(unsigned char*)s;return f;}
int main(void){install_file_t f,out,again;
 f=file("#!/bin/sh\n# retained\n  /sbin/ifup -a  \n\t/sbin/ifdown -a\n/sbin/ifup -a\n/sbin/ifdown -a\n");
 CHECK(!install_vendor_script(&f,&out));CHECK(strstr((char*)out.data,"# retained\n  /etc/4vrs-network/gateway-network-recovery --network-vendor-up || exit $?  \n")!=0);CHECK(strstr((char*)out.data,"\t/etc/4vrs-network/")!=0);CHECK(install_vendor_script(&out,&again)<0);install_file_free(&out);
 f=file("#!/bin/sh\n/sbin/ifup -a\n");CHECK(install_vendor_script(&f,&out)<0);
 f=file("#!/bin/sh\ncase $1 in\nstart)\n /sbin/ntpdate server\n ;;\nesac\n");CHECK(!install_clock_script(&f,0,&out));CHECK(strstr((char*)out.data,"start)\n  test ! -e /etc/4vrs-clock-managed || exit 0\n /sbin/ntpdate server\n")!=0);CHECK(!install_clock_script(&out,0,&again));CHECK(install_file_equal(&out,&again));install_file_free(&out);install_file_free(&again);
 f=file("#!/bin/sh\nhwclock --systohc\n/sbin/halt\n");CHECK(!install_clock_script(&f,1,&out));CHECK(strstr((char*)out.data,"fi\n/sbin/halt\n")!=0);CHECK(!install_clock_script(&out,1,&again));CHECK(install_file_equal(&out,&again));install_file_free(&out);install_file_free(&again);
 f=file("#!/bin/sh\nhwclock --systohc\nhwclock --systohc\n/sbin/halt\n");CHECK(install_clock_script(&f,1,&out)<0);
 f=file("#!/bin/sh\n# /etc/4vrs-clock-managed\nhwclock --systohc\n/sbin/halt\n");CHECK(install_clock_script(&f,1,&out)<0);
 f=file("#! /bin/sh\nhwclock --systohc\nhalt -d -f -i -p\n");CHECK(!install_clock_script(&f,1,&out));CHECK(!memcmp(out.data,"#! /bin/sh\n",11));CHECK(strstr((char*)out.data,"halt -d -f -i -p\n")!=0);install_file_free(&out);
 f=file("#!/usr/bin/env sh\nhwclock --systohc\n");CHECK(install_clock_script(&f,1,&out)<0);
 f=file("#! /bin/sh\ntest -f /etc/4vrs-clock-managed && exit 0\n# own server unchanged\ncase $1 in\nstart)\n /usr/sbin/ntpdate -b -s own-server\n;;\nesac\n");CHECK(!install_clock_script(&f,0,&out));CHECK(install_file_equal(&f,&out));install_file_free(&out);
 f=file("#! /bin/sh\n# retained\nPATH=/sbin:/bin:/usr/sbin:/usr/bin\nif ! test -f /etc/4vrs-clock-managed; then\n    hwclock --systohc\nfi\nhalt -d -f -i -p\n");CHECK(!install_clock_script(&f,1,&out));CHECK(install_file_equal(&f,&out));install_file_free(&out);
 f=file("#!/bin/sh\n# test -f /etc/4vrs-clock-managed && exit 0\n/usr/sbin/ntpdate own-server\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_POSITION);
 f=file("#!/bin/sh\nif false; then\ntest -f /etc/4vrs-clock-managed && exit 0\nfi\n/usr/sbin/ntpdate own-server\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_POSITION);
 f=file("#!/bin/sh\ntest -f /etc/4vrs-clock-managed && exit 0\ntest -f /etc/4vrs-clock-managed && exit 0\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_GUARD);
 f=file("#!/bin/sh\n/usr/sbin/ntpdate own-server\ncase $1 in\nstart)\n /usr/sbin/ntpdate own-server\n;;\nesac\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_POSITION);
 f=file("#!/bin/sh\nhwclock --systohc\nhwclock -w\nhalt\n");CHECK(install_clock_script(&f,1,&out)==-INSTALL_CLOCK_WRITER);
 f=file("#!/bin/sh\nif false; then\nif ! test -f /etc/4vrs-clock-managed; then\n    hwclock --systohc\nfi\nfi\n");CHECK(install_clock_script(&f,1,&out)==-INSTALL_CLOCK_POSITION);

 /* Minimal equivalent start-guard fixture; archived device files stay outside Git. */
 f=file("#! /bin/sh\nPATH=/home/root/ntp\ntest -f /usr/sbin/ntpdate || exit 0\ncase \"$1\" in\nstart)\n  # Gateway owns time policy while this marker exists.\n \t\n  test ! -e /etc/4vrs-clock-managed || exit 0\n  /usr/sbin/ntpdate -b -s example.invalid\n  ;;\nstop|restart)\n  ;;\n*)\n  exit 1\nesac\nexit 0\n");CHECK(!install_clock_script(&f,0,&out));CHECK(install_file_equal(&f,&out));CHECK(out.mode==0755);install_file_free(&out);
 f=file("#! /bin/sh\nPATH=/home/root/ntp\ntest -f /usr/sbin/ntpdate || exit 0\ncase \"$1\" in\nstart)\n  test ! -e /etc/4vrs-clock-managed || exit 0\n  /usr/sbin/ntpdate -b -s example.invalid\n  ;;\nstop|restart)\n  ;;\n*)\n  exit 1\nesac\nexit 0\n");CHECK(!install_clock_script(&f,0,&out));CHECK(install_file_equal(&f,&out));CHECK(out.mode==0755);install_file_free(&out);
 f=file("#! /bin/sh\nPATH=/home/root/ntp\ntest -f /usr/sbin/ntpdate || exit 0\ncase \"$1\" in\nstart)\n  echo unsafe\n  test ! -e /etc/4vrs-clock-managed || exit 0\n  /usr/sbin/ntpdate -b -s example.invalid\n  ;;\nstop|restart)\n  ;;\n*)\n  exit 1\nesac\nexit 0\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_POSITION);
 f=file("#! /bin/sh\nPATH=/home/root/ntp\ntest -f /usr/sbin/ntpdate || exit 0\ncase \"$1\" in\nstart)\n  if false; then\n  test ! -e /etc/4vrs-clock-managed || exit 0\nfi\n  /usr/sbin/ntpdate -b -s example.invalid\n  ;;\nstop|restart)\n  ;;\n*)\n  exit 1\nesac\nexit 0\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_POSITION);
 f=file("#! /bin/sh\nPATH=/home/root/ntp\ntest -f /usr/sbin/ntpdate || exit 0\ncase \"$1\" in\nstop)\n  test ! -e /etc/4vrs-clock-managed || exit 0\n  /usr/sbin/ntpdate -b -s example.invalid\n  ;;\nstop|restart)\n  ;;\n*)\n  exit 1\nesac\nexit 0\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_POSITION);
 f=file("#! /bin/sh\nPATH=/home/root/ntp\ntest -f /usr/sbin/ntpdate || exit 0\nif false; then\ncase \"$1\" in\nstart)\n  test ! -e /etc/4vrs-clock-managed || exit 0\n  /usr/sbin/ntpdate -b -s example.invalid\n  ;;\nstop|restart)\n  ;;\n*)\n  exit 1\nesac\nexit 0\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_POSITION);
 f=file("#! /bin/sh\nPATH=/home/root/ntp\ntest -f /usr/sbin/ntpdate || exit 0\ncase \"$1\" in\nstart)\n  # test ! -e /etc/4vrs-clock-managed || exit 0\n  /usr/sbin/ntpdate -b -s example.invalid\n  ;;\nstop|restart)\n  ;;\n*)\n  exit 1\nesac\nexit 0\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_POSITION);
 f=file("#! /bin/sh\nPATH=/home/root/ntp\ntest -f /usr/sbin/ntpdate || exit 0\ncase \"$1\" in\nstart)\n  test ! -e /etc/4vrs-clock-managed || exit 0\n  test ! -e /etc/4vrs-clock-managed || exit 0\n  /usr/sbin/ntpdate -b -s example.invalid\n  ;;\nstop|restart)\n  ;;\n*)\n  exit 1\nesac\nexit 0\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_GUARD);
 f=file("#! /bin/sh\nPATH=/home/root/ntp\ntest -f /usr/sbin/ntpdate || exit 0\ncase \"$1\" in\nstart)\n  test ! -e /etc/4vrs-clock-managed || exit 0\n  /usr/sbin/ntpdate -b -s example.invalid\n  ;;\nstop|restart)\n/usr/sbin/ntpdate other\n  ;;\n*)\n  exit 1\nesac\nexit 0\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_WRITER);
 f=file("#! /bin/sh\nPATH=$(touch /bad)\ntest -f /usr/sbin/ntpdate || exit 0\ncase \"$1\" in\nstart)\n  test ! -e /etc/4vrs-clock-managed || exit 0\n  /usr/sbin/ntpdate -b -s example.invalid\n  ;;\nstop|restart)\n  ;;\n*)\n  exit 1\nesac\nexit 0\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_POSITION);
 f=file("#!/bin/sh\ncase \"$1\" in\nstart)\n  # Clock ownership\n  test ! -e /etc/4vrs-clock-managed || exit 0\n /usr/sbin/ntpdate own-server\n;;\n*)\necho \"Usage: /etc/init.d/ntpdate {start|stop|restart|force-reload}\"\nesac\n");CHECK(!install_clock_script(&f,0,&out));CHECK(install_file_equal(&f,&out));install_file_free(&out);
 f=file("#!/bin/sh\ncase \"$1\" in\nstart)\n  # Clock ownership\n  test ! -e /etc/4vrs-clock-managed || exit 0\n /usr/sbin/ntpdate own-server\n;;\n*)\necho \"$(/usr/sbin/ntpdate other)\"\nesac\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_WRITER);
 f=file("#!/bin/sh\ncase \"$1\" in\nstart)\n  # Clock ownership\n  test ! -e /etc/4vrs-clock-managed || exit 0\n /usr/sbin/ntpdate own-server\n;;\n*)\necho \"Usage: ntpdate\"; /usr/sbin/ntpdate other\nesac\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_WRITER);
 f=file("#!/bin/sh\ncase \"$1\" in\nstart)\n  # Clock ownership\n  test ! -e /etc/4vrs-clock-managed || exit 0\n /usr/sbin/ntpdate own-server\n;;\n*)\nntpdate other\nesac\n");CHECK(install_clock_script(&f,0,&out)==-INSTALL_CLOCK_WRITER);
 printf("installer scripts: %u checks passed (data only, no shell execution)\n",n);return 0;}
