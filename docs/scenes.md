# Scenes: what the exporter and the runtime support

How a Godot scene becomes a package, which nodes and properties are carried over, and how
the runtime treats them. Whatever is not listed here stops the export with a message that
names the node.

## Exporting a scene

From the editor: **Project > Tools > Export Scene as Retro Package** writes
`build/pkg/<platform>/<scene>.rpk` for every platform.

From the command line, without the editor GUI:

```
godot --headless --path . --import      # once, and after adding images or sounds
godot --headless --path . --script res://addons/retro_runtime/export_cli.gd -- \
    --platform pc --out build/pkg/pc res://scenes/main_menu.tscn res://scenes/test_track.tscn
```

Platforms: `pc`, `dreamcast`. The exit code is 1 if an export failed. A scene that uses
something the runtime cannot handle is not exported; the message names the node.

## What a scene may contain

| Godot                         | Notes                                                         |
|-------------------------------|---------------------------------------------------------------|
| `Node`, `Node3D`, `Node2D`    |                                                               |
| `MeshInstance3D`              | ArrayMesh (also an imported `.obj`) or primitive mesh; `StandardMaterial3D` or no material; albedo colour and one albedo texture; transparency Disabled, Alpha Scissor or Alpha (drawn after everything solid, the far ones first, as in Godot); `visibility_range_begin`/`end` without margins or fading |
| `Camera3D`                    | perspective or orthogonal (`size`), Keep Height; an orthogonal camera not together with fog or a sky background |
| `DirectionalLight3D`, `OmniLight3D`, `SpotLight3D` | inputs of the light bake only; they become `Node3D` nodes. Colour, energy, range, attenuation, spot angle and its attenuation; no shadows, no negative lights |
| `Area3D`, `Area2D` + `CollisionShape3D`, `CollisionShape2D` | box and sphere shapes, rectangle and circle in 2D; emits `area_entered` / `area_exited`; `body_entered` and `body_exited` for static and character bodies |
| `StaticBody3D`, `StaticBody2D` | what the other bodies cannot pass; box and sphere shapes (rectangle and circle), rotated or not; a `HeightMapShape3D` as ground (not rotated; see [terrain.md](terrain.md)); physics material; `one_way_collision` on a `RectangleShape2D` that is not rotated |
| `CharacterBody3D`, `CharacterBody2D` | moved by a component with `rr_body_move_and_slide`; box, sphere or capsule shape that is not rotated; layers and masks as in Godot |
| `Sprite2D`                    | position, rotation, scale, flip, modulate, offset; region and sprite sheets (`hframes`, `vframes`, `frame`; C code changes `node->frame`) |
| `z_index`, `z_as_relative`    | on every 2D node and control: lower levels are drawn first, tree order inside a level; up to 16 different levels in a scene; no `y_sort_enabled` |
| `AnimatedSprite2D`            | `SpriteFrames` with whole textures or `AtlasTexture` regions; speed, loop, frame durations, autoplay, flip; emits `animation_finished` |
| `Camera2D`                    | follows its parent; zoom, offset, limits, both anchor modes, drag margins and drag offsets, position smoothing, `limit_smoothed`; worked out as Godot does it; the view does not rotate. `rr_camera2d_reset_smoothing` |
| `Parallax2D`                  | scroll scale and offset, limits, repeat size and times, autoscroll, follow viewport, ignore camera scroll; sprites, tile maps and particles below it are repeated; not inside another Parallax2D |
| `CanvasLayer`                 | holds the HUD: what is inside is drawn over everything outside, wherever it is in the tree; with a `Camera2D` in the scene, controls must be inside one |
| `TileMapLayer`                | square tiles from one atlas (`TileSetAtlasSource`); tiles flipped and turned in the editor; animated tiles (frame durations, speed; mode Default); one physics layer whose rectangles become solid boxes, one-way or not; no other alternative tiles |
| `RigidBody3D`                 | moved by a physics engine (picophysics): falls, turns, rubs, bounces, is kept out of static bodies (boxes, spheres, height maps) and pushes other rigid bodies; box and sphere shapes, up to 8, not rotated against the body; mass, physics material, gravity scale, linear and angular damp, start velocities, `lock_rotation` and the three angular axis locks, layers and masks, `contact_monitor` with `max_contacts_reported` up to 8 (asked from C); up to 64 in a scene; see "Collision" |
| `RigidBody2D`                 | falls, bounces and rubs against static bodies and tile maps (gravity, mass, physics material, linear damp, start velocity); does not turn, bodies do not push each other |
| `Timer`                       | wait time, one-shot, autostart; emits `timeout`               |
| `RayCast3D`, `RayCast2D`      | hits box and sphere shapes of bodies and areas, height maps, and tile maps; mask, areas/bodies, exclude parent; no `hit_from_inside` |
| `ShapeCast3D`, `ShapeCast2D`  | moves a box, sphere or capsule (rectangle, circle, capsule) along its line and reports the first thing it touches: one result, with collider, point, normal and the safe and unsafe fraction (`rr_raycast_*`, `rr_shapecast_get_safe_fraction`, `rr_shapecast_get_unsafe_fraction`); no margin |
| `Path3D`, `Path2D` + `PathFollow3D`, `PathFollow2D` | the curve is sampled at its bake interval; progress, offsets, loop; rotation none, Y, along the curve, oriented |
| `Marker3D`, `Marker2D`        | a position, like `Node3D` and `Node2D`                        |
| `WorldEnvironment`            | background: clear colour, custom colour or sky (`PanoramaSkyMaterial`, or `ProceduralSkyMaterial` without its sun disc), drawn as a cube of six textures; ambient light for the light bake (colour, background or sky); fog by depth or density |
| `Control`, `VBoxContainer`, `HBoxContainer`, `CenterContainer`, `MarginContainer` | layout only; rectangles are baked at 640x480 |
| `Label`                       | theme font, size and colour; alignment; `\n`; autowrap (arbitrary, word, word smart), done by the runtime so the text can change |
| `Button`                      | text, alignment, icon on the left, theme colours, focus neighbours; `disabled`, `toggle_mode`; emits `pressed` and `toggled` |
| `CheckBox`, `CheckButton`     | a toggle button with the theme's box or switch as its icon; no button groups |
| `TextureButton`               | textures for normal, pressed, focused (or hover) and disabled; stretch modes Scale and Keep |
| `HSlider`, `VSlider`          | range, step, value, `editable`; the directions along it change the value, a held one repeats; emits `value_changed`; theme styles as plain colours, the theme's grabber pictures; no ticks |
| `MultiMeshInstance3D`         | a `MultiMesh` with 3D transforms: the exporter joins the copies into one mesh per 16 m of space, each copy lit where it stands; no instance colours or custom data |
| `GridMap`                     | the cells of a `MeshLibrary` with their orientations: one joined mesh and one `StaticBody3D` with the items' collision shapes per 8 x 8 x 8 cells; layer, mask and physics material of the GridMap; no `cell_scale` |
| `GridContainer`, `HFlowContainer`, `VFlowContainer` | layout only, like the other containers |
| `Panel`, `PanelContainer`     | the theme's `panel` style as a fill colour with a border; no rounded corners |
| `HSeparator`, `VSeparator`    | the line of the theme's `separator` style                     |
| `TextureProgressBar`          | under, progress and over textures with their tints, the four straight fill modes; `node->value`; no nine patch stretch, no radial fill |
| `ScrollContainer`             | cuts off and scrolls the controls below it; `follow_focus` scrolls to the focused button or slider as Godot does; scroll bars as plain rectangles; `rr_scroll_set`, `rr_scroll_get` |
| `RichTextLabel`               | BBCode `[b]`, `[i]`, `[color]`, `[center]`, `[right]`, `[left]`, `[lb]`, `[rb]`, read by the runtime so the text can change; autowrap; the text must fit (no scrolling inside it). Each style in use is one more font atlas |
| `TextureRect`                 | stretch mode Scale                                            |
| `ColorRect`                   | a filled rectangle                                            |
| `ProgressBar`                 | range, value, four fill modes, percentage; theme styles as plain colours |
| `NinePatchRect`               | patch margins, region, draw centre; stretch mode Stretch      |
| `AudioStreamPlayer`           | WAV, mono or stereo (import with Compress Mode PCM): a sound in memory; Ogg Vorbis or MP3: music, streamed from the disc. Volume, `pitch_scale` (sounds only, changeable with `rr_audio_set_pitch_scale`), `bus`, autoplay, loop; emits `finished` |
| Audio buses                   | the project's bus layout: name, volume, mute and send of up to 16 buses; `rr_audio_bus_index`, `rr_audio_bus_set_volume`, `rr_audio_bus_set_mute`, kept across scene changes; no effects, solo or bypass |
| `AudioStreamPlayer2D`, `AudioStreamPlayer3D` | sounds heard from the camera: volume by distance (all four models, unit size, max dB, max distance), panning; no filter, doppler or emission angle |
| `AnimationPlayer`             | tracks: 3D position, rotation, scale; `visible`; 2D `position`, `rotation`, `scale`; `frame` of a `Sprite2D`; `value` of a `ProgressBar`; `progress` of a `PathFollow`; method calls without arguments. Linear or nearest; loop, autoplay, `speed_scale`, the default blend time (weights and rotation as in Godot; `rr_anim_set_speed_scale`, `rr_anim_set_blend_time`); emits `animation_finished` |
| `Tween` (from C)              | `rr_tween_property`: position, scale, 2D rotation or `value` of a node, with Godot's transition and ease types and a delay |
| `CPUParticles3D`              | quads that face the camera (mesh must be a `QuadMesh`); see "Animations and particles" |
| `CPUParticles2D`              | the same particle system in screen units: texture or plain squares, direction, spread, gravity, velocity, scale, colour ramp with two points |
| `Sprite3D`                    | one translucent quad: region, sprite sheet (`node->frame`), pixel size, offset, flip, axis, billboard (on or fixed Y); unlit |
| `Label3D`                     | text as quads in the world: font, size, pixel size, alignment, billboard; no outline |
| Script with `class_name`      | becomes a C component; exports: float, int, bool, node, `@export_file("*.tscn")` scene, `PackedScene` |
| `PackedScene` export          | scene to make instances of at runtime; needs an int export `<field>_max`, see "Instancing scenes" |
| Custom `Resource` export      | a script class that extends `Resource` becomes a read-only C struct, see "Data: resources, autoloads, saved games" |
| Autoloads                     | the nodes from Project Settings > Globals are part of every scene and keep their state |
| Translations                  | the translations from Project Settings > Localization (CSV with Compress disabled, or PO): `Label`, `Button`, `Label3D` and `RichTextLabel` texts that are keys are translated, unless auto translate is off; `rr_tr`, `rr_set_locale`, `rr_get_locale`. The locale starts as the language of the console (PC: the `RR_LOCALE` variable). Latin-1 characters only; no contexts or plural forms |
| Rumble                        | `rr_input_vibrate(weak, strong, seconds)` and `rr_input_stop_vibration`, after `Input.start_joy_vibration`; on the Dreamcast only: the raylib build of the PC cannot shake a pad |
| Groups                        | become tags                                                   |
| Signal connections            | to a method of a node with a component                        |
| Input Map                     | keys and joypad buttons/axes, mapped per platform             |

