# Wii and GameCube build (devkitPPC and libogc of devkitPro). With DEVKITPRO and DEVKITPPC
# set, as devkitPro's installer and its Docker image (devkitpro/devkitppc) do:
#
#   make -C runtime/platform/wii                builds build/wii/retro.dol
#   make -C runtime/platform/wii game           build/wii/sd/ is what goes onto the SD card
#   make -C runtime/platform/wii game BOOT=island      the game starts in that scene
#   make -C runtime/platform/wii CONSOLE=gamecube game  the same for the GameCube, in build/gamecube/
#
# build/wii/sd/ holds apps/retro/ with boot.dol (which the Homebrew Channel lists) and the
# scene packages in apps/retro/data/. They are read from there, so only the scene that is
# shown takes up memory. Export them first on the PC side (make packages), or point
# PACKAGES at another directory.

CONSOLE  ?= wii
ROOT     := ../../..
BUILD    := $(ROOT)/build/$(CONSOLE)
PACKAGES ?= $(ROOT)/build/pkg/wii
APP      := $(BUILD)/sd/apps/retro
TARGET   := $(BUILD)/retro.elf
BOOT     ?= main_menu

ifeq ($(strip $(DEVKITPPC)),)
$(error DEVKITPPC is not set: install devkitPro's devkitPPC and libogc, or run make inside its Docker image)
endif

# The two consoles differ in the machine option, the libraries of libogc and the Wii
# Remote, which only the Wii has.
ifeq ($(CONSOLE),wii)
MACHDEP := -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float
LIBS    := -lasnd -lfat -lwiiuse -lbte -logc -lm
LIBDIR  := $(DEVKITPRO)/libogc/lib/wii
# The SD card in the front of the console.
DATA_DIR := sd:/apps/retro/data
else ifeq ($(CONSOLE),gamecube)
MACHDEP := -DGEKKO -mogc -mcpu=750 -meabi -mhard-float
LIBS    := -lasnd -lfat -logc -lm
LIBDIR  := $(DEVKITPRO)/libogc/lib/cube
# The card of whichever SD adapter was found first.
DATA_DIR := /apps/retro/data
else
$(error CONSOLE must be wii or gamecube, not '$(CONSOLE)')
endif

CC      := $(DEVKITPPC)/bin/powerpc-eabi-gcc
ELF2DOL := $(DEVKITPRO)/tools/bin/elf2dol

SRC := $(patsubst $(ROOT)/runtime/%,%,$(wildcard $(ROOT)/runtime/core/*.c $(ROOT)/runtime/game/*.c)) \
       main.c platform/wii/wii_system.c platform/wii/wii_gx.c
OBJS := $(patsubst %.c,$(BUILD)/obj/%.o,$(SRC))
HEADERS := $(wildcard $(ROOT)/runtime/core/*.h $(ROOT)/runtime/game/*.h $(ROOT)/runtime/platform/wii/*.h)
# The packages, and the music files that are streamed next to them.
RPKS := $(wildcard $(PACKAGES)/*.rpk $(PACKAGES)/*.wav)

# The packages are on the SD card, below the folder of the program.
CFLAGS := -O2 -std=gnu11 -Wall -Wextra -Wdouble-promotion $(MACHDEP) \
          -isystem $(DEVKITPRO)/libogc/include -DRR_DATA_DIR='"$(DATA_DIR)"'

# Test builds: INPUT_SCRIPT='60:ui_accept,90:accelerate:2000' compiles scripted input in
# (see rr_input_set_script). Use a separate BUILD directory for them.
ifneq ($(INPUT_SCRIPT),)
CFLAGS += -DRR_INPUT_SCRIPT='"$(INPUT_SCRIPT)"'
endif

# START_SCENE=features starts with another scene than main_menu when data/boot.txt is
# missing; `make game BOOT=features` is the usual way.
ifneq ($(START_SCENE),)
CFLAGS += -DRR_START_SCENE='"$(START_SCENE)"'
endif

# FPS=1 prints the rate the game runs at every 120 frames. The counter of an emulator
# shows the emulator's rate, not the game's.
ifneq ($(FPS),)
CFLAGS += -DRR_PRINT_FPS
endif

# LOG=1 sends what the program prints to the console's debug output, which an emulator
# writes into its log; without it the output goes nowhere.
ifneq ($(LOG),)
CFLAGS += -DRR_LOG_REPORT
endif

.PHONY: all game clean

all: $(BUILD)/retro.dol

# The options of the last build of this BUILD directory. When they change (another
# INPUT_SCRIPT, FPS, LOG), everything is compiled again; make would not notice.
FLAGS_FILE := $(BUILD)/cflags.txt
ifneq ($(file <$(FLAGS_FILE)),$(CFLAGS))
$(shell mkdir -p $(BUILD))
$(file >$(FLAGS_FILE),$(CFLAGS))
endif

$(BUILD)/obj/%.o: $(ROOT)/runtime/%.c $(HEADERS) $(FLAGS_FILE)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(MACHDEP) -o $@ $(OBJS) -L$(LIBDIR) $(LIBS)

$(BUILD)/retro.dol: $(TARGET)
	$(ELF2DOL) $< $@

game: $(BUILD)/retro.dol $(RPKS) meta.xml
	@test -n "$(RPKS)" || { echo "no packages in $(PACKAGES); run 'make packages' on the PC side first"; exit 1; }
	rm -rf $(APP)/data
	@mkdir -p $(APP)/data
	cp $(BUILD)/retro.dol $(APP)/boot.dol
	cp meta.xml $(APP)/meta.xml
	cp $(RPKS) $(APP)/data/
	@# The scene the game starts in.
	echo "$(BOOT)" > $(APP)/data/boot.txt

clean:
	rm -rf $(BUILD)
