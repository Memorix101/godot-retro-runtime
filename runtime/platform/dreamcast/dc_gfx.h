/* What the Dreamcast system layer (dc_system.c) needs from the graphics backend that is
 * built in: dc_pvr.c or dc_gldc.c, chosen by GFX in the Makefile. The backend also
 * implements the rr_gfx_* functions of rr_platform.h. */
#ifndef DC_GFX_H
#define DC_GFX_H

#include <stdbool.h>

bool dc_gfx_init(void);
void dc_gfx_shutdown(void);

#endif
