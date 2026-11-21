/* RigidBody3D: bodies that fall, turn, rub, bounce and push each other.
 *
 * The work is done by picophysics, a small physics engine in one header made for the N64
 * and the Dreamcast (runtime/third_party/picophysics). This file is what stands between
 * Godot's nodes and that engine:
 *
 * - Every RigidBody3D of the scene is a body of the engine, made of the box and sphere
 *   shapes below the node, with the node's mass, physics material, damping, gravity
 *   scale, start velocities and locked axes. After every step the node is stood where
 *   its body is. A node that game code moved is taken as moved: the body follows.
 * - The engine's static world is made of triangles, and it looks at every triangle it has
 *   for every body. So it is given, before every step, only what of the scene's
 *   StaticBody3D nodes is near a rigid body: the sides of their boxes and the cells of
 *   their height maps as triangles, their spheres as bodies that do not move. They are
 *   taken from the shapes as the scene has them this frame, so a static body that
 *   something moves is where it is.
 * - Layers and masks are kept the way Godot keeps them: a body meets a static body if
 *   its mask has one of the other's layers, and two rigid bodies meet if either one's
 *   mask has one of the other's layers.
 * - A body that monitors its contacts (contact_monitor, max_contacts_reported) is told
 *   what it touched in the step: rr_rigid_body_get_contact_count and its siblings.
 *
 * Where picophysics has its own rules they stand: two frictions are multiplied (Godot
 * takes the smaller one), and a triangle does not bounce by itself. RigidBody2D is not
 * done here: rr_body.c moves those.
 *
 * The engine has one world for the whole program, which is this scene's as long as the
 * scene is the one that was made last. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rr_internal.h"

/* The header is compiled here, as its one implementation, with the warnings off that this
 * project asks for and it does not heed. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wold-style-definition"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846 /* plain C11 has none */
#endif
#define RR_MAX_STATIC_SPHERES 8
#define PICOPHYSICS_MAX_OBJECTS   (64 + RR_MAX_STATIC_SPHERES)
#define PICOPHYSICS_MAX_TRIANGLES 1024
#define PICOPHYSICS_MAX_MANIFOLDS 512
#define PICOPHYSICS_IMPLEMENTATION
#include "../third_party/picophysics/picophysics.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* Bodies with the same layer and mask are of one class; the engine knows a body by a
 * small number, its kind, and is told per pair of kinds whether the two meet. */
#define MAX_CLASSES   6
#define KIND_BODY     1  /* + class */
#define KIND_STATIC   32 /* + class */
#define MAX_KIND_PAIRS 32 /* what the engine's table holds */

typedef struct {
    uint32_t layer, mask;
} Class;

static const RRScene *g_scene;
static Class          g_body_class[MAX_CLASSES], g_static_class[MAX_CLASSES];
static int            g_body_classes, g_static_classes;
static bool           g_pairs_full, g_bodies_full, g_triangles_full, g_spheres_full;
static PPBody        *g_spheres[RR_MAX_STATIC_SPHERES];
static bool           g_gravity_set;

/* --- kinds and what meets what ---------------------------------------------------------- */

/* Called by the engine for every contact of two kinds that meet: notes it for the bodies
 * that monitor theirs. The normal the engine gives points from the first body into what
 * it touches; a body is told the way out of what it touches. */
static bool on_contact(const void *lhs, const void *rhs, BodyKind k0, BodyKind k1, const PPCollision *hit,
                       const void *user)
{
    const PPBody *bodies[2] = { (const PPBody *)lhs, k1 >= KIND_STATIC ? NULL : (const PPBody *)rhs };
    int           side, k;

    (void)k0;
    (void)user;
    for (side = 0; side < 2; side++) {
        RRRigidState *state = bodies[side] != NULL ? (RRRigidState *)bodies[side]->user_data : NULL;
        if (state != NULL && state->contact_count < state->contact_max) {
            float *n = state->contact_normals[state->contact_count++];
            for (k = 0; k < 3; k++) {
                n[k] = side == 0 ? -hit->n.xyz[k] : hit->n.xyz[k];
            }
        }
    }
    return true; /* and the engine is to keep the two apart */
}