Things to know:

- Buttons are drawn as a filled rectangle with the theme's `normal` colour and a 2 pixel
  outline in the `focus` colour; rounded corners and other StyleBox details are not kept.
- The first visible button or slider of a scene that is not disabled has the focus at the
  start. The focus passes over disabled and hidden ones. From C: `rr_button_set_disabled`,
  `rr_button_set_pressed`, `rr_button_is_pressed`, `rr_control_grab_focus`; the value of a
  slider is the node's `value`. The handlers of `toggled` and `value_changed` are
  `ON_SIGNAL_NODE` handlers and get the button or slider.
- Nothing hovers without a mouse: a TextureButton shows its focus texture, or else its
  hover texture, while it has the focus.
- Fonts are baked per size (ASCII and Latin-1) into an atlas. A size 32 atlas is 512x512,
  which is 512 KB of texture memory on the Dreamcast, so use few sizes.
- Dreamcast textures are padded to powers of two and converted to 16 bits. A mesh whose
  texture coordinates leave the 0..1 range (a repeating texture) needs a texture with
  power-of-two sides there.
- Texture filter: nearest or linear, taken from the 2D node (or inherited from its parents
  and the project setting) and from the material of a mesh. There are no mipmaps; the
  mipmap filters count as their plain variant. For pixel art, set Texture > Filter to
  Nearest on the root of the 2D scene.
