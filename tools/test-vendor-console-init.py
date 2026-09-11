"""Compile preserved vendor init functions with inert signal/spawn/log fixtures.
Never runs vendor init as PID1; never delivers a signal or starts a getty.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

vendor=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
source=vendor/'init.c';header=vendor/'init.h'
assert hashlib.sha256(source.read_bytes()).hexdigest()=='5cec4f55b9c7c062daec927dbd9fa686f2c9e57ea4e7d29a54c5d2c287eeb32e'
assert hashlib.sha256(header.read_bytes()).hexdigest()=='f0f347aa57e15e3fd9c63bc0391f8cc90113f5de13b288d5dc68c47479429041'
text=source.read_bytes().decode('latin1')
# Delimit entire functions using their following top-level declarations/comments;
# extracted bytes are never rewritten or normalized in the preserved directory.
read=text[text.index('void read_inittab(void)'):text.index('/*\n *\tWalk through the family list')]
startup=text[text.index('void startup(CHILD *ch)'):text.index('/*\n *\tRead the inittab file.')]
actions=text[text.index('struct actions {'):text.index('/*\n *\tState parser token table')]
prefix=r'''
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <time.h>
#include <utmp.h>
#include "init.h"
static char *fixture_path;
#define INITTAB fixture_path
#define SULOGIN "/fixture/sulogin"
#define ISPOWER(i) ((i)==POWERWAIT||(i)==POWERFAIL||(i)==POWEROKWAIT||(i)==POWERFAILNOW||(i)==CTRLALTDEL)
CHILD *family,*newFamily;
static char *console_dev="/dev/console";
static char runlevel='3';
static int sltime=0,signals,spawned;
static char spawned_command[128];
static struct utmp utproto;
void initlog(int level,char *fmt,...){(void)level;(void)fmt;}
void write_utmp_wtmp(char*a,char*b,int c,int d,char*e){(void)a;(void)b;(void)c;(void)d;(void)e;}
static void do_sleep(int n){(void)n;}
static void *imalloc(size_t n){void*p=calloc(1,n);assert(p);return p;}
static int fake_kill(int pid,int sig){assert(pid==-29472);assert(sig==SIGTERM||sig==SIGKILL);signals++;return 0;}
#define kill fake_kill
static int spawn(CHILD *ch,int *pid){strcpy(spawned_command,ch->process);*pid=40000+ ++spawned;return *pid;}
'''
main=r'''
static int count;
static void check(const char *name,int ok){assert(ok);printf("PASS %s\n",name);count++;}
static CHILD *entry(const char *id){CHILD*c;for(c=family;c;c=c->next)if(!strcmp(c->id,id))return c;abort();}
static void config(const char *action,const char *command){FILE*f=fopen(fixture_path,"w");assert(f);fprintf(f,"s1:2345:%s:%s\ngw:2345:respawn:/fixture/gateway\n",action,command);assert(!fclose(f));}
int main(int argc,char **argv){CHILD*c;assert(argc==2);fixture_path=argv[1];
 config("respawn","/sbin/getty 115200 ttyS1 -L");read_inittab();
 entry("s1")->pid=29472;entry("con")->pid=941;entry("gw")->pid=959;
 for(c=family;c;c=c->next)if(c->pid)c->flags=RUNNING|XECUTED;
 config("respawn","/bin/sh /var/hda/4vrs-bootstrap/hold-ttyS1.sh");read_inittab();
 check("process-only reload sends no signal",signals==0);
 check("current root shell pid and flags retained",entry("s1")->pid==29472&&(entry("s1")->flags&RUNNING));
 check("vendor synthetic console shell retained",entry("con")->pid==941&&(entry("con")->flags&RUNNING));
 check("unrelated gateway retained",entry("gw")->pid==959&&(entry("gw")->flags&RUNNING));
 check("next command changed to hold",!strcmp(entry("s1")->process,"/bin/sh /var/hda/4vrs-bootstrap/hold-ttyS1.sh"));
 entry("s1")->flags&=~RUNNING;startup(entry("s1"));
 check("after receiver exit next spawn is hold",!strcmp(spawned_command,"/bin/sh /var/hda/4vrs-bootstrap/hold-ttyS1.sh"));
 config("respawn","/sbin/getty 115200 ttyS1 -L");read_inittab();
 check("restore leaves running hold alive",signals==0&&entry("s1")->pid==40001&&(entry("s1")->flags&RUNNING));
 entry("s1")->flags&=~RUNNING;startup(entry("s1"));
 check("only after hold exit next spawn is original getty",!strcmp(spawned_command,"/sbin/getty 115200 ttyS1 -L"));
 entry("s1")->pid=29472;
 config("off","/sbin/getty 115200 ttyS1 -L");read_inittab();
 check("off change really signals old group in vendor code",signals==2);
 check("other process groups never signalled",entry("con")->pid==941&&entry("gw")->pid==959);
 printf("TOTAL %d PASS; no real signal, spawn, init, getty or device\n",count);return 0;
}
'''
(out/'init.h').write_bytes(header.read_bytes())
(out/'fixture.c').write_bytes((prefix+actions+startup+read+main).encode('latin1'))
for mode,flags in [('host',[]),('ubsan',['-fsanitize=undefined','-fno-sanitize-recover=all'])]:
 binary=out/('init-'+mode)
 subprocess.run(['gcc','-std=gnu99','-O1',*flags,str(out/'fixture.c'),'-o',str(binary)],check=True)
 run=subprocess.run([binary,out/'inittab.fixture'],capture_output=True,check=True)
 (out/(mode+'.log')).write_bytes(run.stdout+run.stderr)
 print(mode,run.stdout.decode().splitlines()[-1])
(out/'extraction.json').write_text(json.dumps({'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
 'read_inittab_sha256':hashlib.sha256(read.encode('latin1')).hexdigest(),
 'startup_sha256':hashlib.sha256(startup.encode('latin1')).hexdigest(),
 'real_signals':False,'real_spawns':False,'active_init_identity_proven':False},indent=2)+'\n')