static void add_pair(BodyKind a, BodyKind b, bool meet)
{
    if (collision_map_count >= MAX_KIND_PAIRS) {
        if (!g_pairs_full) {
            printf("retro: too many different layers and masks among the rigid and static bodies; "
                   "some pairs meet that should not\n");
            g_pairs_full = true;
        }
        return;
    }
    /* A pair without a function does not meet at all. */
    pp_physics_collision_map_add(a, b, NULL, meet ? on_contact : NULL);
}

static BodyKind body_kind(uint32_t layer, uint32_t mask)
{
    int i, j;

    for (i = 0; i < g_body_classes; i++) {
        if (g_body_class[i].layer == layer && g_body_class[i].mask == mask) {
            return (BodyKind)(KIND_BODY + i);
        }
    }
    if (g_body_classes == MAX_CLASSES) {
        return KIND_BODY; /* add_pair has said so when the table was full */
    }
    i                     = g_body_classes++;
    g_body_class[i].layer = layer;
    g_body_class[i].mask  = mask;
    for (j = 0; j < g_static_classes; j++) {
        add_pair((BodyKind)(KIND_BODY + i), (BodyKind)(KIND_STATIC + j), (mask & g_static_class[j].layer) != 0);
    }
    for (j = 0; j <= i; j++) {
        add_pair((BodyKind)(KIND_BODY + i), (BodyKind)(KIND_BODY + j),
                 (mask & g_body_class[j].layer) != 0 || (g_body_class[j].mask & layer) != 0);
    }
    return (BodyKind)(KIND_BODY + i);
}

static BodyKind static_kind(uint32_t layer)
{
    int i, j;

    for (i = 0; i < g_static_classes; i++) {
        if (g_static_class[i].layer == layer) {
            return (BodyKind)(KIND_STATIC + i);
        }
    }
    if (g_static_classes == MAX_CLASSES) {
        return KIND_STATIC;
    }
    i                       = g_static_classes++;
    g_static_class[i].layer = layer;
    for (j = 0; j < g_body_classes; j++) {
        add_pair((BodyKind)(KIND_BODY + j), (BodyKind)(KIND_STATIC + i), (g_body_class[j].mask & layer) != 0);
    }
    return (BodyKind)(KIND_STATIC + i);
}

/* --- turning between the scene's matrices and the engine's rotations ------------------ */

/* The rotation of a matrix whose three axes are of unit length and square to each other. */
static void quat_of(const float *m, PPQuaternion *q)
{
    /* m[0..2] is the X axis, m[3..5] Y, m[6..8] Z: the columns of the rotation. */
    const float m00 = m[0], m10 = m[1], m20 = m[2], m01 = m[3], m11 = m[4], m21 = m[5];
    const float m02 = m[6], m12 = m[7], m22 = m[8], trace = m00 + m11 + m22;

    if (trace > 0.0f) {
        const float s = sqrtf(trace + 1.0f) * 2.0f;
        q->w = 0.25f * s;
        q->x = (m21 - m12) / s;
        q->y = (m02 - m20) / s;
        q->z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q->w = (m21 - m12) / s;
        q->x = 0.25f * s;
        q->y = (m01 + m10) / s;
        q->z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q->w = (m02 - m20) / s;
        q->x = (m01 + m10) / s;
        q->y = 0.25f * s;
        q->z = (m12 + m21) / s;
    } else {
        const float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q->w = (m10 - m01) / s;
        q->x = (m02 + m20) / s;
        q->y = (m12 + m21) / s;
        q->z = 0.25f * s;
    }
}

/* Where a body is, as a matrix of the world. */
static void body_world(PPBody *body, RRMat34 *out)
{
    PPVec3 x, y, z;
    int    k;

    pp_body_get_right(body, &x);
    pp_body_get_up(body, &y);
    pp_body_get_forward(body, &z); /* the engine's "forward" is the body's +Z */
    for (k = 0; k < 3; k++) {
        out->m[k]     = x.xyz[k];
        out->m[3 + k] = y.xyz[k];
        out->m[6 + k] = z.xyz[k];
        out->m[9 + k] = body->pos.xyz[k];
    }
}

