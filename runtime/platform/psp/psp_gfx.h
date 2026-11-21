/* What the PSP system layer (psp_system.c) needs from the graphics layer (psp_gu.c), which
 * also implements the rr_gfx_* functions of rr_platform.h. */
#ifndef PSP_GFX_H
#define PSP_GFX_H

#include <stdbool.h>

bool psp_gfx_init(void);
void psp_gfx_shutdown(void);

#endif
