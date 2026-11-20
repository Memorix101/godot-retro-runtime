/* The 2D view: where the active Camera2D puts the screen in the 2D world, and where the
 * Parallax2D nodes put themselves because of it.
 *
 * Both follow what Godot's nodes of the same names do, with their properties: the camera
 * with drag margins, smoothing and limits, the parallax nodes with scroll scale, offset,
 * limits, repeat size and autoscroll. The view does not rotate. */
#include <math.h>

#include "rr_internal.h"
#include "rr_platform.h"

/* The floored remainder: always from 0 up to `y`. */
static float posmod(float x, float y)
{
    const float m = fmodf(x, y);
    return m < 0.0f ? m + y : m;
}

/* One axis of the camera. `screen` is the size of the screen along it, `low` and `high`
 * are the sides towards smaller and larger coordinates (left and right, or top and
 * bottom). Returns the world coordinate of the screen's first pixel along the axis. */
static float camera_axis(RRScene *scene, const RRPkgCamera2DNode *d, int axis, float screen, int low,
                         int high, float target, bool drag, float dt)
{
    const float size = screen / d->zoom[axis];              /* of the view, in the world */
    const float anchor = d->top_left ? 0.0f : size * 0.5f;  /* from the view's start to the camera */
    float      *pos = &scene->camera2d_pos[axis], *smooth = &scene->camera2d_smooth[axis];
    float       start, extent;

    if (scene->camera2d_first) {
        *pos = *smooth = target;
    } else {
        if (d->top_left) {
            *pos = target;
        } else if (drag) {
            /* The view stays where it is while the camera is inside the margins. */
            const float most = target + size * 0.5f * d->drag_margin[low];
            const float least = target - size * 0.5f * d->drag_margin[high];
            *pos = *pos < most ? *pos : most;
            *pos = *pos > least ? *pos : least;
        } else {
            const float offset = d->drag_offset[axis];
            *pos = target + screen * 0.5f * d->drag_margin[offset < 0.0f ? high : low] * offset;
        }
        if (d->smoothing && d->limit_smoothed) {
            /* The limits act on the place the view is heading for, so it arrives gently. */
            start = *pos - anchor;
            if (d->limit[low] > d->limit[high] - size) {
                *pos -= start + (size - d->limit[high] - d->limit[low]) * 0.5f;
            } else if (start < d->limit[low]) {
                *pos -= start - d->limit[low];
            } else if (start + size > d->limit[high]) {
                *pos -= start + size - d->limit[high];
            }
        }
        if (d->smoothing) {
            *smooth += (*pos - *smooth) * d->smoothing_speed * dt;
        } else {
            *smooth = *pos;
        }
    }

    start = *smooth - anchor;
    if (!d->smoothing || !d->limit_smoothed) {
        /* Godot measures the view from its start to as far behind the camera as the
         * camera is behind the start, which is nothing for a camera in the corner. */
        extent = 2.0f * anchor;
        if (d->limit[low] > d->limit[high] - extent) {
            start = (d->limit[low] + d->limit[high] - extent) * 0.5f;
        } else if (start < d->limit[low]) {
            start = d->limit[low];
        } else if (start + extent > d->limit[high]) {
            start = d->limit[high] - extent;
        }
    }
    return start + d->offset[axis];
}

static void update_camera(RRScene *scene, float dt)
{
    const RRPkgCamera2DNode *d;
    float                    x, y;

    scene->view[0] = scene->view[1] = 0.0f;
    scene->screen_offset[0] = scene->screen_offset[1] = 0.0f;
    if (scene->camera2d == NULL) {
        return;
    }
    d = (const RRPkgCamera2DNode *)scene->camera2d->data;
    x = camera_axis(scene, d, 0, (float)RR_SCREEN_WIDTH, RR_SIDE_LEFT, RR_SIDE_RIGHT,
                    scene->camera2d->world.m[9], d->drag_h != 0, dt);
    y = camera_axis(scene, d, 1, (float)RR_SCREEN_HEIGHT, RR_SIDE_TOP, RR_SIDE_BOTTOM,
                    scene->camera2d->world.m[10], d->drag_v != 0, dt);
    scene->camera2d_first = false;

    /* Scrolling by whole screen pixels keeps neighbouring tiles free of seams. */
    scene->view[0] = floorf(x * d->zoom[0] + 0.5f) / d->zoom[0];
    scene->view[1] = floorf(y * d->zoom[1] + 0.5f) / d->zoom[1];
    /* What a Parallax2D counts from: the middle of the view, less half a screen. */
    scene->screen_offset[0] = x + (float)RR_SCREEN_WIDTH * 0.5f * (1.0f / d->zoom[0] - 1.0f);
    scene->screen_offset[1] = y + (float)RR_SCREEN_HEIGHT * 0.5f * (1.0f / d->zoom[1] - 1.0f);
}