/* Stands a node where `world` says, whatever its parents are. */
void rr_node_set_world(Node *node, const RRMat34 *world)
{
    const float *p, *w = world->m;
    float        c[9], det, inv, d[3];
    int          i, k;

    if (node->parent == NULL) {
        node->local = *world;
        return;
    }
    /* The inverse of the parent's basis through its cofactors, as rr_body.c does it. */
    p    = node->parent->world.m;
    c[0] = p[4] * p[8] - p[5] * p[7];
    c[1] = p[5] * p[6] - p[3] * p[8];
    c[2] = p[3] * p[7] - p[4] * p[6];
    c[3] = p[2] * p[7] - p[1] * p[8];
    c[4] = p[0] * p[8] - p[2] * p[6];
    c[5] = p[1] * p[6] - p[0] * p[7];
    c[6] = p[1] * p[5] - p[2] * p[4];
    c[7] = p[2] * p[3] - p[0] * p[5];
    c[8] = p[0] * p[4] - p[1] * p[3];
    det  = p[0] * c[0] + p[1] * c[1] + p[2] * c[2];
    inv  = det != 0.0f ? 1.0f / det : 0.0f;
    for (i = 0; i < 4; i++) {
        /* The three axes, and then the place seen from the parent's. */
        for (k = 0; k < 3; k++) {
            d[k] = i < 3 ? w[i * 3 + k] : w[9 + k] - p[9 + k];
        }
        for (k = 0; k < 3; k++) {
            node->local.m[i * 3 + k] = (c[k] * d[0] + c[3 + k] * d[1] + c[6 + k] * d[2]) * inv;
        }
    }
}

/* Puts a body where its node is. */
static void body_follow_node(RRRigidState *state, const Node *node)
{
    PPBody *body = (PPBody *)state->body;
    RRMat34 world;

    rr_node_world_now(node, &world);
    pp_vec3_set(&body->pos, world.m[9], world.m[10], world.m[11]);
    quat_of(world.m, &body->rot);
    state->placed = node->local;
}

/* --- making the bodies ------------------------------------------------------------------ */

void rr_physics_begin(const RRScene *scene)
{
    int i;

    g_scene = scene;
    pp_physics_clear();
    collision_map_count = 0; /* the engine keeps its table of pairs over a clear */
    g_body_classes = g_static_classes = 0;
    g_pairs_full = g_bodies_full = g_triangles_full = g_spheres_full = false;
    g_gravity_set = false;
    for (i = 0; i < RR_MAX_STATIC_SPHERES; i++) {
        g_spheres[i] = NULL;
    }
}

