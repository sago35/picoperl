# Host build of the picoperl interpreter (for development and tests).
# The firmware is built with CMake in firmware/ (see README.md).
CC      ?= cc
CFLAGS  ?= -O2 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter
SRC     := src/pp_value.c src/pp_parse.c src/pp_eval.c src/pp_builtin.c src/pp_keycodes.c src/pp_gfx.c src/pp_fonts.c src/pp_tone.c src/pp_regex.c src/pp_match.c src/pp_storable.c
BIN     := build/picoperl

all: $(BIN)

$(BIN): $(SRC) host/main.c src/picoperl.h src/pp_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(SRC) host/main.c -lm

test: $(BIN)
	perl test/run.pl

# Run the keyboard script on the host with simulated GPIO (real-time sleeps).
#   make sim PRESSED=5:9,6:10   (driven pin:read pin of pressed switches; zero-kb02: col:row)
sim: $(BIN)
	PICOPERL_SIM_CYCLES=$(or $(CYCLES),30) PICOPERL_SIM_PRESSED=$(PRESSED) $(BIN) scripts/main.pl

clean:
	rm -rf build

.PHONY: all test sim clean

# Browser (WebAssembly) build. Needs emscripten (emcc) in PATH.
#   make wasm                          # -> wasm/picoperl.js + wasm/picoperl.wasm
#   python3 -m http.server -d wasm     # then open http://localhost:8000/
wasm:
	emcc -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter $(SRC) host/main.c wasm/shim.c -o wasm/picoperl.js \
	  -sMODULARIZE=1 -sEXPORT_NAME=createPicoperl -sENVIRONMENT=worker,node \
	  -sASYNCIFY -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sINVOKE_RUN=0 \
	  -sSTACK_SIZE=2097152 \
	  -sEXPORTED_RUNTIME_METHODS=callMain,FS,ENV \
	  --embed-file lib@/lib
	@ls -la wasm/picoperl.js wasm/picoperl.wasm

# run the normal test suite against the wasm build (needs node)
wasm-test:
	PICOPERL="node wasm/test-runner.mjs" perl test/run.pl

.PHONY: wasm wasm-test

# Build a firmware image for the zero-kb02 that recreates the drive from SCRIPT on every boot.
#   make uf2 SCRIPT=workshop/01_blinky1/main.pl NAME=01_blinky1
# WORKSHOP=1 also puts every workshop/*/main.pl under /workshop on the drive.
# Needs PICO_SDK_PATH and the ARM toolchain in PATH (see README).
uf2:
	@test -n "$(SCRIPT)" || (echo "usage: make uf2 SCRIPT=path/main.pl NAME=name [BOARD=waveshare_rp2040_zero] [WORKSHOP=1]"; exit 1)
	mkdir -p firmware/build-uf2-$(NAME) dist
	rm -f firmware/build-uf2-$(NAME)/picoperl.uf2
	cd firmware/build-uf2-$(NAME) && cmake .. -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=$(or $(BOARD),waveshare_rp2040_zero) -DPICOPERL_FS_RESET=ON -DPICOPERL_SCRIPT=$(CURDIR)/$(SCRIPT) -DPICOPERL_EMBED_WORKSHOP=$(if $(WORKSHOP),ON,OFF) > cmake.log && { $(MAKE) --no-print-directory > build.log 2>&1 || { grep -E "error|Error" build.log | head -20; exit 1; }; grep "warning: " build.log | grep -v "pico-sdk\|tinyusb\|fatfs" || true; }
	cp firmware/build-uf2-$(NAME)/picoperl.uf2 dist/picoperl-$(NAME).uf2
	@ls -la dist/picoperl-$(NAME).uf2

.PHONY: uf2
