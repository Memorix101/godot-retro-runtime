# Package format (version 30)

A package (`.rpk`) holds one exported scene, baked for exactly one platform. It is the only
contract between the Godot addon (`addons/retro_runtime/exporter.gd`) and the runtime
(`runtime/core/rr_package.h`, `rr_package.c`). Change all of them and this file together, and
bump the version.

The runtime finds a scene by name: `<data directory>/<scene name>.rpk`, where the scene name
is the file name of the `.tscn` without its extension.

## General rules

- All multi-byte values are stored in the byte order of the target platform. The byte order is
  fixed when the package is baked; the loader never swaps.
- Every table and every asset starts at a file offset that is a multiple of 4. Floats are
  32-bit IEEE 754.
- The runtime reads the whole file into one block and uses the tables in place. Nothing is
  parsed into separate heap objects.
- Strings are referenced by their byte offset into the string table. Indices that mean
  "none" are `0xFFFF` (16-bit) or `0xFFFFFFFF` (32-bit).
- Colours in node data are one u32 with the value `0xRRGGBBAA`.
- The loader checks all offsets, counts and indices once when the package is opened and
  rejects the package with a readable error if anything is out of range.

## Header (136 bytes)

| Offset | Type    | Field        | Meaning                                         |
|-------:|---------|--------------|-------------------------------------------------|
| 0      | char[4] | `magic`      | `"RPKG"`                                        |
| 4      | u8      | `platform`   | 1 PC, 2 Dreamcast, 3 PSP, 4 Wii and GameCube    |
| 5      | u8      | `big_endian` | 0 little-endian, 1 big-endian                   |
| 6      | u16     | `version`    | format version, currently 30                     |
| 8      | u32     | `file_size`  | size of the whole file in bytes                 |
| 12     | table   | `strings`    | string table; count is its size in bytes        |
| 20     | table   | `nodes`      | node table                                      |
| 28     | table   | `node_data`  | type-specific node data; count is its size in bytes |
| 36     | table   | `tags`       | tag table                                       |
| 44     | table   | `comps`      | component table                                 |
| 52     | table   | `fields`     | component field table                           |
| 60     | table   | `conns`      | signal connection table                         |
| 68     | table   | `actions`    | input action table                              |
| 76     | table   | `bindings`   | input binding table                             |
| 84     | table   | `assets`     | asset table                                     |
| 92     | table   | `data`       | asset data region; count is its size in bytes   |
| 100    | u32     | `clear_color` | `0xRRGGBBAA`: what the screen is cleared to    |
| 104    | table   | `pools`      | pool table                                      |
| 112    | table   | `resources`  | resource table                                  |
| 120    | table   | `buses`      | audio bus table                                 |
| 128    | table   | `translations` | translation table; count is its size in bytes, 0 if the project has none |

`clear_color` is the background colour of the scene's `WorldEnvironment`, or the default
clear colour of the Godot project if the scene has none.

A `table` is two u32: the file offset, then the number of entries (or the size in bytes where
noted).

The loader reads `magic`, `platform` and `big_endian` as single bytes first, so a package for
the wrong platform or byte order is reported as such instead of as a version mismatch.

## String table

UTF-8 strings, each terminated by a zero byte. Offset 0 is always the empty string. The last
byte of the table is a zero byte. Identical strings are stored once.

## Node table (72 bytes per node)

Nodes are stored in depth-first pre-order. The root is node 0 and a parent always has a lower
index than its children, so world transforms can be computed in one forward pass.

| Offset | Type      | Field        | Meaning                                              |
|-------:|-----------|--------------|------------------------------------------------------|
| 0      | u32       | `name`       | string offset                                        |
| 4      | u16       | `parent`     | node index, `0xFFFF` for the root                    |
| 6      | u8        | `type`       | see node types                                       |
| 7      | u8        | `flags`      | bit 0 visible, bit 1 current camera, bit 7 part of an autoload |
| 8      | float[12] | `xform`      | local transform: basis X, basis Y, basis Z, origin   |
| 56     | u16       | `tag_first`  | first entry in the tag table                         |
| 58     | u16       | `tag_count`  |                                                      |
| 60     | u16       | `comp_first` | first entry in the component table                   |
| 62     | u16       | `comp_count` |                                                      |
| 64     | u32       | `data`       | offset into the node data section, `0xFFFFFFFF` if the type has none |
| 68     | i16       | `z`          | 2D nodes and controls: `z_index`, added to that of the parents it is relative to |
| 70     | u8        | `canvas_layer` | 1: inside a CanvasLayer                            |
| 71     | u8        | `translate`  | 1: the node's text is a key of the translations      |

2D nodes and controls are drawn level by level: lower `z` first, everything inside a
CanvasLayer after everything outside of one, and in tree order inside a level. A scene may
use up to 16 different levels.

The 3D coordinate system is Godot's: right-handed, Y up, a camera looks along its local -Z.
The three basis vectors are the columns of the 3x3 rotation/scale matrix.

2D nodes use the same field: the origin's X and Y are screen pixels (Y down), the basis holds
the 2D rotation and scale in its X and Y vectors, and basis Z is (0, 0, 1).

Node types without a transform (`Node`, the controls, `AudioStreamPlayer`) store identity. As
in Godot they break transform inheritance: a 3D or 2D node below them starts again from the
origin.

### Node types and their node data