void rr_physics_body_init(RRScene *scene, Node *node, RRRigidState *state)
{
    const RRPkgBodyNode *data = (const RRPkgBodyNode *)node->data;
    const float          pull = sqrtf(data->gravity[0] * data->gravity[0] + data->gravity[1] * data->gravity[1] +
                                      data->gravity[2] * data->gravity[2]);
    const BodyKind       kind = body_kind(data->layer, data->mask);
    PPBody              *body;
    Node                *shape;
    int                  shapes = 0;

    state->body = NULL;
    if (scene != g_scene) {
        rr_physics_begin(scene);
    }
    if (object_count >= PICOPHYSICS_MAX_OBJECTS - RR_MAX_STATIC_SPHERES) {
        if (!g_bodies_full) {
            printf("retro: more than %d rigid bodies in a scene; node '%s' and the ones after it do not move\n",
                   PICOPHYSICS_MAX_OBJECTS - RR_MAX_STATIC_SPHERES, node->name);
            g_bodies_full = true;
        }
        return;
    }
    for (shape = rr_node_child(node, NULL); shape != NULL; shape = rr_node_child(node, shape)) {
        shapes += shape->type == RR_NODE_SHAPE && !((const RRPkgShapeNode *)shape->data)->disabled ? 1 : 0;
    }
    if (shapes == 0) {
        return;
    }
    body = pp_physics_create_body(NULL);
    for (shape = rr_node_child(node, NULL); shape != NULL; shape = rr_node_child(node, shape)) {
        const RRPkgShapeNode *s = (const RRPkgShapeNode *)shape->data;
        const float          *at = &shape->local.m[9];
        /* The mass is spread over the shapes evenly. */
        const float           part = (data->mass > 0.0f ? data->mass : 1.0f) / (float)shapes;
        if (shape->type != RR_NODE_SHAPE || s->disabled || body->shape_count >= PP_MAX_SHAPES_PER_BODY) {
            continue;
        }
        if (s->kind == RR_SHAPE_SPHERE) {
            pp_body_add_sphere(body, s->size[0], part, kind, at[0], at[1], at[2]);
        } else {
            pp_body_add_box(body, s->size[0] * 2.0f, s->size[1] * 2.0f, s->size[2] * 2.0f, part, kind, at[0], at[1],
                            at[2]);
        }
    }
    body->kind      = kind;
    body->user_data = state;
    body->bounce    = data->bounce < 0.0f ? 0.0f : (data->bounce > 1.0f ? 1.0f : data->bounce);
    body->friction  = data->friction < 0.0f ? 0.0f : (data->friction > 1.0f ? 1.0f : data->friction);
    body->damping   = data->linear_damp;
    body->a_damping = data->angular_damp;
    body->lock      = (PPAxisLock)data->lock_angular; /* the same three bits */
    /* The engine has one gravity for all and a factor for each body: the first body's
     * gravity is the world's, and the others fall by their own strength along it. */
    if (!g_gravity_set && pull > 0.0f) {
        PPVec3 down;
        pp_vec3_set(&down, data->gravity[0] / pull, data->gravity[1] / pull, data->gravity[2] / pull);
        pp_physics_set_gravity(&down);
        g_gravity_set = true;
    }
    body->gravity_multiplier = pull;
    state->body              = body;
    state->contact_max       = data->contacts < RR_MAX_CONTACTS ? data->contacts : RR_MAX_CONTACTS;
    rr_physics_body_reset(node, state);
}

void rr_physics_body_reset(Node *node, RRRigidState *state)
{
    const RRPkgBodyNode *data = (const RRPkgBodyNode *)node->data;
    PPBody              *body = (PPBody *)state->body;

    state->contact_count = 0;
    state->synced        = false; /* the node may not be where it will be yet */
    if (body != NULL) {
        pp_vec3_set(&body->vel, data->velocity[0], data->velocity[1], data->velocity[2]);
        pp_vec3_set(&body->a_vel, data->angular_velocity[0], data->angular_velocity[1], data->angular_velocity[2]);
    }
}

/* --- the static world near the bodies ------------------------------------------------- */

typedef struct {
    float    lo[3], hi[3]; /* the box around a body and where it may get to in this step */
    uint32_t mask;
} Reach;

/* One triangle of the static world, if it is near a body that can meet it. */
static void add_triangle(const float *a, const float *b, const float *c, const RRPkgBodyNode *owner, BodyKind kind,
                         const Reach *reach, int count)
{
    PPVec3      v[3];
    PPTriangle *made;
    float       lo, hi;
    bool        near = false;
    int         i, k;

    for (i = 0; i < count && !near; i++) {
        if ((reach[i].mask & owner->layer) == 0) {
            continue;
        }
        near = true;
        for (k = 0; k < 3; k++) {
            lo   = a[k] < b[k] ? (a[k] < c[k] ? a[k] : c[k]) : (b[k] < c[k] ? b[k] : c[k]);
            hi   = a[k] > b[k] ? (a[k] > c[k] ? a[k] : c[k]) : (b[k] > c[k] ? b[k] : c[k]);
            near = near && lo <= reach[i].hi[k] && hi >= reach[i].lo[k];
        }
    }
    if (!near) {
        return;
    }
    if (tri_count >= PICOPHYSICS_MAX_TRIANGLES) {
        if (!g_triangles_full) {
            printf("retro: more than %d triangles of static bodies near the rigid bodies; some are not solid\n",
                   PICOPHYSICS_MAX_TRIANGLES);
            g_triangles_full = true;
        }
        return;
    }
    pp_vec3_set(&v[0], a[0], a[1], a[2]);
    pp_vec3_set(&v[1], b[0], b[1], b[2]);
    pp_vec3_set(&v[2], c[0], c[1], c[2]);
    made           = pp_physics_create_triangle(&v[0], &v[1], &v[2], kind);
    made->friction = owner->friction < 0.0f ? 0.0f : (owner->friction > 1.0f ? 1.0f : owner->friction);
    made->bounce   = owner->bounce;
}

