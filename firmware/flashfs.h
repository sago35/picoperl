/* FAT filesystem in the upper part of the on-board flash, shared with the host
 * over USB mass storage. Scripts and modules live here (/main.pl, /lib/...). */
#ifndef FLASHFS_H
#define FLASHFS_H
#include <stdint.h>
#include <stdbool.h>

/* mount (formatting and populating on first boot); returns true when usable */
bool  flashfs_init(void);
bool  flashfs_mounted(void);
/* read a whole file ("/main.pl", "lib/Foo.pm" ...); malloc'd, NUL terminated; NULL if missing */
char *flashfs_read_file(const char *path);
/* search "/lib/<name>" then "/<name>" */
char *flashfs_find_file(const char *name);
/* call often: flushes the write cache; sets the reload request after the host changed files */
void  flashfs_poll(void);
bool  flashfs_reload_pending(void);
bool  flashfs_take_reload_request(void);   /* returns and clears the request */
/* write a file (used to populate defaults) */
bool  flashfs_write_file(const char *path, const char *data, unsigned len);

#endif