| Value | Type                | Node data                         |
|------:|---------------------|-----------------------------------|
| 0     | `Node`              | none                              |
| 1     | `Node3D`            | none                              |
| 2     | `MeshInstance3D`    | mesh (16 bytes)                   |
| 3     | `Camera3D`          | camera (16 bytes)                 |
| 4     | `Area3D`, `Area2D`  | area (12 bytes)                   |
| 5     | `CollisionShape3D`, `CollisionShape2D` | shape (24 bytes) |
| 6     | `Node2D`            | none                              |
| 7     | `Sprite2D`          | sprite (44 bytes)                 |
| 8     | `Control`           | control (16 bytes)                |
| 9     | `Label`             | label (32 bytes)                  |
| 10    | `Button`, `CheckBox`, `CheckButton`, `TextureButton` | button (100 bytes) |
| 11    | `TextureRect`       | texture rect (24 bytes)           |
| 12    | `AudioStreamPlayer` | audio (40 bytes)                  |
| 13    | `AnimationPlayer`   | animation player (12 bytes)       |
| 14    | `CPUParticles3D`, `CPUParticles2D` | particles (84 bytes) |
| 15    | `StaticBody3D`, `StaticBody2D` | body (72 bytes) |
| 16    | `CharacterBody3D`, `CharacterBody2D` | body (72 bytes) |
| 17    | `AnimatedSprite2D`  | animated sprite (28 bytes)        |
| 18    | `Camera2D`          | 2D camera (68 bytes)              |
| 19    | `Timer`             | timer (8 bytes)                   |
| 20    | `WorldEnvironment`  | environment (44 bytes)            |
| 21    | `TileMapLayer`      | tile map (16 bytes)               |
| 22    | `RayCast3D`, `RayCast2D`, `ShapeCast3D`, `ShapeCast2D` | ray cast (36 bytes) |
| 23    | `Path3D`, `Path2D`  | path (4 bytes)                    |
| 24    | `PathFollow3D`, `PathFollow2D` | path follow (16 bytes) |
| 25    | `ColorRect`         | colour rect (20 bytes)            |
| 26    | `ProgressBar`       | progress bar (48 bytes)           |
| 27    | `NinePatchRect`     | nine patch (60 bytes)             |
| 28    | `AudioStreamPlayer2D`, `AudioStreamPlayer3D` | audio (40 bytes) |
| 29    | `Sprite3D`          | 3D sprite (48 bytes)              |
| 30    | `Label3D`           | 3D label (28 bytes)               |
| 31    | `RigidBody3D`, `RigidBody2D` | body (72 bytes)          |
| 32    | `HSlider`, `VSlider` | slider (72 bytes)                |
| 33    | `Parallax2D`        | parallax (60 bytes)               |
| 34    | `Panel`, `PanelContainer`, `HSeparator`, `VSeparator` | panel (40 bytes) |
| 35    | `TextureProgressBar` | texture progress (56 bytes)      |
| 36    | `ScrollContainer`   | scroll (76 bytes)                 |
| 37    | `RichTextLabel`     | rich text (44 bytes)              |

A `CanvasLayer` is stored as a plain `Node` (type 0), a `Marker3D` as a `Node3D` and a
`Marker2D` as a `Node2D`.

Mesh: `u32 mesh` (asset index), `float range_begin`, `float range_end` (the visibility
range: the mesh is drawn while the camera is at least `range_begin` and at most `range_end`
away from the node; 0 means no limit), `u32 skin` (asset index of a skin asset for a mesh
that bones deform, `0xFFFFFFFF` for a mesh that keeps its shape), `u8 tinted`, three bytes
of padding, `u32 color`. A tinted mesh (its `material_override` is local to the scene) can
change its albedo while the game runs: its vertex colours were baked with a white albedo,
`color` (RGBA) is the albedo it starts with, and the runtime keeps a copy of the mesh asset
for the node, whose vertex colours it multiplies with the albedo. A tinted mesh has no LOD
levels.

Camera: `float fov` (vertical, degrees), `float near`, `float far`, `float size` (an orthogonal
camera: the height of its view in world units; 0 for a perspective camera, and then the
field of view counts).

Area: `u32 layer`, `u32 mask`, `u8 monitoring`, `u8 monitorable`, `u8[2] pad`. Area A is told
about area B when A is monitoring, B is monitorable and `A.mask & B.layer` is not zero.

Shape: `u32 kind` (see below), `float[3] size`, `u8 disabled`, `u8 one_way` (2D, below a static
body: it stops only what comes down onto it), `u8[2] pad`, `u32 map` (asset
index of a height map for kind 4, else 0xFFFFFFFF). A shape is always
a direct child of its area or body. A 2D shape is stored as a box that is 0.5 thick along Z
(`RectangleShape2D`), as a sphere (`CircleShape2D`) or as a capsule; with all 2D nodes at
Z = 0 that gives the 2D result.

Animated sprite: `u32 frames` (asset index of a sprite frames asset), `u32 color`,
`float[2] offset`, `float speed_scale`, `u16 animation` (index of the animation shown at
the start), `u8 centered`, `u8 flip_h`, `u8 flip_v`, `u8 playing` (1 if that animation
plays right away, from `autoplay`), `u8[2] pad`. Each frame is centred by its own size.

Timer: `float wait_time` (seconds), `u8 one_shot`, `u8 autostart`, `u8[2] pad`. Emits
`timeout`.

Environment: `u32 fog_color`, `float fog_density`, `float fog_begin`, `float fog_end`,
`u8 fog_enabled`, `u8 fog_mode` (0 exponential: fog = 1 - exp(-density * depth); 1 depth:
linear from none at `fog_begin` to all at `fog_end`), `u8[2] pad`. The background colour
is in the header, and the ambient light is part of the baked vertex colours.
Then `u32[6] sky`: the asset indices of six textures, or `0xFFFFFFFF` six times without a
sky. They are the faces of a cube around the camera in the order +X, -X, +Y, -Y, +Z, -Z.
Seen from inside, right and up on the pictures are: +X: -Z, +Y; -X: +Z, +Y; +Y: +X, +Z;
-Y: +X, -Z; +Z: +X, +Y; -Z: -X, +Y.

