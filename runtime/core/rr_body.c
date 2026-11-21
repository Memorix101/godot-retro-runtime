/* Physics bodies: CharacterBody nodes that are moved against the box shapes of StaticBody
 * nodes, in 2D and 3D.
 *
 * This is not Godot's physics engine. It covers what a platformer or a racer needs from
 * it: rr_body_move_and_slide() moves a body by its velocity and stops it at solid boxes,
 * the way CharacterBody3D.move_and_slide() does for boxes that are not rotated. All boxes
 * are taken as their axis-aligned bounds in world space. The move is done one axis at a
 * time, so a body slides along a wall or a floor instead of stopping dead.
 *
 * 2D bodies use the same code: their matrices keep Z at zero and their shapes are half a
 * unit thick, so the third axis never gets in the way. */
#include <math.h>

#include <string.h>

#include "rr_internal.h"

/* Boxes that touch or overlap by less than this on the axes across the movement do not
 * count as a hit. Without it a body that stands on a floor would catch on the floor's
 * side when it walks, through nothing but rounding. */
#define SKIN 0.005f

/* How deep a body may be in a one-way solid and still be carried by it, in the pixels of
 * the 2D world (Godot's one_way_collision_margin). */
#define ONE_WAY_MARGIN 1.0f

/* The shape the body came to stand on in the move that is being made, RR_NONE16 for none. */
static uint16_t g_floor;

typedef struct {
    float min[3], max[3];
} Bounds;

/* Axis-aligned bounds of a box shape under a world transform. */
static void shape_bounds(Bounds *b, const RRMat34 *world, const RRPkgShapeNode *shape)
{
    const float *m = world->m;
    int          i, k;

    for (i = 0; i < 3; i++) {
        float half = 0.0f;
        for (k = 0; k < 3; k++) {
            half += fabsf(m[k * 3 + i]) * shape->size[k];
        }
        b->min[i] = m[9 + i] - half;
        b->max[i] = m[9 + i] + half;
    }
}

static bool is_solid_box(const Node *n)
{
    const RRPkgShapeNode *s = (const RRPkgShapeNode *)n->data;
    return n->type == RR_NODE_SHAPE && s->kind == RR_SHAPE_BOX && !s->disabled &&
           !(n->flags & RR_NODE_FLAG_FREE);
}

/* Bounds of all box shapes of a body, with the body at its current local transform.
 * The world matrix of the body itself is one update old inside a component, so it is
 * built here from the parent's. */
static bool body_bounds(Node *body, Bounds *out)
{
    RRMat34 body_world, shape_world;
    Node   *shape;
    bool    any = false;
    int     i;

    if (body->parent != NULL) {
        rr_mat34_mul(&body_world, &body->parent->world, &body->local);
    } else {
        body_world = body->local;
    }
    for (shape = rr_node_child(body, NULL); shape != NULL; shape = rr_node_child(body, shape)) {
        Bounds b;
        if (!is_solid_box(shape)) {
            continue;
        }
        rr_mat34_mul(&shape_world, &body_world, &shape->local);
        shape_bounds(&b, &shape_world, (const RRPkgShapeNode *)shape->data);
        for (i = 0; i < 3; i++) {
            if (!any || b.min[i] < out->min[i]) {
                out->min[i] = b.min[i];
            }
            if (!any || b.max[i] > out->max[i]) {
                out->max[i] = b.max[i];
            }
        }
        any = true;
    }
    return any;
}

/* True if the boxes overlap: by any amount along `axis`, by more than SKIN across it. */
static bool hits(const Bounds *a, const Bounds *b, int axis)
{
    int i;
    for (i = 0; i < 3; i++) {
        const float margin = i == axis ? 0.0f : SKIN;
        if (a->max[i] - b->min[i] <= margin || b->max[i] - a->min[i] <= margin) {
            return false;
        }
    }
    return true;
}

/* Moves `box` back out of `solid` along the axis it just moved on, if the two overlap. */
static bool push_out(Bounds *box, const Bounds *solid, int axis, float step, float *moved)
{
    float back;

    if (!hits(box, solid, axis)) {
        return false;
    }
    back = step > 0.0f ? solid->min[axis] - box->max[axis] : solid->max[axis] - box->min[axis];
    box->min[axis] += back;
    box->max[axis] += back;
    *moved += back;
    return true;
}

/* The same for the boxes that are moved one axis at a time: a one-way box (2D, so down is
 * +Y) stops the body only on its way down, and only if its underside was above the box
 * before this move. `moved` is how far the body has come along the axis so far. */
static bool passes_one_way(const Bounds *box, const Bounds *solid, int axis, float step, float moved)
{
    return axis != 1 || step <= 0.0f || box->max[1] - moved > solid->min[1] + ONE_WAY_MARGIN;
}

