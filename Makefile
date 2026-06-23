TARGET ?=
JOBS ?= 3

ifneq ($(strip $(TARGET)),)
.DEFAULT_GOAL := target
else
.DEFAULT_GOAL := help
endif

ROOT := $(CURDIR)
BUILD := build
VERSION ?= 1.1.1
VERSION_WORDS := $(subst ., ,$(VERSION))
VERSION_MAJOR := $(word 1,$(VERSION_WORDS))
VERSION_MINOR := $(word 2,$(VERSION_WORDS))
VERSION_PATCH := $(word 3,$(VERSION_WORDS))
VERSION_DEFINES := -DAPP_VERSION_MAJOR=$(VERSION_MAJOR) -DAPP_VERSION_MINOR=$(VERSION_MINOR)
ifneq ($(strip $(VERSION_PATCH)),)
VERSION_DEFINES += -DAPP_VERSION_PATCH=$(VERSION_PATCH)
endif

Z88DK_HOME ?= $(HOME)/Programowanie/z88dk
Z88DK_BIN ?= $(Z88DK_HOME)/bin
ifneq ($(wildcard $(Z88DK_BIN)),)
export PATH := $(Z88DK_BIN):$(PATH)
endif
ifneq ($(wildcard $(Z88DK_HOME)/lib/config),)
ZCCCFG ?= $(Z88DK_HOME)/lib/config
export ZCCCFG
endif

ZCC ?= $(if $(wildcard $(Z88DK_BIN)/zcc),$(Z88DK_BIN)/zcc,zcc)
APPMAKE ?= $(if $(wildcard $(Z88DK_BIN)/z88dk-appmake),$(Z88DK_BIN)/z88dk-appmake,z88dk-appmake)
ZESARUX ?= /Applications/ZEsarUX.app/Contents/MacOS/zesarux
ZESARUX_DIR := $(dir $(ZESARUX))

ORG := 32768
CLEARADDR := 32767
USRADDR := 32768
LOADING_SCREEN := assets/loading.scr

COMMON_C := \
	src/main.c src/scld.c src/sprite.c src/sprites.c src/player.c \
	src/bullet.c src/enemy.c src/collision.c src/geometry.c src/input.c \
	src/rng.c src/score.c src/sfx.c src/hud.c src/music.c

