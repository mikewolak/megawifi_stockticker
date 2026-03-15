################################################################################
# stock_ticker — Sega Genesis stock ticker using SGDK + mw-api
#
# Builds with native m68k-elf-gcc against SGDK's libmd.a.
# The mw-api ext/mw sources are compiled directly (not from libmd.a LTO objs).
#
# Prerequisites:
#   ~/sgdk         — SGDK install (libmd.a, inc/, src/)
#   m68k-elf-gcc   — m68k toolchain in PATH (rosco toolchain)
#
# Targets:
#   all    — build out/rom.bin (default)
#   run    — build and launch in MAME
#   clean  — remove build artefacts
################################################################################

GDK    = $(HOME)/sgdk
PREFIX ?= m68k-elf-
CC     = $(PREFIX)gcc
AS     = $(PREFIX)as
LD     = $(PREFIX)ld
OBJCPY = $(PREFIX)objcopy

OUT    = out
SRC    = src

# Finnhub API token — read from TOKEN.md (git-ignored) or passed on the command line.
# To set up: cp TOKEN.md.example TOKEN.md  then fill in your key.
# Override at build time: make FINNHUB_TOKEN=yourkey
TOKEN_FILE := $(CURDIR)/TOKEN.md
ifeq ($(strip $(FINNHUB_TOKEN)),)
FINNHUB_TOKEN := $(shell sed -n 's/^Token:[[:space:]]*//p' $(TOKEN_FILE) 2>/dev/null | head -n1)
endif
ifeq ($(strip $(FINNHUB_TOKEN)),)
$(warning )
$(warning *** No Finnhub token found. Create TOKEN.md from TOKEN.md.example)
$(warning *** or run: make FINNHUB_TOKEN=<your_key>)
$(warning )
endif

MW_SRC   = $(GDK)/src/ext/mw
RESCOMP  = java -jar $(GDK)/bin/rescomp.jar
RES_DIR  = res
RES_SRC  = $(RES_DIR)/nasdaq_bg.res
RES_PNG  = $(RES_DIR)/nasdaq_bg.png
RES_S    = $(OUT)/nasdaq_bg.s
RES_H    = $(OUT)/nasdaq_bg.h
RES_OBJ  = $(OUT)/nasdaq_bg.o

INCS   = -I$(SRC) -I$(OUT) -I$(GDK)/inc -I$(GDK)/res

CFLAGS  = -DSGDK_GCC -DMODULE_MEGAWIFI=1 -m68000 -O2 -fomit-frame-pointer
CFLAGS += -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter
CFLAGS += -fno-builtin -ffunction-sections -fdata-sections -fms-extensions
CFLAGS += $(INCS)

# Optional: inject Finnhub API token at build time:
#   make FINNHUB_TOKEN=xyz
ifdef FINNHUB_TOKEN
CFLAGS += -DFINNHUB_TOKEN=\"$(FINNHUB_TOKEN)\"
endif

AFLAGS  = $(CFLAGS) -x assembler-with-cpp -Wa,--register-prefix-optional,--bitwise-or

LDFLAGS = -m68000 -n -T $(GDK)/md.ld -nostdlib -fno-lto
LDFLAGS += -Wl,--gc-sections
LIBGCC  = $(shell $(CC) -m68000 -print-libgcc-file-name)
LIBMD   = $(GDK)/lib/libmd.a

ROM     = $(OUT)/stock_ticker.bin
ELF     = $(OUT)/stock_ticker.out

BUILD_NUM_FILE = $(OUT)/build_num
BUILD_NUM      = $(shell cat $(BUILD_NUM_FILE) 2>/dev/null || echo 0)

# Boot objects (SGDK provides rom_head.c and sega.s)
BOOT_OBJ_HEAD = $(OUT)/rom_head.o
BOOT_BIN_HEAD = $(OUT)/rom_head.bin
BOOT_OBJ_SEGA = $(OUT)/sega.o

# mw-api objects (compiled from SGDK ext/mw — bypasses LTO objs in libmd.a)
MW_OBJS  = $(OUT)/megawifi.o
MW_OBJS += $(OUT)/lsd.o
MW_OBJS += $(OUT)/16c550.o
MW_OBJS += $(OUT)/json.o

# Application object
APP_OBJ = $(OUT)/stock_ticker.o

ALL_OBJS = $(BOOT_OBJ_SEGA) $(RES_OBJ) $(APP_OBJ) $(MW_OBJS)

################################################################################

.PHONY: all run clean

all: $(OUT) $(ROM)

run: all
	mame genesis -cart $(ROM) -skip_gameinfo -window -nomaximize -resolution 1024x768 -video soft

clean:
	rm -f $(OUT)/*.o $(OUT)/*.bin $(OUT)/*.out $(OUT)/*.s $(OUT)/*.h

$(OUT):
	mkdir -p $(OUT)

################################################################################
# Boot
################################################################################

$(BOOT_OBJ_HEAD): $(GDK)/src/boot/rom_head.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BOOT_BIN_HEAD): $(BOOT_OBJ_HEAD)
	$(OBJCPY) -O binary $< $@

$(BOOT_OBJ_SEGA): $(GDK)/src/boot/sega.s $(BOOT_BIN_HEAD)
	$(CC) $(AFLAGS) -c $< -o $@

################################################################################
# Resources (rescomp — NASDAQ logo background tiles)
################################################################################

$(RES_PNG): money.png tools/prep_nasdaq.py
	python3 tools/prep_nasdaq.py $< $@

$(RES_S) $(RES_H): $(RES_SRC) $(RES_PNG)
	$(RESCOMP) $(RES_SRC) $(RES_S)

$(RES_OBJ): $(RES_S)
	$(CC) $(AFLAGS) -c $< -o $@

################################################################################
# mw-api sources
################################################################################

$(OUT)/megawifi.o: $(SRC)/megawifi.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/lsd.o: $(MW_SRC)/lsd.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/16c550.o: $(MW_SRC)/16c550.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/json.o: $(MW_SRC)/json.c
	$(CC) $(CFLAGS) -Wno-unused-function -c $< -o $@

################################################################################
# Application
################################################################################

$(OUT)/stock_ticker.o: $(SRC)/stock_ticker.c $(OUT)
	@next=$$(($(BUILD_NUM) + 1)); echo $$next > $(BUILD_NUM_FILE); echo "Build: $$next"
	$(CC) $(CFLAGS) -DBUILD_NUM=$$(cat $(BUILD_NUM_FILE)) -c $< -o $@

################################################################################
# Link
################################################################################

$(ELF): $(ALL_OBJS) $(LIBMD)
	$(CC) $(LDFLAGS) $(ALL_OBJS) $(LIBMD) $(LIBGCC) -o $@

$(ROM): $(ELF)
	$(OBJCPY) -O binary $(ELF) $(ROM)
	@echo "ROM: $(ROM)"
