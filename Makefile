# PC build, packages and tests, on Windows or Linux.
#
#   Windows: from an MSYS2 MINGW64 shell (pacman -S mingw-w64-x86_64-gcc make diffutils)
#   Linux:   gcc, make, curl and the X11/OpenGL headers
#            (Debian/Ubuntu: apt install build-essential curl libgl-dev libx11-dev)
#
#   make raylib     download raylib into third_party/ (once)
#   make            build the runtime and the tools
#   make packages   import the assets and export the game scenes with Godot (headless)
#   make run        start the game in the PC runtime
#   make dist       zip the PC game (runtime, packages) into build/dist/
#   make test       run the exporter, loader and game flow tests
#
# The console builds live in the Makefiles of runtime/platform/dreamcast, psp and wii.

ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wdouble-promotion

# raylib is linked statically, so the runtime is one executable without DLLs or shared
# raylib. That needs raylib's own release, which has GLFW built in (the MSYS2 package is
# built against glfw3.dll). `make raylib` fetches it; RAYLIB=<folder> takes one from
# elsewhere. On Linux only the system's libGL and libX11 stay dynamic.
ifeq ($(OS),Windows_NT)
GODOT       ?= ./godot/Godot_v4.7.2-stable_win64_console.exe
EXE         := .exe
RAYLIB_NAME := raylib-6.0_win64_mingw-w64
RAYLIB_ARC  := zip
RAYLIB_SHA  := e0e22b3d3be5ca9d302fa4af524a69cd038fd7ddbf37c41b2f9af0ec8fe0c2e6
RAYLIB_SYS  := -static -lopengl32 -lgdi32 -lwinmm
UNPACK      := bsdtar -xf
else
GODOT       ?= ./godot/Godot_v4.7.2-stable_linux.x86_64
EXE         :=
RAYLIB_NAME := raylib-6.0_linux_amd64
RAYLIB_ARC  := tar.gz
RAYLIB_SHA  := 1199fb5be52a663c099ed42eae5eabba83754ca9a8d7f82e1514ae4d6623874b
RAYLIB_SYS  := -lGL -lX11 -lm -lpthread -ldl -lrt
UNPACK      := tar -xzf
endif
RAYLIB_URL  := https://github.com/raysan5/raylib/releases/download/6.0/$(RAYLIB_NAME).$(RAYLIB_ARC)
RAYLIB      ?= third_party/$(RAYLIB_NAME)
RAYLIB_LIB  := $(RAYLIB)/lib/libraylib.a
RAYLIB_LIBS ?= $(RAYLIB_LIB) $(RAYLIB_SYS)

BUILD := build

# The scenes of the game. The first scene the runtime loads is main_menu.
SCENES := scenes/main_menu.tscn scenes/test_track.tscn scenes/sprites_2d.tscn \
          scenes/platformer_2d.tscn scenes/platformer_3d.tscn scenes/racing.tscn \
          scenes/island.tscn scenes/ball_roll.tscn scenes/stunt_track.tscn scenes/features.tscn \
          scenes/airplane.tscn scenes/highway_title.tscn scenes/highway.tscn \
          scenes/neonleague_title.tscn scenes/neonleague.tscn scenes/starter_racing.tscn \
          scenes/spelunky_title.tscn scenes/spelunky_level.tscn
# Scenes that are instanced by the ones above.
PARTS  := $(wildcard scenes/platformer/*.tscn scenes/parts/*.tscn scenes/racing/*.tscn \
                    scenes/airplane/*.tscn scenes/highway/*.tscn scenes/neonleague/*.tscn \
                    scenes/starter_racing/*.tscn scenes/spelunky/*.tscn)

CORE_SRC := $(wildcard runtime/core/*.c)
GAME_SRC := $(wildcard runtime/game/*.c)
PC_SRC   := runtime/main.c runtime/platform/rr_file_stdio.c runtime/platform/pc/pc_raylib.c
HEADERS  := $(wildcard runtime/core/*.h runtime/game/*.h runtime/third_party/*/*.h)

RUNTIME := $(BUILD)/pc/retro$(EXE)
RPKDUMP := $(BUILD)/tools/rpkdump$(EXE)