- Models with a skeleton work: rigidly skinned meshes are cut into pieces per bone, others get a skin; see "Animations and particles".
- WAV files must be imported with Compress Mode "PCM (Uncompressed)".

## Instancing scenes

`PackedScene.instantiate()` and `Node.queue_free()` exist as `rr_instantiate` and
`rr_queue_free`. The runtime allocates nothing while a scene runs, so the instances are
made by the exporter: a script that exports a `PackedScene` also exports an int with the
same name plus `_max`, the number of instances that can exist at the same time.

```gdscript
@export var bullet_scene: PackedScene
@export var bullet_scene_max: int = 4
```

```c
Node *bullet = rr_instantiate(ship->bullet_scene); /* NULL if all 4 are in use */
if (bullet != NULL) {
    bullet->local.m[9] = x; /* place it */
}
...
rr_queue_free(bullet); /* out of the tree at the end of the frame, free for reuse */
```

- An instance starts in the state of its scene file every time, and the ready handlers
  of its components run again.
- Instances are children of the scene's root; they cannot be put below another node.
- `rr_queue_free` works on any node but the root. A node that is not an instance of a
  `PackedScene` is gone until the scene is loaded again.
- An instanced scene cannot contain controls, cameras or a `WorldEnvironment`.
- The 2D test scene uses it: the ship fires `scenes/parts/bullet.tscn`, and a bullet
  frees the target it hits and itself.