/* Bounds of one solid rectangle of a tile map. Like 2D shapes it is half a unit thick. */
static void tile_box_bounds(Bounds *b, const RRMat34 *world, const RRPkgTileBox *box)
{
    const float          cx = box->x + box->w * 0.5f, cy = box->y + box->h * 0.5f;
    const RRPkgShapeNode shape = { RR_SHAPE_BOX, { box->w * 0.5f, box->h * 0.5f, 0.5f }, 0, 0, { 0, 0 }, RR_NONE32 };
    RRMat34              at = *world;

    at.m[9]  += world->m[0] * cx + world->m[3] * cy;
    at.m[10] += world->m[1] * cx + world->m[4] * cy;
    shape_bounds(b, &at, &shape);
}

/* Turns a displacement in world space into one in the space of the body's parent. */
static void world_to_parent(const Node *parent, const float *world, float *local)
{
    const float *m;
    float        c[9], det, inv;
    int          i;

    if (parent == NULL) {
        local[0] = world[0];
        local[1] = world[1];
        local[2] = world[2];
        return;
    }
    /* Inverse of the parent's basis through its cofactors. */
    m = parent->world.m;
    c[0] = m[4] * m[8] - m[5] * m[7];
    c[1] = m[5] * m[6] - m[3] * m[8];
    c[2] = m[3] * m[7] - m[4] * m[6];
    c[3] = m[2] * m[7] - m[1] * m[8];
    c[4] = m[0] * m[8] - m[2] * m[6];
    c[5] = m[1] * m[6] - m[0] * m[7];
    c[6] = m[1] * m[5] - m[2] * m[4];
    c[7] = m[2] * m[3] - m[0] * m[5];
    c[8] = m[0] * m[4] - m[1] * m[3];
    det = m[0] * c[0] + m[1] * c[1] + m[2] * c[2];
    inv = det != 0.0f ? 1.0f / det : 0.0f;
    for (i = 0; i < 3; i++) {
        local[i] = (c[i] * world[0] + c[3 + i] * world[1] + c[6 + i] * world[2]) * inv;
    }
}

/* --- the general solver ---------------------------------------------------------------
 * For bodies with a sphere or capsule shape, for scenes whose static shapes are rotated
 * or round, and for RigidBody nodes. The body is moved by its whole step and then pushed
 * out of what it overlaps, along the shortest way, a few times over. */

typedef struct {
    float n[3];  /* from the solid towards the body, unit length */
    float depth; /* how far the two overlap along n */
} Contact;

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* The point of a box that is closest to `p`. Returns true if `p` is inside the box. */
static bool closest_on_box(const RRVolume *box, const float *p, float *out)
{
    const float d[3] = { p[0] - box->center[0], p[1] - box->center[1], p[2] - box->center[2] };
    bool        inside = true;
    int         i, k;

    out[0] = box->center[0];
    out[1] = box->center[1];
    out[2] = box->center[2];
    for (i = 0; i < 3; i++) {
        float q = dot3(d, box->axis[i]);
        if (q > box->half[i]) {
            q      = box->half[i];
            inside = false;
        } else if (q < -box->half[i]) {
            q      = -box->half[i];
            inside = false;
        }
        for (k = 0; k < 3; k++) {
            out[k] += box->axis[i][k] * q;
        }
    }
    return inside;
}

static bool sphere_contact(const float *center, float radius, const RRVolume *solid, Contact *c)
{
    float d[3], dist;
    int   i, k;

    if (solid->sphere) {
        for (k = 0; k < 3; k++) {
            d[k] = center[k] - solid->center[k];
        }
        dist = sqrtf(dot3(d, d));
        if (dist >= radius + solid->half[0]) {
            return false;
        }
        c->depth = radius + solid->half[0] - dist;
    } else {
        float closest[3];
        if (closest_on_box(solid, center, closest)) {
            /* The centre is inside: out through the nearest side. */
            const float rel[3] = { center[0] - solid->center[0], center[1] - solid->center[1],
                                   center[2] - solid->center[2] };
            float       best = 1.0e30f;
            for (i = 0; i < 3; i++) {
                const float q = dot3(rel, solid->axis[i]);
                if (solid->half[i] - fabsf(q) < best) {
                    best = solid->half[i] - fabsf(q);
                    for (k = 0; k < 3; k++) {
                        c->n[k] = q < 0.0f ? -solid->axis[i][k] : solid->axis[i][k];
                    }
                }
            }
            c->depth = best + radius;
            return true;
        }
        for (k = 0; k < 3; k++) {
            d[k] = center[k] - closest[k];
        }
        dist = sqrtf(dot3(d, d));
        if (dist >= radius) {
            return false;
        }
        c->depth = radius - dist;
    }
    for (k = 0; k < 3; k++) {
        c->n[k] = dist > 0.00001f ? d[k] / dist : (k == 1 ? 1.0f : 0.0f);
    }
    return true;
}

