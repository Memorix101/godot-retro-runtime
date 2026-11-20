/* RayCast3D and RayCast2D nodes.
 *
 * A ray runs from its node to `target` and reports the first thing it hits: the box and
 * sphere shapes of bodies and areas, and the solid boxes of tile maps. Enabled rays are
 * updated once per frame after the nodes have moved, as Godot does in its physics step;
 * rr_raycast_force_update does it at once. A ray that starts inside a shape does not hit
 * that shape (Godot's hit_from_inside = false). */
#include <math.h>

#include "rr_internal.h"

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static RRRayState *state_of(const Node *ray)
{
    RRScene       *scene = ray->scene;
    const uint16_t index = (uint16_t)(ray - scene->nodes);
    uint32_t       i;

    if (ray->type != RR_NODE_RAYCAST) {
        return NULL;
    }
    for (i = 0; i < scene->ray_count; i++) {
        if (scene->rays[i].node == index) {
            return &scene->rays[i];
        }
    }
    return NULL;
}

void rr_raycast_init(RRScene *scene, Node *ray)
{
    RRRayState *state = &scene->rays[scene->ray_count++];

    state->node     = (uint16_t)(ray - scene->nodes);
    state->collider = RR_NONE16;
    state->safe = state->unsafe = 1.0f;
}

/* Where the ray from `from` along `dir` (its whole length) enters the volume: 0..1, or a
 * negative number if it misses or starts inside. */
static float hit_volume(const RRVolume *v, const float *from, const float *dir, float *normal)
{
    const float d[3] = { v->center[0] - from[0], v->center[1] - from[1], v->center[2] - from[2] };
    int         i, k;

    if (v->sphere) {
        const float a = dot3(dir, dir), b = dot3(d, dir);
        const float c = dot3(d, d) - v->half[0] * v->half[0];
        const float disc = b * b - a * c;
        float       t;
        if (c <= 0.0f || a <= 0.0f || disc < 0.0f) {
            return -1.0f;
        }
        t = (b - sqrtf(disc)) / a;
        if (t < 0.0f || t > 1.0f) {
            return -1.0f;
        }
        for (i = 0; i < 3; i++) {
            normal[i] = (from[i] + dir[i] * t - v->center[i]) / v->half[0];
        }
        return t;
    } else {
        float t_in = -1.0e30f, t_out = 1.0e30f;
        int   axis_in = 0;
        float sign_in = 1.0f;

        for (i = 0; i < 3; i++) {
            const float e = dot3(v->axis[i], d), f = dot3(v->axis[i], dir);
            if (fabsf(f) > 0.000001f) {
                float t0 = (e - v->half[i]) / f, t1 = (e + v->half[i]) / f;
                float sign = -1.0f; /* entering through the side that faces -axis */
                if (t0 > t1) {
                    const float swap = t0;
                    t0   = t1;
                    t1   = swap;
                    sign = 1.0f;
                }
                if (t0 > t_in) {
                    t_in    = t0;
                    axis_in = i;
                    sign_in = sign;
                }
                if (t1 < t_out) {
                    t_out = t1;
                }
            } else if (fabsf(e) > v->half[i]) {
                return -1.0f; /* parallel to this pair of sides and outside of them */
            }
        }
        if (t_in > t_out || t_in < 0.0f || t_in > 1.0f) {
            return -1.0f;
        }
        for (k = 0; k < 3; k++) {
            normal[k] = v->axis[axis_in][k] * sign_in;
        }
        return t_in;
    }
}

/* Where the ray meets the ground of a height map, as hit_volume. The ray is walked in
 * steps of half a cell until it is below the ground, and the place is then narrowed
 * down between the last two steps. */
