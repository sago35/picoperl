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
    return mounted;
}

void flashfs_poll(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (cache_dirty && now - last_host_write_ms > 100) cache_flush();
    if (host_wrote && now - last_host_write_ms > 1500) {
        cache_flush();
        host_wrote = false;
        reload_requested = true;
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