/* Where a Parallax2D node is this frame, in the space of its parent. */
static void place_parallax(const RRScene *scene, Node *node)
{
    const RRPkgParallaxNode *p = (const RRPkgParallaxNode *)node->data;
    const float              screens[2] = { (float)RR_SCREEN_WIDTH, (float)RR_SCREEN_HEIGHT };
    /* It moves by itself only if it repeats along one axis at least, as in Godot. */
    const bool               moving = (p->repeat_size[0] > 0.0f || p->repeat_size[1] > 0.0f) &&
                                      (p->autoscroll[0] != 0.0f || p->autoscroll[1] != 0.0f);
    int                      a;

    for (a = 0; a < 2; a++) {
        /* Without a camera nothing tells the node where the view is: it keeps its own value. */
        const float screen_offset = p->ignore_camera_scroll || scene->camera2d == NULL
                                        ? p->screen_offset[a] : scene->screen_offset[a];
        /* The length of the node's axis: its scale. */
        const float scale = sqrtf(node->local.m[a * 3] * node->local.m[a * 3] +
                                  node->local.m[a * 3 + 1] * node->local.m[a * 3 + 1]);
        float       offset = p->scroll_offset[a], scroll = screen_offset, at;

        if (p->limit_begin[a] <= p->limit_end[a] - screens[a]) {
            scroll = scroll < p->limit_begin[a] ? p->limit_begin[a] : scroll;
            scroll = scroll > p->limit_end[a] - screens[a] ? p->limit_end[a] - screens[a] : scroll;
        }
        scroll *= p->scroll_scale[a];
        if (moving) {
            offset += p->autoscroll[a] * scene->time;
        }
        if (p->repeat_size[a] > 0.0f) {
            at = screen_offset - posmod(scroll - offset, p->repeat_size[a] * scale);
        } else {
            at = screen_offset + offset - scroll;
        }
        if (!p->follow_viewport) {
            at -= screen_offset;
        }
        node->local.m[9 + a] = at;
    }
}

void rr_canvas_update(RRScene *scene, float dt)
{
    uint32_t i;

    update_camera(scene, dt);
    for (i = 0; i < scene->node_count; i++) {
        Node *node = &scene->nodes[i];
        if (node->type == RR_NODE_PARALLAX && !(node->flags & RR_NODE_FLAG_FREE)) {
            place_parallax(scene, node);
            rr_scene_update_subtree(scene, node);
        }
    }
}

void rr_camera2d_reset_smoothing(RRScene *scene)
{
    /* The view jumps to where it was heading for. */
    scene->camera2d_smooth[0] = scene->camera2d_pos[0];
    scene->camera2d_smooth[1] = scene->camera2d_pos[1];
}

const Node *rr_canvas_repeat(const Node *node, float *step, int *times)
{
    const Node *at;

    for (at = node->parent; at != NULL; at = at->parent) {
        if (at->type == RR_NODE_PARALLAX) {
            const RRPkgParallaxNode *p = (const RRPkgParallaxNode *)at->data;
            if (p->repeat_times == 0 || (p->repeat_size[0] <= 0.0f && p->repeat_size[1] <= 0.0f)) {
                return NULL;
            }
            step[0] = p->repeat_size[0];
            step[1] = p->repeat_size[1];
            *times  = p->repeat_times;
            return at;
        }
    }
    return NULL;
}