static float hit_height(const RRVolume *v, const float *from, const float *dir, float *normal)
{
    const float cell = v->scale[0] < v->scale[2] ? v->scale[0] : v->scale[2];
    const float length = sqrtf(dir[0] * dir[0] + dir[2] * dir[2]);
    float       t0 = 0.0f, t1 = 1.0f, last, height;
    int         steps, i, a;

    /* The part of the ray that is over the map. */
    for (a = 0; a < 3; a += 2) {
        const float lo = v->center[a] - v->half[a], hi = v->center[a] + v->half[a];
        if (fabsf(dir[a]) > 0.000001f) {
            float in = (lo - from[a]) / dir[a], out = (hi - from[a]) / dir[a];
            if (in > out) {
                const float swap = in;
                in  = out;
                out = swap;
            }
            t0 = in > t0 ? in : t0;
            t1 = out < t1 ? out : t1;
        } else if (from[a] < lo || from[a] > hi) {
            return -1.0f;
        }
    }
    if (t0 > t1) {
        return -1.0f;
    }
    if (!rr_height_at(v, from[0] + dir[0] * t0, from[2] + dir[2] * t0, &height, NULL) ||
        from[1] + dir[1] * t0 < height) {
        return -1.0f; /* starts below the ground */
    }
    steps = (int)(length * (t1 - t0) / (0.5f * cell)) + 1;
    steps = steps > 512 ? 512 : steps;
    last  = t0;
    for (i = 1; i <= steps; i++) {
        const float t = t0 + (t1 - t0) * (float)i / (float)steps;
        if (rr_height_at(v, from[0] + dir[0] * t, from[2] + dir[2] * t, &height, NULL) &&
            from[1] + dir[1] * t < height) {
            float above = last, below = t;
            for (a = 0; a < 10; a++) {
                const float mid = 0.5f * (above + below);
                if (rr_height_at(v, from[0] + dir[0] * mid, from[2] + dir[2] * mid, &height, NULL) &&
                    from[1] + dir[1] * mid < height) {
                    below = mid;
                } else {
                    above = mid;
                }
            }
            rr_height_at(v, from[0] + dir[0] * above, from[2] + dir[2] * above, &height, normal);
            return above;
        }
        last = t;
    }
    return -1.0f;
}

/* --- shape casts ----------------------------------------------------------------------
 * A box, sphere or capsule that is moved along the line. There is no test for a moving
 * shape, so it is put down at places along the line, close enough together that nothing
 * as large as the shape fits in between, and the first place where it touches is then
 * narrowed down between two of them. */

#define CAST_STEPS  64 /* places along the line at most */
#define CAST_HALVES 8  /* times the first touch is narrowed down, as in Godot */

typedef struct {
    RRVolume shape;  /* at the start of the line */
    float    dir[3]; /* the whole line */
    int      steps;
    float    best, safe; /* the first touch so far, and how far the shape got before it */
    uint16_t collider;
    float    normal[3], depth;
} ShapeCast;

static bool touches_at(const ShapeCast *c, const RRVolume *solid, float t, float *normal, float *depth)
{
    RRVolume at = c->shape;
    int      a;

    for (a = 0; a < 3; a++) {
        at.center[a] += c->dir[a] * t;
    }
    return rr_volume_contact(&at, solid, normal, depth);
}

/* Where along the line the shape first touches one solid; kept if it is the first of all. */
static void cast_against(ShapeCast *c, const RRVolume *solid, uint16_t owner)
{
    float normal[3], depth, open = 0.0f, hit = -1.0f;
    int   i, a;

    for (i = 0; i <= c->steps; i++) {
        const float t = (float)i / (float)c->steps;
        if (touches_at(c, solid, t, normal, &depth)) {
            hit = t;
            break;
        }
        open = t;
    }
    if (hit < 0.0f) {
        return;
    }
    for (i = 0; i < CAST_HALVES && hit > open; i++) {
        const float mid = 0.5f * (open + hit);
        if (touches_at(c, solid, mid, normal, &depth)) {
            hit = mid;
        } else {
            open = mid;
        }
    }
    if (hit < c->best && touches_at(c, solid, hit, normal, &depth)) {
        c->best     = hit;
        c->safe     = open;
        c->collider = owner;
        c->depth    = depth;
        for (a = 0; a < 3; a++) {
            c->normal[a] = normal[a];
        }
    }
}

/* How far a volume reaches from its centre along a unit direction. */
static float reach(const RRVolume *v, const float *n)
{
    if (v->sphere) {
        return v->half[0];
    }
    if (v->capsule) {
        return v->seg * fabsf(dot3(v->axis[1], n)) + v->radius;
    }
    return v->half[0] * fabsf(dot3(v->axis[0], n)) + v->half[1] * fabsf(dot3(v->axis[1], n)) +
           v->half[2] * fabsf(dot3(v->axis[2], n));
}