/* A capsule is a sphere at the point of its line that is closest to the solid. */
static bool capsule_contact(const RRVolume *capsule, const RRVolume *solid, Contact *c)
{
    float ref[3] = { solid->center[0], solid->center[1], solid->center[2] }, p[3];
    int   round, k;

    for (round = 0; round < 2; round++) {
        float t = 0.0f;
        for (k = 0; k < 3; k++) {
            t += (ref[k] - capsule->center[k]) * capsule->axis[1][k];
        }
        t = t > capsule->seg ? capsule->seg : (t < -capsule->seg ? -capsule->seg : t);
        for (k = 0; k < 3; k++) {
            p[k] = capsule->center[k] + capsule->axis[1][k] * t;
        }
        if (solid->sphere) {
            break;
        }
        closest_on_box(solid, p, ref);
    }
    return sphere_contact(p, capsule->radius, solid, c);
}

/* Two boxes: the separating axis test, keeping the axis with the smallest overlap. */
static bool box_contact(const RRVolume *a, const RRVolume *b, Contact *c)
{
    const float t[3] = { a->center[0] - b->center[0], a->center[1] - b->center[1],
                         a->center[2] - b->center[2] };
    float       best = 1.0e30f;
    int         i, j, k, n;

    for (n = 0; n < 15; n++) {
        float axis[3], length, ra = 0.0f, rb = 0.0f, dist, overlap;

        if (n < 3) {
            for (k = 0; k < 3; k++) {
                axis[k] = a->axis[n][k];
            }
        } else if (n < 6) {
            for (k = 0; k < 3; k++) {
                axis[k] = b->axis[n - 3][k];
            }
        } else {
            const float *u = a->axis[(n - 6) / 3], *v = b->axis[(n - 6) % 3];
            axis[0] = u[1] * v[2] - u[2] * v[1];
            axis[1] = u[2] * v[0] - u[0] * v[2];
            axis[2] = u[0] * v[1] - u[1] * v[0];
            length  = sqrtf(dot3(axis, axis));
            if (length < 0.001f) {
                continue; /* parallel edges: the side axes cover it */
            }
            for (k = 0; k < 3; k++) {
                axis[k] /= length;
            }
        }
        for (i = 0; i < 3; i++) {
            ra += a->half[i] * fabsf(dot3(a->axis[i], axis));
            rb += b->half[i] * fabsf(dot3(b->axis[i], axis));
        }
        dist    = dot3(t, axis);
        overlap = ra + rb - fabsf(dist);
        if (overlap <= 0.0f) {
            return false;
        }
        /* An edge axis has to be clearly better than a side to win, or resting boxes
         * would flip between the two. */
        if (overlap < best - (n >= 6 ? 0.001f : 0.0f)) {
            best = overlap;
            for (j = 0; j < 3; j++) {
                c->n[j] = dist < 0.0f ? -axis[j] : axis[j];
            }
        }
    }
    c->depth = best;
    return true;
}

/* A body on the ground of a height map. The ground is solid all the way down, so what
 * is below its surface is lifted out along the normal of the ground there. A sphere and
 * the lower end of a capsule are tested as what they are; a box by the four corners and
 * the middle of its underside. */
static bool height_contact(const RRVolume *mover, const RRVolume *ground, Contact *c)
{
    float points[5][3], radius = 0.0f, height, normal[3];
    int   count = 1, i, k;
    bool  any = false;

    if (mover->sphere) {
        radius = mover->half[0];
        for (k = 0; k < 3; k++) {
            points[0][k] = mover->center[k];
        }
    } else if (mover->capsule) {
        const float down = mover->axis[1][1] > 0.0f ? -mover->seg : mover->seg;
        radius = mover->radius;
        for (k = 0; k < 3; k++) {
            points[0][k] = mover->center[k] + mover->axis[1][k] * down;
        }
    } else {
        const float down = mover->axis[1][1] > 0.0f ? -mover->half[1] : mover->half[1];
        count = 5;
        for (i = 0; i < 5; i++) {
            const float x = i == 4 ? 0.0f : ((i & 1) ? mover->half[0] : -mover->half[0]);
            const float z = i == 4 ? 0.0f : ((i & 2) ? mover->half[2] : -mover->half[2]);
            for (k = 0; k < 3; k++) {
                points[i][k] = mover->center[k] + mover->axis[0][k] * x + mover->axis[1][k] * down +
                               mover->axis[2][k] * z;
            }
        }
    }
    c->depth = 0.0f;
    for (i = 0; i < count; i++) {
        float depth;
        if (!rr_height_at(ground, points[i][0], points[i][2], &height, normal)) {
            continue;
        }
        /* The distance to the plane of the triangle below the point. */
        depth = radius - (points[i][1] - height) * normal[1];
        if (depth > c->depth) {
            c->depth = depth;
            for (k = 0; k < 3; k++) {
                c->n[k] = normal[k];
            }
            any = true;
        }
    }
    return any;
}

