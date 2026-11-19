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
/* call often: flushes the write cache; requests a reload after the host changed the
 * content of /main.pl or /lib (other writes, e.g. Windows metadata, are ignored) */
void  flashfs_poll(void);
bool  flashfs_reload_pending(void);
bool  flashfs_take_reload_request(void);   /* returns and clears the request */
const char *flashfs_change_info(void);     /* what the last detected change was (for the log) */
/* stdio-like handles for the interpreter's open/close/<$fh> */
int   flashfs_filetest(const char *path, long long *size);   /* -1: no such path, else PP_FT_* bits */
void *flashfs_fopen(const char *path, const char *mode);   /* mode: r w a r+ w+ */
int   flashfs_fclose(void *f);
int   flashfs_fread(void *f, void *buf, int n);
int   flashfs_fwrite(void *f, const void *buf, int n);
long  flashfs_fseek(void *f, long off, int whence);
long  flashfs_ftell(void *f);
int   flashfs_feof(void *f);
const char *flashfs_io_error(void);
/* write a file (used to populate defaults) */
bool  flashfs_write_file(const char *path, const char *data, unsigned len);

#endif