static void cast_shape(Node *node, RRRayState *state)
{
    const RRScene          *scene = node->scene;
    const RRPkgRayCastNode *data = (const RRPkgRayCastNode *)node->data;
    const RRPkgShapeNode    shape = { data->shape, { data->shape_size[0], data->shape_size[1], data->shape_size[2] },
                                      0, 0, { 0, 0 }, RR_NONE32 };
    ShapeCast               c;
    RRMat34                 world;
    float                   lo[3], hi[3], length, smallest;
    uint32_t                i, k;
    int                     a;

    rr_node_world_now(node, &world);
    rr_volume_make(&c.shape, &world, &shape);
    for (a = 0; a < 3; a++) {
        c.dir[a] = world.m[a] * data->target[0] + world.m[3 + a] * data->target[1] +
                   world.m[6 + a] * data->target[2];
    }
    c.best     = 2.0f;
    c.safe     = 1.0f;
    c.collider = RR_NONE16;
    /* Steps of half the shape's smallest side: what is thinner than that may be missed. */
    length   = sqrtf(dot3(c.dir, c.dir));
    smallest = c.shape.sphere ? c.shape.half[0] : (c.shape.capsule ? c.shape.radius : c.shape.half[0]);
    if (!c.shape.sphere && !c.shape.capsule) {
        smallest = c.shape.half[1] < smallest ? c.shape.half[1] : smallest;
        /* A 2D shape is half a unit thick, which is no size of its own. */
        smallest = c.shape.half[2] < smallest && data->shape_size[2] != 0.5f ? c.shape.half[2] : smallest;
    }
    c.steps = smallest > 0.0f ? (int)(length / smallest) + 1 : CAST_STEPS;
    c.steps = c.steps > CAST_STEPS ? CAST_STEPS : c.steps;

    /* The box around the whole way of the shape. */
    rr_volume_bounds(&c.shape, lo, hi);
    for (a = 0; a < 3; a++) {
        lo[a] += c.dir[a] < 0.0f ? c.dir[a] : 0.0f;
        hi[a] += c.dir[a] > 0.0f ? c.dir[a] : 0.0f;
    }

    for (i = 0; i < scene->collider_count; i++) {
        const RRCollider *col = &scene->colliders[i];
        const Node       *owner = &scene->nodes[col->node];
        uint32_t          layer;
        bool              apart = false;

        if (!col->live || (owner->flags & RR_NODE_FLAG_FREE) ||
            (data->exclude_parent && owner == node->parent)) {
            continue;
        }
        if (col->area) {
            const RRPkgAreaNode *area = (const RRPkgAreaNode *)owner->data;
            if (!data->collide_areas || !area->monitorable) {
                continue;
            }
            layer = area->layer;
        } else {
            if (!data->collide_bodies) {
                continue;
            }
            layer = ((const RRPkgBodyNode *)owner->data)->layer;
        }
        for (a = 0; a < 3; a++) {
            apart = apart || col->min[a] > hi[a] || col->max[a] < lo[a];
        }
        if ((data->mask & layer) == 0 || apart) {
            continue;
        }
        for (k = 0; k < col->shape_count; k++) {
            const RRVolume *v = &scene->volumes[col->shape_first + k];
            if (v->used && !(scene->nodes[scene->collider_shapes[col->shape_first + k]].flags & RR_NODE_FLAG_FREE)) {
                cast_against(&c, v, col->node);
            }
        }
    }
    /* The solid boxes of tile maps. */
    for (i = 0; i < scene->tilemap_count && data->collide_bodies; i++) {
        const Node             *n = &scene->nodes[scene->tilemaps[i]];
        const RRPkgTileMapNode *map_data = (const RRPkgTileMapNode *)n->data;
        const RRPkgTileMap     *map = (const RRPkgTileMap *)rr_package_asset(scene->pkg, map_data->tiles);
        const RRPkgTileBox     *boxes = (const RRPkgTileBox *)((const uint8_t *)map + map->boxes_off);

        if ((n->flags & RR_NODE_FLAG_FREE) || (data->mask & map_data->layer) == 0) {
            continue;
        }
        for (k = 0; k < map->box_count; k++) {
            const RRPkgShapeNode box = { RR_SHAPE_BOX, { boxes[k].w * 0.5f, boxes[k].h * 0.5f, 0.5f },
                                         0, 0, { 0, 0 }, RR_NONE32 };
            const float          cx = boxes[k].x + boxes[k].w * 0.5f, cy = boxes[k].y + boxes[k].h * 0.5f;
            RRMat34              at = n->world;
            RRVolume             solid;

            at.m[9]  += at.m[0] * cx + at.m[3] * cy;
            at.m[10] += at.m[1] * cx + at.m[4] * cy;
            rr_volume_make(&solid, &at, &box);
            cast_against(&c, &solid, scene->tilemaps[i]);
        }
    }

    state->collider = c.collider;
    state->safe     = c.collider != RR_NONE16 ? c.safe : 1.0f;
    state->unsafe   = c.collider != RR_NONE16 ? c.best : 1.0f;
    if (c.collider != RR_NONE16) {
        /* The place on the shape that touches: its far side along the normal. */
        const float inside = reach(&c.shape, c.normal) - c.depth;
        for (a = 0; a < 3; a++) {
            state->normal[a] = c.normal[a];
            state->point[a]  = c.shape.center[a] + c.dir[a] * c.best - c.normal[a] * inside;
        }
    }
}

