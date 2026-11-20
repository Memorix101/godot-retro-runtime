# The demos

What the test game in this repository contains, and how it is played. The island demo is
described in [terrain.md](terrain.md).

## Controls

| Action        | PC                  | Dreamcast            |
|---------------|---------------------|----------------------|
| Menu up/down  | arrow keys, d-pad   | d-pad, stick         |
| Confirm       | Enter, Space        | A, START             |
| Back          | Escape              | B                    |
| Accelerate    | W, Up, A, right trigger | A, right trigger |
| Brake         | S, Down, B, left trigger | B, left trigger |
| Steer         | A/D, Left/Right, d-pad, stick | d-pad, stick |
| Platformer: move | WASD, arrow keys, d-pad, stick | d-pad, stick |
| Platformer: jump | Space, A         | A                    |
| Ball: tilt    | WASD, arrow keys, d-pad, stick | d-pad, stick |
| Racing: gas   | W, Up, A, right trigger | A, right trigger |
| Racing: brake | S, Down, B, left trigger | B, left trigger |
| Racing: steer | A/D, Left/Right, d-pad, stick | d-pad, stick |

The actions come from the project's Input Map (Project Settings > Input Map).

## Platformer demos

Both are reached from the main menu and use assets by Kenney (www.kenney.nl, CC0; the
licence files are in `assets/kenney/`). Only the files the demos use are in the repository.

- **2D** (`scenes/platformer_2d.tscn`, "Pixel Platformer" pack): a scrolling level in one
  `TileMapLayer` whose solid tiles carry their collision in the `TileSet`; coins, spikes
  and the flag are `Area2D` nodes in
  the groups `coin`, `hazard` and `goal`, the animated ones with an `AnimatedSprite2D`.
  The player is a `CharacterBody2D` with a `Camera2D` below it. The scene is written by
  `tools/make_platformer_2d.gd` from a text map; it is a normal scene and can be edited in
  Godot, but running the tool again overwrites it.
- **3D** (`scenes/platformer_3d.tscn`, "Platformer Kit"): the level is put together from
  the small scenes in `scenes/platformer/` (block, platform, coin, spikes, flag). Ground
  pieces are `StaticBody3D` nodes with a box shape and the player is a `CharacterBody3D`;
  coins, spikes and the flag are areas in the groups `coin`, `hazard` and `goal`.
  A platform rides on a `PathFollow3D` (component `PathPatrol`) and carries the player
  across a gap. Crates in the group `enemy` patrol the same way: touching one sends the
  player back to the start, landing on one removes it. A `RayCast3D` below the player
  finds the ground, and a flat `Sprite3D` is put there as a shadow.
  One step gives way under the player and comes back later (component `CrumblePlatform`:
  an area with `body_entered`, two `Timer` nodes and a tween), a spring throws the player
  up to a bonus coin, and a small flag is a checkpoint. The player has the lives the
  autoload `Game` gives it; the HUD shows them with the time of the run and the best
  time, which is kept in a saved game.

## Ball demo

`scenes/ball_roll.tscn` ("Ball Roll" in the main menu) is a course of floors without
railings that hangs in the sky, some 175 m from the start pad to the goal: bridges, a
square with posts, slopes down and up, an S of narrow floors, a trough between two banks
and a drop onto the last floor. A ball with the Platformer Kit's character inside is
rolled over it; 38 coins lie on the way, two small flags are checkpoints, and the goal is
a gate between two flags. Falling off costs a life. The scene is written by
`tools/make_ball_roll.gd` from a list of floors, each a strip from one point to another;
it can be edited in Godot, but running the tool again overwrites it.

- The ball is a `RigidBody3D` with a sphere shape and a physics material without
  friction; the floors are `StaticBody3D` nodes with one box shape each, flat, sloped or
  banked. Falling, running down a slope, glancing off a post and dropping off an edge are
  the runtime's rigid body at work. Its component (`BallRoller`,
  `runtime/game/ball_roll.c`) only adds the push of the stick, as if the floor were leaned
  that way, and caps the speed.
- The camera stays behind the ball and swings round to where the ball is going, the faster
  the ball the quicker; a ball that rolls straight back at it does not turn it, so pulling
  back brakes. It leans with the stick, which is what makes the floor look tilted.
- The runtime's bodies do not turn, so the component turns the ball's shell itself, by
  the way the ball went over the ground. The shell is a translucent, unlit sphere in two
  colours. The rider inside faces the way the ball rolls and plays its walk clip faster
  the faster the ball is (`rr_anim_set_speed_scale`).
- The floors share one material: a tile texture that repeats every two metres, tinted by
  vertex colours, lit by the light bake.

It takes some braking: at full speed the ball needs about 7 m to stop. The run in
`make test` finishes in 30 seconds without hurrying. In Flycast the scene runs at 60 fps
where it was looked at (the start, the first bridge and slope, the first checkpoint); the
draw time there was about 2 ms.

## Racing demo

`scenes/racing.tscn` ("Racing" in the main menu) is a three-lap race against three
opponents, built from Kenney's "Racing Kit" (models in `assets/kenney/racing-kit/`). The
scene is written by `tools/make_racing.gd` from a string of track pieces (`S` straight,
`G` start grid, `L`/`R` corners); it can be edited in Godot, but running the tool again
overwrites it.

- The track's centre line is a `Path3D`. The opponents ride `PathFollow3D` nodes on it
  (component `AiDriver`: a base speed that varies a little, slower when far ahead of
  the player and faster when far behind); the race position compares the distance
  travelled along the path.
- The player is a `CharacterBody3D` (component `RaceCar`) with its own simple car model:
  speed along the heading, grip that pulls the velocity onto the heading, steering that
  grows with speed. The barriers are `StaticBody3D` boxes; hitting one only takes away the
  speed into the wall.
- Laps are counted by four checkpoint areas on a second `Path3D`, which have to be
  passed in order. A `Timer` drives the countdown, another one returns to the menu after
  the finish. The best lap is kept in a saved game.
- The HUD is made of `Label` and `ColorRect` nodes; the sky and the fog come from the
  scene's `WorldEnvironment`.

The kit's models are heavy for a Dreamcast, so the generator keeps the scene lean: the
barriers share one 8-corner mesh with a striped texture instead of the kit's barrier,
opponents further away than 20 m are drawn as a few boxes (`visibility_range_begin/end`
of two `MeshInstance3D` nodes), and trees disappear beyond 100 m. In Flycast the race
runs at 60 fps on the start grid, where all cars and the whole straight are in view (the
rest of the lap was not measured with the PVR backend). With GLdc it was about 40 fps
there and 50 to 60 on the rest of the track.

