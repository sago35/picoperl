/*
 * Flash-backed FAT filesystem + USB mass storage.
 *
 * Layout: the last PICOPERL_FS_SIZE bytes of the flash hold a FAT12 volume
 * (no partition table). The host sees it as a removable drive "PICOPERL";
 * the firmware reads it through FatFs. Writes from the host are cached per
 * 4 KiB erase block and flushed when the block changes, on SYNCHRONIZE CACHE
 * or after a short idle period. After the host has written, the firmware
 * restarts the interpreter (USB stays up) so the new script is picked up.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "tusb.h"
#include "ff.h"
#include "diskio.h"
#include "flashfs.h"

#ifndef PICOPERL_FS_SIZE
#define PICOPERL_FS_SIZE (1024 * 1024)
#endif
#define FS_OFFSET   (PICO_FLASH_SIZE_BYTES - PICOPERL_FS_SIZE)
#define SECTOR_SIZE 512
#define BLOCK_SIZE  FLASH_SECTOR_SIZE          /* 4096: smallest erasable unit */
#define SECTORS     (PICOPERL_FS_SIZE / SECTOR_SIZE)

struct pp_embedded_file { const char *name; const char *src; };
extern const struct pp_embedded_file pp_embedded_files[];
extern const int pp_embedded_count;

static FATFS fatfs;
static bool mounted;

/* ---- raw flash block device -------------------------------------------- */

static uint8_t  cache[BLOCK_SIZE] __attribute__((aligned(4)));
static int32_t  cache_block = -1;   /* block number held in cache, -1 = none */
static bool     cache_dirty;
static uint32_t last_host_write_ms;
static bool     host_wrote;

static const uint8_t *flash_ptr(uint32_t offset) {
    /* read through the uncached XIP alias so freshly programmed data is visible */
    return (const uint8_t *)(XIP_NOCACHE_NOALLOC_BASE + FS_OFFSET + offset);
}

static bool reload_requested;
static bool fs_stale;
static void cache_flush(void);               /* the host wrote: FatFs' cached view must be refreshed */

static void remount_if_stale(void) {
    if (!fs_stale || !mounted) return;
    cache_flush();
    f_mount(NULL, "", 0);
    mounted = f_mount(&fatfs, "", 1) == FR_OK;
    fs_stale = false;
}

static void cache_flush(void) {
    if (cache_block < 0 || !cache_dirty) return;
    if (memcmp(cache, flash_ptr((uint32_t)cache_block * BLOCK_SIZE), BLOCK_SIZE) != 0) {
        uint32_t off = FS_OFFSET + (uint32_t)cache_block * BLOCK_SIZE;
        /* single core: just keep interrupt handlers (which live in flash) out of the way */
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(off, BLOCK_SIZE);
        flash_range_program(off, cache, BLOCK_SIZE);
        restore_interrupts(ints);
    }
    cache_dirty = false;
}

static void cache_load(uint32_t block) {
    if (cache_block == (int32_t)block) return;
    cache_flush();
    memcpy(cache, flash_ptr(block * BLOCK_SIZE), BLOCK_SIZE);
    cache_block = (int32_t)block;
    cache_dirty = false;
}

static void dev_read(uint32_t lba, uint32_t offset, void *buf, uint32_t len) {
    uint32_t addr = lba * SECTOR_SIZE + offset;
    while (len) {
        uint32_t block = addr / BLOCK_SIZE, in = addr % BLOCK_SIZE;
        uint32_t n = BLOCK_SIZE - in; if (n > len) n = len;
        const uint8_t *src = (cache_block == (int32_t)block) ? cache + in : flash_ptr(addr);
        memcpy(buf, src, n);
        buf = (uint8_t *)buf + n; addr += n; len -= n;
    }
}

static void dev_write(uint32_t lba, uint32_t offset, const void *buf, uint32_t len) {
    uint32_t addr = lba * SECTOR_SIZE + offset;
    while (len) {
        uint32_t block = addr / BLOCK_SIZE, in = addr % BLOCK_SIZE;
        uint32_t n = BLOCK_SIZE - in; if (n > len) n = len;
        cache_load(block);
        if (memcmp(cache + in, buf, n) != 0) { memcpy(cache + in, buf, n); cache_dirty = true; }
        buf = (const uint8_t *)buf + n; addr += n; len -= n;
    }
}