COMMON_ASM := src/blit.asm src/enemy_update.asm src/collide.asm src/sfx.asm
MUSIC_ASM := src/music_ay.asm src/pt3prom.asm src/tune.asm
HEADERS := $(wildcard include/*.h)
COMMON_C_ABS := $(addprefix $(ROOT)/,$(COMMON_C))
COMMON_ASM_ABS := $(addprefix $(ROOT)/,$(COMMON_ASM))
MUSIC_ASM_ABS := $(addprefix $(ROOT)/,$(MUSIC_ASM))

ZCC_BASE := $(ZCC) +zx -SO3 -clib=sdcc_iy -startup=31 -iquote$(ROOT)/include \
	$(VERSION_DEFINES)
APPMAKE_TAP = $(APPMAKE) +zx --binfile $(1)_CODE.bin --org $(ORG) \
	--output $(2) --screen $(LOADING_SCREEN) --clearaddr $(CLEARADDR) --usraddr $(USRADDR)
CHECK_ZX128_LAYOUT ?= tools/check_zx128_layout.py
CHECK_ZX_STACK_LAYOUT ?= tools/check_zx_stack_layout.py
STACK_TOP := 65535
ZX128_STACK_TOP := 49152
TAP_PREFIX := aw-$(VERSION)
CODE_PREFIX := aw-$(subst .,-,$(VERSION))
TIMEX_CODE_BASE := $(BUILD)/$(CODE_PREFIX)-timex
ZX128_CODE_BASE := $(BUILD)/$(CODE_PREFIX)-zx128k
ZX48_CODE_BASE := $(BUILD)/$(CODE_PREFIX)-zx48k
TIMEX_TAP := $(BUILD)/$(TAP_PREFIX)-timex.tap
ZX128_TAP := $(BUILD)/$(TAP_PREFIX)-zx128k.tap
ZX48_TAP := $(BUILD)/$(TAP_PREFIX)-zx48k.tap

.PHONY: help all target timex zx128 zx48 clean test measure run run-timex run-tc2048 run-tc2068 run-ay run-zx128 run-zx48

help:
	@echo "Attribute Wars build targets"
	@echo
	@echo "Build:"
	@echo "  make all            build every platform TAP"
	@echo "  make timex          build Timex TC2048/TC2068 TAP -> $(TIMEX_TAP)"
	@echo "  make zx128          build ZX Spectrum 128K TAP -> $(ZX128_TAP)"
	@echo "  make zx48           build ZX Spectrum 48K TAP -> $(ZX48_TAP)"
	@echo "  make TARGET=zx128   build one target through TARGET=..."
	@echo
	@echo "Run in ZEsarUX:"
	@echo "  make run-tc2048     run Timex TC2048 build"
	@echo "  make run-tc2068     run Timex TC2068 build"
	@echo "  make run-zx128      run ZX Spectrum 128K build"
	@echo "  make run-zx48       run ZX Spectrum 48K build"
	@echo
	@echo "Other:"
	@echo "  make test           run host unit tests"
	@echo "  make measure        build T-state harness and print marker addresses"
	@echo "  make clean          remove build/"

all:
	@$(MAKE) --no-print-directory -j$(JOBS) timex zx128 zx48

target:
	@test -n "$(strip $(TARGET))" || { echo "usage: make TARGET=<timex|zx128|zx48|all|test|clean>" >&2; exit 2; }
	@$(MAKE) --no-print-directory $(TARGET)

timex: $(TIMEX_TAP)
zx128: $(ZX128_TAP)
zx48: $(ZX48_TAP)

$(BUILD):
	mkdir -p $@

$(TIMEX_TAP): $(COMMON_C) $(COMMON_ASM) $(MUSIC_ASM) $(HEADERS) $(LOADING_SCREEN) tools/check_zx_stack_layout.py | $(BUILD)
	mkdir -p $(BUILD)/obj-timex
	cd $(BUILD)/obj-timex && $(ZCC_BASE) \
		-DAW_TIMEX_SPRITE_SCRATCH \
		-pragma-define:REGISTER_SP=$(STACK_TOP) \
		$(COMMON_C_ABS) $(COMMON_ASM_ABS) $(MUSIC_ASM_ABS) \
		-o $(ROOT)/$(TIMEX_CODE_BASE) -create-app -m
	$(CHECK_ZX_STACK_LAYOUT) $(TIMEX_CODE_BASE).map
	$(call APPMAKE_TAP,$(TIMEX_CODE_BASE),$@)
	rm -f $(TIMEX_CODE_BASE).tap
	ls -l $@

$(ZX128_TAP): $(COMMON_C) $(COMMON_ASM) src/zx128_page.asm $(HEADERS) $(LOADING_SCREEN) tools/check_zx128_layout.py | $(BUILD)
	mkdir -p $(BUILD)/obj-zx128
	cd $(BUILD)/obj-zx128 && $(ZCC_BASE) \
		-DZX128_PAGE_FLIP -DZX_SINCLAIR_DUAL_STICK -DZX128_NO_MUSIC \
		-Ca-DZX128_PAGE_FLIP \
		-pragma-define:REGISTER_SP=$(ZX128_STACK_TOP) \
		$(COMMON_C_ABS) $(COMMON_ASM_ABS) $(ROOT)/src/zx128_page.asm \
		-o $(ROOT)/$(ZX128_CODE_BASE) -create-app -m
	$(CHECK_ZX128_LAYOUT) $(ZX128_CODE_BASE).map
	$(call APPMAKE_TAP,$(ZX128_CODE_BASE),$@)
	rm -f $(ZX128_CODE_BASE).tap
	ls -l $@

$(ZX48_TAP): $(COMMON_C) $(COMMON_ASM) $(MUSIC_ASM) $(HEADERS) $(LOADING_SCREEN) tools/check_zx_stack_layout.py | $(BUILD)
	mkdir -p $(BUILD)/obj-zx48
	cd $(BUILD)/obj-zx48 && $(ZCC_BASE) \
		-DZX48_SINGLE_BUFFER -DZX_SINCLAIR_DUAL_STICK \
		-pragma-define:REGISTER_SP=$(STACK_TOP) \
		$(COMMON_C_ABS) $(COMMON_ASM_ABS) $(MUSIC_ASM_ABS) \
		-o $(ROOT)/$(ZX48_CODE_BASE) -create-app -m
	$(CHECK_ZX_STACK_LAYOUT) $(ZX48_CODE_BASE).map
	$(call APPMAKE_TAP,$(ZX48_CODE_BASE),$@)
	rm -f $(ZX48_CODE_BASE).tap
	ls -l $@

test:
	./test/run.sh

measure: $(BUILD)/measure_CODE.bin
	@echo "marker addresses (.map):"
	@awk '/^_mark[0-9A-C][[:space:]]/ {printf "  %-8s %s\n", $$1, $$3}' $(BUILD)/measure.map

$(BUILD)/measure_CODE.bin: src/measure_main.c src/scld.c src/sprite.c src/sprites.c src/enemy.c src/bullet.c src/collision.c src/rng.c src/music.c src/blit.asm src/enemy_update.asm src/collide.asm $(MUSIC_ASM) $(HEADERS) | $(BUILD)
	mkdir -p $(BUILD)/obj-measure
	cd $(BUILD)/obj-measure && $(ZCC_BASE) -m \
		$(ROOT)/src/measure_main.c $(ROOT)/src/scld.c \
		$(ROOT)/src/sprite.c $(ROOT)/src/sprites.c \
		$(ROOT)/src/enemy.c $(ROOT)/src/bullet.c \
		$(ROOT)/src/collision.c $(ROOT)/src/rng.c $(ROOT)/src/music.c \
		$(ROOT)/src/blit.asm $(ROOT)/src/enemy_update.asm \
		$(ROOT)/src/collide.asm $(MUSIC_ASM_ABS) \
		-o $(ROOT)/$(BUILD)/measure -create-app >/dev/null

run: run-tc2048
run-timex: run-tc2048

run-tc2048: timex
	@test -x "$(ZESARUX)" || { echo "ZEsarUX binary not found: $(ZESARUX)" >&2; exit 1; }
	cd "$(ZESARUX_DIR)" && ./zesarux --noconfigfile --machine TC2048 \
		--enabletimexvideo --joystickemulated Kempston --nosplash --verbose 0 \
		"$(ROOT)/$(TIMEX_TAP)"

run-tc2068 run-ay: timex
	@test -x "$(ZESARUX)" || { echo "ZEsarUX binary not found: $(ZESARUX)" >&2; exit 1; }
	cd "$(ZESARUX_DIR)" && ./zesarux --noconfigfile --machine TC2068 \
		--enabletimexvideo --joystickemulated Kempston --nosplash --verbose 0 \
		"$(ROOT)/$(TIMEX_TAP)"

run-zx128: zx128
	@test -x "$(ZESARUX)" || { echo "ZEsarUX binary not found: $(ZESARUX)" >&2; exit 1; }
	cd "$(ZESARUX_DIR)" && ./zesarux --noconfigfile --machine 128k \
		--nosplash --verbose 0 "$(ROOT)/$(ZX128_TAP)"

run-zx48: zx48
	@test -x "$(ZESARUX)" || { echo "ZEsarUX binary not found: $(ZESARUX)" >&2; exit 1; }
	cd "$(ZESARUX_DIR)" && ./zesarux --noconfigfile --machine 48k \
		--nosplash --verbose 0 "$(ROOT)/$(ZX48_TAP)"

clean:
	rm -rf $(BUILD)
