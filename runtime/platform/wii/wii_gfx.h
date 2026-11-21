/* What the Wii/GameCube system layer (wii_system.c) needs from the graphics layer
 * (wii_gx.c), which also implements the rr_gfx_* functions of rr_platform.h. */
#ifndef WII_GFX_H
#define WII_GFX_H

#include <stdbool.h>

bool wii_gfx_init(void);
void wii_gfx_shutdown(void);

/* In wii_system.c: a frame is through, the pads are read again for the next one. */
void wii_frame_done(void);

#endif