/* The six sides of a box. The engine's triangles stop only what comes from the side their
 * corners go round counter-clockwise, so every side faces out of the box. */
static void add_box(const RRVolume *box, const RRPkgBodyNode *owner, BodyKind kind, const Reach *reach, int count)
{
    /* Corner i: bit 0 is +X, bit 1 is +Y, bit 2 is +Z of the box. Each side as four
     * corners that go round it counter-clockwise, seen from outside. */
    static const uint8_t sides[6][4] = { { 1, 3, 7, 5 }, { 0, 4, 6, 2 }, { 2, 6, 7, 3 },
                                         { 0, 1, 5, 4 }, { 4, 5, 7, 6 }, { 0, 2, 3, 1 } };
    float                corner[8][3];
    int                  i, k;

    for (i = 0; i < 8; i++) {
        for (k = 0; k < 3; k++) {
            corner[i][k] = box->center[k] + box->axis[0][k] * ((i & 1) ? box->half[0] : -box->half[0]) +
                           box->axis[1][k] * ((i & 2) ? box->half[1] : -box->half[1]) +
                           box->axis[2][k] * ((i & 4) ? box->half[2] : -box->half[2]);
        }
    }
    for (i = 0; i < 6; i++) {
        add_triangle(corner[sides[i][0]], corner[sides[i][1]], corner[sides[i][2]], owner, kind, reach, count);
        add_triangle(corner[sides[i][0]], corner[sides[i][2]], corner[sides[i][3]], owner, kind, reach, count);
    }
}

/* The cells of a height map below the bodies, two triangles each, split along the same
 * diagonal as everywhere else (rr_height_at). */
static void add_height_map(const RRVolume *ground, const RRPkgBodyNode *owner, BodyKind kind, const Reach *reach,
                           int count)
{
    const RRPkgHeightMap *map = ground->map;
    int                   i, x, z;

    for (i = 0; i < count; i++) {
        int x0 = (int)floorf((reach[i].lo[0] - ground->origin[0]) / ground->scale[0]);
        int x1 = (int)floorf((reach[i].hi[0] - ground->origin[0]) / ground->scale[0]);
        int z0 = (int)floorf((reach[i].lo[2] - ground->origin[2]) / ground->scale[2]);
        int z1 = (int)floorf((reach[i].hi[2] - ground->origin[2]) / ground->scale[2]);
        if ((reach[i].mask & owner->layer) == 0) {
            continue;
        }
        x0 = x0 < 0 ? 0 : x0;
        z0 = z0 < 0 ? 0 : z0;
        x1 = x1 > map->width - 2 ? map->width - 2 : x1;
        z1 = z1 > map->depth - 2 ? map->depth - 2 : z1;
        for (z = z0; z <= z1; z++) {
            for (x = x0; x <= x1; x++) {
                const float *row = &map->heights[z * map->width + x];
                float        p[4][3];
                int          n;
                for (n = 0; n < 4; n++) {
                    p[n][0] = ground->origin[0] + (float)(x + (n & 1)) * ground->scale[0];
                    p[n][1] = ground->origin[1] + row[(n & 1) + ((n & 2) ? map->width : 0)] * ground->scale[1];
                    p[n][2] = ground->origin[2] + (float)(z + (n >> 1)) * ground->scale[2];
                }
                /* p[0] is (x, z), p[1] (x + 1, z), p[2] (x, z + 1), p[3] (x + 1, z + 1). Two
                 * bodies above the same cell get it only once: the engine drops doubles
                 * of a contact, and the first body's reach is the one that is asked. */
                add_triangle(p[0], p[2], p[1], owner, kind, &reach[i], 1);
                add_triangle(p[1], p[2], p[3], owner, kind, &reach[i], 1);
            }
        }
    }
}