/* ---- FatFs disk I/O ------------------------------------------------------ */

DSTATUS disk_status(BYTE pdrv) { (void)pdrv; return 0; }
DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return 0; }
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    (void)pdrv;
    dev_read((uint32_t)sector, 0, buff, count * SECTOR_SIZE);
    return RES_OK;
}
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    (void)pdrv;
    dev_write((uint32_t)sector, 0, buff, count * SECTOR_SIZE);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    (void)pdrv;
    switch (cmd) {
    case CTRL_SYNC: cache_flush(); return RES_OK;
    case GET_SECTOR_COUNT: *(LBA_t *)buff = SECTORS; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = BLOCK_SIZE / SECTOR_SIZE; return RES_OK;
    default: return RES_PARERR;
    }
}

/* ---- files --------------------------------------------------------------- */

bool flashfs_mounted(void) { return mounted; }

char *flashfs_read_file(const char *path) {
    remount_if_stale();
    if (!mounted) return NULL;
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return NULL;
    UINT size = (UINT)f_size(&f);
    char *buf = malloc(size + 1);
    if (!buf) { f_close(&f); return NULL; }
    UINT br = 0;
    FRESULT r = f_read(&f, buf, size, &br);
    f_close(&f);
    if (r != FR_OK) { free(buf); return NULL; }
    buf[br] = 0;
    return buf;
}

char *flashfs_find_file(const char *name) {
    char path[160];
    snprintf(path, sizeof path, "/lib/%s", name);
    char *s = flashfs_read_file(path);
    if (s) return s;
    snprintf(path, sizeof path, "/%s", name);
    return flashfs_read_file(path);
}

static void mkdirs_for(const char *path) {
    char tmp[160];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = 0; f_mkdir(tmp); *p = '/'; }
    }
}

bool flashfs_write_file(const char *path, const char *data, unsigned len) {
    remount_if_stale();
    if (!mounted) return false;
    mkdirs_for(path);
    FIL f;
    if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
    UINT bw = 0;
    FRESULT r = f_write(&f, data, len, &bw);
    f_close(&f);
    return r == FR_OK && bw == len;
}

/* ---- stdio-like handles for the interpreter ------------------------------- */

static char io_errmsg[40] = "";

static const char *fr_str(FRESULT r) {
    switch (r) {
    case FR_NO_FILE: case FR_NO_PATH: return "No such file or directory";
    case FR_DENIED: case FR_WRITE_PROTECTED: return "Permission denied";
    case FR_EXIST: return "File exists";
    case FR_INVALID_NAME: return "Invalid argument";
    case FR_NOT_ENOUGH_CORE: return "Out of memory";
    default: snprintf(io_errmsg, sizeof io_errmsg, "I/O error (FatFs %d)", (int)r); return io_errmsg;
    }
}
static const char *io_err = "";
const char *flashfs_io_error(void) { return io_err; }

