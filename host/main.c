/* picoperl host CLI: runs a script on a desktop OS (for tests / development). */
#include "../src/picoperl.h"
#include "../src/pp_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

void pp_host_poll(void) {}
void pp_host_fatal(const char *msg) { fputs(msg, stderr); exit(255); }
int pp_host_const(const char *name, pp_val *out) { return pp_keycodes_const(name, out); }
uint32_t pp_host_millis(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
int64_t pp_host_time(void) { return (int64_t)time(NULL); }
static long sim_cycles;
static pp_interp *I_for_stats;
#include <malloc.h>
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
    usleep(ms * 1000);
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

static pp_val n_gpio_init(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; int p = pp_list_arg_int(a, 0, 0) & 63; sim_dir[p] = 0; return pp_undef(); }
static pp_val n_gpio_set_dir(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; sim_dir[pp_list_arg_int(a, 0, 0) & 63] = pp_list_arg_int(a, 1, 0); return pp_undef(); }
static pp_val n_gpio_pull_up(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; sim_pull[pp_list_arg_int(a, 0, 0) & 63] = 1; return pp_undef(); }
static pp_val n_gpio_pull_down(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; sim_pull[pp_list_arg_int(a, 0, 0) & 63] = -1; return pp_undef(); }
static pp_val n_gpio_put(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; sim_gpio[pp_list_arg_int(a, 0, 0) & 63] = pp_list_arg_int(a, 1, 0) ? 1 : 0; return pp_undef(); }
static int sim_get(int p);
static pp_val n_gpio_get(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    return pp_int(sim_get(pp_list_arg_int(a, 0, 0) & 63));
}
static int sim_get(int p) {
    if (sim_dir[p]) return sim_gpio[p];
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
static pp_val n_encoder_init(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(1); }
static int ws_pins[4] = { -1, -1, -1, -1 };
static pp_val n_ws2812_init(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    int pin = pp_list_arg_int(a, 0, 16);
    for (int i = 0; i < 4; i++) { if (ws_pins[i] == pin) return pp_int(i); if (ws_pins[i] < 0) { ws_pins[i] = pin; return pp_int(i); } }
    return pp_int(-1);
}
static pp_val n_ws2812_write_raw(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    if (getenv("PICOPERL_SIM_QUIET")) return pp_int(a->n - 1);
    printf("[ws2812 pin %d]", ws_pins[pp_list_arg_int(a, 0, 0) & 3]);
    for (int i = 1; i < a->n; i++) printf(" %08x", (unsigned)pp_to_int(a->v[i]));
    printf("\n"); fflush(stdout);
    return pp_int(a->n - 1);
}
static pp_val n_ws2812_put_color(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    int r = 0, g = 0, b = 0;
    if (a->n >= 2 && a->v[1].t == PP_REF && a->v[1].u.o->kind == PP_OHASH) {
        pp_obj *h = a->v[1].u.o;
        r = (int)pp_to_int(pp_hash_get(h, "R", 1)); g = (int)pp_to_int(pp_hash_get(h, "G", 1)); b = (int)pp_to_int(pp_hash_get(h, "B", 1));
    } else { r = pp_list_arg_int(a, 1, 0); g = pp_list_arg_int(a, 2, 0); b = pp_list_arg_int(a, 3, 0); }
    if (!getenv("PICOPERL_SIM_QUIET")) { printf("[ws2812 pin %d] R=%02x G=%02x B=%02x\n", ws_pins[pp_list_arg_int(a, 0, 0) & 3], r, g, b); fflush(stdout); }
    return pp_int(1);
}
static pp_val n_encoder_read(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    /* PICOPERL_SIM_ENCODER=N: report N steps once */
    (void)I; (void)a; (void)ctx; (void)out;
    static int done;
    const char *e = getenv("PICOPERL_SIM_ENCODER");
    if (e && !done) { done = 1; return pp_int(atoi(e)); }
    return pp_int(0);
}
static pp_val n_sleep_ms(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    pp_host_sleep_ms((uint32_t)pp_list_arg_int(a, 0, 0));
    return pp_undef();
}
static pp_val n_sleep_us(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; if (getenv("PICOPERL_SIM_FAST")) return pp_undef(); usleep(pp_list_arg_int(a, 0, 0)); return pp_undef(); }
static pp_val n_time_ms(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(pp_host_millis()); }
static pp_val n_usb_mounted(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(1); }
static pp_val n_hid_ready(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(1); }
static pp_val n_hid_keyboard_report(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    printf("[hid] mod=0x%02x keys=", (unsigned)pp_list_arg_int(a, 0, 0));
    for (int i = 1; i < a->n && i <= 6; i++) printf("%s0x%02x", i > 1 ? "," : "", (unsigned)pp_to_int(a->v[i]));
    printf("\n");
    return pp_int(1);
}
static pp_val n_hid_leds(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(0); }
static pp_val n_hid_consumer_ready(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(1); }
static pp_val n_hid_consumer_report(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
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
    pp_register(I, "Time::HiRes::_sleep_ms", n_sleep_ms);
    pp_register(I, "Time::HiRes::_usleep", n_sleep_us);
    pp_register(I, "Time::HiRes::_time_ms", n_time_ms);
    pp_register(I, "Usb::_mounted", n_usb_mounted);
    pp_register(I, "Hid::_ready", n_hid_ready);
    pp_register(I, "Hid::_keyboard_report", n_hid_keyboard_report);
    pp_register(I, "Hid::_leds", n_hid_leds);
    pp_register(I, "Hid::_consumer_ready", n_hid_consumer_ready);
    pp_register(I, "Hid::_consumer_report", n_hid_consumer_report);
    pp_register(I, "Board::_led", n_board_led);
    pp_register(I, "Board::_reboot_bootsel", n_reboot_bootsel);
    pp_register(I, "Board::_heap_free", n_heap_free);
    pp_register(I, "Matrix::_scan", n_matrix_scan);
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