/* A static sphere is a body of the engine that does not move. */
static void add_sphere(const RRVolume *ball, const RRPkgBodyNode *owner, BodyKind kind, int *used)
{
    PPBody *body;

    if (*used >= RR_MAX_STATIC_SPHERES) {
        if (!g_spheres_full) {
            printf("retro: more than %d static spheres near the rigid bodies; some are not solid\n",
                   RR_MAX_STATIC_SPHERES);
            g_spheres_full = true;
        }
        return;
    }
    if (g_spheres[*used] == NULL) {
        PPVec3 at;
        pp_vec3_set(&at, 0.0f, 0.0f, 0.0f);
        g_spheres[*used] = pp_physics_create_sphere(1.0f, &at, 0.0f, kind);
    }
    body                           = g_spheres[(*used)++];
    body->is_alive                 = true;
    body->kind                     = kind;
    body->shapes[0].kind           = kind;
    body->shapes[0].sphere.radius  = ball->half[0];
    body->friction                 = owner->friction < 0.0f ? 0.0f : (owner->friction > 1.0f ? 1.0f : owner->friction);
    body->bounce                   = owner->bounce < 0.0f ? 0.0f : (owner->bounce > 1.0f ? 1.0f : owner->bounce);
    pp_vec3_set(&body->pos, ball->center[0], ball->center[1], ball->center[2]);
}

/* Gives the engine what of the static bodies is near the rigid ones. */
static void gather_static(const RRScene *scene, const Reach *reach, int count)
{
    uint32_t i, s;
    int      k, b, spheres = 0;

    tri_count = 0; /* the engine has no call that drops its triangles and keeps its bodies */
    for (i = 0; i < scene->collider_count; i++) {
        const RRCollider    *col = &scene->colliders[i];
        const Node          *owner = &scene->nodes[col->node];
        const RRPkgBodyNode *data = (const RRPkgBodyNode *)owner->data;
        bool                 near = false;
        BodyKind             kind;

        if (col->area || owner->type != RR_NODE_STATIC_BODY || !col->live || (owner->flags & RR_NODE_FLAG_FREE) ||
            data->two_d) {
            continue;
        }
        for (b = 0; b < count && !near; b++) {
            near = (reach[b].mask & data->layer) != 0;
            for (k = 0; k < 3; k++) {
                near = near && col->min[k] <= reach[b].hi[k] && col->max[k] >= reach[b].lo[k];
            }
        }
        if (!near) {
            continue;
        }
        kind = static_kind(data->layer);
        for (s = 0; s < col->shape_count; s++) {
            const RRVolume *v = &scene->volumes[col->shape_first + s];
            if (!v->used || (scene->nodes[scene->collider_shapes[col->shape_first + s]].flags & RR_NODE_FLAG_FREE)) {
                continue;
            }
            if (v->map != NULL) {
                add_height_map(v, data, kind, reach, count);
            } else if (v->sphere) {
                add_sphere(v, data, kind, &spheres);
            } else if (!v->capsule) {
                add_box(v, data, kind, reach, count);
            }
        }
    }
    for (k = spheres; k < RR_MAX_STATIC_SPHERES; k++) {
        if (g_spheres[k] != NULL) {
            g_spheres[k]->is_alive = false;
        }
    }
}

/* --- the step ----------------------------------------------------------------------------- */

