/* Basic types and small math helpers shared by the whole runtime. */
#ifndef RR_TYPES_H
#define RR_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Platform ids as stored in the package header. */
enum {
    RR_PLATFORM_ANY       = 0, /* only for tools: accept any platform */
    RR_PLATFORM_PC        = 1,
    RR_PLATFORM_DREAMCAST = 2,
    RR_PLATFORM_PSP       = 3,
    RR_PLATFORM_WII       = 4
};

/* Affine transform: basis X, basis Y, basis Z, origin (the columns of a 3x4 matrix).
 * Same conventions as Godot: right-handed, Y up, cameras look along -Z. */
typedef struct {
    float m[12];
} RRMat34;

static inline void rr_mat34_identity(RRMat34 *t)
{
    static const RRMat34 id = { { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                  0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f } };
    *t = id;
}

/* out = a * b (b is applied first). out must not alias a or b. */
static inline void rr_mat34_mul(RRMat34 *out, const RRMat34 *a, const RRMat34 *b)
{
    int c, r;
    for (c = 0; c < 4; c++) {
        for (r = 0; r < 3; r++) {
            out->m[c * 3 + r] = a->m[0 + r] * b->m[c * 3 + 0] +
                                a->m[3 + r] * b->m[c * 3 + 1] +
                                a->m[6 + r] * b->m[c * 3 + 2];
        }
    }
    out->m[9]  += a->m[9];
    out->m[10] += a->m[10];
    out->m[11] += a->m[11];
}

/* Error reporting: functions that can fail return false and leave a message here. */
void        rr_set_error(const char *fmt, ...);
const char *rr_last_error(void);

#endif