static void cast(Node *ray, RRRayState *state)
{
    const RRScene          *scene = ray->scene;
    const RRPkgRayCastNode *data = (const RRPkgRayCastNode *)ray->data;
    RRMat34                 world;
    float                   from[3], dir[3], best = 2.0f, normal[3], ray_min[3], ray_max[3];
    uint32_t                i, k;
    int                     a;

    if (data->shape != 0) {
        cast_shape(ray, state);
        return;
    }
    rr_node_world_now(ray, &world);
    for (a = 0; a < 3; a++) {
        from[a] = world.m[9 + a];
        dir[a]  = world.m[a] * data->target[0] + world.m[3 + a] * data->target[1] +
                  world.m[6 + a] * data->target[2];
    }
    state->collider = RR_NONE16;

    /* The box around the ray, to pass over areas and bodies that are nowhere near it. */
    for (a = 0; a < 3; a++) {
        ray_min[a] = dir[a] < 0.0f ? from[a] + dir[a] : from[a];
        ray_max[a] = dir[a] < 0.0f ? from[a] : from[a] + dir[a];
    }

    /* Areas and bodies, with the shapes the trigger tests worked out (rr_colliders_refresh). */
    for (i = 0; i < scene->collider_count; i++) {
        const RRCollider *col = &scene->colliders[i];
        const Node       *owner = &scene->nodes[col->node];
        uint32_t          layer;
        bool              apart = false;

        if (!col->live || (owner->flags & RR_NODE_FLAG_FREE) ||
            (data->exclude_parent && owner == ray->parent)) {
            continue;
        }
        if (col->area) {
            const RRPkgAreaNode *area = (const RRPkgAreaNode *)owner->data;
            if (!data->collide_areas || !area->monitorable) {
                continue;
            }
            layer = area->layer;
        } else {
            if (!data->collide_bodies) {
                continue;
            }
            layer = ((const RRPkgBodyNode *)owner->data)->layer;
        }
        for (a = 0; a < 3; a++) {
            apart = apart || col->min[a] > ray_max[a] || col->max[a] < ray_min[a];
        }
        if ((data->mask & layer) == 0 || apart) {
            continue;
        }
        for (k = 0; k < col->shape_count; k++) {
            const RRVolume *v = &scene->volumes[col->shape_first + k];
            float           t;

            if (!v->used || (scene->nodes[scene->collider_shapes[col->shape_first + k]].flags & RR_NODE_FLAG_FREE)) {
                continue;
            }
            t = v->map != NULL ? hit_height(v, from, dir, normal) : hit_volume(v, from, dir, normal);
            if (t >= 0.0f && t < best) {
                best            = t;
                state->collider = col->node;
                for (a = 0; a < 3; a++) {
                    state->normal[a] = normal[a];
                }
            }
        }
    }

    /* The solid boxes of tile maps. */
    for (i = 0; i < scene->tilemap_count && data->collide_bodies; i++) {
        const Node             *n = &scene->nodes[scene->tilemaps[i]];
        const RRPkgTileMapNode *map_data = (const RRPkgTileMapNode *)n->data;
        const RRPkgTileMap     *map = (const RRPkgTileMap *)rr_package_asset(scene->pkg, map_data->tiles);
        const RRPkgTileBox     *boxes = (const RRPkgTileBox *)((const uint8_t *)map + map->boxes_off);
        const float            *m = n->world.m;
        float                   length[3], t;
        RRVolume                volume;

        if ((n->flags & RR_NODE_FLAG_FREE) || (data->mask & map_data->layer) == 0) {
            continue;
        }
        /* The axes of the map are the same for all of its boxes. */
        volume.sphere = false;
        for (a = 0; a < 3; a++) {
            length[a] = sqrtf(dot3(&m[a * 3], &m[a * 3]));
            for (k = 0; k < 3; k++) {
                volume.axis[a][k] = length[a] > 0.0f ? m[a * 3 + k] / length[a] : 0.0f;
            }
        }
        volume.half[2] = 0.5f;
        for (k = 0; k < map->box_count; k++) {
            const float cx = boxes[k].x + boxes[k].w * 0.5f, cy = boxes[k].y + boxes[k].h * 0.5f;
            volume.center[0] = m[0] * cx + m[3] * cy + m[9];
            volume.center[1] = m[1] * cx + m[4] * cy + m[10];
            volume.center[2] = m[2] * cx + m[5] * cy + m[11];
            volume.half[0]   = boxes[k].w * 0.5f * length[0];
            volume.half[1]   = boxes[k].h * 0.5f * length[1];
            t = hit_volume(&volume, from, dir, normal);
            if (t >= 0.0f && t < best) {
                best            = t;
                state->collider = scene->tilemaps[i];
                for (a = 0; a < 3; a++) {
                    state->normal[a] = normal[a];
                }
            }
        }
    }
    if (state->collider != RR_NONE16) {
        for (a = 0; a < 3; a++) {
            state->point[a] = from[a] + dir[a] * best;
        }
    }
}

