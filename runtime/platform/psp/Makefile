# PSP build (PSP SDK of pspdev). With the SDK's tools on the PATH (PSPDEV set):
#
#   make -C runtime/platform/psp               builds build/psp/retro/EBOOT.PBP
#   make -C runtime/platform/psp game          also copies the packages: build/psp/retro/ is the game
#   make -C runtime/platform/psp game BOOT=island     the game starts in that scene
#
# build/psp/retro/ is a folder for ms0:/PSP/GAME/ on the memory stick, and what an emulator
# is pointed at (its EBOOT.PBP). The scene packages are in its data/ folder and are read
# from there, so only the scene that is shown takes up memory. Export them first on the PC
# side (make packages), or point PACKAGES at another directory.

ROOT     := ../../..
BUILD    := $(ROOT)/build/psp
PACKAGES ?= $(ROOT)/build/pkg/psp
GAME     := $(BUILD)/retro
TARGET   := $(BUILD)/retro.elf
BOOT     ?= main_menu

PSPSDK := $(shell psp-config --pspsdk-path)
PSPDEV := $(shell psp-config --pspdev-path)
ifeq ($(PSPSDK),)
$(error psp-config is not on the PATH: set PSPDEV and add $$PSPDEV/bin to the PATH)
endif

SRC := $(patsubst $(ROOT)/runtime/%,%,$(wildcard $(ROOT)/runtime/core/*.c $(ROOT)/runtime/game/*.c)) \
       main.c platform/psp/psp_system.c platform/psp/psp_gu.c
OBJS := $(patsubst %.c,$(BUILD)/obj/%.o,$(SRC))
HEADERS := $(wildcard $(ROOT)/runtime/core/*.h $(ROOT)/runtime/game/*.h $(ROOT)/runtime/platform/psp/*.h)
# The packages, and the music files that are streamed next to them.
RPKS := $(wildcard $(PACKAGES)/*.rpk $(PACKAGES)/*.wav)

# The game counts in a screen of 848 x 480 (see psp_baker.gd); the packages are in data/
# next to the program, which is where it is started.
CFLAGS := -O2 -G0 -std=gnu11 -Wall -Wextra -Wdouble-promotion \
          -I$(PSPDEV)/psp/include -I$(PSPSDK)/include -D_PSP_FW_VERSION=600 \
          -DRR_SCREEN_WIDTH=848 -DRR_SCREEN_HEIGHT=480 -DRR_DATA_DIR='"data"'

# The program is built as a relocatable module (PRX), which is what custom firmware
# starts from the memory stick.
LDFLAGS := -L$(PSPDEV)/psp/lib -L$(PSPSDK)/lib -specs=$(PSPSDK)/lib/prxspecs \
           -Wl,-q,-T$(PSPSDK)/lib/linkfile.prx -Wl,-zmax-page-size=128
LIBS    := -lpspgu -lpspvram -lpspaudio -lpsputility -lm \
           -lpspdebug -lpspdisplay -lpspge -lpspctrl -lpspnet -lpspnet_apctl

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

# LOG=1 writes what the program prints into retro.log next to it (errors into
# retro_errors.log), for test builds: an emulator has no console to read it from.
ifneq ($(LOG),)
CFLAGS += -DRR_LOG_FILE='"retro"'
endif

.PHONY: all game clean

all: $(GAME)/EBOOT.PBP

# The options of the last build of this BUILD directory. When they change (another
# INPUT_SCRIPT, FPS, LOG), everything is compiled again; make would not notice.
FLAGS_FILE := $(BUILD)/cflags.txt
ifneq ($(file <$(FLAGS_FILE)),$(CFLAGS))
$(shell mkdir -p $(BUILD))
$(file >$(FLAGS_FILE),$(CFLAGS))
endif

$(BUILD)/obj/%.o: $(ROOT)/runtime/%.c $(HEADERS) $(FLAGS_FILE)
	@mkdir -p $(dir $@)
	psp-gcc $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	psp-gcc $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(PSPSDK)/lib/prxexports.o $(LIBS)
	psp-fixup-imports $@

$(BUILD)/retro.prx: $(TARGET)
	psp-prxgen $< $@

# MEMSIZE=1 asks for the larger memory of the later models where there is one.
$(BUILD)/PARAM.SFO:
	mksfoex -d MEMSIZE=1 'Retro Runtime' $@

$(GAME)/EBOOT.PBP: $(BUILD)/retro.prx $(BUILD)/PARAM.SFO
	@mkdir -p $(GAME)
	pack-pbp $@ $(BUILD)/PARAM.SFO NULL NULL NULL NULL NULL $(BUILD)/retro.prx NULL

game: $(GAME)/EBOOT.PBP $(RPKS)
	@test -n "$(RPKS)" || { echo "no packages in $(PACKAGES); run 'make packages' on the PC side first"; exit 1; }
	rm -rf $(GAME)/data
	@mkdir -p $(GAME)/data
	cp $(RPKS) $(GAME)/data/
	@# The scene the game starts in.
	echo "$(BOOT)" > $(GAME)/data/boot.txt

clean:
	rm -rf $(BUILD)
