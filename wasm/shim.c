/* Bridges board activity to the playground page and input widgets back into the
 * simulation. Compiled only into the wasm build (see the `wasm` Makefile target):
 * host/main.c calls the pp_wasm_* hooks below (they are no-ops in native builds),
 * and the page's widgets call the EMSCRIPTEN_KEEPALIVE setters through the worker
 * while the script sleeps. postMessage is missing under node (make wasm-test):
 * events are simply dropped there. */
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include "../src/picoperl.h"

EM_JS(void, js_note, (const char *what, int a, int b, int c, int d, int e), {
  if (typeof postMessage === "function")
    postMessage({ type: "board", what: UTF8ToString(what), a: a, b: b, c: c, d: d, e: e });
});
EM_JS(void, js_note_frame, (int w, int h, const uint8_t *buf, int len), {
  if (typeof postMessage === "function")
    postMessage({ type: "board", what: "oled", w: w, h: h, data: HEAPU8.slice(buf, buf + len) });
});
EM_JS(void, js_note_leds, (int pin, const uint8_t *rgb, int n), {
  if (typeof postMessage === "function")
    postMessage({ type: "board", what: "ws2812", pin: pin, data: HEAPU8.slice(rgb, rgb + n * 3) });
});
EM_JS(void, js_note_keys, (int mod, const char *names), {
  if (typeof postMessage === "function")
    postMessage({ type: "board", what: "hid_keys", mod: mod, names: UTF8ToString(names) });
});

void pp_wasm_note(const char *what, int a, int b, int c, int d, int e) { js_note(what, a, b, c, d, e); }
void pp_wasm_note_frame(int w, int h, const uint8_t *buf, int len) { js_note_frame(w, h, buf, len); }
void pp_wasm_note_leds(int pin, const uint8_t *rgb, int n) { js_note_leds(pin, rgb, n); }

void pp_wasm_note_keys(int mod, const uint8_t *keys, int n) {
    char names[160]; int len = 0;
    for (int i = 0; i < n && len < (int)sizeof names - 12; i++) {
        char hex[8];
        const char *nm = keys[i] ? pp_keycode_name(keys[i]) : NULL;
        if (!keys[i]) continue;
        if (!nm) { snprintf(hex, sizeof hex, "0x%02X", keys[i]); nm = hex; }
        len += snprintf(names + len, sizeof names - (size_t)len, "%s%s", len ? " " : "", nm);
    }
    names[len] = 0;
    js_note_keys(mod, names);
}

/* ---- input overrides, set from the page via the worker ------------------- */

static signed char gpio_ov[64];
static int adc_ov[64];
static int enc_pending;
static int ov_ready;
static void ov_init(void) {
    if (ov_ready) return;
    memset(gpio_ov, -1, sizeof gpio_ov);
    for (int i = 0; i < 64; i++) adc_ov[i] = -1;
    ov_ready = 1;
}

EMSCRIPTEN_KEEPALIVE void pp_wasm_gpio_set(int pin, int level) { ov_init(); gpio_ov[pin & 63] = (signed char)level; }
EMSCRIPTEN_KEEPALIVE void pp_wasm_encoder_add(int steps) { enc_pending += steps; }
EMSCRIPTEN_KEEPALIVE void pp_wasm_adc_set(int pin, int v16) { ov_init(); adc_ov[pin & 63] = v16; }

int pp_wasm_gpio_override(int pin) { ov_init(); return gpio_ov[pin & 63]; }
int pp_wasm_encoder_take(void) { int p = enc_pending; enc_pending = 0; return p; }
int pp_wasm_adc_override(int pin) { ov_init(); return adc_ov[pin & 63]; }
