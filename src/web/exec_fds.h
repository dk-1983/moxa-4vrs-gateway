#ifndef FOURVRS_WEB_EXEC_FDS_H
#define FOURVRS_WEB_EXEC_FDS_H
/* Called in the single-threaded child before exec, or before worker startup.
 * Enumerate actual descriptors: a numerical cap or current soft rlimit can
 * miss descriptors inherited above that limit. /proc is required on target. */
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
static int web_exec_close_from(int first)
{
 DIR *d=opendir("/proc/self/fd");struct dirent *e;int held;
 if(!d)return -1;held=dirfd(d);
 for(;;){char *end;long fd;errno=0;e=readdir(d);
  if(!e){int failed=errno!=0;closedir(d);return failed?-1:0;}
  fd=strtol(e->d_name,&end,10);
  if(!*end&&fd>=first&&fd<=INT_MAX&&fd!=held&&close((int)fd)){closedir(d);return -1;}
 }
}
#endif
