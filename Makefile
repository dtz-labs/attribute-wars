TARGET ?=
JOBS ?= 3

ifneq ($(strip $(TARGET)),)
.DEFAULT_GOAL := target
else
.DEFAULT_GOAL := help
endif

ROOT := $(CURDIR)
BUILD := build

Z88DK_HOME ?= $(HOME)/Programowanie/z88dk
ifneq ($(wildcard $(Z88DK_HOME)/bin/zcc),)
export PATH := $(Z88DK_HOME)/bin:$(PATH)
export ZCCCFG := $(Z88DK_HOME)/lib/config
endif

ZCC ?= zcc
APPMAKE ?= z88dk-appmake
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

ZCC_BASE := $(ZCC) +zx -SO3 -clib=sdcc_iy -startup=31 -iquote$(ROOT)/include
APPMAKE_TAP = $(APPMAKE) +zx --binfile $(1)_CODE.bin --org $(ORG) \
	--output $(2) --screen $(LOADING_SCREEN) --clearaddr $(CLEARADDR) --usraddr $(USRADDR)

.PHONY: help all target timex zx128 zx48 clean test run run-timex run-tc2048 run-tc2068 run-ay run-zx128 run-zx48

help:
	@echo "Attribute Wars build targets"
	@echo
	@echo "Build:"
	@echo "  make all            build every platform TAP"
	@echo "  make timex          build Timex TC2048/TC2068 TAP -> build/game.tap"
	@echo "  make zx128          build ZX Spectrum 128K TAP -> build/game-zx128.tap"
	@echo "  make zx48           build ZX Spectrum 48K TAP -> build/game-zx48.tap"
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
	@echo "  make clean          remove build/"

all:
	@$(MAKE) --no-print-directory -j$(JOBS) timex zx128 zx48

target:
	@test -n "$(strip $(TARGET))" || { echo "usage: make TARGET=<timex|zx128|zx48|all|test|clean>" >&2; exit 2; }
	@$(MAKE) --no-print-directory $(TARGET)

timex: $(BUILD)/game.tap
zx128: $(BUILD)/game-zx128.tap
zx48: $(BUILD)/game-zx48.tap

$(BUILD):
	mkdir -p $@

$(BUILD)/game.tap: $(COMMON_C) $(COMMON_ASM) $(MUSIC_ASM) $(HEADERS) $(LOADING_SCREEN) | $(BUILD)
	mkdir -p $(BUILD)/obj-timex
	cd $(BUILD)/obj-timex && $(ZCC_BASE) \
		$(COMMON_C_ABS) $(COMMON_ASM_ABS) $(MUSIC_ASM_ABS) \
		-o $(ROOT)/$(BUILD)/game -create-app
	$(call APPMAKE_TAP,$(BUILD)/game,$@)
	ls -l $@

$(BUILD)/game-zx128.tap: $(COMMON_C) $(COMMON_ASM) src/zx128_page.asm $(HEADERS) $(LOADING_SCREEN) tools/check_zx128_layout.py | $(BUILD)
	mkdir -p $(BUILD)/obj-zx128
	cd $(BUILD)/obj-zx128 && $(ZCC_BASE) \
		-DZX128_PAGE_FLIP -DZX_SINCLAIR_DUAL_STICK -DZX128_NO_MUSIC \
		-Ca-DZX128_PAGE_FLIP \
		-pragma-define:REGISTER_SP=49152 \
		$(COMMON_C_ABS) $(COMMON_ASM_ABS) $(ROOT)/src/zx128_page.asm \
		-o $(ROOT)/$(BUILD)/game-zx128 -create-app -m
	tools/check_zx128_layout.py $(BUILD)/game-zx128.map
	$(call APPMAKE_TAP,$(BUILD)/game-zx128,$@)
	ls -l $@

$(BUILD)/game-zx48.tap: $(COMMON_C) $(COMMON_ASM) $(MUSIC_ASM) $(HEADERS) $(LOADING_SCREEN) | $(BUILD)
	mkdir -p $(BUILD)/obj-zx48
	cd $(BUILD)/obj-zx48 && $(ZCC_BASE) \
		-DZX48_SINGLE_BUFFER -DZX_SINCLAIR_DUAL_STICK \
		$(COMMON_C_ABS) $(COMMON_ASM_ABS) $(MUSIC_ASM_ABS) \
		-o $(ROOT)/$(BUILD)/game-zx48 -create-app
	$(call APPMAKE_TAP,$(BUILD)/game-zx48,$@)
	ls -l $@

test:
	./test/run.sh

run: run-tc2048
run-timex: run-tc2048

run-tc2048: timex
	@test -x "$(ZESARUX)" || { echo "ZEsarUX binary not found: $(ZESARUX)" >&2; exit 1; }
	cd "$(ZESARUX_DIR)" && ./zesarux --noconfigfile --machine TC2048 \
		--enabletimexvideo --joystickemulated Kempston --nosplash --verbose 0 \
		"$(ROOT)/$(BUILD)/game.tap"

run-tc2068 run-ay: timex
	@test -x "$(ZESARUX)" || { echo "ZEsarUX binary not found: $(ZESARUX)" >&2; exit 1; }
	cd "$(ZESARUX_DIR)" && ./zesarux --noconfigfile --machine TC2068 \
		--enabletimexvideo --joystickemulated Kempston --nosplash --verbose 0 \
		"$(ROOT)/$(BUILD)/game.tap"

run-zx128: zx128
	@test -x "$(ZESARUX)" || { echo "ZEsarUX binary not found: $(ZESARUX)" >&2; exit 1; }
	cd "$(ZESARUX_DIR)" && ./zesarux --noconfigfile --machine 128k \
		--nosplash --verbose 0 "$(ROOT)/$(BUILD)/game-zx128.tap"

run-zx48: zx48
	@test -x "$(ZESARUX)" || { echo "ZEsarUX binary not found: $(ZESARUX)" >&2; exit 1; }
	cd "$(ZESARUX_DIR)" && ./zesarux --noconfigfile --machine 48k \
		--nosplash --verbose 0 "$(ROOT)/$(BUILD)/game-zx48.tap"

clean:
	rm -rf $(BUILD)
