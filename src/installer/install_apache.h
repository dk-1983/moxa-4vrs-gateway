#ifndef FOURVRS_INSTALL_APACHE_H
#define FOURVRS_INSTALL_APACHE_H
#include "installer/install_transaction.h"
/* Bit 0: Gateway; bit 1: vendor Apache. Journaled before any stop. */
#define INSTALL_APACHE_RUNNING 2U
int install_apache_plan(const char *,const char *,install_plan_t *);
int install_apache_inspect(unsigned int *);
int install_apache_ports(void);
int install_apache_stop(void);
int install_apache_restore(unsigned int);
#endif
