/* Path and PathFollow nodes (2D and 3D).
 *
 * The exporter samples the curve of a Path at equal distances, so following it is a
 * table lookup with a straight line between two neighbours. A PathFollow node takes its
 * local transform from its progress, the distance along the curve, which is the node's
 * `value`: a component changes that and the node moves before the frame is drawn.
 *
 * Rotation is simpler than Godot's: the 3D modes look along the curve with +Y as up
 * (Y: turned around the Y axis only), or with the curve's own up vectors (oriented). */
#include <math.h>

#include "rr_internal.h"

static const RRPkgCurve *curve_of(const Node *path)
{
    const RRPkgPathNode *data = (const RRPkgPathNode *)path->data;
    return (const RRPkgCurve *)rr_package_asset(path->scene->pkg, data->curve);
}

static bool normalize(float *v)
{
    const float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length < 0.00001f) {
        return false;
    }
    v[0] /= length;
    v[1] /= length;
    v[2] /= length;
    return true;
}

static void cross(const float *a, const float *b, float *out)
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

float rr_path_get_length(const Node *path)
{
    return path->type == RR_NODE_PATH ? curve_of(path)->length : 0.0f;
}

void rr_path_follow_apply(Node *follow)
{
    const RRPkgPathFollowNode *data = (const RRPkgPathFollowNode *)follow->data;
    const RRPkgCurve          *curve = curve_of(follow->parent);
    const RRPkgCurvePoint     *a, *b;
    float                     *m = follow->local.m;
    float                      progress = follow->value, t, forward[3], up[3], x[3], y[3], z[3];
    uint32_t                   index;
    int                        i;

    if (data->loop) {
        progress = fmodf(progress, curve->length);
        if (progress < 0.0f) {
            progress += curve->length;
        }
    } else if (progress < 0.0f) {
        progress = 0.0f;
    } else if (progress > curve->length) {
        progress = curve->length;
    }
    follow->value = progress;

    index = (uint32_t)(progress / curve->spacing);
    if (index > curve->point_count - 2) {
        index = curve->point_count - 2;
    }
    t = progress / curve->spacing - (float)index;
    a = &curve->points[index];
    b = &curve->points[index + 1];
    for (i = 0; i < 3; i++) {
        m[9 + i]   = a->pos[i] + (b->pos[i] - a->pos[i]) * t;
        forward[i] = b->pos[i] - a->pos[i];
        up[i]      = a->up[i] + (b->up[i] - a->up[i]) * t;
    }

    if (data->rotation != RR_PATH_ROTATION_NONE) {
        if (data->rotation != RR_PATH_ROTATION_ORIENTED) {
            up[0] = 0.0f;
            up[1] = 1.0f;
            up[2] = 0.0f;
        }
        if (data->rotation == RR_PATH_ROTATION_Y) {
            forward[1] = 0.0f;
        }
        if (data->two_d) {
            /* X points along the curve. */
            if (normalize(forward)) {
                m[0] = forward[0];
                m[1] = forward[1];
                m[3] = -forward[1];
                m[4] = forward[0];
            }
        } else if (normalize(forward)) {
            /* -Z points along the curve, as a camera or a car model looks. */
            z[0] = -forward[0];
            z[1] = -forward[1];
            z[2] = -forward[2];
            cross(up, z, x);
            if (normalize(x)) {
                cross(z, x, y);
                for (i = 0; i < 3; i++) {
                    m[i]     = x[i];
                    m[3 + i] = y[i];
                    m[6 + i] = z[i];
                }
            }
        }
    }
    /* The offsets move the node sideways and up, along its own axes. */
    for (i = 0; i < 3; i++) {
        m[9 + i] += m[i] * data->h_offset + m[3 + i] * data->v_offset;
    }
}