static bool volume_contact(const RRVolume *mover, const RRVolume *solid, Contact *c)
{
    int k;

    if (solid->map != NULL) {
        return height_contact(mover, solid, c);
    }
    if (mover->sphere) {
        return sphere_contact(mover->center, mover->half[0], solid, c);
    }
    if (mover->capsule) {
        return capsule_contact(mover, solid, c);
    }
    if (solid->sphere) {
        /* The other way round, and the direction turned over. */
        if (!sphere_contact(solid->center, solid->half[0], mover, c)) {
            return false;
        }
        for (k = 0; k < 3; k++) {
            c->n[k] = -c->n[k];
        }
        return true;
    }
    return box_contact(mover, solid, c);
}

#define MAX_BODY_SHAPES 4
#define FLOOR_COS       0.7071f /* 45 degrees, Godot's default floor_max_angle */
#define FLOOR_SNAP      0.1f    /* Godot's default floor_snap_length */

typedef struct {
    Node    *body;
    float   *velocity;
    float    up[3];
    bool     character;       /* slides and stands; else bounces and rubs */
    float    bounce, friction, rest_speed;
    float    fall;            /* how far this move takes the body down */
    RRVolume shapes[MAX_BODY_SHAPES];
    int      shape_count;
} Mover;

/* A one-way solid (2D) stops only what comes down onto it: the contact has to push the
 * body up, and the body must not have been deeper in it than this move took it down.
 * Everything else goes through, as with Godot's one_way_collision. */
static bool lets_through(const Mover *m, const RRVolume *solid, const Contact *c)
{
    return solid->one_way && (dot3(c->n, m->up) <= FLOOR_COS || c->depth > m->fall + ONE_WAY_MARGIN);
}

/* Takes the body out of one solid and changes its velocity for the contact. */
static void resolve(Mover *m, const Contact *c, float solid_bounce, float solid_friction,
                    uint16_t solid_shape)
{
    const float n_up = dot3(c->n, m->up);
    const float vn = dot3(m->velocity, c->n);
    float       push[3], local[3];
    int         s, k;

    if (m->character && n_up > FLOOR_COS) {
        /* A floor carries the body straight up, so it does not creep down a slope. */
        const float v_up = dot3(m->velocity, m->up);
        for (k = 0; k < 3; k++) {
            push[k] = m->up[k] * (c->depth / n_up);
            if (v_up < 0.0f) {
                m->velocity[k] -= m->up[k] * v_up;
            }
        }
        m->body->flags |= RR_NODE_FLAG_ON_FLOOR;
        g_floor = solid_shape;
    } else {
        for (k = 0; k < 3; k++) {
            push[k] = c->n[k] * c->depth;
        }
        if (m->character) {
            m->body->flags |= n_up < -FLOOR_COS ? RR_NODE_FLAG_ON_CEILING : RR_NODE_FLAG_ON_WALL;
            if (vn < 0.0f) {
                for (k = 0; k < 3; k++) {
                    m->velocity[k] -= c->n[k] * vn;
                }
            }
        } else if (vn < 0.0f) {
            /* Godot's rules for two materials: the bounces add up, the smaller friction
             * counts. A body that only presses on a floor does not bounce. */
            float bounce = m->bounce + solid_bounce, friction = m->friction, speed = 0.0f, tangent[3];
            bounce   = -vn < m->rest_speed ? 0.0f : (bounce > 1.0f ? 1.0f : bounce);
            friction = solid_friction < friction ? solid_friction : friction;
            for (k = 0; k < 3; k++) {
                m->velocity[k] -= c->n[k] * vn * (1.0f + bounce);
            }
            for (k = 0; k < 3; k++) {
                tangent[k] = m->velocity[k] - c->n[k] * dot3(m->velocity, c->n);
            }
            speed = sqrtf(dot3(tangent, tangent));
            if (speed > 0.00001f) {
                /* What the contact took away along its normal, times the friction. */
                const float loss = friction * -vn * (1.0f + bounce);
                const float keep = loss >= speed ? 0.0f : (speed - loss) / speed;
                for (k = 0; k < 3; k++) {
                    m->velocity[k] -= tangent[k] * (1.0f - keep);
                }
            }
        }
    }
    world_to_parent(m->body->parent, push, local);
    for (k = 0; k < 3; k++) {
        m->body->local.m[9 + k] += local[k];
        for (s = 0; s < m->shape_count; s++) {
            m->shapes[s].center[k] += push[k];
        }
    }
}

