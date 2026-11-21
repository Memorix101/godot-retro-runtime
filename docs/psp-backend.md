# The PSP backend

The PSP platform layer is `runtime/platform/psp/`: `psp_gu.c` for the graphics, on the GU
library of the PSP SDK, and `psp_system.c` for everything else. It was written against
PPSSPP 1.20.4 and has not run on a real console yet.

## The screen

The PSP's screen has 480 x 272 pixels, the project's scenes are made for 640 x 480. The
game counts in a screen of 848 x 480: the 480 lines of the project with the width of the
PSP's shape. The exporter lays the controls out for that size (`screen_size()` of
`psp_baker.gd`), the runtime is compiled with `RR_SCREEN_WIDTH=848`, and the backend
scales every 2D quad by 272 / 480 when it draws it. This is what Godot does with the
stretch mode `canvas_items` and the aspect `expand`.

So a scene shows the same height as on the other platforms and more to the left and right.
A control that is anchored to the middle or to an edge of the screen follows; one that was
placed by its position stays where it is on a screen of 640, which is left of the middle
(the 2D test scene is made that way and leaves a strip on the right empty). Text is a little more than half as high as on
the other platforms.

## Graphics

- The graphics chip transforms vertices itself. The baked vertex is the one it reads
  (texture coordinates, colour, position, 24 bytes), so a mesh is drawn straight from the
  package, one command per strip.
- The chip does not cut triangles at the edges of the screen. It works in a square of
  4096 x 4096 pixels around the screen and leaves out every triangle that has a corner
  outside of it, which is what happens to the ground under a camera. A mesh whose bounding
  sphere reaches out of that square or in front of the near plane is therefore cut triangle
  by triangle in the backend; all other meshes do not pay for it. The sky and the particles
  go the same way.
- Translucent surfaces are held back and drawn after the solid ones, the far ones first,
  without writing the depth, as on the PC.
- Textures are 16-bit (565, 5551 or 4444, picked by the alpha an image has), at most
  512 x 512, padded to powers of two. They are rearranged into the chip's block order
  ("swizzled") when a scene loads. The frame buffers (32-bit) and the depth buffer take
  1.4 MB of the 2 MB of video memory; textures get the rest and go into main memory after
  that.
- Fog is the chip's own, which grows evenly between two distances. Depth fog is exact;
  exponential fog is replaced by even fog that ends at 2.3 / density, where the
  exponential one has swallowed 90 %.
- The display list of a frame, with the vertices that are made in the backend (2D quads,
  particles, cut triangles), has 2 MB. A full list prints a message and leaves polygons
  out.

## Sound, input, saved games

- The sounds are mixed in the runtime, in a thread that feeds one output channel at
  44100 Hz: up to 64 sound effects (16-bit samples that stay in the package) with volume,
  panning and pitch, and one piece of music that is read from its WAV file while it plays.
- Buttons are the PSP's own; Godot's joypad button A is Cross, B is Circle, X is Square,
  Y is Triangle. The left stick is the analog stick. The trigger axes are the L and R
  buttons, which are either pressed or not. Keys of the Input Map are left out.
- A saved game is a file `<name>.sav` next to the program, so the game has to run from
  the memory stick to save.
- The language is the one the console is set to. HOME leaves the program.

## What was checked

In PPSSPP 1.20.4, with a build that prints its frame rate (`FPS=1`): all demo scenes start
and are drawn, the feature scene reports `features: ok`, and a scripted input goes from
the menu onto the stunt track and through the loop to the first checkpoint. PPSSPP showed
60 frames per second in all of them; it does not model the speed of the console, so this
says nothing about a real PSP. The sound has not been listened to.