void *flashfs_fopen(const char *path, const char *mode) {
    remount_if_stale();
    if (!mounted) { io_err = "filesystem not mounted"; return NULL; }
    BYTE fl;
    if (mode[0] == 'r') fl = FA_READ | (mode[1] == '+' ? FA_WRITE : 0);
    else if (mode[0] == 'w') fl = FA_WRITE | FA_CREATE_ALWAYS | (mode[1] == '+' ? FA_READ : 0);
    else fl = FA_WRITE | FA_OPEN_APPEND;
    FIL *f = malloc(sizeof *f);
    if (!f) { io_err = "Out of memory"; return NULL; }
    FRESULT r = f_open(f, path, fl);
    if (r != FR_OK) { free(f); io_err = fr_str(r); return NULL; }
    return f;
}
int flashfs_fclose(void *f) {
    FRESULT r = f_close((FIL *)f);
    free(f);
    if (r != FR_OK) { io_err = fr_str(r); return -1; }
    return 0;
}
int flashfs_fread(void *f, void *buf, int n) {
    UINT br = 0;
    if (f_read((FIL *)f, buf, (UINT)n, &br) != FR_OK) return 0;
    return (int)br;
}
int flashfs_fwrite(void *f, const void *buf, int n) {
    UINT bw = 0;
    FRESULT r = f_write((FIL *)f, buf, (UINT)n, &bw);
    if (r != FR_OK) { io_err = fr_str(r); return (int)bw; }
    return (int)bw;
}
long flashfs_fseek(void *f, long off, int whence) {
    FIL *fp = f;
    FSIZE_t base = whence == 1 ? f_tell(fp) : whence == 2 ? f_size(fp) : 0;
    long target = (long)base + off;
    if (target < 0) { io_err = "Invalid argument"; return -1; }
    FRESULT r = f_lseek(fp, (FSIZE_t)target);
    if (r != FR_OK) { io_err = fr_str(r); return -1; }
    return (long)f_tell(fp);
}
long flashfs_ftell(void *f) { return (long)f_tell((FIL *)f); }
int  flashfs_feof(void *f) { return f_eof((FIL *)f) ? 1 : 0; }

static const char readme_txt[] =
    "picoperl\r\n"
    "========\r\n"
    "This drive lives in the flash of the board. /main.pl runs at boot;\r\n"
    "modules under /lib are found by `use` and `require`.\r\n"
    "Save a file and the board restarts with the new code.\r\n"
    "Messages from print/warn/die appear on the USB serial port\r\n"
    "(open it at 1200 baud to enter the UF2 bootloader).\r\n";

static void populate_defaults(void) {
    for (int i = 0; i < pp_embedded_count; i++) {
        char path[160];
        const char *name = pp_embedded_files[i].name;
        if (i == 0) snprintf(path, sizeof path, "/main.pl");
        else snprintf(path, sizeof path, "/lib/%s", name);
        flashfs_write_file(path, pp_embedded_files[i].src, (unsigned)strlen(pp_embedded_files[i].src));
    }
    flashfs_write_file("/README.TXT", readme_txt, (unsigned)strlen(readme_txt));
}

/* ---- change detection ---------------------------------------------------- */
/* Reload only when the script files really change. Windows writes to the drive
   just after mounting it (System Volume Information, FAT metadata, indexer
   files), which must not restart the script: compare a content hash of
   /main.pl and everything under /lib instead of trusting the write itself. */

static uint32_t script_hash;

/* per-file snapshot so a spurious reload can be diagnosed over serial */
#define SNAP_MAX 48
typedef struct { uint32_t ch; char path[40]; } snap_ent;
static snap_ent snap[SNAP_MAX]; static int nsnap;
static snap_ent scan[SNAP_MAX]; static int nscan;
static char change_info[176] = "no scan yet";

const char *flashfs_change_info(void) { return change_info; }

static uint32_t fnv1a(uint32_t h, const void *p, uint32_t n) {
    const uint8_t *b = p;
    while (n--) { h ^= *b++; h *= 16777619u; }
    return h;
}

static uint32_t hash_file(const char *path) {
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return 0;
    uint32_t h = fnv1a(2166136261u, path, (uint32_t)strlen(path) + 1);
    uint8_t buf[256];
    UINT br;
    while (f_read(&f, buf, sizeof buf, &br) == FR_OK && br > 0) h = fnv1a(h, buf, br);
    f_close(&f);
    if (nscan < SNAP_MAX) { scan[nscan].ch = h; snprintf(scan[nscan].path, sizeof scan[nscan].path, "%s", path); nscan++; }
    return h;
}

/* order-independent sum, so directory-entry shuffling does not count as a change */
static void hash_dir(const char *dir, int depth, uint32_t *total) {
    DIR d;
    static FILINFO fi;               /* FILINFO is large (LFN); single-threaded here */
    if (depth > 4 || f_opendir(&d, dir) != FR_OK) return;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        char path[192];
        snprintf(path, sizeof path, "%s/%s", dir, fi.fname);
        if (fi.fattrib & AM_DIR) hash_dir(path, depth + 1, total);
        else *total += hash_file(path);
    }
    f_closedir(&d);
}

