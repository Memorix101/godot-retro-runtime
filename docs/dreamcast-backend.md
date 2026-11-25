# The Dreamcast graphics backend

The Dreamcast backend (`runtime/platform/dreamcast/dc_pvr.c`) talks to the PowerVR directly.
The baked data is already in PVR form (strips, packed ARGB, 16-bit textures), so a vertex
costs one matrix transform, one division and 32 bytes written. The three polygon lists are
filled in main memory and sent by DMA at the end of the frame; strips that cross the near
plane are clipped in the backend; fog is the PVR's table fog. The translucent list is drawn
in call order, not sorted by the hardware.

The older backend on GLdc (`dc_gldc.c`) is still there for comparison: `make GFX=gldc`.
Measured in Flycast with `FPS=1`, the time the program itself needs for drawing one frame:

| Scene                     | GLdc                               | PVR     |
|---------------------------|------------------------------------|---------|
| Island, start             | 11.2 ms + 6.0 ms in the buffer swap | 2.0 ms |
| Racing, start grid        | about 40 fps (time not recorded)   | 2.4 to 2.9 ms |

Flycast does not model the SH-4's cache or the PVR's fill rate, so a real console will
differ; the numbers compare the two backends, not more. The list buffers (`OP_UNITS` and
so on in `dc_pvr.c`; 24576 units for solid polygons, 4096 each for punch-through and
translucent ones) take 2.1 MB of main memory and 1 MB of video memory twice. A full list
prints a message and leaves the polygons drawn last out, which shows as meshes that
flicker. A build with `FPS=1` prints the peaks of the lists every 120 frames (`pvr: peak
units op .../24576 ..., full in N of 120 frames, ... KB video memory free`). The highway
demo needs up to some 20500 solid units, with Godot's mesh LOD at
`rendering/mesh_lod/lod_change/threshold_pixels.dreamcast = 10` (see
[package-format.md](package-format.md#mesh-asset)).
