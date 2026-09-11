#ifndef FOURVRS_INSTALL_SCRIPTS_H
#define FOURVRS_INSTALL_SCRIPTS_H
#include "installer/install_files.h"
int install_vendor_script(const install_file_t *,install_file_t *);
int install_clock_script(const install_file_t *,unsigned int halt,install_file_t *);
/* Negative result is a bounded diagnostic, never configuration text. */
enum install_clock_error { INSTALL_CLOCK_FORMAT=1, INSTALL_CLOCK_GUARD=2,
 INSTALL_CLOCK_POSITION=3, INSTALL_CLOCK_WRITER=4 };
#endif