Audio: `u32 sound` (asset index of a sound, or of music for type 12), `float volume`
(linear), `u8 autoplay`, `u8 loop`, `u8 mode` (0 plain, 1 heard in the 2D world, 2 in the
3D world), `u8 model` (3D: 0 inverse distance, 1 inverse square, 2 logarithmic, 3 none),
`float unit_size`, `float max_distance` (0 = no limit), `float max_gain` (linear),
`float attenuation` (2D: exponent of the fall-off), `float pan_strength`, `float pitch`
(speed of playback, 1.0 as recorded; always 1.0 for music), `u16 bus` (index in the audio
bus table), `u16 pad`. A player that
does not loop emits `finished`.

3D sprite: `u32 texture`, `u32 color`, `float[4] region` (the sprite sheet in pixels of the
image), `float[2] offset` (pixels), `float pixel_size` (size of a pixel in the world),
`u16 hframes`, `u16 vframes`, `u16 frame`, `u8 centered`, `u8 flip_h`, `u8 flip_v`,
`u8 billboard` (0 off, 1 faces the camera, 2 turns around Y only), `u8 axis` (the axis the
quad faces: 0 X, 1 Y, 2 Z), `u8 pad`.

3D label: `u32 text`, `u32 font` (asset index), `u32 color`, `float pixel_size`,
`float[2] offset`, `u8 h_align`, `u8 v_align` (0 begin, 1 centre, 2 end, around the node's
origin), `u8 billboard`, `u8 pad`.