static void move_and_collide(Node *body, float *velocity, float dt, bool character)
{
    const RRScene       *scene = body->scene;
    const RRPkgBodyNode *data = (const RRPkgBodyNode *)body->data;
    Mover                m;
    RRMat34              body_world, shape_world;
    Node                *shape;
    float                step[3], local[3];
    uint32_t             i, k;
    int                  round, s, a;
    bool                 any = true;

    m.body        = body;
    m.velocity    = velocity;
    m.character   = character;
    m.bounce      = data->bounce;
    m.friction    = data->friction;
    m.up[0]       = 0.0f;
    m.up[1]       = data->two_d ? -1.0f : 1.0f;
    m.up[2]       = 0.0f;
    /* Slower than twice what gravity adds in a frame is resting, not bouncing. */
    m.rest_speed  = 2.0f * dt * sqrtf(dot3(data->gravity, data->gravity)) + 0.0001f;
    m.shape_count = 0;
    m.fall        = -dot3(velocity, m.up) * dt;
    m.fall        = m.fall > 0.0f ? m.fall : 0.0f;

    for (a = 0; a < 3; a++) {
        step[a] = velocity[a] * dt;
    }
    world_to_parent(body->parent, step, local);
    for (a = 0; a < 3; a++) {
        body->local.m[9 + a] += local[a];
    }
    rr_node_world_now(body, &body_world);
    for (shape = rr_node_child(body, NULL); shape != NULL && m.shape_count < MAX_BODY_SHAPES;
         shape = rr_node_child(body, shape)) {
        if (shape->type == RR_NODE_SHAPE && !((const RRPkgShapeNode *)shape->data)->disabled) {
            rr_mat34_mul(&shape_world, &body_world, &shape->local);
            rr_volume_make(&m.shapes[m.shape_count++], &shape_world, (const RRPkgShapeNode *)shape->data);
        }
    }

    for (round = 0; round < 4 && any; round++) {
        float near_min[3], near_max[3];

        any = false;
        /* The box around the body as it is now, a little larger: only static bodies whose
         * box touches it are looked at shape by shape. Their shapes are the ones the
         * trigger tests worked out for this frame (rr_colliders_refresh). */
        for (s = 0; s < m.shape_count; s++) {
            float lo[3], hi[3];
            rr_volume_bounds(&m.shapes[s], lo, hi);
            for (a = 0; a < 3; a++) {
                near_min[a] = s == 0 || lo[a] < near_min[a] ? lo[a] : near_min[a];
                near_max[a] = s == 0 || hi[a] > near_max[a] ? hi[a] : near_max[a];
            }
        }
        for (i = 0; i < scene->collider_count; i++) {
            const RRCollider    *col = &scene->colliders[i];
            const Node          *owner = &scene->nodes[col->node];
            const RRPkgBodyNode *other = (const RRPkgBodyNode *)owner->data;
            bool                 apart = false;

            if (col->area || owner->type != RR_NODE_STATIC_BODY || !col->live ||
                (owner->flags & RR_NODE_FLAG_FREE) || (data->mask & other->layer) == 0) {
                continue;
            }
            for (a = 0; a < 3; a++) {
                apart = apart || col->min[a] > near_max[a] + 0.01f || col->max[a] < near_min[a] - 0.01f;
            }
            if (apart) {
                continue;
            }
            for (k = 0; k < col->shape_count; k++) {
                const uint16_t  shape = scene->collider_shapes[col->shape_first + k];
                const RRVolume *solid = &scene->volumes[col->shape_first + k];
                Contact         c;

                if (!solid->used || (scene->nodes[shape].flags & RR_NODE_FLAG_FREE)) {
                    continue;
                }
                for (s = 0; s < m.shape_count; s++) {
                    if (volume_contact(&m.shapes[s], solid, &c) && !lets_through(&m, solid, &c)) {
                        resolve(&m, &c, other->bounce, other->friction, shape);
                        any = true;
                    }
                }
            }
        }
        for (i = 0; i < scene->tilemap_count; i++) {
            const Node             *map_node = &scene->nodes[scene->tilemaps[i]];
            const RRPkgTileMapNode *map_data = (const RRPkgTileMapNode *)map_node->data;
            const RRPkgTileMap     *map = (const RRPkgTileMap *)rr_package_asset(scene->pkg, map_data->tiles);
            const RRPkgTileBox     *boxes = (const RRPkgTileBox *)((const uint8_t *)map + map->boxes_off);

            if ((data->mask & map_data->layer) == 0 || (map_node->flags & RR_NODE_FLAG_FREE)) {
                continue;
            }
            for (k = 0; k < map->box_count; k++) {
                const RRPkgShapeNode box = { RR_SHAPE_BOX, { boxes[k].w * 0.5f, boxes[k].h * 0.5f, 0.5f },
                                             0, (uint8_t)boxes[k].one_way, { 0, 0 }, RR_NONE32 };
                const float          cx = boxes[k].x + boxes[k].w * 0.5f, cy = boxes[k].y + boxes[k].h * 0.5f;
                RRMat34              at = map_node->world;
                RRVolume             solid;
                Contact              c;

                at.m[9]  += at.m[0] * cx + at.m[3] * cy;
                at.m[10] += at.m[1] * cx + at.m[4] * cy;
                rr_volume_make(&solid, &at, &box);
                for (s = 0; s < m.shape_count; s++) {
                    if (volume_contact(&m.shapes[s], &solid, &c) && !lets_through(&m, &solid, &c)) {
                        resolve(&m, &c, 0.0f, 1.0f, RR_NONE16);
                        any = true;
                    }
                }
            }
        }
    }
}

/* True if the old, cheaper way gives the right answer: the body has box shapes only, and
 * so has everything it can run into, none of them rotated. */