void rr_raycasts_update(RRScene *scene)
{
    uint32_t i;

    for (i = 0; i < scene->ray_count; i++) {
        Node *ray = &scene->nodes[scene->rays[i].node];
        if (((const RRPkgRayCastNode *)ray->data)->enabled && !(ray->flags & RR_NODE_FLAG_FREE)) {
            cast(ray, &scene->rays[i]);
        }
    }
}

void rr_raycast_force_update(Node *ray)
{
    RRRayState *state = state_of(ray);
    if (state != NULL) {
        cast(ray, state);
    }
}

bool rr_raycast_is_colliding(const Node *ray)
{
    const RRRayState *state = state_of(ray);
    return state != NULL && state->collider != RR_NONE16;
}

Node *rr_raycast_get_collider(const Node *ray)
{
    const RRRayState *state = state_of(ray);
    return state != NULL && state->collider != RR_NONE16 ? &ray->scene->nodes[state->collider] : NULL;
}

void rr_raycast_get_collision_point(const Node *ray, float *out)
{
    const RRRayState *state = state_of(ray);
    int               i;
    for (i = 0; i < 3; i++) {
        out[i] = state != NULL && state->collider != RR_NONE16 ? state->point[i] : 0.0f;
    }
}

void rr_raycast_get_collision_normal(const Node *ray, float *out)
{
    const RRRayState *state = state_of(ray);
    int               i;
    for (i = 0; i < 3; i++) {
        out[i] = state != NULL && state->collider != RR_NONE16 ? state->normal[i] : 0.0f;
    }
}

float rr_shapecast_get_safe_fraction(const Node *cast)
{
    const RRRayState *state = state_of(cast);
    return state != NULL && state->collider != RR_NONE16 ? state->safe : 1.0f;
}

float rr_shapecast_get_unsafe_fraction(const Node *cast)
{
    const RRRayState *state = state_of(cast);
    return state != NULL && state->collider != RR_NONE16 ? state->unsafe : 1.0f;
}
