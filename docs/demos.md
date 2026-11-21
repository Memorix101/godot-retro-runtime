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
| Stunt track   | gas, brake and steering as in the racing demo; the brake held at a standstill reverses; R or Y puts the car back at the last checkpoint; Escape or Start goes back to the menu | the same on the pad |
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

## Stunt track

`scenes/stunt_track.tscn` ("Stunt Track", the first entry of the main menu) is a road in
the sky, 840 m from the start gate to the finish, driven against the clock: down a slope,
through a loop, round a banked half circle, over a jump, round a banked left turn, over a
hill and through an S between rails. Three gates are checkpoints; a run counts when it
went through all of them, and the best time is kept in a saved game. The car can be put
back at the last checkpoint at any time. Around the start lies a square of 170 m, to try the
car on: room to turn, to slide and to drift. The car is the Racing Kit's. Putting the car
back and leaving for the menu are actions of their own in the Input Map (`respawn`,
`leave`), because on a pad the buttons of `jump` and `ui_cancel` are gas and brake. The
scene is written by
`tools/make_stunt_track.gd`, which lays the road like a pen draws a line (`_run`: so many
metres, turning, climbing and banking on the way; `_loop`; `_gap`); it can be edited in
Godot, but running the tool again overwrites it.

- The road is a ribbon of one mesh per ten cross sections, and its collision one box
  between every two cross sections, turned every way, upside down in the loop. Rails are
  boxes too; where a rail begins it comes in from the side over 12 m, so that it is no
  blunt edge across the way.
- The car (`StuntCar`, `runtime/game/stunt_car.c`) is a plain node. The runtime's bodies
  do not turn, so the body of the car is a box in picophysics, a small physics engine in
  one header by Kazade, made for the N64 and the Dreamcast (`runtime/third_party/picophysics`,
  public domain; one line of it was changed, see the README there). picophysics moves
  the body, lets it fall and turn, and keeps it out of a world of triangles, which the
  component makes from the box shapes of the static bodies in the group "track": the top
  of every piece of road, and top and sides of the rails. The engine looks at every
  triangle it has for every body, so it is only given the ones within 6.5 m of the car.
  Four `RayCast3D` nodes of the runtime are the wheels: each one that finds the road
  within the length of its spring pushes the car away from it there, with a damper. That
  carries the car, lays it the way the road lies, lets it dip and lean, tips it over the
  edge of the road when two wheels reach into the air, and lets it tumble when it falls.
- The driving follows the raycast vehicle that Godot's own `VehicleBody3D` is: each tyre
  pushes at the place where it touches the road. Sideways it pushes against sliding
  across the way its wheel points; along that way the rear tyres push with the engine
  and all four with the brake; together no more than the load on the wheel times the
  grip lets through. The front wheels are turned by the steering, less the faster the
  car goes. Nothing says how the car is to turn: it turns because of where the tyres
  push. Steered too hard for its speed it runs wide. The brake held at a standstill is
  the reverse gear.
- The drift is a slide at speed that is held, not a turn on the spot. The brake in a turn
  starts it, above 50 km/h. While it lasts all four tyres slide and hold only a part of
  what they did (`drift`), so the car's way bends in a wide curve and its speed stays;
  the engine pushes on. The car stands at an angle to its way that the steering says: the
  stick right over is about 35 degrees, the stick let go brings the nose back to the way,
  and steering the other way throws the tail round to the other side. For that the nose
  is turned as fast as the way bends, plus what is missing to the angle asked for: this
  one thing is set, not left to the tyres. When the car runs straight again the tyres
  catch. In the run that was measured, at 100 km/h, the car stood at 33 degrees after
  0.6 s, kept its speed, went round a curve of some 30 m radius, and ran straight again
  0.6 s after the steering was let go.
- The air presses a fast car onto the road, which with its speed is what holds it in the
  loop. The loop is turned a little against the road into it, so that a car that drives
  straight on is carried over to the loop's other end and comes out straight.
- Beside the start stand three crates, two on the square and one on top of them. Every
  node in the group "crate" gets a light box in picophysics as its body (a crate weighs
  an eighth of the car), which the car can ram and which falls, tumbles and comes to
  rest by the engine alone. A crate that falls off the square is gone until the scene is
  loaded again.
- At a rail the body is stopped and turned by picophysics, where the box meets it. Right
  after that the nose is brought round to where the car is going, so that it runs along
  the rail and does not go on pushing into it. A car that lies still without standing on
  its wheels is put back.
- The camera looks where the car is going, not where its nose points, and goes through
  the loop with it.

What was driven with the body in picophysics: with the gas alone the car goes down the
slope and through the loop to the first checkpoint, without turning by itself on the way
(this is in `make test`), then scrapes round the banked turn along its outer rail at
about 80 km/h, leaves the road after it, tumbles as it falls and is put back. A drift on
the square at 100 km/h came out the same as before the change of body. The crates stood
still for three seconds (they crept a centimetre) and flew apart when the car drove into
them at 50 km/h, which cost the car hardly any speed. Not driven with the new body:
the jump, the hill, the S and the finish, and a landing on the roof. Those were driven by
scripts only with earlier versions of the car, never in one run without a fall. How it
feels with a pad in the hand only a person can say. In Flycast the scene runs at 60 fps
from the start into the banked turn (game logic about 4 ms a frame, of which picophysics
is some 0.3 to 0.5 ms with the three crates standing; drawing about 3 ms). The rest was
not measured there, and the crates were not rammed there.

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

