#ifndef FOURVRS_INSTALL_FILES_H
#define FOURVRS_INSTALL_FILES_H
#include <stddef.h>
#define INSTALL_FILE_LIMIT (8U*1024U*1024U)
#define INSTALL_PATH_LIMIT 240U
typedef struct install_file {
    unsigned int kind; /* 0 absent, 1 regular, 2 symbolic link */
    unsigned int mode;
    size_t size;
    unsigned char *data;
} install_file_t;
/* Parents must be real directories owned by effective uid, not group/world
 * writable. root is an existing trusted directory, not a user supplied target
 * in the production CLI. All relative components are validated. */
int install_path(const char *root,const char *relative,char out[1024]);
int install_file_read(const char *,const char *,install_file_t *);
int install_file_publish(const char *,const char *,const install_file_t *);
int install_file_equal(const install_file_t *,const install_file_t *);
void install_file_free(install_file_t *);
int install_directory_sync(const char *);
#endif