static bool boxes_only(const Node *body)
{
    Node *shape;

    if (!body->scene->solids_are_boxes) {
        return false;
    }
    for (shape = rr_node_child((Node *)body, NULL); shape != NULL;
         shape = rr_node_child((Node *)body, shape)) {
        if (shape->type == RR_NODE_SHAPE && ((const RRPkgShapeNode *)shape->data)->kind != RR_SHAPE_BOX) {
            return false;
        }
    }
    return true;
}

bool rr_body_solids_are_boxes(const RRScene *scene)
{
    uint32_t i;
    int      a, b;

    for (i = 0; i < scene->solid_count; i++) {
        const Node  *shape = &scene->nodes[scene->solids[i]];
        const float *m = shape->world.m;

        if (((const RRPkgShapeNode *)shape->data)->kind != RR_SHAPE_BOX) {
            return false;
        }
        for (a = 0; a < 3; a++) {
            for (b = 0; b < 3; b++) {
                if (a != b && fabsf(m[a * 3 + b]) > 0.0001f) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* --- RigidBody ------------------------------------------------------------------------
 * A RigidBody2D is moved here: it falls, bounces and rubs by itself. It does not turn:
 * there is no angular
 * velocity, and bodies do not push each other; they collide with static bodies and tile
 * maps only. A RigidBody3D is a body of the physics engine (rr_physics.c), which turns it
 * and lets bodies push each other; the functions here pass on to it. */

static RRRigidState *rigid_state(const Node *body)
{
    RRScene       *scene = body->scene;
    const uint16_t index = (uint16_t)(body - scene->nodes);
    uint32_t       i;

    if (body->type != RR_NODE_RIGID_BODY) {
        return NULL;
    }
    for (i = 0; i < scene->rigid_count; i++) {
        if (scene->rigids[i].node == index) {
            return &scene->rigids[i];
        }
    }
    return NULL;
}

void rr_rigid_body_init(RRScene *scene, Node *body)
{
    RRRigidState *state = &scene->rigids[scene->rigid_count++];

    memset(state, 0, sizeof(*state));
    state->node = (uint16_t)(body - scene->nodes);
    /* A RigidBody3D gets its body when the whole scene is there (rr_rigid_bodies_ready):
     * its shapes are its children, which are made after it. */
    if (((const RRPkgBodyNode *)body->data)->two_d) {
        rr_rigid_body_reset(body);
    }
}

void rr_rigid_bodies_ready(RRScene *scene)
{
    uint32_t i;

    rr_physics_begin(scene);
    for (i = 0; i < scene->rigid_count; i++) {
        Node *body = &scene->nodes[scene->rigids[i].node];
        if (!((const RRPkgBodyNode *)body->data)->two_d) {
            rr_physics_body_init(scene, body, &scene->rigids[i]);
        }
    }
}

void rr_rigid_body_reset(Node *body)
{
    const RRPkgBodyNode *data = (const RRPkgBodyNode *)body->data;
    RRRigidState        *state = rigid_state(body);
    int                  a;

    if (!data->two_d) {
        rr_physics_body_reset(body, state);
        return;
    }
    for (a = 0; a < 3; a++) {
        state->velocity[a] = data->velocity[a];
    }
}

void rr_rigid_bodies_update(RRScene *scene, float dt)
{
    uint32_t i;
    int      a;

    /* A long frame must not carry a body through a wall. */
    if (dt > 1.0f / 20.0f) {
        dt = 1.0f / 20.0f;
    }
    /* The 2D bodies are moved here, without turning. */
    for (i = 0; i < scene->rigid_count; i++) {
        RRRigidState        *state = &scene->rigids[i];
        Node                *body = &scene->nodes[state->node];
        const RRPkgBodyNode *data = (const RRPkgBodyNode *)body->data;
        const float          keep = 1.0f - data->linear_damp * dt;

        if (!data->two_d || (body->flags & RR_NODE_FLAG_FREE)) {
            continue;
        }
        for (a = 0; a < 3; a++) {
            state->velocity[a] = (state->velocity[a] + data->gravity[a] * dt) * (keep > 0.0f ? keep : 0.0f);
        }
        move_and_collide(body, state->velocity, dt, false);
    }
    /* The 3D bodies are the physics engine's. */
    rr_physics_step(scene, dt);
}

/* True for a body that the physics engine moves: a RigidBody3D. */
static bool in_engine(const Node *body, const RRRigidState *state)
{
    return state != NULL && !((const RRPkgBodyNode *)body->data)->two_d;
}

void rr_rigid_body_get_linear_velocity(const Node *body, float *out)
{
    const RRRigidState *state = rigid_state(body);
    int                 a;

    if (in_engine(body, state)) {
        rr_physics_get_velocity(state, out, NULL);
        return;
    }
    for (a = 0; a < 3; a++) {
        out[a] = state != NULL ? state->velocity[a] : 0.0f;
    }
}

void rr_rigid_body_set_linear_velocity(Node *body, const float *velocity)
{
    RRRigidState *state = rigid_state(body);
    int           a;

    if (in_engine(body, state)) {
        rr_physics_set_velocity(state, velocity, NULL);
        return;
    }
    for (a = 0; state != NULL && a < 3; a++) {
        state->velocity[a] = velocity[a];
    }
}

void rr_rigid_body_get_angular_velocity(const Node *body, float *out)
{
    const RRRigidState *state = rigid_state(body);

    out[0] = out[1] = out[2] = 0.0f;
    if (in_engine(body, state)) {
        rr_physics_get_velocity(state, NULL, out);
    }
}

void rr_rigid_body_set_angular_velocity(Node *body, const float *velocity)
{
    RRRigidState *state = rigid_state(body);

    if (in_engine(body, state)) {
        rr_physics_set_velocity(state, NULL, velocity);
    }
}

void rr_rigid_body_apply_impulse(Node *body, const float *impulse)
{
    rr_rigid_body_apply_impulse_at(body, impulse, NULL);
}

void rr_rigid_body_apply_impulse_at(Node *body, const float *impulse, const float *position)
{
    RRRigidState *state = rigid_state(body);
    const float   mass = state != NULL ? ((const RRPkgBodyNode *)body->data)->mass : 1.0f;
    int           a;

    if (in_engine(body, state)) {
        rr_physics_apply_impulse(state, impulse, position);
        return;
    }
    for (a = 0; state != NULL && a < 3; a++) {
        state->velocity[a] += impulse[a] / (mass > 0.0f ? mass : 1.0f);
    }
}

int rr_rigid_body_get_contact_count(const Node *body)
{
    const RRRigidState *state = rigid_state(body);
    return state != NULL ? state->contact_count : 0;
}

void rr_rigid_body_get_contact_normal(const Node *body, int index, float *out)
{
    const RRRigidState *state = rigid_state(body);
    int                 a;

    for (a = 0; a < 3; a++) {
        out[a] = state != NULL && index >= 0 && index < state->contact_count ? state->contact_normals[index][a] : 0.0f;
    }
}

static void move_and_slide(Node *body, float *velocity, float dt)
{
    const RRScene        *scene = body->scene;
    const RRPkgBodyNode  *data = (const RRPkgBodyNode *)body->data;
    Bounds                box;
    float                 moved[3] = { 0.0f, 0.0f, 0.0f }, local[3];
    int                   axis;
    uint32_t              i;

    const uint8_t         flags_before = body->flags;

    if (body->type != RR_NODE_CHARACTER_BODY) {
        return;
    }
    body->flags &= (uint8_t)~(RR_NODE_FLAG_ON_FLOOR | RR_NODE_FLAG_ON_WALL | RR_NODE_FLAG_ON_CEILING);
    if (!boxes_only(body)) {
        const bool  was_on_floor = (flags_before & RR_NODE_FLAG_ON_FLOOR) != 0;
        const float up = data->two_d ? -1.0f : 1.0f;

        move_and_collide(body, velocity, dt, true);
        if (was_on_floor && !(body->flags & RR_NODE_FLAG_ON_FLOOR) && velocity[1] * up <= 0.0f && dt > 0.0f) {
            /* Godot's floor snap: a body that walked off the ground without jumping is
             * put back down if the ground is within reach (floor_snap_length, 0.1). This
             * is what keeps it on the floor when it walks down a slope. */
            const RRMat34 before = body->local;
            const uint8_t flags = body->flags;
            float         down[3] = { 0.0f, -up * FLOOR_SNAP / dt, 0.0f };

            move_and_collide(body, down, dt, true);
            if (body->flags & RR_NODE_FLAG_ON_FLOOR) {
                velocity[1] = 0.0f;
            } else {
                body->local = before;
                body->flags = flags;
                g_floor     = RR_NONE16;
            }
        }
        return;
    }
    if (!body_bounds(body, &box)) {
        /* Nothing to collide with: the body just moves. */
        box.min[0] = box.min[1] = box.min[2] = 0.0f;
        box.max[0] = box.max[1] = box.max[2] = -1.0f;
    }

    for (axis = 0; axis < 3; axis++) {
        const float step = velocity[axis] * dt;
        bool        blocked = false;

        if (step == 0.0f) {
            continue;
        }
        box.min[axis] += step;
        box.max[axis] += step;
        moved[axis] = step;
        /* Only static bodies whose box (worked out by rr_colliders_refresh) reaches the
         * body's box are looked at shape by shape. */
        for (i = 0; i < scene->collider_count; i++) {
            const RRCollider    *col = &scene->colliders[i];
            const Node          *owner = &scene->nodes[col->node];
            const RRPkgBodyNode *other = (const RRPkgBodyNode *)owner->data;
            uint32_t             k;
            int                  a;
            bool                 apart = false;

            if (col->area || owner->type != RR_NODE_STATIC_BODY || !col->live ||
                (data->mask & other->layer) == 0) {
                continue;
            }
            for (a = 0; a < 3; a++) {
                apart = apart || col->min[a] > box.max[a] + SKIN || col->max[a] < box.min[a] - SKIN;
            }
            if (apart) {
                continue;
            }
            for (k = 0; k < col->shape_count; k++) {
                const uint16_t index = scene->collider_shapes[col->shape_first + k];
                const Node    *shape = &scene->nodes[index];
                Bounds         solid;

                if (!is_solid_box(shape)) {
                    continue;
                }
                shape_bounds(&solid, &shape->world, (const RRPkgShapeNode *)shape->data);
                if (((const RRPkgShapeNode *)shape->data)->one_way &&
                    passes_one_way(&box, &solid, axis, step, moved[axis])) {
                    continue;
                }
                if (push_out(&box, &solid, axis, step, &moved[axis])) {
                    blocked = true;
                    if (axis == 1 && (data->two_d ? step > 0.0f : step < 0.0f)) {
                        g_floor = index;
                    }
                }
            }
        }
        /* The solid boxes of tile maps. */
        for (i = 0; i < scene->tilemap_count; i++) {
            const Node             *map_node = &scene->nodes[scene->tilemaps[i]];
            const RRPkgTileMapNode *map_data = (const RRPkgTileMapNode *)map_node->data;
            const RRPkgTileMap     *map = (const RRPkgTileMap *)rr_package_asset(scene->pkg, map_data->tiles);
            const RRPkgTileBox     *boxes = (const RRPkgTileBox *)((const uint8_t *)map + map->boxes_off);
            uint32_t                k;

            if ((data->mask & map_data->layer) == 0 || (map_node->flags & RR_NODE_FLAG_FREE)) {
                continue;
            }
            for (k = 0; k < map->box_count; k++) {
                Bounds solid;
                tile_box_bounds(&solid, &map_node->world, &boxes[k]);
                if (boxes[k].one_way && passes_one_way(&box, &solid, axis, step, moved[axis])) {
                    continue;
                }
                if (push_out(&box, &solid, axis, step, &moved[axis])) {
                    blocked = true;
                }
            }
        }
        if (blocked) {
            /* Up is +Y in 3D and -Y on the screen in 2D. */
            const bool downwards = data->two_d ? step > 0.0f : step < 0.0f;
            if (axis != 1) {
                body->flags |= RR_NODE_FLAG_ON_WALL;
            } else {
                body->flags |= downwards ? RR_NODE_FLAG_ON_FLOOR : RR_NODE_FLAG_ON_CEILING;
            }
            velocity[axis] = 0.0f;
        }
    }

    world_to_parent(body->parent, moved, local);
    body->local.m[9]  += local[0];
    body->local.m[10] += local[1];
    body->local.m[11] += local[2];
}

static RRCharState *char_state(const Node *body)
{
    RRScene       *scene = body->scene;
    const uint16_t index = (uint16_t)(body - scene->nodes);
    uint32_t       i;

    for (i = 0; i < scene->char_count; i++) {
        if (scene->chars[i].node == index) {
            return &scene->chars[i];
        }
    }
    return NULL;
}

void rr_body_move_and_slide(Node *body, float *velocity, float dt)
{
    RRCharState *state;
    int          a;

    if (body->type != RR_NODE_CHARACTER_BODY) {
        return;
    }
    state = char_state(body);
    if (state != NULL && state->floor != RR_NONE16) {
        /* A floor that moves takes the body along: by as much as it moved since the
         * body came to stand on it. */
        const Node *floor = &body->scene->nodes[state->floor];
        float       carried[3], local[3];
        for (a = 0; a < 3; a++) {
            carried[a] = floor->world.m[9 + a] - state->floor_pos[a];
        }
        if (!(floor->flags & RR_NODE_FLAG_FREE) &&
            (carried[0] != 0.0f || carried[1] != 0.0f || carried[2] != 0.0f)) {
            world_to_parent(body->parent, carried, local);
            for (a = 0; a < 3; a++) {
                body->local.m[9 + a] += local[a];
            }
        }
    }
    g_floor = RR_NONE16;
    move_and_slide(body, velocity, dt);
    if (state != NULL) {
        state->floor = g_floor;
        for (a = 0; g_floor != RR_NONE16 && a < 3; a++) {
            state->floor_pos[a] = body->scene->nodes[g_floor].world.m[9 + a];
        }
    }
}

bool rr_body_is_on_floor(const Node *body)
{
    return (body->flags & RR_NODE_FLAG_ON_FLOOR) != 0;
}

bool rr_body_is_on_wall(const Node *body)
{
    return (body->flags & RR_NODE_FLAG_ON_WALL) != 0;
}

bool rr_body_is_on_ceiling(const Node *body)
{
    return (body->flags & RR_NODE_FLAG_ON_CEILING) != 0;
}

bool rr_volume_contact(const RRVolume *mover, const RRVolume *solid, float *normal, float *depth)
{
    Contact c;
    int     k;

    if (!volume_contact(mover, solid, &c)) {
        return false;
    }
    for (k = 0; k < 3; k++) {
        normal[k] = c.n[k];
    }
    *depth = c.depth;
    return true;
}