static uint32_t compute_script_hash(void) {
    nscan = 0;
    remount_if_stale();
    if (!mounted) return 0;
    uint32_t total = hash_file("/main.pl");
    hash_dir("/lib", 0, &total);
    return total;
}

/* describe the difference between snap[] and scan[] in change_info */
static void describe_change(uint32_t oldh, uint32_t newh) {
    const char *what = NULL; const char *which = "";
    for (int i = 0; i < nscan && !what; i++) {
        int j = 0;
        while (j < nsnap && strcmp(snap[j].path, scan[i].path) != 0) j++;
        if (j == nsnap) { what = "new"; which = scan[i].path; }
        else if (snap[j].ch != scan[i].ch) { what = "edited"; which = scan[i].path; }
    }
    for (int j = 0; j < nsnap && !what; j++) {
        int i = 0;
        while (i < nscan && strcmp(snap[j].path, scan[i].path) != 0) i++;
        if (i == nscan) { what = "removed"; which = snap[j].path; }
    }
    if (!what) what = mounted ? "same names, sum differs" : "not mounted";
    snprintf(change_info, sizeof change_info, "hash %08lx->%08lx files %d->%d %s %s",
             (unsigned long)oldh, (unsigned long)newh, nsnap, nscan, what, which);
}

static void take_snapshot(void) {
    memcpy(snap, scan, sizeof snap[0] * (size_t)nscan);
    nsnap = nscan;
}

bool flashfs_init(void) {
#if PICOPERL_FS_RESET
    /* development mode: the drive is recreated from the built-in files on every boot,
       so the state of the board is fully determined by the firmware image */
    FRESULT r = FR_NO_FILESYSTEM;
#else
    FRESULT r = f_mount(&fatfs, "", 1);
#endif
    if (r == FR_NO_FILESYSTEM) {
        MKFS_PARM opt = { FM_FAT | FM_SFD, 1, 0, 128, BLOCK_SIZE };
        uint8_t *work = malloc(BLOCK_SIZE);
        r = work ? f_mkfs("", &opt, work, BLOCK_SIZE) : FR_NOT_ENOUGH_CORE;
        free(work);
        if (r == FR_OK) r = f_mount(&fatfs, "", 1);
        if (r == FR_OK) {
            mounted = true;
            f_setlabel("PICOPERL");
            populate_defaults();
        }
    }
    mounted = (r == FR_OK);
    cache_flush();
    if (mounted) { script_hash = compute_script_hash(); take_snapshot(); }
    return mounted;
}

void flashfs_poll(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (cache_dirty && now - last_host_write_ms > 100) cache_flush();
    if (host_wrote && now - last_host_write_ms > 1500) {
        cache_flush();
        host_wrote = false;
        uint32_t h = compute_script_hash();
        if (h != script_hash) {
            describe_change(script_hash, h);
            take_snapshot();
            script_hash = h;
            reload_requested = true;
        }
    }
}

bool flashfs_take_reload_request(void) {
    bool r = reload_requested;
    reload_requested = false;
    return r;
}

bool flashfs_reload_pending(void) { return reload_requested; }

/* ---- TinyUSB mass storage callbacks --------------------------------------- */

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    memcpy(vendor_id, "picoperl", 8);
    memcpy(product_id, "PICOPERL drive  ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) { (void)lun; return true; }

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = SECTORS;
    *block_size = SECTOR_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)lun; (void)power_condition; (void)start;
    if (load_eject && !start) cache_flush();
    return true;
}

bool tud_msc_is_writable_cb(uint8_t lun) { (void)lun; return true; }

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    (void)lun;
    if (lba >= SECTORS) return -1;
    dev_read(lba, offset, buffer, bufsize);
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    (void)lun;
    if (lba >= SECTORS) return -1;
    dev_write(lba, offset, buffer, bufsize);
    last_host_write_ms = to_ms_since_boot(get_absolute_time());
    host_wrote = true;
    fs_stale = true;
    return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
    (void)buffer; (void)bufsize;
    switch (scsi_cmd[0]) {
    case 0x35:   /* SYNCHRONIZE CACHE (10) */
        cache_flush();
        return 0;
    default:
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        return -1;
    }
}