EXPORT  = $(GODOT) --headless --path . --script res://addons/retro_runtime/export_cli.gd --
ADDON  := $(wildcard addons/retro_runtime/*.gd addons/retro_runtime/bakers/*.gd)
ASSETS := $(wildcard assets/*/*.png assets/*/*.wav assets/*/*.import \
                    assets/kenney/*/*.obj assets/kenney/*/*.mtl assets/kenney/*/*/*.png)
EXPORT_DEPS := $(ADDON) $(wildcard components/*.gd components/tests/*.gd) project.godot $(BUILD)/.imported

.PHONY: all raylib packages dist test run clean

all: $(RUNTIME) $(RPKDUMP)

# Box2D, the engine behind RigidBody2D, is built once into a library of its own: it is C17,
# has its own warnings, and does not change. Its SIMD paths are off, as on the consoles,
# so that the PC shows what the consoles do.
BOX2D      := runtime/third_party/box2d
BOX2D_SRC  := $(wildcard $(BOX2D)/src/*.c)
BOX2D_OBJ  := $(patsubst $(BOX2D)/src/%.c,$(BUILD)/pc/box2d/%.o,$(BOX2D_SRC))
BOX2D_LIB  := $(BUILD)/pc/libbox2d.a
BOX2D_DEPS := $(wildcard $(BOX2D)/src/*.h $(BOX2D)/src/*.inl $(BOX2D)/include/box2d/*.h)

$(BUILD)/pc/box2d/%.o: $(BOX2D)/src/%.c $(BOX2D_DEPS)
	@mkdir -p $(dir $@)
	$(CC) -std=c17 -O2 -w -DNDEBUG -DBOX2D_DISABLE_SIMD -I$(BOX2D)/include -I$(BOX2D)/src -c -o $@ $<

$(BOX2D_LIB): $(BOX2D_OBJ)
	@rm -f $@
	ar rcs $@ $(BOX2D_OBJ)

$(RUNTIME): $(CORE_SRC) $(GAME_SRC) $(PC_SRC) $(HEADERS) $(RAYLIB_LIB) $(BOX2D_LIB) $(BOX2D_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(RAYLIB)/include -o $@ $(CORE_SRC) $(GAME_SRC) $(PC_SRC) $(BOX2D_LIB) $(RAYLIB_LIBS)

$(RAYLIB_LIB):
	@echo "raylib is missing: $@ does not exist. Run 'make raylib' to download it," \
	      "or pass RAYLIB=<folder of a raylib 6.0 release for this system>."; exit 1

# The checksum is of lib/libraylib.a, so that a changed download is not linked unnoticed.
raylib:
	@mkdir -p third_party
	curl -L --fail -o third_party/$(RAYLIB_NAME).$(RAYLIB_ARC) $(RAYLIB_URL)
	$(UNPACK) third_party/$(RAYLIB_NAME).$(RAYLIB_ARC) -C third_party
	@rm -f third_party/$(RAYLIB_NAME).$(RAYLIB_ARC); touch $(RAYLIB_LIB)
	echo "$(RAYLIB_SHA) *third_party/$(RAYLIB_NAME)/lib/libraylib.a" | sha256sum -c -

$(RPKDUMP): tools/rpkdump.c runtime/core/rr_package.c runtime/core/rr_dump.c \
            runtime/platform/rr_file_stdio.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tools/rpkdump.c runtime/core/rr_package.c runtime/core/rr_dump.c \
	    runtime/platform/rr_file_stdio.c

# --- packages ---------------------------------------------------------------------------
# Scenes that use images or sounds can only be loaded after Godot has imported them.

$(BUILD)/.imported: $(ASSETS)
	@mkdir -p $(BUILD)
	$(GODOT) --headless --path . --import
	@touch $@

packages: $(BUILD)/pkg/pc/.exported $(BUILD)/pkg/dreamcast/.exported $(BUILD)/pkg/psp/.exported $(BUILD)/pkg/wii/.exported

$(BUILD)/pkg/%/.exported: $(SCENES) $(PARTS) $(EXPORT_DEPS)
	$(EXPORT) --platform $* --out $(BUILD)/pkg/$* $(addprefix res://,$(SCENES))
	@touch $@

# Zip of the PC game that runs after unpacking, in build/dist/. Same as the editor's
# "Project > Tools > Package PC Build as Zip".
dist: $(RUNTIME) $(BUILD)/.imported
	$(GODOT) --headless --path . --script res://addons/retro_runtime/package_cli.gd

run: $(RUNTIME) $(BUILD)/pkg/pc/.exported
	$(RUNTIME) --data $(BUILD)/pkg/pc

# --- tests ------------------------------------------------------------------------------
# 1. Tree dump of a scene that uses every node type, compared with the expected text.
# 2. A scene with an unsupported node must fail and name that node; so must a PackedScene
#    field without the number of instances to make.
# 3. The runtime must reject a package baked for another platform.
# 4. A scene with many areas and bodies must report every overlap once, with rays and
#    bodies hitting the right things, and a scene that
#    checks runtime features by itself (rays, paths, ...) must report ok. The controls that
#    take input are driven by a scripted input and must emit their signals in order.
# 5. Game flow with scripted input: on the first racer's track the car reaches the
#    finish trigger and the menu comes back; then the same for the 2D test scene, and
#    the Quit button ends the program. In the 2D test scene the ship shoots all targets
#    with instanced bullets, more of them than the pool holds at a time. Both platformer
#    demos are played to their flag, and the racing demo is driven for three laps by a
#    recorded input (tests/input/racing.txt). The ball of the ball demo is rolled over
#    its whole course and through the goal gate. The menu starts the stunt track, where
#    the car goes down the slope and through the loop to the first checkpoint.
#
# The game flow tests open a window for a few seconds. RR_MUTE keeps them silent.

T := $(BUILD)/test
FLOW = RR_SAVE_DIR=$(T) RR_MUTE=1 $(RUNTIME) --data $(BUILD)/pkg/pc --frames 900 --input

test: all $(BUILD)/pkg/pc/.exported
	@mkdir -p $(T)
	@echo "== tree dump"
	$(EXPORT) --platform pc --out $(T)/pc res://scenes/tests/dump_test.tscn
	$(RPKDUMP) $(T)/pc/dump_test.rpk > $(T)/dump_test.txt
	diff -u --strip-trailing-cr tests/expected/dump_test.txt $(T)/dump_test.txt
	@echo "== unsupported node"
	@if $(EXPORT) --platform pc --out $(T)/pc res://scenes/tests/unsupported.tscn \
	        > $(T)/unsupported.log 2>&1; then \
	    echo "FAIL: export of unsupported.tscn succeeded"; exit 1; fi
	grep -q "node 'Unsupported/Effects/Sparks' (GPUParticles3D): unsupported node type" $(T)/unsupported.log
	@test ! -e $(T)/pc/unsupported.rpk || { echo "FAIL: a package was written anyway"; exit 1; }
	@echo "== PackedScene field without a pool size"
	@if $(EXPORT) --platform pc --out $(T)/pc res://scenes/tests/pool_no_max.tscn \
	        > $(T)/pool_no_max.log 2>&1; then \
	    echo "FAIL: export of pool_no_max.tscn succeeded"; exit 1; fi
	grep -q "node 'PoolNoMax/Spawner' (Node3D): exported field 'spawn' is a PackedScene" $(T)/pool_no_max.log
	@echo "== wrong platform"
	$(EXPORT) --platform dreamcast --out $(T)/dreamcast res://scenes/tests/dump_test.tscn
	@if $(RUNTIME) dump_test --data $(T)/dreamcast --dump > $(T)/wrong_platform.log 2>&1; then \
	    echo "FAIL: the PC runtime accepted a Dreamcast package"; exit 1; fi
	grep -q "baked for platform 'dreamcast'" $(T)/wrong_platform.log
	@echo "== many areas and bodies: every overlap reported once, rays and bodies hit right"
	$(EXPORT) --platform pc --out $(T)/pc res://scenes/tests/stress_triggers.tscn
	RR_MUTE=1 $(RUNTIME) stress_triggers --data $(T)/pc --frames 32 > $(T)/stress.log
	@test "$$(tr -d '\r' < $(T)/stress.log | tr '\n' ' ')" = \
	    "scene: stress_triggers stress: entered 360 stress: exited 20 stress: rays 20/20 hit, 10/10 through the gaps stress: walkers at 0.00 and -0.40 " || \
	    { echo "FAIL: unexpected counts:"; cat $(T)/stress.log; exit 1; }
	@echo "== self-checking feature scene"
	RR_SAVE_DIR=$(T) RR_MUTE=1 $(RUNTIME) features --data $(BUILD)/pkg/pc --frames 120 > $(T)/features.log
	@grep -q "^features: ok" $(T)/features.log || \
	    { echo "FAIL: the feature scene reports:"; cat $(T)/features.log; exit 1; }
	@echo "== controls: focus passes a disabled button, toggles, slider steps and repeats"
	$(EXPORT) --platform pc --out $(T)/pc res://scenes/tests/ui_test.tscn
	RR_MUTE=1 $(RUNTIME) ui_test --data $(T)/pc --frames 400 --input "10:ui_accept,20:ui_down,30:ui_accept,40:ui_down,50:ui_accept,60:ui_down,70:ui_right,80:ui_left:120,210:ui_down,220:ui_accept,230:ui_down,240:ui_accept,250:ui_up,260:ui_up,270:ui_up,280:ui_up,290:ui_up,300:ui_accept" > $(T)/ui.log
	@test "$$(tr -d '\r' < $(T)/ui.log | tr '\n' ' ')" = \
	    "scene: ui_test ui: pressed Plain ui: toggled Check on ui: toggled Switch off ui: value Volume 6.00 ui: value Volume 5.00 ui: value Volume 4.00 ui: value Volume 3.00 ui: value Volume 2.00 ui: value Volume 1.00 ui: value Volume 0.00 ui: pressed Picture ui: pressed Unlock ui: pressed Locked quit " || \
	    { echo "FAIL: unexpected output:"; cat $(T)/ui.log; exit 1; }
	@echo "== one-way platforms: up through them, down onto them, sideways through them"
	$(EXPORT) --platform pc --out $(T)/pc res://scenes/tests/tile_test.tscn
	RR_MUTE=1 $(RUNTIME) tile_test --data $(T)/pc --frames 130 > $(T)/tiles.log
	@test "$$(tr -d '\r' < $(T)/tiles.log | tr '\n' ' ')" = \
	    "scene: tile_test tiles: jumper 244.0 roller 244.0 faller 227.0 riser 227.0 sider 240.0 " || \
	    { echo "FAIL: unexpected output:"; cat $(T)/tiles.log; exit 1; }
	@echo "== 2D view: Camera2D (drag, smoothing, limits) and Parallax2D give what Godot gives"
	$(EXPORT) --platform pc --out $(T)/pc res://scenes/tests/canvas_test.tscn
	$(GODOT) --headless --fixed-fps 60 --path . --script res://tools/canvas_reference.gd > $(T)/canvas_godot.log
	RR_MUTE=1 $(RUNTIME) canvas_test --data $(T)/pc --frames 152 > $(T)/canvas.log
	@grep -q "^canvas: center " $(T)/canvas.log && \
	    test "$$(grep '^canvas:' $(T)/canvas.log | tr -d '\r')" = "$$(grep '^canvas:' $(T)/canvas_godot.log | tr -d '\r')" || \
	    { echo "FAIL: the runtime and Godot disagree:"; cat $(T)/canvas.log $(T)/canvas_godot.log; exit 1; }
	@echo "== ScrollContainer: follows the focus as far as Godot does"
	$(EXPORT) --platform pc --out $(T)/pc res://scenes/tests/scroll_test.tscn
	$(GODOT) --headless --path . --script res://tools/scroll_reference.gd > $(T)/scroll_godot.log
	RR_MUTE=1 $(RUNTIME) scroll_test --data $(T)/pc --frames 165 --input "10:ui_down,20:ui_down,30:ui_down,40:ui_down,50:ui_down,60:ui_down,70:ui_down,80:ui_down,105:ui_up,115:ui_up,125:ui_up,135:ui_up,145:ui_up" > $(T)/scroll.log
	@grep -q "^scroll: bottom " $(T)/scroll.log && \
	    test "$$(grep '^scroll:' $(T)/scroll.log | tr -d '\r')" = "$$(grep '^scroll:' $(T)/scroll_godot.log | tr -d '\r')" || \
	    { echo "FAIL: the runtime and Godot disagree:"; cat $(T)/scroll.log $(T)/scroll_godot.log; exit 1; }
	@echo "== AnimationPlayer: speed scale and blending between clips give what Godot gives"
	$(EXPORT) --platform pc --out $(T)/pc res://scenes/tests/anim_test.tscn
	$(GODOT) --headless --fixed-fps 60 --path . --script res://tools/anim_reference.gd > $(T)/anim_godot.log
	RR_MUTE=1 $(RUNTIME) anim_test --data $(T)/pc --frames 75 > $(T)/anim.log
	@test "$$(grep -c '^anim:' $(T)/anim.log)" = 4 && \
	    test "$$(grep '^anim:' $(T)/anim.log | tr -d '\r' | sed 's/-0\.00/0.00/g')" = \
	         "$$(grep '^anim:' $(T)/anim_godot.log | tr -d '\r' | sed 's/-0\.00/0.00/g')" || \
	    { echo "FAIL: the runtime and Godot disagree:"; cat $(T)/anim.log $(T)/anim_godot.log; exit 1; }
	@echo "== GridMap: a body stops at its floor and wall where Godot's physics stops it"
	$(EXPORT) --platform pc --out $(T)/pc res://scenes/tests/grid_test.tscn
	$(GODOT) --headless --fixed-fps 60 --path . --script res://tools/grid_reference.gd > $(T)/grid_godot.log
	RR_MUTE=1 $(RUNTIME) grid_test --data $(T)/pc --frames 485 > $(T)/grid.log
	@grep -q "^grid: walker at " $(T)/grid.log && \
	    test "$$(grep '^grid:' $(T)/grid.log | tr -d '\r')" = "$$(grep '^grid:' $(T)/grid_godot.log | tr -d '\r')" || \
	    { echo "FAIL: the runtime and Godot disagree:"; cat $(T)/grid.log $(T)/grid_godot.log; exit 1; }
	@echo "== RigidBody2D: bodies fall, turn, stack, bounce and pass one-way platforms, and rest where Godot's rest"
	$(EXPORT) --platform pc --out $(T)/pc res://scenes/tests/rigid2d_test.tscn
	$(GODOT) --headless --fixed-fps 60 --path . --script res://tools/rigid2d_reference.gd > $(T)/rigid2d_godot.log
	RR_MUTE=1 $(RUNTIME) rigid2d_test --data $(T)/pc --frames 305 > $(T)/rigid2d.log
	@grep -q "^rigid2d: ok" $(T)/rigid2d.log && \
	    test "$$(grep '^rigid2d: box' $(T)/rigid2d.log | tr -d '\r' | sed 's/-0 /0 /g')" = \
	         "$$(grep '^rigid2d: box' $(T)/rigid2d_godot.log | tr -d '\r' | sed 's/-0 /0 /g')" || \
	    { echo "FAIL: the runtime fails its checks or disagrees with Godot:"; cat $(T)/rigid2d.log $(T)/rigid2d_godot.log; exit 1; }
	@echo "== ShapeCast: a sphere, box, capsule and circle touch where Godot's physics says"
	$(EXPORT) --platform pc --out $(T)/pc res://scenes/tests/cast_test.tscn
	$(GODOT) --headless --fixed-fps 60 --path . --script res://tools/cast_reference.gd > $(T)/cast_godot.log
	RR_MUTE=1 $(RUNTIME) cast_test --data $(T)/pc --frames 6 > $(T)/cast.log
	@test "$$(grep -c '^cast:' $(T)/cast.log)" = 5 && \
	    test "$$(grep '^cast:' $(T)/cast.log | tr -d '\r' | sed 's/-0\.0/0.0/g')" = \
	         "$$(grep '^cast:' $(T)/cast_godot.log | tr -d '\r' | sed 's/-0\.0/0.0/g')" || \
	    { echo "FAIL: the runtime and Godot disagree:"; cat $(T)/cast.log $(T)/cast_godot.log; exit 1; }
	@echo "== ConcavePolygonShape3D: rays, a character body and a rigid body meet its faces where Godot's physics says, and the rigid body reports body_entered"
	$(EXPORT) --platform pc --out $(T)/pc res://scenes/tests/concave_test.tscn
	$(GODOT) --headless --fixed-fps 60 --path . --script res://tools/concave_reference.gd > $(T)/concave_godot.log
	RR_MUTE=1 $(RUNTIME) concave_test --data $(T)/pc --frames 485 > $(T)/concave.log
	@test "$$(grep -c '^concave:' $(T)/concave.log)" = 12 && \
	    test "$$(grep '^concave:' $(T)/concave.log | tr -d '\r' | sed 's/-0\.0/0.0/g')" = \
	         "$$(grep '^concave:' $(T)/concave_godot.log | tr -d '\r' | sed 's/-0\.0/0.0/g')" || \
	    { echo "FAIL: the runtime and Godot disagree:"; cat $(T)/concave.log $(T)/concave_godot.log; exit 1; }
	@echo "== skinning: vertices between two bones are where the pose of Godot's skeleton puts them"
	$(EXPORT) --platform pc --out $(T)/pc res://scenes/tests/skin_test.tscn
	$(GODOT) --headless --fixed-fps 60 --path . --script res://tools/skin_reference.gd > $(T)/skin_godot.log
	RR_MUTE=1 $(RUNTIME) skin_test --data $(T)/pc --frames 85 > $(T)/skin.log
	@grep -q "^skin: " $(T)/skin.log && \
	    test "$$(grep '^skin:' $(T)/skin.log | tr -d '\r' | sed 's/-0\.00/0.00/g')" = \
	         "$$(grep '^skin:' $(T)/skin_godot.log | tr -d '\r' | sed 's/-0\.00/0.00/g')" || \
	    { echo "FAIL: the runtime and Godot disagree:"; cat $(T)/skin.log $(T)/skin_godot.log; exit 1; }
	@echo "== placeholders: every C component has a script, and what the runtime prints is GDScript"
	$(GODOT) --headless --path . --script res://addons/retro_runtime/placeholders_cli.gd -- --check > $(T)/placeholders.log 2>&1 || \
	    { echo "FAIL:"; cat $(T)/placeholders.log; exit 1; }
	@echo "== game flow: the first racer's track -> finish trigger -> menu"
	RR_SAVE_DIR=$(T) RR_MUTE=1 $(RUNTIME) test_track --data $(BUILD)/pkg/pc --frames 900 \
	    --input "30:accelerate:800" > $(T)/flow_track.log
	@test "$$(grep '^scene:' $(T)/flow_track.log | tr -d '\r' | tr '\n' ' ')" = \
	    "scene: test_track scene: main_menu " || \
	    { echo "FAIL: unexpected scene sequence:"; cat $(T)/flow_track.log; exit 1; }
	@echo "== game flow: menu -> stunt track, down the slope and through the loop to the first checkpoint -> cancel -> menu"
	$(FLOW) "10:ui_accept,40:accelerate:600,660:leave" > $(T)/flow_stunt.log
	@test "$$(grep -E '^(scene|stunt):' $(T)/flow_stunt.log | tr -d '\r' | tr '\n' ' ')" = \
	    "scene: main_menu scene: stunt_track stunt: checkpoint 1/3 scene: main_menu " || \
	    { echo "FAIL: unexpected output:"; cat $(T)/flow_stunt.log; exit 1; }
	@echo "== game flow: menu -> 2D test -> cancel -> menu -> quit"
	$(FLOW) "10:ui_down,20:ui_accept,40:steer_right:60,120:ui_cancel,140:ui_down,150:ui_down,160:ui_down,170:ui_down,180:ui_down,190:ui_down,195:ui_down,200:ui_down,205:ui_down,210:ui_down,215:ui_down,218:ui_down,221:ui_down,225:ui_accept" > $(T)/flow_2d.log
	@test "$$(tr -d '\r' < $(T)/flow_2d.log | tr '\n' ' ')" = \
	    "scene: main_menu scene: sprites_2d scene: main_menu quit " || \
	    { echo "FAIL: unexpected scene sequence:"; cat $(T)/flow_2d.log; exit 1; }
	@echo "== game flow: menu -> 2D test, all targets shot with instanced bullets -> menu"
	$(FLOW) "10:ui_down,20:ui_accept,30:jump,40:steer_right:30,80:jump,90:steer_left:60,100:jump,110:jump,120:jump,130:jump,135:jump,160:jump" > $(T)/flow_pool.log
	@test "$$(tr -d '\r' < $(T)/flow_pool.log | tr '\n' ' ')" = \
	    "scene: main_menu scene: sprites_2d scene: main_menu " || \
	    { echo "FAIL: unexpected scene sequence:"; cat $(T)/flow_pool.log; exit 1; }
	@echo "== game flow: menu -> feature test, report on screen -> cancel -> menu"
	$(FLOW) "10:ui_down,20:ui_down,30:ui_down,40:ui_down,50:ui_down,55:ui_down,60:ui_down,65:ui_down,70:ui_down,75:ui_down,78:ui_down,82:ui_accept,230:ui_cancel" > $(T)/flow_features.log
	@test "$$(tr -d '\r' < $(T)/flow_features.log | tr '\n' ' ')" = \
	    "scene: main_menu scene: features scene: features features: ok scene: main_menu " || \
	    { echo "FAIL: unexpected output:"; cat $(T)/flow_features.log; exit 1; }
	@echo "== game flow: racing, three laps driven by a recorded input -> menu"
	RR_SAVE_DIR=$(T) RR_MUTE=1 $(RUNTIME) racing --data $(BUILD)/pkg/pc --frames 3300 \
	    --input "$$(cat tests/input/racing.txt)" > $(T)/flow_racing.log
	@grep -q "^race: lap 3 " $(T)/flow_racing.log && grep -q "^race: finish P" $(T)/flow_racing.log && \
	    test "$$(grep '^scene:' $(T)/flow_racing.log | tr -d '\r' | tr '\n' ' ')" = \
	        "scene: racing scene: main_menu " || \
	    { echo "FAIL: the race did not run to the end:"; cat $(T)/flow_racing.log; exit 1; }
	@echo "== game flow: island, walked over the height map up to the flag -> menu"
	RR_SAVE_DIR=$(T) RR_MUTE=1 $(RUNTIME) island --data $(BUILD)/pkg/pc --frames 600 \
	    --input "10:move_up:400,10:move_right:165" > $(T)/flow_island.log
	@test "$$(tr -d '\r' < $(T)/flow_island.log | tr '\n' ' ')" = \
	    "scene: island scene: main_menu " || \
	    { echo "FAIL: unexpected scene sequence:"; cat $(T)/flow_island.log; exit 1; }
	@echo "== game flow: ball demo, rolled over the whole course through the goal gate -> menu"
	@rm -f $(T)/RR_BALL.sav
	RR_SAVE_DIR=$(T) RR_MUTE=1 $(RUNTIME) ball_roll --data $(BUILD)/pkg/pc --frames 2000 \
	    --input "40:move_up:399,439:move_down:70,520:move_right:30,650:move_up:25,805:move_down:21,845:move_right:30,1095:move_down:9,1108:move_left:30,1240:move_up:110,1396:move_down:46,1455:move_left:30,1585:move_up:400" > $(T)/flow_ball.log
	@test "$$(tr -d '\r' < $(T)/flow_ball.log | grep '^scene:' | tr '\n' ' ')" = \
	    "scene: ball_roll scene: main_menu " || \
	    { echo "FAIL: unexpected scene sequence:"; cat $(T)/flow_ball.log; exit 1; }
	@test -s $(T)/RR_BALL.sav || { echo "FAIL: the time of the run was not saved"; exit 1; }
	@echo "== game flow: menu -> 2D platformer, played to the flag -> menu"
	$(FLOW) "10:ui_down,20:ui_down,30:ui_accept,30:move_right:870,115:jump:20,166:jump:20,266:jump:20,320:jump:20,424:jump:20,489:jump:20" > $(T)/flow_platformer_2d.log
	@test "$$(tr -d '\r' < $(T)/flow_platformer_2d.log | tr '\n' ' ')" = \
	    "scene: main_menu scene: platformer_2d scene: main_menu " || \
	    { echo "FAIL: unexpected scene sequence:"; cat $(T)/flow_platformer_2d.log; exit 1; }
	@echo "== game flow: menu -> 3D platformer, played to the flag -> menu"
	$(FLOW) "10:ui_down,20:ui_down,30:ui_down,40:ui_accept,40:move_right:417,99:jump:20,174:jump:20,219:jump:20,264:jump:20,413:jump:20,517:move_right:200,525:jump:20" > $(T)/flow_platformer_3d.log
	@test "$$(tr -d '\r' < $(T)/flow_platformer_3d.log | tr '\n' ' ')" = \
	    "scene: main_menu scene: platformer_3d scene: main_menu " || \
	    { echo "FAIL: unexpected scene sequence:"; cat $(T)/flow_platformer_3d.log; exit 1; }
	@echo "== game flow: menu -> airplane, started and flown through the first gate -> cancel -> menu"
	RR_SAVE_DIR=$(T) RR_MUTE=1 $(RUNTIME) --data $(BUILD)/pkg/pc --frames 1600 \
	    --input "10:ui_down,15:ui_down,20:ui_down,25:ui_down,30:ui_down,35:ui_down,40:ui_down,50:ui_accept,110:start,1500:ui_cancel" \
	    > $(T)/flow_airplane.log
	@test "$$(tr -d '\r' < $(T)/flow_airplane.log | tr '\n' ' ')" = \
	    "scene: main_menu scene: airplane airplane: GATES 1/9 scene: main_menu " || \
	    { echo "FAIL: unexpected output:"; cat $(T)/flow_airplane.log; exit 1; }
	@echo "== game flow: menu -> highway title -> game: rivals taken down, three crashes, game over -> title -> menu"
	RR_SAVE_DIR=$(T) RR_MUTE=1 $(RUNTIME) --data $(BUILD)/pkg/pc --frames 2950 \
	    --input "10:ui_down,15:ui_down,20:ui_down,25:ui_down,30:ui_down,35:ui_down,40:ui_down,45:ui_down,50:ui_accept,80:start,90:accelerate:3000,120:steer_left:20,380:steer_right:20,680:steer_left:20,980:steer_right:20,1280:steer_left:20,1580:steer_right:20,1880:steer_left:20,2180:steer_right:20,2480:steer_left:20,2800:start,2850:ui_cancel" \
	    > $(T)/flow_highway.log
	@grep -q "^highway: takedown" $(T)/flow_highway.log && \
	    test "$$(grep -E '^(scene|highway: (crash|game over))' $(T)/flow_highway.log | sed 's/, score.*//' | tr -d '\r' | tr '\n' ' ')" = \
	        "scene: main_menu scene: highway_title scene: highway highway: crash highway: crash highway: crash highway: game over scene: highway_title scene: main_menu " || \
	    { echo "FAIL: unexpected output:"; cat $(T)/flow_highway.log; exit 1; }
	@echo "== game flow: menu -> neon league title -> game: three deaths, game over -> title -> exit -> menu"
	RR_SAVE_DIR=$(T) RR_MUTE=1 $(RUNTIME) --data $(BUILD)/pkg/pc --frames 1950 \
	    --input "10:ui_down,15:ui_down,20:ui_down,25:ui_down,30:ui_down,35:ui_down,40:ui_down,45:ui_down,50:ui_down,60:ui_accept,90:ui_accept,1800:nl_back,1850:ui_down,1860:ui_down,1870:ui_down,1880:ui_accept" \
	    > $(T)/flow_neonleague.log
	@test "$$(grep -E '^(scene|neonleague: (died|game over))' $(T)/flow_neonleague.log | sed 's/, score.*//' | tr -d '\r' | tr '\n' ' ')" = \
	    "scene: main_menu scene: neonleague_title scene: neonleague neonleague: died, 2 lives left neonleague: died, 1 lives left neonleague: died, 0 lives left neonleague: game over scene: neonleague_title scene: main_menu " || \
	    { echo "FAIL: unexpected output:"; cat $(T)/flow_neonleague.log; exit 1; }
	@echo "== game flow: menu -> Starter Kit Racing, driven into the first bend -> leave -> menu"
	RR_SAVE_DIR=$(T) RR_MUTE=1 $(RUNTIME) --data $(BUILD)/pkg/pc --frames 500 \
	    --input "10:ui_down,15:ui_down,20:ui_down,25:ui_down,30:ui_down,35:ui_down,40:ui_down,45:ui_down,50:ui_down,55:ui_down,65:ui_accept,90:accelerate:300,200:steer_right:60,450:leave" \
	    > $(T)/flow_starter.log
	@test "$$(grep -E '^scene' $(T)/flow_starter.log | tr -d '\r' | tr '\n' ' ')" = \
	    "scene: main_menu scene: starter_racing scene: main_menu " || \
	    { echo "FAIL: unexpected output:"; cat $(T)/flow_starter.log; exit 1; }
	@echo "ALL TESTS PASSED"

clean:
	rm -rf $(BUILD)/pc $(BUILD)/tools $(BUILD)/pkg $(BUILD)/test $(BUILD)/.imported
