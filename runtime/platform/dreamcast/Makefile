# Dreamcast build (KallistiOS). Run inside the KOS environment:
#
#   source /opt/toolchains/dc/kos/environ.sh
#   make -C runtime/platform/dreamcast          builds build/dc/retro.elf
#   make -C runtime/platform/dreamcast cdi      builds build/dc/retro.cdi (needs mkdcdisc)
#   make -C runtime/platform/dreamcast cdi BOOT=island      the disc starts in that scene
#   make -C runtime/platform/dreamcast cdi PADDING=0        a small image for emulators, written in a second
#
# The scene packages go onto the disc as /cd/data/<scene>.rpk and are read from there, so
# only the scene that is shown takes up memory. Export them first on the PC side (make
# packages), or point PACKAGES at another directory.
#
# ROMDISK=1 links the packages into the ELF instead (/rd/<scene>.rpk). That costs their
# whole size in RAM and is only meant for test builds that are started as a bare ELF in
# an emulator, without a disc.

ROOT     := ../../..
BUILD    := $(ROOT)/build/dc
PACKAGES ?= $(ROOT)/build/pkg/dreamcast
TARGET   := $(BUILD)/retro.elf
MKDCDISC ?= mkdcdisc

# A disc image is padded to the size of a whole disc (740 MB), which a burned disc needs
# and which takes a minute to write onto a Windows drive from WSL. PADDING=0 leaves the
# padding out: 13 MB, for emulators.
PADDING ?= 1
ifeq ($(PADDING),0)
CDI_FLAGS := --disable-data-track-padding
else
CDI_FLAGS :=
endif

# The graphics backend: pvr (dc_pvr.c, straight on the PowerVR) or gldc (dc_gldc.c, the
# older one on the GLdc port, kept for comparison).
GFX ?= pvr
ifeq ($(GFX),gldc)
GFX_LIBS := -lGL
else ifeq ($(GFX),pvr)
GFX_LIBS :=
else
$(error GFX must be pvr or gldc, not '$(GFX)')
endif

SRC := $(patsubst $(ROOT)/runtime/%,%,$(wildcard $(ROOT)/runtime/core/*.c $(ROOT)/runtime/game/*.c)) \
       main.c platform/rr_file_stdio.c platform/dreamcast/dc_system.c platform/dreamcast/dc_$(GFX).c
OBJS := $(patsubst %.c,$(BUILD)/obj/%.o,$(SRC))
HEADERS := $(wildcard $(ROOT)/runtime/core/*.h $(ROOT)/runtime/game/*.h $(ROOT)/runtime/platform/dreamcast/*.h \
                    $(ROOT)/runtime/third_party/*/*.h)
# The packages, and the music files that are streamed next to them.
RPKS := $(wildcard $(PACKAGES)/*.rpk $(PACKAGES)/*.wav)

ifneq ($(ROMDISK),)
DATA_DIR := /rd
ROMDISK_OBJ := $(BUILD)/romdisk.o
else
DATA_DIR := /cd/data
ROMDISK_OBJ :=
endif

CFLAGS := $(KOS_CFLAGS) -std=gnu11 -Wall -Wextra -Wdouble-promotion \
          -DRR_DATA_DIR='"$(DATA_DIR)"'

# Test builds: INPUT_SCRIPT='60:ui_accept,90:accelerate:2000' compiles scripted input in
# (see rr_input_set_script). Use a separate BUILD directory for them.
ifneq ($(INPUT_SCRIPT),)
CFLAGS += -DRR_INPUT_SCRIPT='"$(INPUT_SCRIPT)"'
endif

# START_SCENE=features starts with another scene than main_menu, for test builds.
ifneq ($(START_SCENE),)
CFLAGS += -DRR_START_SCENE='"$(START_SCENE)"'
endif

# FPS=1 prints the rate the game runs at to the serial output every 120 frames. The
# counter of an emulator shows the emulator's rate, not the game's.
ifneq ($(FPS),)
CFLAGS += -DRR_PRINT_FPS
endif

.PHONY: all cdi clean

all: $(TARGET)

# The options of the last build of this BUILD directory. When they change (another
# INPUT_SCRIPT, START_SCENE, FPS), everything is compiled again; make would not notice.
FLAGS_FILE := $(BUILD)/cflags.txt
ifneq ($(file <$(FLAGS_FILE)),$(CFLAGS))
$(shell mkdir -p $(BUILD))
$(file >$(FLAGS_FILE),$(CFLAGS))
endif

$(BUILD)/obj/%.o: $(ROOT)/runtime/%.c $(HEADERS) $(FLAGS_FILE)
	@mkdir -p $(dir $@)
	kos-cc $(CFLAGS) -c $< -o $@

# Romdisk with the packages as /rd/<scene>.rpk (same steps as KOS' Makefile.rules, but
# with all intermediate files under build/dc).
$(BUILD)/romdisk.o: $(RPKS)
	@test -n "$(RPKS)" || { echo "no packages in $(PACKAGES); run 'make packages' on the PC side first"; exit 1; }
	rm -rf $(BUILD)/romdisk
	@mkdir -p $(BUILD)/romdisk
	cp $(RPKS) $(BUILD)/romdisk/
	$(KOS_GENROMFS) -f $(BUILD)/romdisk.img -d $(BUILD)/romdisk
	$(KOS_BASE)/utils/bin2c/bin2c $(BUILD)/romdisk.img $(BUILD)/romdisk_tmp.c romdisk
	kos-cc -o $(BUILD)/romdisk_tmp.o -c $(BUILD)/romdisk_tmp.c
	$(KOS_CC) -o $@ -r $(BUILD)/romdisk_tmp.o $(KOS_LIB_PATHS) -Wl,--whole-archive -lromdiskbase -nostdlib
	@# On a Windows drive under WSL the large .c file is often still held by a scanner; a
	@# leftover is harmless, so this must not fail the build.
	-rm -f $(BUILD)/romdisk_tmp.c $(BUILD)/romdisk_tmp.o

$(TARGET): $(OBJS) $(ROMDISK_OBJ)
	kos-cc -o $@ $(OBJS) $(ROMDISK_OBJ) $(GFX_LIBS) -lwav -lm

cdi: $(TARGET) $(RPKS)
	@test -z "$(ROMDISK)" || { echo "a disc image needs the packages on the disc: build without ROMDISK=1"; exit 1; }
	@test -n "$(RPKS)" || { echo "no packages in $(PACKAGES); run 'make packages' on the PC side first"; exit 1; }
	rm -rf $(BUILD)/data
	@mkdir -p $(BUILD)/data
	cp $(RPKS) $(BUILD)/data/
	@# BOOT=island starts the disc in that scene instead of the one compiled in.
	@test -z "$(BOOT)" || echo "$(BOOT)" > $(BUILD)/data/boot.txt
	$(MKDCDISC) -e $(TARGET) -d $(BUILD)/data -o $(BUILD)/retro.cdi -n "Retro Runtime" --allow-overwrite $(CDI_FLAGS)

clean:
	rm -rf $(BUILD)
