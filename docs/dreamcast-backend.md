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
differ; the numbers compare the two backends, not more. The list buffers take 1.3 MB of
main memory and 640 KB of video memory twice (`OP_UNITS` and so on in `dc_pvr.c`); a full
list prints a message and leaves polygons out.