Body (static, character and rigid): `u32 layer`, `u32 mask`, `u8 two_d`, `u8[3] pad`,
`float bounce`, `float friction` (of the physics material; 0 and 1 without one), for
rigid bodies `float[3] gravity` (the acceleration, with the body's gravity scale),
`float mass`, `float linear_damp`, `float[3] velocity` (at the start), and, because a
rigid body turns, `float angular_damp`, `float[3] angular_velocity` (at the
start: axis times radians per second, in the directions of the world; a 2D body has Z only), `u8 lock_angular`
(bit 0, 1, 2: the body does not turn around the world's X, Y, Z; `lock_rotation` is all
three, and for a 2D body it is bit 2), `u8 contacts` (how many contacts it reports per step, from `contact_monitor` and
`max_contacts_reported`; 0 for none), `u8[2] pad`. What a kind of body does not have is 0
(mass 1). `two_d` tells where the floor is: below the body is -Y in 3D and +Y in 2D.

Shape kinds: 1 box (`size` = half extents), 2 sphere (radius in `size[0]`), 3 capsule
(radius in `size[0]`, half the length of its line along Y in `size[1]`; only for
character and rigid bodies), 4 height map (`HeightMapShape3D`; `size` is 0 and `map` names
the heights; only below a static body, and never rotated: the world transform of the
shape is a position and three positive scales).

Ray cast: `float[3] target` (end of the ray in the node's space, Z is 0 in 2D),
`u32 mask`, `u8 enabled`, `u8 collide_areas`, `u8 collide_bodies`, `u8 exclude_parent`,
`u32 shape`, `float[3] shape_size`. `shape` is 0 for a ray. For a ShapeCast it is the kind of
the shape that is moved along the line (1 box, 2 sphere, 3 capsule) with its size as for a
collision shape; the node then reports the first thing the shape touches and how far along
the line that is.

Path: `u32 curve` (asset index of a curve asset).

Path follow: `float progress` (distance along the curve of the parent path at the start),
`float h_offset`, `float v_offset`, `u8 rotation` (0 none, 1 around Y only, 2 looking along
the curve with +Y up, 3 with the curve's up vectors; in 2D 0 or 2), `u8 loop`, `u8 two_d`,
`u8 pad`. The parent of a path follow node is a path node.

Colour rect: `float[4] rect`, `u32 color`.

Progress bar: `float[4] rect`, `float min`, `float max`, `float value`, `u32 bg_color`,
`u32 fill_color`, `u32 font` (asset index, `0xFFFFFFFF` without the percentage),
`u32 text_color`, `u8 fill_mode` (0 begin to end, 1 end to begin, 2 top to bottom,
3 bottom to top), `u8 show_percentage`, `u8[2] pad`.

Nine patch: `float[4] rect`, `u32 texture`, `u32 color`, `float[4] region` (pixels of the
image), `float[4] margin` (left, top, right, bottom: the size of the border patches in
pixels), `u8 draw_center`, `u8[3] pad`.

Tile map: `u32 tiles` (asset index of a tile map asset), `u32 color`, `u32 layer`,
`u32 mask` (collision layer and mask of its solid boxes; character bodies stop at them
like at static bodies).

2D camera: `float[4] limit` (left, top, right, bottom in world coordinates),
`float[2] offset`, `float[2] zoom`, `u8 enabled`, `u8 top_left` (1: the node is the top
left corner of the view, 0: its centre), `u8 smoothing`, `u8 limit_smoothed`,
`float[4] drag_margin` (left, top, right, bottom), `float[2] drag_offset`,
`float smoothing_speed`, `u8 drag_h`, `u8 drag_v`, `u8[2] pad`. These are the properties of
Godot's Camera2D, and the runtime works the view out from them the way Godot does
(`rr_canvas.c`). The first enabled camera in tree order decides what the 2D nodes show:
screen = (world - view origin) * zoom, with the origin rounded to whole screen pixels.
Controls are not moved by it.

Parallax (60 bytes; Parallax2D): `float[2]` each for `scroll_scale`, `scroll_offset`,
`repeat_size`, `autoscroll`, `limit_begin`, `limit_end` and `screen_offset`, then
`u16 repeat_times`, `u8 follow_viewport`, `u8 ignore_camera_scroll`: the node's properties
as the scene file has them. The runtime sets the node's position every frame from where
the view is, and draws the sprites, tile maps and particles below it `repeat_times` more
times, one `repeat_size` apart.

Sprite (44 bytes): `u32 texture` (asset index), `u32 color`, `float[2] offset`,
`float[4] region` (x, y, width, height in pixels of the image: the sprite sheet, which is
the node's region or the whole texture), `u16 hframes`, `u16 vframes` (the sheet is a grid
of that many cells), `u16 frame` (the cell shown at the start, counted row by row),
`u8 centered`, `u8 flip_h`, `u8 flip_v`, `u8[3] pad`. The quad has the size of one cell and
starts at `offset`, moved back by half a cell if `centered`.

All control types start with `float[4] rect`: x, y, width and height in screen pixels, as
Godot laid the control out at the target's screen size. The rectangle is absolute; controls
do not move with their parent at runtime.

Label: `rect`, `u32 text` (string offset), `u32 font` (asset index), `u32 color`,
`u8 h_align`, `u8 v_align` (0 begin, 1 center, 2 end), `u8 wrap`, `u8 pad`. `wrap` is
Godot's autowrap mode: 0 off, 1 arbitrary (lines break anywhere), 2 word, 3 word smart (as
word, but a word wider than the line is broken too). The runtime breaks the lines at the
width of the rectangle when it draws the text.

Button (100 bytes; also CheckBox, CheckButton and TextureButton):

```
float[4]  rect
u32       text                 string offset
u32       font                 asset index; 0xFFFFFFFF for a button without text (TextureButton)
u32       text_color, focus_text_color
u32       bg_color             fill; alpha 0 for none
u32       focus_color          outline when focused
u16[4]    neighbors            node index of the button or slider the focus moves to for
                               left, top, right, bottom; 0xFFFF for none
u32       disabled_text_color, pressed_text_color
u32       disabled_color, pressed_color      the fill in these states
u32       icon, icon_pressed   texture assets or 0xFFFFFFFF: a picture left of the text (the
                               icon of a Button, the box of a CheckBox, the switch of a
                               CheckButton); icon_pressed while the button is pressed
u32[4]    textures             TextureButton: texture asset for normal, pressed, focused,
                               disabled; 0xFFFFFFFF for a state without one
float     margin               between the edges of the button and what is inside
float     separation           between the icon and the text
u8        h_align              of the text: 0 begin, 1 center, 2 end
u8        toggle               1: accept switches the button on and off
u8        disabled, pressed    the state at the start
```

The neighbours are worked out with every button enabled, so that a button the game enables
later can be reached; the runtime passes over the ones that are disabled or hidden. For a
TextureButton with stretch mode Keep, `rect` has the size of its normal texture.

Slider (72 bytes; HSlider and VSlider):

```
float[4]  rect
float     min, max
float     step                 what one press of a direction changes the value by
float     value                at the start
float     track                thickness of the track in pixels
u32       track_color, fill_color, focus_fill_color
u32       grabber, focus_grabber, disabled_grabber     texture assets
u16[4]    neighbors            as for buttons
u8        vertical             1 for a VSlider: the value grows upwards
u8        editable
u8[2]     pad
```

Panel (40 bytes; Panel, PanelContainer, HSeparator, VSeparator): `rect`, `u32 color` (the
fill; alpha 0 for none), `u32 border_color`, `float[4] border` (width of the border on the
left, top, right and bottom, inside the rectangle). For a separator the rectangle is its
line and there is no border.

Texture progress (56 bytes; TextureProgressBar): `rect`, `float min`, `float max`,
`float value`, `u32 under`, `u32 progress`, `u32 over` (texture assets, `0xFFFFFFFF` for
none), `u32 tint_under`, `u32 tint_progress`, `u32 tint_over`, `u8 fill_mode` (0 left to
right, 1 right to left, 2 top to bottom, 3 bottom to top), `u8[3] pad`. The textures are
drawn at their own size from the rectangle's top left corner.

Scroll (76 bytes; ScrollContainer): `rect`, `float[2] content` (size of what is scrolled),
`float[2] start` (scroll position in the scene, horizontal and vertical), `float[4] bar_h`,
`float[4] bar_v` (rectangles of the scroll bars; no width: not shown), `u32 bar_color`,
`u32 grabber_color`, `u8 scroll_h`, `u8 scroll_v` (1: scrolls along that axis),
`u8 follow_focus`, `u8 pad`. The baked rectangles of the controls below it are where they
were with the container scrolled to `start`; the runtime moves them by how far it is
scrolled away from that and draws only what is inside `rect`.

Rich text (44 bytes; RichTextLabel): `rect`, `u32 text`, `u32[4] fonts` (font assets for
normal, bold, italic, bold and italic; `0xFFFFFFFF`: the normal one is used), `u32 color`,
`u8 bbcode` (0: the text is drawn as it is), `u8 wrap` (as for a label), `u8 v_align`,
`u8 pad`. The runtime reads the tags while it draws: `[b]`, `[i]`, `[color=#rrggbb]` or
`[color=#rrggbbaa]`, `[center]`, `[right]`, `[left]`, `[lb]` and `[rb]` for the brackets.
The exporter turns colour names into hex values and refuses other tags.

Texture rect: `rect`, `u32 texture` (asset index), `u32 color`. The texture is stretched over
the rectangle.

Animation player: `u32 animations` (asset index of an animation asset), `float speed_scale`,
`float blend_time` (seconds a clip takes to take over from the one that plays when it is
started; Godot's default blend time). The node itself has
no transform.

Particles (84 bytes), a system of camera-facing quads that live in world space:

| Offset | Type     | Field             | Meaning                                              |
|-------:|----------|-------------------|------------------------------------------------------|
| 0      | u32      | `texture`         | asset index, `0xFFFFFFFF` for plain coloured quads   |
| 4      | u32      | `amount`          | particles per lifetime, 1 to 512                     |
| 8      | float    | `lifetime`        | seconds                                              |
| 12     | float    | `explosiveness`   | 0: births spread over the lifetime, 1: all at once   |
| 16     | float[3] | `direction`       | in the node's space, unit length                     |
| 28     | float    | `spread`          | half angle of the cone around the direction, radians |
| 32     | float[3] | `gravity`         | world space                                          |
| 44     | float[2] | `velocity`        | minimum and maximum start speed                      |
| 52     | float[2] | `size`            | width and height of the quad                         |
| 60     | float[2] | `scale`           | minimum and maximum factor on the size               |
| 68     | u32[2]   | `color`           | at birth and at death, blended in between            |
| 76     | float    | `emission_radius` | 0: born at the node, else around it                  |
| 80     | u8       | `one_shot`        | 1: stops after one lifetime                          |
| 81     | u8       | `emitting`        | 1: emits from the start                              |
| 82     | u8       | `two_d`           | 1: a `CPUParticles2D`: units are pixels, Z is 0, drawn with the 2D nodes |
| 83     | u8       | `pad`             | 0                                                    |

Particle `i` of a system is born at `lifetime * (1 - explosiveness) * i / amount` inside each
emission cycle, which lasts one lifetime.

## Tag table (4 bytes per entry)

One u32 string offset per entry. A node's tags (its Godot groups) are the `tag_count` entries
starting at `tag_first`, sorted by name.

## Component table (12 bytes per component)

| Offset | Type | Field         | Meaning                                              |
|-------:|------|---------------|------------------------------------------------------|
| 0      | u32  | `name`        | string offset; the name the C component registers    |
| 4      | u16  | `node`        | index of the node that owns the component            |
| 6      | u16  | `field_count` |                                                      |
| 8      | u32  | `field_first` | first entry in the field table                       |

Components are sorted by owning node, so each node's components are one contiguous range.

## Field table (12 bytes per field)

| Offset | Type  | Field   | Meaning                          |
|-------:|-------|---------|----------------------------------|
| 0      | u32   | `name`  | string offset                    |
| 4      | u8    | `type`  | see field types                  |
| 5      | u8[3] | `pad`   | 0                                |
| 8      | u32   | `value` | 32 bits, meaning depends on type |

Field types:

| Value | Type  | `value`                                             |
|------:|-------|-----------------------------------------------------|
| 1     | float | the float                                           |
| 2     | int   | signed 32-bit integer                               |
| 3     | bool  | 0 or 1                                              |
| 4     | node  | node index, `0xFFFFFFFF` if not assigned            |
| 5     | scene | string offset of a scene name, empty if not assigned |
| 6     | packed scene | index into the pool table, `0xFFFFFFFF` if not assigned |
| 7     | resource | index into the resource table, `0xFFFFFFFF` if not assigned |

## Signal connection table (12 bytes per connection)

| Offset | Type | Field    | Meaning                                  |
|-------:|------|----------|------------------------------------------|
| 0      | u16  | `source` | index of the emitting node               |
| 2      | u16  | `target` | index of the receiving node              |
| 4      | u32  | `signal` | string offset of the signal name         |
| 8      | u32  | `method` | string offset of the handler name        |

These are the connections made in the editor. When a scene is created, the runtime looks up
`method` among the signal handlers of the target node's components and refuses the scene if
there is none.

## Pool table (12 bytes per pool)

One entry per assigned `PackedScene` field of a component: the instances the runtime
hands out with `rr_instantiate`.

| Offset | Type | Field            | Meaning                                    |
|-------:|------|------------------|--------------------------------------------|
| 0      | u32  | `name`           | string offset of the scene's name          |
| 4      | u16  | `first_node`     | index of the root of the first instance    |
| 6      | u16  | `node_count`     | number of nodes of one instance            |
| 8      | u16  | `instance_count` | the value of the `<field>_max` export      |
| 10     | u16  | `pad`            | 0                                          |

The instances are ordinary nodes, with their components and signal connections in the
ordinary tables. They are children of the root, stored one after the other from
`first_node` on, each a whole subtree of `node_count` nodes. The runtime keeps them out of
the tree until they are asked for, and puts an instance back into the state stored here
every time it is handed out.

## Resource table (12 bytes per resource)

The custom resources (scripts with a `class_name` that extend `Resource`) that components
or other resources refer to. A resource that is used in several places is stored once.

| Offset | Type | Field         | Meaning                                  |
|-------:|------|---------------|------------------------------------------|
| 0      | u32  | `name`        | string offset of the class name          |
| 4      | u16  | `field_count` | number of exported values                |
| 6      | u16  | `pad`         | 0                                        |
| 8      | u32  | `field_first` | first entry in the field table           |

The runtime makes one C struct per entry when the scene is created and refuses the scene
if the class is not registered or its fields do not match.

## Audio bus table (12 bytes per bus)

The buses of the project's bus layout (the Audio tab of the editor), in its order; bus 0 is
Master. Every package of a project holds the same table. Up to 16 buses; a layout with
effects, solo or bypass is refused by the exporter.

| Offset | Type  | Field    | Meaning                                                      |
|-------:|-------|----------|--------------------------------------------------------------|
| 0      | u32   | `name`   | string offset                                                |
| 4      | float | `volume` | linear, 1.0 = unchanged                                      |
| 8      | u16   | `send`   | index of the bus it plays into, always a lower one; `0xFFFF` for Master |
| 10     | u8    | `mute`   | 1: silent                                                    |
| 11     | u8    | `pad`    | 0                                                            |

A player is heard at its own volume times the volume of its bus and of every bus on the way
to Master. The runtime keeps volume and mute per bus name once it has seen a bus, so what
the game sets stays across scene changes.

## Translation table

The messages of the project's translations (Project Settings > Localization), the same in
every package of a project. All entries are string offsets.

```
u32       locale_count
u32       key_count
u32       fallback                     index of the locale that is used where the wanted
                                       one has no message
u32       locales[locale_count]        locale names: "en", "de", "pt_BR"
u32       keys[key_count]              sorted by their bytes, as strcmp does
u32       messages[locale_count * key_count]   per locale, in the order of the keys; 0: none
```

A node with `translate` set shows the message of its text in the locale that is set, or
the text itself if it is no key. The locale is the language of the machine until the game
sets another one; a locale of the same language will do ("de" for "de_AT").

## Input action table (12 bytes per action)

| Offset | Type  | Field           | Meaning                                    |
|-------:|-------|-----------------|--------------------------------------------|
| 0      | u32   | `name`          | string offset                              |
| 4      | u16   | `binding_first` | first entry in the binding table           |
| 6      | u16   | `binding_count` |                                            |
| 8      | float | `deadzone`      | the action counts as pressed from this strength on |

The project's Input Map, plus the built-in `ui_left`, `ui_up`, `ui_right`, `ui_down`,
`ui_accept` and `ui_cancel`. Every package carries the whole table.

## Input binding table (4 bytes per binding)

| Offset | Type | Field  | Meaning                                   |
|-------:|------|--------|-------------------------------------------|
| 0      | u8   | `kind` | 1 key, 2 pad button, 3 pad axis           |
| 1      | i8   | `sign` | axes only: -1 or 1, the direction that counts |
| 2      | u16  | `code` | platform-specific                         |

Codes per platform:

| Platform  | Key          | Pad button              | Pad axis                                         |
|-----------|--------------|-------------------------|--------------------------------------------------|
| PC        | raylib key   | raylib `GamepadButton`  | raylib `GamepadAxis`                             |
| Dreamcast | not present  | KallistiOS `CONT_*` mask | 0 stick X, 1 stick Y, 2 left trigger, 3 right trigger |
| PSP       | not present  | PSP SDK `PSP_CTRL_*` mask | 0 stick X, 1 stick Y, 2 the L button, 3 the R button |
| Wii       | not present  | libogc `PAD_BUTTON_*` mask (a GameCube pad) | 0 stick X, 1 stick Y, 2 left trigger, 3 right trigger |

Events for inputs a platform does not have are left out by the baker.

## Asset table (16 bytes per asset)

| Offset | Type  | Field    | Meaning                                         |
|-------:|-------|----------|-------------------------------------------------|
| 0      | u32   | `name`   | string offset, for diagnostics                  |
| 4      | u8    | `type`   | 1 mesh, 2 texture, 3 font, 4 sound, 5 animation, 6 sprite frames, 7 tile map, 8 curve, 9 music, 10 height map, 11 skin |
| 5      | u8[3] | `pad`    | 0                                               |
| 8      | u32   | `offset` | file offset of the data, inside the data region |
| 12     | u32   | `size`   | size of the data in bytes                       |

### Mesh asset

Offsets inside a mesh are relative to the start of the mesh data.

```
u32      surface_count
u32      lod_count                    simpler versions of the mesh, 0 for none
u32      lods_off                     offset of the LOD table (lod_count entries of 8 bytes)
float[3] center                       bounding sphere in the mesh's own space
float    radius
Surface  surfaces[surface_count]      24 bytes each
...      strip length arrays, vertex arrays, then the LOD table
```

The bounding sphere encloses all vertices of all surfaces. The runtime transforms it with
the node and skips the mesh when the sphere is outside the camera's view volume.

LOD entry (8 bytes), from fine to coarse: `float edge`, `u32 mesh`. `mesh` is the asset
index of a mesh written before this one, with no LOD levels of its own: the whole mesh at
that level of Godot's automatic mesh LOD (each surface at its level of that number, or its
last one). `edge` is Godot's LOD edge length (the error of the level, in the mesh's units;
the largest of the surfaces') divided by the platform's
`rendering/mesh_lod/lod_change/threshold_pixels`, which the exporter reads with the
`.<platform>` override (`.dreamcast` and so on) where there is one. The runtime picks the
coarsest level with `edge <= distance * lod_multiplier / (scale * screen_width)`, which is
Godot's rule with the threshold already in: `distance` from the camera to the nearest point
of the bounding sphere (an orthogonal camera: 1), `lod_multiplier` the width of the view at
distance 1 (orthogonal: half the width of the view), `scale` the node's largest axis scale.
Inside the sphere the full mesh is drawn.

Surface (24 bytes):

| Offset | Type | Field          | Meaning                                              |
|-------:|------|----------------|------------------------------------------------------|
| 0      | u32  | `texture`      | asset index of a texture stored before the mesh, `0xFFFFFFFF` for none |
| 4      | u8   | `blend`        | 0 opaque, 1 punch-through, 2 translucent             |
| 5      | u8   | `flags`        | 0 (bit 0 reserved for the environment map)           |
| 6      | u16  | `pad`          | 0                                                    |
| 8      | u32  | `strip_count`  |                                                      |
| 12     | u32  | `vertex_count` | sum of all strip lengths                             |
| 16     | u32  | `strips_off`   | u16 length per strip, padded to a multiple of 4 bytes |
| 20     | u32  | `verts_off`    | vertices of all strips, one strip after the other    |

Geometry is stored as non-indexed triangle strips. Each strip has at least 3 vertices. The
first triangle of a strip is counter-clockwise when seen from its front side, and the winding
alternates along the strip in the usual way.

Lighting is baked into the vertex colours by the exporter (see [scenes.md](scenes.md#light-bake)), together with the
material's albedo colour. A surface with a texture is drawn with the texture colour multiplied
by the vertex colour. Its texture coordinates are already scaled to the image inside the
stored texture (see "Texture asset"), so the runtime uses them as they are.

Vertex layout, PC (24 bytes): `float[3] position`, `float[2] uv`, `u8[4] r, g, b, a`.

Vertex layout, Dreamcast (24 bytes): `float[3] position`, `float[2] uv`, `u32` colour as
`0xAARRGGBB` (PVR packed colour).

Vertex layout, PSP (24 bytes): `float[2] uv`, `u8[4] r, g, b, a`, `float[3] position`, the
order the PSP's graphics chip reads a vertex in. Code that works on any platform finds the
position with `rr_vertex_position_offset()`.

Vertex layout, Wii and GameCube (24 bytes): `float[3] position`, `u32` colour as
`0xRRGGBBAA`, `float[2] uv`.

### Texture asset

| Offset | Type  | Field            | Meaning                                    |
|-------:|-------|------------------|--------------------------------------------|
| 0      | u16   | `width`          | stored width                               |
| 2      | u16   | `height`         | stored height                              |
| 4      | u16   | `content_width`  | width of the image inside the stored pixels |
| 6      | u16   | `content_height` |                                            |
| 8      | u8    | `format`         | see below                                  |
| 9      | u8    | `filter`         | 0 linear, 1 nearest                        |
| 10     | u8[2] | `pad`            | 0                                          |
| 12     |       | pixels           | rows from top to bottom                    |

| Value | Format   | Platform  | Pixel                                         |
|------:|----------|-----------|-----------------------------------------------|
| 1     | RGBA8888 | PC        | 4 bytes r, g, b, a                            |
| 2     | RGB565   | Dreamcast | u16, for images without alpha                 |
| 3     | ARGB1555 | Dreamcast | u16, for images whose alpha is fully on or off |
| 4     | ARGB4444 | Dreamcast | u16, for everything else                      |
| 5     | BGR565   | PSP       | u16 with red in the low bits, for images without alpha |
| 6     | ABGR1555 | PSP       | u16, for images whose alpha is fully on or off |
| 7     | ABGR4444 | PSP       | u16, for everything else                      |
| 8     | GX RGB565 | Wii      | u16, for images without alpha                 |
| 9     | GX RGB5A3 | Wii      | u16: top bit set, 5 bits a colour; or 3 bits of alpha and 4 bits a colour |
| 10    | GX IA8   | Wii       | u16: alpha in the high byte, grey in the low one; for grey images with alpha |

On the PC the stored size is the image size. On the Dreamcast it is the next power of two
(at least 8, at most 1024); the image sits in the top left corner and its last row and column
are repeated into the padding. Texture coordinates of the image run from 0 to
`content / stored`. Dreamcast textures are stored row by row, not twiddled. The PSP stores
its textures the same way, with at most 512 pixels a side; its runtime swizzles them when
they are loaded. The Wii's textures are padded the same way (at most 1024) but stored in
blocks of 4 x 4 pixels: block after block in rows, and inside a block row after row.

The filter belongs to the texture asset. An image that is used with both filters is stored
twice. There are no mipmaps.

### Font asset

A font at one size: an atlas texture and a glyph table.

```
u32    texture        asset index of the atlas; it is always stored before the font
float  line_height
float  ascent         distance from the top of a line to the baseline
u32    glyph_count
Glyph  glyphs[glyph_count]    20 bytes each, sorted by codepoint
```

Glyph: `u32 codepoint`, `u16 x, y, w, h` (place in the atlas, in pixels; w and h are 0 for
glyphs without pixels, like the space), `i16 off_x, off_y` (from the pen position on the
baseline to the top left corner of the glyph), `float advance`.

The exporter bakes ASCII (32 to 126) and the Latin-1 supplement (160 to 255) with Godot's
text server. The atlas is white with the glyph coverage in the alpha channel.

### Animation asset

All clips of one AnimationPlayer. Tracks refer to nodes by index, so the asset belongs to that
player. Offsets are relative to the start of the asset.

```
u32   clip_count
u32   autoplay        clip index, 0xFFFFFFFF for none
Clip  clips[clip_count]       20 bytes each, sorted by name
...   track and key arrays
```

Clip: `u32 name` (string offset), `float length` (seconds), `u8 loop`, `u8[3] pad`,
`u32 track_count`, `u32 tracks_off`.

Track (12 bytes): `u16 node`, `u8 kind` (1 position, 2 rotation, 3 scale, 4 visible,
5 2D position, 6 2D rotation, 7 2D scale, 8 sprite frame, 9 value, 10 method call),
`u8 interp` (0 nearest, 1 linear), `u32 key_count` (at least 1), `u32 keys_off`.

Key (20 bytes): `float time`, `float[4] value`. Position and scale use x, y, z; rotation is a
quaternion x, y, z, w; visible is 0 or 1 in the first value. The 2D tracks use x, y or one
angle in radians; frame and value (the value of a progress bar, the progress of a path
follower) use the first value. A method key holds the string offset of the handler's name
as a number in the first value; the handler is called when the clip passes the key.
Keys are sorted by time. Before
the first key and after the last one a track holds that key's value; a looping clip does not
blend from its last key back to its first, so the exporter demands keys at both ends.

A track replaces one part of the node's local transform (or its visible flag) and leaves the
other parts as they are.

### Sprite frames asset

Godot's `SpriteFrames`. Offsets are relative to the start of the asset.

```
u32         anim_count                at least 1
SpriteAnim  anims[anim_count]         20 bytes each, sorted by name
...         frame arrays
```

SpriteAnim (20 bytes): `u32 name` (string offset), `float fps`, `u8 loop`, `u8[3] pad`,
`u32 frame_count` (at least 1), `u32 frames_off`.

Frame (24 bytes): `u32 texture` (asset index of a texture stored before this asset),
`float[4] region` (x, y, width, height in pixels of the image, the whole image or the
region of an `AtlasTexture`), `float duration` (in units of 1 / fps). A frame is shown for
`duration / (fps * speed_scale)` seconds.

### Tile map asset

The cells of one `TileMapLayer`. Offsets are relative to the start of the asset.

```
u32       texture          asset index of a texture stored before this asset
float[2]  tile_size        size of a cell, and of the picture of a tile
u32       cell_count
u32       box_count
u32       cells_off        Cell cells[cell_count], 12 bytes each, sorted by y, then x
u32       boxes_off        Box boxes[box_count], 20 bytes each
u32       anim_count
u32       anims_off        Animation anims[anim_count], 12 bytes each
```

Cell: `i16 x`, `i16 y` (cell coordinates), `u16 u`, `u16 v` (top left corner of the tile's
picture in pixels of the image), `u8 flags`, `u8 pad`, `u16 anim` (index of the tile's
animation, `0xFFFF` for a tile that stands still). The cell covers `x * tile_size` to
`(x + 1) * tile_size` in the node's space. `flags`: 1 flipped horizontally, 2 flipped
vertically, 4 transposed (X and Y of the picture swapped). As in Godot the transpose comes
first, so a quarter turn to the right is 4 + 1.

Box: `float x, y, w, h`, a solid rectangle in the node's space, and `u32 one_way` (1: it
stops only what comes down onto it). The exporter makes them from the collision polygons of
the tiles (rectangles only, turned and flipped with the tile) and joins neighbours in a
row.

Animation: `u32 frame_count`, `u32 frames_off` (relative to the start of the asset),
`float duration` (seconds of all frames together; then it starts again). Frame: `u16 u`,
`u16 v` (the picture, as for a cell) and `float end`, the time at which it ends, counted
from the start of the animation. All tiles run on the clock of the scene.

### Skin asset

How the bones of a skeleton deform one mesh. The asset belongs to one mesh node; the bones
are nodes of the scene. Offsets are relative to the start of the asset.

```
u32       bone_count       1 to 256
u32       vertex_count     the vertices of all surfaces of the mesh, in their order
u32       bones_off        u16 bones[bone_count]: node index of each bone
u32       binds_off        Transform binds[bone_count], 48 bytes each: from the mesh's
                           space at rest into the space of the bone
u32       influences_off   Influence influences[vertex_count], 8 bytes each
```

Influence: `u8[4] bone` (indices into `bones`), `u8[4] weight` (they add up to 255; 0 for
a slot that is not used, and the used ones come first). A vertex at `p` in the mesh asset
is drawn at the sum over its bones of weight / 255 x (world transform of the bone node x
bind x `p`), taken back into the space of the mesh node. The runtime keeps a copy of the
mesh asset per mesh node with a skin and writes the positions into it once per frame.

### Music asset

Music is not stored in the package. The exporter writes it as a WAV file (16-bit PCM,
stereo, 44100 Hz for the PC, the PSP and the Wii and 22050 Hz for the Dreamcast;
little-endian on every platform, as WAV files are) into the directory of the
package, and the runtime streams it from there.

```
u32       file             string offset of the file name, like "music.wav"
float     length           seconds
```

### Height map asset

The ground of a `HeightMapShape3D`: a grid of heights, as Godot stores it.

```
u16       width            points along X, at least 2
u16       depth            points along Z, at least 2
float     min              the lowest height
float     max              the highest height
float     heights[width * depth]   row by row along X
```

The points are one unit apart in the shape's own space and centred on its origin: point
(x, z) is at `(x - (width - 1) / 2, heights[z * width + x], z - (depth - 1) / 2)`. The
node's scale makes that metres. Every cell between four points is two triangles, split
along the diagonal from point (x + 1, z) to point (x, z + 1). The ground is solid all the
way down.

### Curve asset

The curve of a path, sampled by the exporter at equal distances along it.

```
u32       point_count      at least 2
float     spacing          distance between two neighbours along the curve
float     length           (point_count - 1) * spacing
Point     points[point_count]
```

Point (24 bytes): `float[3] pos` (in the path's space, Z is 0 in 2D), `float[3] up` (the
up direction of a 3D curve at that point, +Y if the curve has none).

### Sound asset

| Offset | Type | Field          | Meaning                                |
|-------:|------|----------------|----------------------------------------|
| 0      | u32  | `sample_rate`  | in Hz                                  |
| 4      | u32  | `sample_count` |                                        |
| 8      | u8   | `format`       | 1 PCM16 (PC, PSP, Wii), 2 Yamaha 4-bit ADPCM (Dreamcast) |
| 9      | u8   | `channels`     | 1 or 2                                 |
| 10     | u16  | `pad`          | 0                                      |
| 12     | u32  | `data_size`    | bytes of sample data that follow       |

PCM16 samples are signed and in the platform's byte order. ADPCM is the format the AICA plays
directly: two samples per byte, low nibble first.
