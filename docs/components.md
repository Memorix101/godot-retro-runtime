# Writing a component

A component is a C struct with a registration block (`runtime/core/rr_component.h`), plus a
placeholder script in `components/` that declares the same exported fields and signal
handler shells under the same class name. What a component can call is in
`runtime/core/rr_scene.h`: tags, `rr_emit`, input actions, `rr_change_scene`, `rr_quit`,
label text, sound playback, animations (`rr_anim_play`) and particle systems
(`rr_particles_restart`).

The placeholder need not be written by hand. Register the component in C, build the PC
runtime, then **Project > Tools > Generate Missing Placeholder Scripts** (or
`godot --headless --path . --script res://addons/retro_runtime/placeholders_cli.gd`)
writes `components/<name>.gd` for every component the project has no class for:
`extends Node`, the exported fields with their types, a handler shell for every signal
handler. A script that exists is never changed, so default values, doc comments and
narrower types (`Camera3D` for a node field) can be added there. The runtime itself prints
the scripts with `retro --placeholders`.

Examples: `runtime/game/main_menu.c` (button signals, scene change),
`runtime/game/player_car.c` (input, trigger signal with a node argument, label text) and
`runtime/game/test_2d.c` (moving and rotating 2D nodes) and the two platformer files
(character bodies, sprite animations, child nodes, groups). New components are added to the list
in `runtime/game/game.c`. The runtime refuses to load a scene whose component fields do not
match the C struct, or whose connected signal has no handler.