## Data: resources, autoloads, saved games

**Custom resources.** A script with a `class_name` that extends `Resource` and exports
values is a plain struct in C. Register it like a component, with the struct named like
the class, and refer to it from a component (or another resource):

```c
typedef struct { float top_speed; int32_t gears; } CarStats;

RESOURCE_BEGIN(CarStats)
    EXPORT_FLOAT(top_speed)
    EXPORT_INT(gears)
RESOURCE_END()

/* in a component: */
const CarStats *stats;          /* @export var stats: CarStats */
EXPORT_RESOURCE(stats, CarStats)
```

Supported field types are the ones of components, except node references. Arrays are
not supported. The values are read-only: everything that uses the same resource shares
one struct.

**Autoloads.** The runtime has one scene at a time, so the exporter puts the project's
autoloads (Project Settings > Globals; a script or a scene) into every package, below
the scene's root. The structs of their components are kept from scene to scene; only the
exported fields are set from the package again, and the ready handlers run in every
scene. `rr_autoload(scene, "Game")` finds the node, `rr_node_component(node, "GameState")`
its struct. The project has one: `Game` (`components/game_state.gd`,
`runtime/game/game_state.c`). An autoload cannot be named like a `class_name`; that is
Godot's rule.

**Saved games.** `rr_save_write(name, data, size)` and `rr_save_read(name, data, size)`
store a block of bytes under a name of up to 12 characters (`A-Z`, `0-9`, `_`): on the
Dreamcast as a file on the memory card in port A slot 1, on the PC as `<name>.sav` in the
directory `RR_SAVE_DIR` (default: the working directory). The Dreamcast file has no icon
yet.

## Light bake

Lighting is baked into the vertex colours at export time: albedo colour times
(ambient + the sum over the lights of light colour x energy x N.L x fall-off). The lights
are the visible `DirectionalLight3D`, `OmniLight3D` and `SpotLight3D` nodes of the scene,
with the fall-off over distance and across a spot's cone that Godot's renderer uses. A
scene without any light gets a default one from above. Unshaded materials skip the bake.

Things to know:

- The light is worked out per vertex, so a lamp only shows on a surface that has vertices
  near it: divide a large floor into smaller squares.
- It is baked for where a mesh is in the scene file. A mesh that moves or turns while the
  game runs keeps the light of its start.
- Meshes that bake to the same colours share one asset. Under an `OmniLight3D` or
  `SpotLight3D` equal meshes in different places differ, and each takes its own memory.
- There are no shadows.

## Animations and particles

Both are made with Godot's own tools and need no code.

**AnimationPlayer.** A clip may move, turn and scale `Node3D` nodes and switch the
`visible` property of any node. The clip named in Autoplay starts with the scene; a
component starts others with `rr_anim_play(player, "name")`. One player plays one clip at
a time, without blending. Not supported, and reported by the exporter: other property
tracks (also 2D ones), method and audio tracks, cubic interpolation, easing of single
keys, ping-pong loops, speed scale. A looping track needs a key at the start and at the
end of the clip. The `RESET` clip of the editor is left out.

