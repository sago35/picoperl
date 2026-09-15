/* picoperl host CLI: runs a script on a desktop OS (for tests / development). */
#include "../src/picoperl.h"
#include "../src/pp_internal.h"
#include "../src/pp_gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

static char *script_dir;

void pp_host_write(int fd, const char *buf, int len) {
    fwrite(buf, 1, len, fd == 2 ? stderr : stdout);
    if (fd == 2) fflush(stderr); else fflush(stdout);
}

static char *read_whole(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    if (fread(buf, 1, n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    buf[n] = 0; fclose(f);
    return buf;
}

/* @INC on the host: the script's directory, ./lib, <script dir>/lib, <script dir>/../lib, PICOPERL_LIB */
char *pp_host_read_file(const char *name) {
    char *s = read_whole(name);
    if (s) return s;
    char path[1024];
    if (script_dir) { snprintf(path, sizeof path, "%s/%s", script_dir, name); if ((s = read_whole(path))) return s; }
    snprintf(path, sizeof path, "lib/%s", name); if ((s = read_whole(path))) return s;
    if (script_dir) {
        snprintf(path, sizeof path, "%s/lib/%s", script_dir, name); if ((s = read_whole(path))) return s;
        snprintf(path, sizeof path, "%s/../lib/%s", script_dir, name); if ((s = read_whole(path))) return s;
    }
    if (getenv("PICOPERL_LIB")) { snprintf(path, sizeof path, "%s/%s", getenv("PICOPERL_LIB"), name); if ((s = read_whole(path))) return s; }
    return NULL;
}

void pp_host_poll(void);   /* defined below the GPIO simulation */
void pp_host_fatal(const char *msg) { fputs(msg, stderr); exit(255); }
int pp_host_const(const char *name, pp_val *out) { return pp_keycodes_const(name, out) || pp_tone_const(name, out); }

/* ---- file I/O: plain stdio ---- */
void *pp_host_fopen(const char *path, const char *mode) {
    char m[4];
    snprintf(m, sizeof m, "%sb", mode);   /* binary, like the board */
    return fopen(path, m);
}
int  pp_host_fclose(void *f) { return fclose((FILE *)f); }
int  pp_host_fread(void *f, void *buf, int n) { return (int)fread(buf, 1, (size_t)n, (FILE *)f); }
int  pp_host_fwrite(void *f, const void *buf, int n) { return (int)fwrite(buf, 1, (size_t)n, (FILE *)f); }
long pp_host_fseek(void *f, long off, int whence) {
    if (fseek((FILE *)f, off, whence == 1 ? SEEK_CUR : whence == 2 ? SEEK_END : SEEK_SET) != 0) return -1;
    return ftell((FILE *)f);
}
long pp_host_ftell(void *f) { return ftell((FILE *)f); }
int  pp_host_feof(void *f) {
    int c = fgetc((FILE *)f);
    if (c == EOF) return 1;
    ungetc(c, (FILE *)f);
    return 0;
}
const char *pp_host_io_error(void) { return strerror(errno); }
uint32_t pp_host_millis(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
int64_t pp_host_time(void) { return (int64_t)time(NULL); }
static long sim_cycles;
static pp_interp *I_for_stats;

/* sleep in short chunks so watched inputs are polled and a due event or
 * alarm() wakes the script early, like a perl signal would */
static void host_wait_us(uint64_t us);
static void host_sleep_us_poll(uint64_t us) {
    for (;;) {
        pp_host_poll();
        if (I_for_stats && pp_events_pending(I_for_stats)) return;
        if (!us) return;
        uint64_t chunk = us > 50000 ? 50000 : us;
        host_wait_us(chunk);
        us -= chunk;
    }
}
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
/* emscripten's libc has no mallinfo2: report zero heap use (only affects the [mem] stats line) */
struct mallinfo2 { size_t uordblks; };
static struct mallinfo2 mallinfo2(void) { struct mallinfo2 mi = {0}; return mi; }
/* usleep busy-waits in wasm; yield to the browser event loop instead (needs -sASYNCIFY) */
static void host_wait_us(uint64_t us) { emscripten_sleep((unsigned)(us / 1000)); }
/* board events for the playground page and input overrides from its widgets (wasm/shim.c) */
void pp_wasm_note(const char *what, int a, int b, int c, int d, int e);
void pp_wasm_note_frame(int w, int h, const uint8_t *buf, int len);
void pp_wasm_note_leds(int pin, const uint8_t *rgb, int n);
void pp_wasm_note_keys(int mod, const uint8_t *keys, int n);
int  pp_wasm_gpio_override(int pin);   /* input level forced from the page; -1 = none */
int  pp_wasm_encoder_take(void);       /* pending encoder steps clicked on the page */
int  pp_wasm_adc_override(int pin);    /* 16-bit ADC value forced from the page; -1 = none */
#else
#include <malloc.h>
static void host_wait_us(uint64_t us) { usleep(us); }
static void pp_wasm_note(const char *what, int a, int b, int c, int d, int e) { (void)what; (void)a; (void)b; (void)c; (void)d; (void)e; }
static void pp_wasm_note_frame(int w, int h, const uint8_t *buf, int len) { (void)w; (void)h; (void)buf; (void)len; }
static void pp_wasm_note_leds(int pin, const uint8_t *rgb, int n) { (void)pin; (void)rgb; (void)n; }
static void pp_wasm_note_keys(int mod, const uint8_t *keys, int n) { (void)mod; (void)keys; (void)n; }
static int  pp_wasm_gpio_override(int pin) { (void)pin; return -1; }
static int  pp_wasm_encoder_take(void) { return 0; }
static int  pp_wasm_adc_override(int pin) { (void)pin; return -1; }
#endif
void pp_host_sleep_ms(uint32_t ms) {
    pp_interp *I = I_for_stats;
    /* PICOPERL_SIM_CYCLES=N stops the (otherwise endless) loop after N sleeps; PICOPERL_SIM_FAST skips the wait */
    if (getenv("PICOPERL_MEMSTAT") && sim_cycles == 0) {
        struct mallinfo2 mi = mallinfo2();
        long nsym = 0, symbytes = 0;
        for (int b = 0; b < PP_SYM_BUCKETS; b++) for (pp_sym *sy = I->syms[b]; sy; sy = sy->next) { nsym++; symbytes += 24 + strlen(sy->name) + 1; }
        printf("[mem] heap in use: %zu bytes; live AST nodes %ld (~%ld B on ARM), strings %ld, objects %ld (~%ld B), symbols %ld (~%ld B), globals %d\n",
               mi.uordblks, pp_stat_nodes, pp_stat_nodes * 32, pp_stat_strs, pp_stat_objs, pp_stat_objs * 56, nsym, symbytes, I->globals->n); fflush(stdout);
    }
    const char *lim = getenv("PICOPERL_SIM_CYCLES");
    if (lim && ++sim_cycles >= atol(lim)) { printf("[sim] %ld cycles done\n", sim_cycles); fflush(stdout); exit(0); }
    if (getenv("PICOPERL_SIM_FAST")) return;
    host_sleep_us_poll((uint64_t)ms * 1000);
}
char *pp_host_read_line(void) {
    char buf[4096];
    if (!fgets(buf, sizeof buf, stdin)) return NULL;
    return strdup(buf);
}

/* --- simulated board API so keyboard scripts can be smoke-tested on the host --- */
static int sim_gpio[64];
static int sim_dir[64];
static int sim_pull[64];

static pp_val n_gpio_init(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; int p = pp_list_arg_int(a, 0, 0) & 63; sim_dir[p] = 0; pp_wasm_note("gpio_dir", p, 0, 0, 0, 0); return pp_undef(); }
static pp_val n_gpio_set_dir(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; int p = pp_list_arg_int(a, 0, 0) & 63; sim_dir[p] = pp_list_arg_int(a, 1, 0); pp_wasm_note("gpio_dir", p, sim_dir[p], 0, 0, 0); return pp_undef(); }
static pp_val n_gpio_pull_up(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; int p = pp_list_arg_int(a, 0, 0) & 63; sim_pull[p] = 1; pp_wasm_note("gpio_pull", p, 1, 0, 0, 0); return pp_undef(); }
static pp_val n_gpio_pull_down(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; int p = pp_list_arg_int(a, 0, 0) & 63; sim_pull[p] = -1; pp_wasm_note("gpio_pull", p, -1, 0, 0, 0); return pp_undef(); }
static pp_val n_gpio_put(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; sim_gpio[pp_list_arg_int(a, 0, 0) & 63] = pp_list_arg_int(a, 1, 0) ? 1 : 0; return pp_undef(); }
static int sim_get(int p);
static pp_val n_gpio_get(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    return pp_int(sim_get(pp_list_arg_int(a, 0, 0) & 63));
}
static int sim_get(int p) {
    if (sim_dir[p]) return sim_gpio[p];
    int ov = pp_wasm_gpio_override(p);
    if (ov >= 0) return ov;
    /* PICOPERL_SIM_PRESSED="2:4,3:5" = switches between output pin 2 and input pin 4, 3 and 5.
       A pressed switch makes the input read the level of the output pin it is wired to
       (works for active-low/pull-up and active-high/pull-down matrices); an output at its
       idle level is not distinguishable from the pull resistor, like a real diode matrix. */
    const char *env = getenv("PICOPERL_SIM_PRESSED");
    if (env) {
        const char *q = env;
        while (*q) {
            int r = -1, c = -1;
            if (sscanf(q, "%d:%d", &r, &c) == 2 && c == p && r >= 0 && r < 64 && sim_dir[r]) {
                int idle = sim_pull[p] < 0 ? 0 : 1;
                if (sim_gpio[r] != idle) return sim_gpio[r];
            }
            /* a bare pin number: switch to GND on that input (direct key), reads low */
            if (sscanf(q, "%d", &r) == 1 && (q[strspn(q, "0123456789")] == ',' || q[strspn(q, "0123456789")] == 0) && r == p) return 0;
            while (*q && *q != ',') q++;
            if (*q == ',') q++;
        }
    }
    return sim_pull[p] < 0 ? 0 : 1;
}
static pp_val n_matrix_scan(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 2 || !pp_val_is_array_ref(a->v[0]) || !pp_val_is_array_ref(a->v[1])) return pp_die(I, "matrix_scan: expected two array refs");
    pp_obj *outs = a->v[0].u.o, *ins = a->v[1].u.o;
    int active = pp_list_arg_int(a, 2, 0) ? 1 : 0;
    pp_obj *res = pp_new_array();
    for (int o = 0; o < outs->u.arr.n; o++) {
        int op = (int)pp_to_int(outs->u.arr.v[o]) & 63;
        sim_gpio[op] = active;
        for (int i = 0; i < ins->u.arr.n; i++) { int v = sim_get((int)pp_to_int(ins->u.arr.v[i]) & 63); pp_array_push(res, pp_int(active ? v : !v)); }
        sim_gpio[op] = !active;
    }
    pp_val r = pp_ref(res); pp_obj_decref(res);
    return r;
}
static pp_val n_encoder_init(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; pp_wasm_note("encoder", (int)pp_list_arg_int(a, 0, -1), (int)pp_list_arg_int(a, 1, -1), 0, 0, 0); return pp_int(1); }
/* ADC simulation: PICOPERL_SIM_ADC="29:0x7440,28:0x8000" gives 16-bit values per pin (default mid scale) */
static pp_val n_adc_configure(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    int p = pp_list_arg_int(a, 0, -1);
    if (!((p >= 26 && p <= 29) || p == 4)) return pp_undef();
    pp_wasm_note("adc", p, 0, 0, 0, 0);
    return pp_int(1);
}
static pp_val n_adc_read(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    int pin = pp_list_arg_int(a, 0, -1);
    long v16 = 0x8000;
    const char *env = getenv("PICOPERL_SIM_ADC");
    if (env) { const char *q = env; while (*q) { int p; long v; if (sscanf(q, "%d:%li", &p, &v) == 2 && p == pin) v16 = v; while (*q && *q != ',') q++; if (*q == ',') q++; } }
    int ov = pp_wasm_adc_override(pin);
    if (ov >= 0) v16 = ov;
    return pp_int((v16 >> 4) & 0xFFF);   /* natives return the raw 12-bit sample */
}
/* SSD1306 simulation: the display handle is the Gfx handle; PICOPERL_SIM_OLED=1 prints the frame as ASCII
   (one character per 2x4 pixel block), PICOPERL_SIM_OLED=count prints only the number of refreshes. */
static long oled_refreshes;
static pp_val n_ssd_init(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return pp_int(pp_list_arg_int(a, 0, 0)); }
static pp_val n_ssd_display(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    pp_gfx *g = pp_gfx_handle(pp_list_arg_int(a, 0, 0));
    if (!g) return pp_int(0);
    oled_refreshes++;
    pp_wasm_note_frame(g->w, g->h, g->buf, g->w * g->pages);
    const char *mode = getenv("PICOPERL_SIM_OLED");
    if (!mode) return pp_int(1);
    if (strcmp(mode, "count") == 0) { printf("[oled] refresh #%ld\n", oled_refreshes); fflush(stdout); return pp_int(1); }
    printf("[oled] frame #%ld (%dx%d, 2x4 pixels per char)\n", oled_refreshes, g->w, g->h);
    for (int by = 0; by < g->h; by += 4) {
        putchar('|');
        for (int bx = 0; bx < g->w; bx += 2) {
            int n = 0;
            for (int y = by; y < by + 4 && y < g->h; y++) for (int x = bx; x < bx + 2 && x < g->w; x++) n += (g->buf[(y >> 3) * g->w + x] >> (y & 7)) & 1;
            putchar(n == 0 ? ' ' : n < 4 ? '.' : n < 7 ? '+' : '#');
        }
        puts("|");
    }
    fflush(stdout);
    return pp_int(1);
}
static pp_val n_ssd_noop(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(1); }
static pp_val n_i2c_write(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return pp_int(a->n > 2 && a->v[2].t == PP_STR ? a->v[2].u.s->len : 0); }
static pp_val n_i2c_read(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; int n = pp_list_arg_int(a, 2, 1); char z[256]; if (n > 256) n = 256; memset(z, 0, sizeof z); return pp_strn(z, n); }
static int ws_pins[4] = { -1, -1, -1, -1 };
static pp_val n_ws2812_init(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    int pin = pp_list_arg_int(a, 0, 16);
    for (int i = 0; i < 4; i++) { if (ws_pins[i] == pin) return pp_int(i); if (ws_pins[i] < 0) { ws_pins[i] = pin; return pp_int(i); } }
    return pp_int(-1);
}
static pp_val n_ws2812_write_raw(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    uint8_t rgb[128 * 3]; int nn = a->n - 1;
    if (nn > 128) nn = 128;
    for (int i = 0; i < nn; i++) {   /* 0xGGRRBBxx per LED */
        unsigned v = (unsigned)pp_to_int(a->v[i + 1]);
        rgb[i * 3] = (v >> 16) & 255; rgb[i * 3 + 1] = (v >> 24) & 255; rgb[i * 3 + 2] = (v >> 8) & 255;
    }
    pp_wasm_note_leds(ws_pins[pp_list_arg_int(a, 0, 0) & 3], rgb, nn);
    if (getenv("PICOPERL_SIM_QUIET")) return pp_int(nn);   /* like the firmware: at most 128 sent */
    printf("[ws2812 pin %d]", ws_pins[pp_list_arg_int(a, 0, 0) & 3]);
    for (int i = 1; i < a->n; i++) printf(" %08x", (unsigned)pp_to_int(a->v[i]));
    printf("\n"); fflush(stdout);
    return pp_int(nn);   /* like the firmware: at most 128 sent */
}
static pp_val n_ws2812_put_color(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    int r = 0, g = 0, b = 0;
    if (a->n >= 2 && a->v[1].t == PP_REF && a->v[1].u.o->kind == PP_OHASH) {
        pp_obj *h = a->v[1].u.o;
        r = (int)pp_to_int(pp_hash_get(h, "R", 1)); g = (int)pp_to_int(pp_hash_get(h, "G", 1)); b = (int)pp_to_int(pp_hash_get(h, "B", 1));
    } else { r = pp_list_arg_int(a, 1, 0); g = pp_list_arg_int(a, 2, 0); b = pp_list_arg_int(a, 3, 0); }
    uint8_t rgb[3] = { (uint8_t)r, (uint8_t)g, (uint8_t)b };
    pp_wasm_note_leds(ws_pins[pp_list_arg_int(a, 0, 0) & 3], rgb, 1);
    if (!getenv("PICOPERL_SIM_QUIET")) { printf("[ws2812 pin %d] R=%02x G=%02x B=%02x\n", ws_pins[pp_list_arg_int(a, 0, 0) & 3], r, g, b); fflush(stdout); }
    return pp_int(1);
}
static pp_val n_encoder_read(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    /* PICOPERL_SIM_ENCODER=N: report N steps once */
    (void)I; (void)a; (void)ctx; (void)out;
    int pend = pp_wasm_encoder_take();
    if (pend) return pp_int(pend);
    static int done;
    const char *e = getenv("PICOPERL_SIM_ENCODER");
    if (e && !done) { done = 1; return pp_int(atoi(e)); }
    return pp_int(0);
}

/* ---- deferred events for Perl callbacks (Gpio->new(on_falling => ...)) ----
 * The page's widgets (wasm) or the PICOPERL_SIM_* env change simulated inputs;
 * pp_host_poll turns level changes into events for PicoPerl::Event. */
static uint8_t watch_mask[64];      /* bit 1: falling, bit 2: rising */
static uint8_t watch_last[64];
static int enc_watch_on, enc_env_sent;

static pp_val n_gpio_watch(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    int p = pp_list_arg_int(a, 0, 0) & 63;
    watch_mask[p] = (uint8_t)pp_list_arg_int(a, 1, 3);
    watch_last[p] = (uint8_t)sim_get(p);
    return pp_int(1);
}
static pp_val n_encoder_watch(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    enc_watch_on = (int)pp_list_arg_int(a, 0, 1);
    return pp_int(1);
}

void pp_host_poll(void) {
    pp_interp *I = I_for_stats;
    if (!I) return;
    for (int p = 0; p < 64; p++) {
        if (!watch_mask[p]) continue;
        int v = sim_get(p);
        if (v == watch_last[p]) continue;
        watch_last[p] = (uint8_t)v;
        if (watch_mask[p] & (v ? 2 : 1)) pp_event_push(I, PP_EV_GPIO, p, v);
    }
    if (enc_watch_on) {
        int s = pp_wasm_encoder_take();
        if (!s && !enc_env_sent && getenv("PICOPERL_SIM_ENCODER")) { s = atoi(getenv("PICOPERL_SIM_ENCODER")); enc_env_sent = 1; }
        if (s) pp_event_push(I, PP_EV_ENCODER, 0, s);
    }
}
static pp_val n_sleep_ms(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    pp_host_sleep_ms((uint32_t)pp_list_arg_int(a, 0, 0));
    return pp_undef();
}
static pp_val n_sleep_us(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; if (getenv("PICOPERL_SIM_FAST")) return pp_undef(); host_sleep_us_poll((uint64_t)pp_list_arg_int(a, 0, 0)); return pp_undef(); }
static pp_val n_time_ms(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(pp_host_millis()); }
static pp_val n_usb_mounted(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(1); }
static pp_val n_hid_ready(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(1); }
static pp_val n_hid_keyboard_report(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    uint8_t keys[6]; int nk = 0;
    for (int i = 1; i < a->n && i <= 6; i++) keys[nk++] = (uint8_t)pp_to_int(a->v[i]);
    pp_wasm_note_keys((int)pp_list_arg_int(a, 0, 0), keys, nk);
    if (getenv("PICOPERL_SIM_QUIET")) return pp_int(1);
    printf("[hid] mod=0x%02x keys=", (unsigned)pp_list_arg_int(a, 0, 0));
    for (int i = 0; i < nk; i++) printf("%s0x%02x", i > 0 ? "," : "", keys[i]);
    printf("\n");
    return pp_int(1);
}
static pp_val n_hid_mouse_ready(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(1); }
static pp_val n_hid_mouse_report(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    pp_wasm_note("hid_mouse", (int)pp_list_arg_int(a, 0, 0), (int)pp_list_arg_int(a, 1, 0), (int)pp_list_arg_int(a, 2, 0),
                 (int)pp_list_arg_int(a, 3, 0), (int)pp_list_arg_int(a, 4, 0));
    if (getenv("PICOPERL_SIM_QUIET")) return pp_int(1);
    printf("[mouse] buttons=0x%02x dx=%d dy=%d wheel=%d pan=%d\n", (unsigned)pp_list_arg_int(a, 0, 0), (int)pp_list_arg_int(a, 1, 0),
           (int)pp_list_arg_int(a, 2, 0), (int)pp_list_arg_int(a, 3, 0), (int)pp_list_arg_int(a, 4, 0));
    return pp_int(1);
}
static pp_val n_midi_ready(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(1); }
static pp_val n_midi_write(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    int n = a->n - 1;
    pp_wasm_note("midi", n > 0 ? (int)pp_to_int(a->v[1]) : 0, n > 1 ? (int)pp_to_int(a->v[2]) : 0,
                 n > 2 ? (int)pp_to_int(a->v[3]) : 0, (int)pp_list_arg_int(a, 0, 0), 0);
    if (getenv("PICOPERL_SIM_QUIET")) return pp_int(n);
    printf("[midi]");
    for (int i = 1; i < a->n; i++) printf(" %02x", (unsigned)pp_to_int(a->v[i]) & 0xFF);
    printf("\n"); fflush(stdout);
    return pp_int(n);
}
/* PWM: prints what the firmware would program; the period is modelled with a 16-bit counter at 125 MHz */
static int pwm_top_of[8] = { 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535 };
static pp_val n_pwm_init(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return pp_int((pp_list_arg_int(a, 0, 0) >> 1) & 7); }
static pp_val n_pwm_set_freq(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    int pin = (int)pp_list_arg_int(a, 0, 0) & 31;
    double hz = a->n > 1 ? pp_to_num(a->v[1]) : 0;
    if (!(hz > 0)) { pp_wasm_note("pwm_stop", pin, 0, 0, 0, 0); if (!getenv("PICOPERL_SIM_QUIET")) printf("[pwm pin %d] off\n", pin); return pp_int(0); }
    double div = 125e6 / (hz * 65536.0);
    int div16 = (int)(div * 16.0); if ((double)div16 < div * 16.0) div16++;
    if (div16 < 16) div16 = 16;
    long wrap = (long)(125e6 / (div16 / 16.0) / hz + 0.5) - 1;
    if (wrap > 0xFFFF) wrap = 0xFFFF;
    pwm_top_of[(pin >> 1) & 7] = (int)wrap;
    double real = 125e6 / (div16 / 16.0) / (double)(wrap + 1);
    pp_wasm_note("pwm_freq", pin, (int)(real * 1000 + 0.5), 0, 0, 0);   /* mHz */
    if (!getenv("PICOPERL_SIM_QUIET")) { printf("[pwm pin %d] %.2f Hz (div %.4f, wrap %ld)\n", pin, real, div16 / 16.0, wrap); fflush(stdout); }
    return pp_num(real);
}
static pp_val n_pwm_set_duty(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    int pin = (int)pp_list_arg_int(a, 0, 0);
    double d = a->n > 1 ? pp_to_num(a->v[1]) : 0.0;
    pp_wasm_note("pwm_duty", pin, (int)(d * 1000 + 0.5), 0, 0, 0);
    if (!getenv("PICOPERL_SIM_QUIET")) { printf("[pwm pin %d] duty %.3f\n", pin, d); fflush(stdout); }
    return pp_undef();
}
static pp_val n_pwm_enable(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; pp_wasm_note("pwm_en", (int)pp_list_arg_int(a, 0, 0), (int)pp_list_arg_int(a, 1, 1), 0, 0, 0); return pp_undef(); }
static pp_val n_pwm_stop(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    pp_wasm_note("pwm_stop", (int)pp_list_arg_int(a, 0, 0), 0, 0, 0, 0);
    if (!getenv("PICOPERL_SIM_QUIET")) { printf("[pwm pin %d] stop\n", (int)pp_list_arg_int(a, 0, 0)); fflush(stdout); }
    return pp_undef();
}
static pp_val n_pwm_top(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return pp_int(pwm_top_of[(pp_list_arg_int(a, 0, 0) >> 1) & 7]); }
static pp_val n_hid_leds(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(0); }
static pp_val n_hid_consumer_ready(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(1); }
static pp_val n_hid_consumer_report(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    pp_wasm_note("hid_consumer", (int)pp_list_arg_int(a, 0, 0), 0, 0, 0, 0);
    if (getenv("PICOPERL_SIM_QUIET")) return pp_int(1);
    printf("[consumer] usage=0x%03x\n", (unsigned)pp_list_arg_int(a, 0, 0));
    return pp_int(1);
}
static pp_val n_board_led(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_undef(); }
static pp_val n_reboot_bootsel(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; printf("[reboot to BOOTSEL]\n"); exit(0); }
static pp_val n_heap_free(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    /* host: report what a 264 KiB board would have left, scaled from the host heap use (rough) */
    (void)I; (void)a; (void)ctx; (void)out;
    struct mallinfo2 mi = mallinfo2();
    long est = 200 * 1024 - (long)(mi.uordblks * 0.6);
    return pp_int(est < 0 ? 0 : est);
}
static pp_val n_sim_exit_after(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_undef(); }

void pp_host_register(pp_interp *I) {
    pp_register(I, "Gpio::_init", n_gpio_init);
    pp_register(I, "Gpio::_set_dir", n_gpio_set_dir);
    pp_register(I, "Gpio::_pull_up", n_gpio_pull_up);
    pp_register(I, "Gpio::_pull_down", n_gpio_pull_down);
    pp_register(I, "Gpio::_put", n_gpio_put);
    pp_register(I, "Gpio::_get", n_gpio_get);
    pp_register(I, "Gpio::_watch", n_gpio_watch);
    pp_register(I, "Encoder::_watch", n_encoder_watch);
    pp_register(I, "Time::HiRes::_sleep_ms", n_sleep_ms);
    pp_register(I, "Time::HiRes::_usleep", n_sleep_us);
    pp_register(I, "Time::HiRes::_time_ms", n_time_ms);
    pp_register(I, "Usb::_mounted", n_usb_mounted);
    pp_register(I, "Hid::_ready", n_hid_ready);
    pp_register(I, "Hid::_keyboard_report", n_hid_keyboard_report);
    pp_register(I, "Hid::_leds", n_hid_leds);
    pp_register(I, "Hid::_consumer_ready", n_hid_consumer_ready);
    pp_register(I, "Hid::_consumer_report", n_hid_consumer_report);
    pp_register(I, "Hid::_mouse_ready", n_hid_mouse_ready);
    pp_register(I, "Hid::_mouse_report", n_hid_mouse_report);
    pp_register(I, "Midi::_ready", n_midi_ready);
    pp_register(I, "Midi::_write", n_midi_write);
    pp_register(I, "Pwm::_init", n_pwm_init);
    pp_register(I, "Pwm::_set_freq", n_pwm_set_freq);
    pp_register(I, "Pwm::_set_duty", n_pwm_set_duty);
    pp_register(I, "Pwm::_enable", n_pwm_enable);
    pp_register(I, "Pwm::_stop", n_pwm_stop);
    pp_register(I, "Pwm::_top", n_pwm_top);
    pp_register(I, "Board::_led", n_board_led);
    pp_register(I, "Board::_reboot_bootsel", n_reboot_bootsel);
    pp_register(I, "Board::_heap_free", n_heap_free);
    pp_register(I, "Matrix::_scan", n_matrix_scan);
    pp_register(I, "Adc::_configure", n_adc_configure);
    pp_register(I, "I2c::_init", n_ssd_noop);
    pp_register(I, "I2c::_write", n_i2c_write);
    pp_register(I, "I2c::_read", n_i2c_read);
    pp_register(I, "Ssd1306::_init", n_ssd_init);
    pp_register(I, "Ssd1306::_display", n_ssd_display);
    pp_register(I, "Ssd1306::_invert", n_ssd_noop);
    pp_register(I, "Ssd1306::_contrast", n_ssd_noop);
    pp_gfx_register(I);
    pp_register(I, "Adc::_read", n_adc_read);
    pp_register(I, "Ws2812::_init", n_ws2812_init);
    pp_register(I, "Ws2812::_write_raw", n_ws2812_write_raw);
    pp_register(I, "Ws2812::_put_color", n_ws2812_put_color);
    pp_register(I, "Encoder::_init", n_encoder_init);
    pp_register(I, "Encoder::_read", n_encoder_read);
    pp_keycodes_register(I);
    pp_define_const(I, "Board::LED_PIN", pp_int(25));
    pp_define_const(I, "Board::NAME", pp_cstr("host-sim"));
    (void)n_sim_exit_after;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: picoperl script.pl [args...]\n"); return 2; }
    char *src;
    if (strcmp(argv[1], "-e") == 0) { if (argc < 3) return 2; src = strdup(argv[2]); argv++; argc--; }
    else src = read_whole(argv[1]);
    if (!src) { fprintf(stderr, "Can't open perl script \"%s\": No such file or directory\n", argv[1]); return 2; }
    char *slash = strrchr(argv[1], '/');
    if (slash) { script_dir = strndup(argv[1], slash - argv[1]); }
    pp_interp *I = pp_new();
    I_for_stats = I;
    if (getenv("PICOPERL_STACK_LIMIT")) I->stack_limit = (size_t)atol(getenv("PICOPERL_STACK_LIMIT"));   /* emulate the board's stack budget */
    if (getenv("PICOPERL_MAX_DEPTH")) I->max_depth = atoi(getenv("PICOPERL_MAX_DEPTH"));
    pp_obj *argvcell = I->g_argv;
    for (int i = 2; i < argc; i++) pp_array_push(argvcell, pp_cstr(argv[i]));
    pp_obj *zero = pp_global(I, pp_intern(I, "$0", 2));
    zero->u.scalar = pp_cstr(argv[1]);
    int rc = pp_run(I, argv[1], src);
    fflush(stdout);
    pp_free(I);
    free(src);
    return rc;
}