void rr_physics_step(RRScene *scene, float dt)
{
    static Reach reach[PICOPHYSICS_MAX_OBJECTS];
    uint32_t     i;
    int          count = 0, k, s;

    if (scene != g_scene) {
        return; /* a scene that is not the one made last has no bodies in the engine */
    }
    for (i = 0; i < scene->rigid_count; i++) {
        RRRigidState        *state = &scene->rigids[i];
        Node                *node = &scene->nodes[state->node];
        const RRPkgBodyNode *data = (const RRPkgBodyNode *)node->data;
        PPBody              *body = (PPBody *)state->body;
        float                size = 0.0f, speed;

        if (body == NULL) {
            continue;
        }
        /* A body whose node is out of the tree is out of the world. */
        body->is_alive       = !(node->flags & RR_NODE_FLAG_FREE);
        state->contact_count = 0;
        if (!body->is_alive) {
            continue;
        }
        /* The node was moved by something else than this file since the last step: by
         * game code, an animation or its parent. The body goes where the node is. */
        if (!state->synced || memcmp(&state->placed, &node->local, sizeof(RRMat34)) != 0) {
            body_follow_node(state, node);
            state->synced = true;
        }
        for (s = 0; s < body->shape_count; s++) {
            const PPShape *shape = &body->shapes[s];
            const float    r = shape->type == PP_OBJECT_TYPE_SPHERE ? shape->sphere.radius : shape->box.bounding_radius;
            const float    far = pp_vec3_length(&shape->offset) + r;
            size = far > size ? far : size;
        }
        speed = pp_vec3_length(&body->vel);
        for (k = 0; k < 3; k++) {
            reach[count].lo[k] = body->pos.xyz[k] - size - speed * dt - 0.25f;
            reach[count].hi[k] = body->pos.xyz[k] + size + speed * dt + 0.25f;
        }
        reach[count].mask = data->mask;
        count++;
    }
    if (count == 0) {
        return;
    }
    gather_static(scene, reach, count);
    pp_physics_step(dt, 6, 3);
    for (i = 0; i < scene->rigid_count; i++) {
        RRRigidState *state = &scene->rigids[i];
        Node         *node = &scene->nodes[state->node];
        PPBody       *body = (PPBody *)state->body;
        RRMat34       world;

        if (body == NULL || !body->is_alive) {
            continue;
        }
        body_world(body, &world);
        rr_node_set_world(node, &world);
        state->placed = node->local;
    }
}

/* --- what game code asks and tells ---------------------------------------------------- */

static PPBody *body_of(const RRRigidState *state)
{
    return state != NULL ? (PPBody *)state->body : NULL;
}

void rr_physics_get_velocity(const RRRigidState *state, float *linear, float *angular)
{
    const PPBody *body = body_of(state);
    int           k;

    for (k = 0; k < 3; k++) {
        if (linear != NULL) {
            linear[k] = body != NULL ? body->vel.xyz[k] : 0.0f;
        }
        if (angular != NULL) {
            angular[k] = body != NULL ? body->a_vel.xyz[k] : 0.0f;
        }
    }
}

void rr_physics_set_velocity(RRRigidState *state, const float *linear, const float *angular)
{
    PPBody *body = body_of(state);

    if (body != NULL && linear != NULL) {
        pp_vec3_set(&body->vel, linear[0], linear[1], linear[2]);
    }
    if (body != NULL && angular != NULL) {
        pp_vec3_set(&body->a_vel, angular[0], angular[1], angular[2]);
    }
}

void rr_physics_apply_impulse(RRRigidState *state, const float *impulse, const float *offset)
{
    PPBody *body = body_of(state);
    PPVec3  push, arm, twist;

    if (body == NULL || body->inv_mass == 0.0f) {
        return;
    }
    pp_vec3_set(&push, impulse[0], impulse[1], impulse[2]);
    pp_vec3_scale(&push, body->inv_mass, &twist);
    pp_vec3_add(&body->vel, &twist, &body->vel);
    if (offset != NULL) {
        /* Away from the middle a push also turns the body, the way the engine works out
         * its own pushes. */
        pp_vec3_set(&arm, offset[0], offset[1], offset[2]);
        pp_vec3_cross(&arm, &push, &twist);
        pp_mat3_mult(&body->inv_inertia, &twist, &twist);
        pp_vec3_add(&body->a_vel, &twist, &body->a_vel);
    }
}