**Characters with a skeleton.** A `Skeleton3D` is exported as plain nodes: one node per
bone, which the bone tracks of the model's AnimationPlayer then move. So an imported
`.glb` with its animations can be instanced as it is. What happens to a mesh depends on
its weights:

- Skinned rigidly (every vertex belongs to exactly one bone, as in most low-poly
  characters): the mesh is cut into one piece per bone, which costs the console nothing
  per vertex.
- With vertices between bones: the mesh stays whole and gets a skin. The runtime moves
  every vertex with its bones once per frame (up to four bones per vertex, up to 256 per
  mesh) into a copy of the mesh, which takes the size of the mesh again in memory.

Set the loop mode of clips like `walk` in Godot's Advanced Import Settings. The lighting
is baked in the rest pose and does not change while the mesh moves, and a mesh with a skin
is left out of view by the place it has at rest. Blend shapes are not supported.

**CPUParticles3D.** Exported settings: amount (up to 512), lifetime, one shot,
explosiveness, emitting, direction, spread, gravity, initial velocity, scale, colour,
a colour ramp with two points (start and end, for fading), emission shape Point or
Sphere, and a `QuadMesh` with an optional `StandardMaterial3D` (albedo colour and
texture). The exporter stops if any other setting is changed from its default.
Particles live in world space. The particle colour is always applied; set Vertex Color
> Use as Albedo on the material so the editor shows the same. `GPUParticles3D` is not
supported. A one-shot system is fired from a component with `rr_particles_restart`.

The player of the 3D platformer is Kenney's animated character
(`assets/kenney/platformer-kit/character-oobi.glb`); its component picks the clip `idle`,
`walk`, `jump` or `fall` from what the player is doing.
The coins of the 3D platformer (`scenes/platformer/coin.tscn`) show both: a looping
`spin` clip, a `collect` clip that the player component starts, and a burst of particles
at the place of the coin. The flag has a system that emits all the time.

## Collision

Collision is the runtime's own lean take on Godot's bodies, not Godot's physics engine.
`rr_body_move_and_slide` moves a character body against the static bodies and tile maps,
and `rr_body_is_on_floor` and its siblings tell what it touched. It works in two ways:

- If the body and everything solid in the scene are boxes that are not rotated, it moves
  one axis at a time and stops exactly at the boxes. This is the cheap way, and the one
  the platformer demos use.
- Otherwise (a sphere or capsule shape, a rotated box, a sphere or a height map among
  the static shapes) the body is moved and then pushed out of what it overlaps along the
  shortest way. A floor of up to 45 degrees carries the body, so it can walk on slopes.
  A body that leaves the floor without jumping is put back down if the floor is within
  0.1 m (Godot's floor snap), which keeps it on the ground when it walks downhill.

A `RigidBody3D` is a body of a physics engine: picophysics, a small one in a single
header, made for the N64 and the Dreamcast (`runtime/third_party/picophysics`, public
domain). It is set up from the node the way Godot reads it: the box and sphere shapes
below it, its mass, physics material, gravity scale, damping, start velocities, locked
axes, layer and mask. The engine moves and turns it, and after every step the node is
stood where its body is; a node that game code or an animation moved takes its body
along. Rigid bodies push each other. The static bodies they meet are given to the engine
as triangles (the sides of boxes, the cells of height maps) and as bodies that do not
move (spheres), each frame only what is near a rigid body and as it stands in that frame,
so a static body that something moves is solid where it is. Layers and masks work as in
Godot. From C: `rr_rigid_body_get/set_linear_velocity`, `..._angular_velocity`,
`rr_rigid_body_apply_impulse`, `rr_rigid_body_apply_impulse_at`, and for a body that
monitors its contacts `rr_rigid_body_get_contact_count` and `..._get_contact_normal`.

Where it differs from Godot: two frictions are multiplied (Godot takes the smaller), the
bounce of a pair is the larger of the two, a resting body may tremble a little, and a
body that comes in fast may bounce a step's way before the surface. A rigid body does
not meet character bodies, capsules or tile maps, and areas do not report it. No joints,
no sleeping, no continuous collision detection beyond what the engine does by itself.

A `RigidBody2D` is moved the second way by the runtime itself, with gravity, bounce and
friction. It does not turn and does not collide with other bodies.

