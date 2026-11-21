/* RigidBody2D: bodies that fall, turn, rub, bounce, stack and push each other.
 *
 * The work is done by Box2D (runtime/third_party/box2d), the 2D physics engine by Erin
 * Catto. This file is what stands between Godot's nodes and that engine:
 *
 * - A scene with at least one RigidBody2D gets a world of the engine. Every RigidBody2D
 *   is a body in it, made of the rectangle, circle and capsule shapes below the node,
 *   with the node's mass, physics material, damping, gravity scale, start velocities and
 *   lock_rotation. After every step the node is stood where its body is. A node that
 *   game code moved is taken as moved: the body follows.
 * - Every StaticBody2D and every TileMapLayer with collision is a body that does not
 *   move by itself. If something moves its node (an animation, a path, game code), the
 *   body is put there before the step.
 * - Layers and masks are kept the way Godot keeps them: a rigid body meets a static body
 *   or a tile map if its mask has one of the other's layers, and two rigid bodies meet if
 *   either one's mask has one of the other's layers.
 * - Two materials are mixed as Godot mixes them: the smaller friction counts, the bounces
 *   add up.
 * - one_way_collision works as in Godot: such a shape stops only what comes down onto it.
 * - A body that monitors its contacts (contact_monitor, max_contacts_reported) can be
 *   asked what it touches.
 *
 * Box2D is made for bodies of 0.1 to 10 metres, and a 2D scene is measured in pixels, so
 * the world of the engine is the scene scaled down: RR_PIXELS_PER_METRE pixels are one
 * metre. Everything that goes in or comes out is turned over here.
 *
 * Box2D gets its memory from malloc: when the world is made, when bodies are made, and
 * when one of its buffers has to grow. That is during the first steps of a scene; a scene
 * that runs along does not allocate. The engine runs on the one thread of the runtime. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rr_internal.h"

#include "../third_party/box2d/include/box2d/box2d.h"

#define RR_PIXELS_PER_METRE 100.0f
#define TO_METRES           (1.0f / RR_PIXELS_PER_METRE)

/* Static bodies and tile maps of one scene, and the kinds of shapes there are. */
#define MAX_STATICS 512
#define MAX_INFOS   (2 * MAX_STATICS + 256)

/* What the engine's callbacks need to know of a shape: whose it is. Many shapes share one. */
typedef struct {
    uint32_t    layer, mask;
    bool        fixed;   /* of a static body or a tile map */
    bool        one_way; /* stops only what comes down onto it */
    const Node *node;    /* the body or tile map: its -Y is what "down onto it" comes from */
} Info;

typedef struct {
    uint16_t node;
    b2BodyId body;
    RRMat34  placed; /* the world transform the body was made or moved for */
    bool     off;    /* the node is out of the tree */
} Fixed;

static const RRScene *g_scene;
static b2WorldId      g_world;
static bool           g_has_world;
static Info           g_infos[MAX_INFOS];
static int            g_info_count;
static Fixed          g_fixed[MAX_STATICS];
static int            g_fixed_count;
static float          g_gravity; /* strength of the world's gravity, pixels per second squared */
static float          g_step;    /* seconds of the step that is being made */
static bool           g_full;

/* --- Godot's rules, as the engine asks for them -------------------------------------- */

static float mix_friction(float a, uint64_t id_a, float b, uint64_t id_b)
{
    (void)id_a;
    (void)id_b;
    return a < b ? a : b;
}

static float mix_bounce(float a, uint64_t id_a, float b, uint64_t id_b)
{
    (void)id_a;
    (void)id_b;
    return a + b > 1.0f ? 1.0f : a + b;
}

static bool may_meet(b2ShapeId shape_a, b2ShapeId shape_b, void *context)
{
    const Info *a = (const Info *)b2Shape_GetUserData(shape_a), *b = (const Info *)b2Shape_GetUserData(shape_b);

    (void)context;
    if (a == NULL || b == NULL) {
        return true;
    }
    if (a->fixed) {
        return (b->mask & a->layer) != 0;
    }
    if (b->fixed) {
        return (a->mask & b->layer) != 0;
    }
    return (a->mask & b->layer) != 0 || (b->mask & a->layer) != 0;
}

/* A one-way shape lets through what does not come down onto it: the contact has to push
 * the body the way the shape's own -Y points, and the body must not be deeper in the shape
 * than this step took it, plus ONE_WAY_DEPTH. That allowance is what a body that has just
 * landed may be pressed in before the engine has pushed it out again (its contacts are
 * springs); a body that is deeper was not above the shape and goes through. */
#define ONE_WAY_DEPTH 0.05f /* metres */
static void before_solve(b2ShapeId shape_a, b2ShapeId shape_b, b2Manifold *manifold, void *context)
{
    const Info *a = (const Info *)b2Shape_GetUserData(shape_a), *b = (const Info *)b2Shape_GetUserData(shape_b);
    const Info *platform = a != NULL && a->one_way ? a : (b != NULL && b->one_way ? b : NULL);
    RRMat34     world;
    b2Vec2      v;
    float       up[2], length, push, fall, deepest = 0.0f;
    int         i;

    (void)context;
    if (platform == NULL) {
        return;
    }
    rr_node_world_now(platform->node, &world);
    up[0]  = -world.m[3];
    up[1]  = -world.m[4];
    length = sqrtf(up[0] * up[0] + up[1] * up[1]);
    if (length <= 0.0f) {
        return;
    }
    /* The normal points from A to B: from the platform to the body it has to point up. */
    push = (manifold->normal.x * up[0] + manifold->normal.y * up[1]) / length * (platform == a ? 1.0f : -1.0f);
    if (push < 0.7071f) {
        manifold->pointCount = 0;
        return;
    }
    v    = b2Body_GetLinearVelocity(b2Shape_GetBody(platform == a ? shape_b : shape_a));
    fall = -(v.x * up[0] + v.y * up[1]) / length * g_step;
    fall = fall > 0.0f ? fall : 0.0f;
    for (i = 0; i < manifold->pointCount; i++) {
        deepest = manifold->points[i].separation < deepest ? manifold->points[i].separation : deepest;
    }
    if (-deepest > fall + ONE_WAY_DEPTH) {
        manifold->pointCount = 0;
    }
}

/* The same question for a fast body, before the engine stops it at a shape it would
 * otherwise fly through in one step: a one-way shape stops only what moves down onto it. */
static bool before_impact(b2ShapeId shape_a, b2ShapeId shape_b, b2Pos point, b2Vec2 normal, void *context)
{
    const Info *a = (const Info *)b2Shape_GetUserData(shape_a), *b = (const Info *)b2Shape_GetUserData(shape_b);
    const Info *platform = a != NULL && a->one_way ? a : (b != NULL && b->one_way ? b : NULL);
    RRMat34     world;
    b2Vec2      v;

    (void)point;
    (void)normal;
    (void)context;
    if (platform == NULL) {
        return true;
    }
    rr_node_world_now(platform->node, &world);
    v = b2Body_GetLinearVelocity(b2Shape_GetBody(platform == a ? shape_b : shape_a));
    return v.x * world.m[3] + v.y * world.m[4] > 0.0f; /* along the shape's +Y: down onto it */
}

/* --- shapes ------------------------------------------------------------------------------- */

static Info *new_info(const Node *node, uint32_t layer, uint32_t mask, bool fixed, bool one_way)
{
    Info *info;

    if (g_info_count >= MAX_INFOS) {
        return &g_infos[MAX_INFOS - 1]; /* cannot happen: MAX_INFOS covers every body and map */
    }
    info          = &g_infos[g_info_count++];
    info->node    = node;
    info->layer   = layer;
    info->mask    = mask;
    info->fixed   = fixed;
    info->one_way = one_way;
    return info;
}

/* The lengths of a world transform's X and Y axes: how much the node and its parents
 * scale what is below it. */
static void scale_of(const RRMat34 *world, float *sx, float *sy)
{
    *sx = sqrtf(world->m[0] * world->m[0] + world->m[1] * world->m[1]);
    *sy = sqrtf(world->m[3] * world->m[3] + world->m[4] * world->m[4]);
}

static float angle_of(const RRMat34 *m)
{
    return atan2f(m->m[1], m->m[0]);
}

/* Gives a body the CollisionShape2D nodes below `owner`. Returns how many it made. */
static int add_shapes(b2BodyId body, const Node *owner, const RRMat34 *owner_world, float bounce, float friction,
                      uint32_t layer, uint32_t mask, bool fixed)
{
    b2ShapeDef def = b2DefaultShapeDef();
    Info      *plain = NULL, *one_way = NULL;
    Node      *shape;
    float      sx, sy;
    int        made = 0;

    scale_of(owner_world, &sx, &sy);
    def.material.friction    = friction < 0.0f ? 0.0f : friction;
    def.material.restitution = bounce < 0.0f ? 0.0f : (bounce > 1.0f ? 1.0f : bounce);
    def.density              = 1.0f;
    def.enableCustomFiltering = true;
    for (shape = rr_node_child((Node *)owner, NULL); shape != NULL; shape = rr_node_child((Node *)owner, shape)) {
        const RRPkgShapeNode *s = (const RRPkgShapeNode *)shape->data;
        const float          *m = shape->local.m;
        const b2Vec2          at = { m[9] * sx * TO_METRES, m[10] * sy * TO_METRES };
        const float           turn = atan2f(m[1], m[0]);
        /* A shape node may be scaled too. */
        const float           wx = sx * sqrtf(m[0] * m[0] + m[1] * m[1]), wy = sy * sqrtf(m[3] * m[3] + m[4] * m[4]);

        if (shape->type != RR_NODE_SHAPE || s->disabled) {
            continue;
        }
        if (s->one_way && fixed) {
            if (one_way == NULL) {
                one_way = new_info(owner, layer, mask, fixed, true);
            }
            def.userData             = one_way;
            def.enablePreSolveEvents = true;
        } else {
            if (plain == NULL) {
                plain = new_info(owner, layer, mask, fixed, false);
            }
            def.userData             = plain;
            def.enablePreSolveEvents = false; /* the callback runs if either shape asks for it */
        }
        if (s->kind == RR_SHAPE_SPHERE) {
            const b2Circle circle = { at, s->size[0] * wx * TO_METRES };
            b2CreateCircleShape(body, &def, &circle);
        } else if (s->kind == RR_SHAPE_CAPSULE) {
            /* The line of a capsule runs along the shape's own Y. */
            const float    half = s->size[1] * wy * TO_METRES, c = cosf(turn), sn = sinf(turn);
            const b2Capsule capsule = { { at.x + sn * half, at.y - c * half },
                                        { at.x - sn * half, at.y + c * half },
                                        s->size[0] * wx * TO_METRES };
            b2CreateCapsuleShape(body, &def, &capsule);
        } else if (s->kind == RR_SHAPE_BOX) {
            const b2Polygon box = b2MakeOffsetBox(s->size[0] * wx * TO_METRES, s->size[1] * wy * TO_METRES, at,
                                                  b2MakeRot(turn));
            b2CreatePolygonShape(body, &def, &box);
        } else {
            continue;
        }
        made++;
    }
    return made;
}

/* --- the world ---------------------------------------------------------------------------- */

static void drop_world(void)
{
    if (g_has_world) {
        b2DestroyWorld(g_world);
        g_has_world = false;
    }
    g_scene       = NULL;
    g_info_count  = 0;
    g_fixed_count = 0;
    g_full        = false;
}

static b2BodyId new_fixed(const RRScene *scene, const Node *node)
{
    b2BodyDef def = b2DefaultBodyDef();
    Fixed    *f = &g_fixed[g_fixed_count++];

    f->node = (uint16_t)(node - scene->nodes);
    f->off  = false;
    rr_node_world_now(node, &f->placed);
    def.type     = b2_staticBody;
    def.position = (b2Vec2){ f->placed.m[9] * TO_METRES, f->placed.m[10] * TO_METRES };
    def.rotation = b2MakeRot(angle_of(&f->placed));
    f->body      = b2CreateBody(g_world, &def);
    return f->body;
}

/* Makes the world of a scene that has a RigidBody2D, with everything that does not move. */
static void make_world(const RRScene *scene, const float *gravity)
{
    b2WorldDef def = b2DefaultWorldDef();
    uint32_t   i, k;

    drop_world();
    g_scene   = scene;
    g_gravity = sqrtf(gravity[0] * gravity[0] + gravity[1] * gravity[1]);
    def.gravity             = (b2Vec2){ gravity[0] * TO_METRES, gravity[1] * TO_METRES };
    def.frictionCallback    = mix_friction;
    def.restitutionCallback = mix_bounce;
    g_world     = b2CreateWorld(&def);
    g_has_world = true;
    b2World_SetCustomFilterCallback(g_world, may_meet, NULL);
    b2World_SetPreSolveCallback(g_world, before_solve, before_impact, NULL);

    for (i = 0; i < scene->node_count; i++) {
        const Node          *n = &scene->nodes[i];
        const RRPkgBodyNode *data = (const RRPkgBodyNode *)n->data;
        b2BodyId             body;

        if (n->type != RR_NODE_STATIC_BODY || !data->two_d) {
            continue;
        }
        if (g_fixed_count >= MAX_STATICS) {
            g_full = true;
            break;
        }
        body = new_fixed(scene, n);
        add_shapes(body, n, &g_fixed[g_fixed_count - 1].placed, data->bounce, data->friction, data->layer, data->mask,
                   true);
    }
    for (i = 0; i < scene->tilemap_count && g_fixed_count < MAX_STATICS; i++) {
        const Node             *n = &scene->nodes[scene->tilemaps[i]];
        const RRPkgTileMapNode *data = (const RRPkgTileMapNode *)n->data;
        const RRPkgTileMap     *map = (const RRPkgTileMap *)rr_package_asset(scene->pkg, data->tiles);
        const RRPkgTileBox     *boxes = (const RRPkgTileBox *)((const uint8_t *)map + map->boxes_off);
        b2ShapeDef              shape = b2DefaultShapeDef();
        Info                   *plain = NULL, *one_way = NULL;
        b2BodyId                body;
        float                   sx, sy;

        if (map->box_count == 0) {
            continue;
        }
        body = new_fixed(scene, n);
        scale_of(&g_fixed[g_fixed_count - 1].placed, &sx, &sy);
        shape.material.friction     = 1.0f;
        shape.material.restitution  = 0.0f;
        shape.enableCustomFiltering = true;
        for (k = 0; k < map->box_count; k++) {
            const b2Vec2    at = { (boxes[k].x + boxes[k].w * 0.5f) * sx * TO_METRES,
                                   (boxes[k].y + boxes[k].h * 0.5f) * sy * TO_METRES };
            const b2Polygon box = b2MakeOffsetBox(boxes[k].w * 0.5f * sx * TO_METRES, boxes[k].h * 0.5f * sy * TO_METRES,
                                                  at, b2MakeRot(0.0f));
            if (boxes[k].one_way) {
                if (one_way == NULL) {
                    one_way = new_info(n, data->layer, data->mask, true, true);
                }
                shape.userData = one_way;
            } else {
                if (plain == NULL) {
                    plain = new_info(n, data->layer, data->mask, true, false);
                }
                shape.userData = plain;
            }
            shape.enablePreSolveEvents = boxes[k].one_way != 0;
            b2CreatePolygonShape(body, &shape, &box);
        }
    }
    if (g_full) {
        printf("retro: more than %d static bodies and tile maps in a 2D scene; the rest is not solid for rigid bodies\n",
               MAX_STATICS);
    }
}

void rr_physics2d_begin(const RRScene *scene)
{
    (void)scene;
    drop_world();
}

/* --- making the bodies ------------------------------------------------------------------ */

void rr_physics2d_body_init(RRScene *scene, Node *node, RRRigidState *state)
{
    const RRPkgBodyNode *data = (const RRPkgBodyNode *)node->data;
    const float          pull = sqrtf(data->gravity[0] * data->gravity[0] + data->gravity[1] * data->gravity[1]);
    b2BodyDef            def = b2DefaultBodyDef();
    b2BodyId             body;
    b2MassData           mass;
    RRMat34              world;

    state->has_body2d = false;
    if (g_scene != scene) {
        make_world(scene, data->gravity);
    }
    rr_node_world_now(node, &world);
    def.type           = b2_dynamicBody;
    def.position       = (b2Vec2){ world.m[9] * TO_METRES, world.m[10] * TO_METRES };
    def.rotation       = b2MakeRot(angle_of(&world));
    def.linearDamping  = data->linear_damp;
    def.angularDamping = data->angular_damp;
    def.gravityScale   = g_gravity > 0.0f ? pull / g_gravity : 0.0f;
    def.motionLocks.angularZ = (data->lock_angular & 4) != 0;
    body = b2CreateBody(g_world, &def);
    if (add_shapes(body, node, &world, data->bounce, data->friction, data->layer, data->mask, false) == 0) {
        b2DestroyBody(body);
        return;
    }
    /* The shapes were made with a density of 1: the mass they came to is brought to the
     * node's, and how hard the body is to turn goes along with it. */
    mass = b2Body_GetMassData(body);
    if (mass.mass > 0.0f && data->mass > 0.0f) {
        mass.rotationalInertia *= data->mass / mass.mass;
        mass.mass               = data->mass;
        b2Body_SetMassData(body, mass);
    }
    state->body2d      = b2StoreBodyId(body);
    state->has_body2d  = true;
    state->contact_max = data->contacts < RR_MAX_CONTACTS ? data->contacts : RR_MAX_CONTACTS;
    rr_physics2d_body_reset(node, state);
}

void rr_physics2d_body_reset(Node *node, RRRigidState *state)
{
    const RRPkgBodyNode *data = (const RRPkgBodyNode *)node->data;

    state->synced = false; /* the node may not be where it will be yet */
    if (state->has_body2d) {
        b2Body_SetLinearVelocity(b2LoadBodyId(state->body2d), (b2Vec2){ data->velocity[0] * TO_METRES, data->velocity[1] * TO_METRES });
        b2Body_SetAngularVelocity(b2LoadBodyId(state->body2d), data->angular_velocity[2]);
    }
}

static void read_contacts(RRRigidState *state);

/* --- the step ----------------------------------------------------------------------------- */

void rr_physics2d_step(RRScene *scene, float dt)
{
    uint32_t i;
    int      k;

    if (!g_has_world || scene != g_scene) {
        return;
    }
    /* What does not move by itself is put where something else moved it. */
    for (k = 0; k < g_fixed_count; k++) {
        Fixed      *f = &g_fixed[k];
        const Node *node = &scene->nodes[f->node];
        const bool  off = (node->flags & RR_NODE_FLAG_FREE) != 0;
        RRMat34     world;

        if (off != f->off) {
            if (off) {
                b2Body_Disable(f->body);
            } else {
                b2Body_Enable(f->body);
            }
            f->off = off;
        }
        if (off) {
            continue;
        }
        rr_node_world_now(node, &world);
        if (memcmp(&world, &f->placed, sizeof(world)) != 0) {
            b2Body_SetTransform(f->body, (b2Vec2){ world.m[9] * TO_METRES, world.m[10] * TO_METRES },
                                b2MakeRot(angle_of(&world)));
            f->placed = world;
        }
    }
    for (i = 0; i < scene->rigid_count; i++) {
        RRRigidState *state = &scene->rigids[i];
        Node         *node = &scene->nodes[state->node];
        const bool    off = (node->flags & RR_NODE_FLAG_FREE) != 0;

        if (!state->has_body2d) {
            continue;
        }
        if (off != state->off) {
            if (off) {
                b2Body_Disable(b2LoadBodyId(state->body2d));
            } else {
                b2Body_Enable(b2LoadBodyId(state->body2d));
            }
            state->off = off;
        }
        /* The node was moved by something else than this file since the last step: the
         * body goes where the node is. */
        if (!off && (!state->synced || memcmp(&state->placed, &node->local, sizeof(RRMat34)) != 0)) {
            RRMat34 world;
            rr_node_world_now(node, &world);
            b2Body_SetTransform(b2LoadBodyId(state->body2d), (b2Vec2){ world.m[9] * TO_METRES, world.m[10] * TO_METRES },
                                b2MakeRot(angle_of(&world)));
            b2Body_SetAwake(b2LoadBodyId(state->body2d), true);
            state->placed = node->local;
            state->synced = true;
        }
    }

    g_step = dt;
    b2World_Step(g_world, dt, 4);

    for (i = 0; i < scene->rigid_count; i++) {
        RRRigidState *state = &scene->rigids[i];
        Node         *node = &scene->nodes[state->node];
        RRMat34       world;
        b2Vec2        at;
        b2Rot         turn;

        if (!state->has_body2d || state->off) {
            continue;
        }
        at   = b2Body_GetPosition(b2LoadBodyId(state->body2d));
        turn = b2Body_GetRotation(b2LoadBodyId(state->body2d));
        rr_mat34_identity(&world);
        world.m[0]  = turn.c;
        world.m[1]  = turn.s;
        world.m[3]  = -turn.s;
        world.m[4]  = turn.c;
        world.m[9]  = at.x * RR_PIXELS_PER_METRE;
        world.m[10] = at.y * RR_PIXELS_PER_METRE;
        world.m[11] = node->world.m[11];
        rr_node_set_world(node, &world);
        state->placed = node->local;
        read_contacts(state);
    }
}

/* --- what game code asks and tells ---------------------------------------------------- */

void rr_physics2d_get_velocity(const RRRigidState *state, float *linear, float *angular)
{
    if (linear != NULL) {
        const b2Vec2 v = state->has_body2d ? b2Body_GetLinearVelocity(b2LoadBodyId(state->body2d)) : (b2Vec2){ 0.0f, 0.0f };
        linear[0] = v.x * RR_PIXELS_PER_METRE;
        linear[1] = v.y * RR_PIXELS_PER_METRE;
        linear[2] = 0.0f;
    }
    if (angular != NULL) {
        angular[0] = angular[1] = 0.0f;
        angular[2] = state->has_body2d ? b2Body_GetAngularVelocity(b2LoadBodyId(state->body2d)) : 0.0f;
    }
}

void rr_physics2d_set_velocity(RRRigidState *state, const float *linear, const float *angular)
{
    if (!state->has_body2d) {
        return;
    }
    if (linear != NULL) {
        b2Body_SetLinearVelocity(b2LoadBodyId(state->body2d), (b2Vec2){ linear[0] * TO_METRES, linear[1] * TO_METRES });
    }
    if (angular != NULL) {
        b2Body_SetAngularVelocity(b2LoadBodyId(state->body2d), angular[2]);
    }
}

void rr_physics2d_apply_impulse(RRRigidState *state, const float *impulse, const float *offset)
{
    const b2Vec2 push = { impulse[0] * TO_METRES, impulse[1] * TO_METRES };

    if (!state->has_body2d) {
        return;
    }
    if (offset == NULL) {
        b2Body_ApplyLinearImpulseToCenter(b2LoadBodyId(state->body2d), push, true);
    } else {
        const b2Vec2 at = b2Body_GetPosition(b2LoadBodyId(state->body2d));
        b2Body_ApplyLinearImpulse(b2LoadBodyId(state->body2d), push,
                                  (b2Vec2){ at.x + offset[0] * TO_METRES, at.y + offset[1] * TO_METRES }, true);
    }
}

/* What the body touches right now, for a body that monitors its contacts: the directions
 * out of what it touches, into `state->contact_normals`. */
static void read_contacts(RRRigidState *state)
{
    b2ContactData touching[RR_MAX_CONTACTS];
    int           count, i;

    state->contact_count = 0;
    if (!state->has_body2d || state->contact_max == 0 || state->off) {
        return;
    }
    count = b2Body_GetContactData(b2LoadBodyId(state->body2d), touching, state->contact_max);
    for (i = 0; i < count; i++) {
        /* The normal points from shape A to shape B. */
        const bool  is_a = B2_ID_EQUALS(b2Shape_GetBody(touching[i].shapeIdA), b2LoadBodyId(state->body2d));
        const float sign = is_a ? -1.0f : 1.0f;
        float      *n = state->contact_normals[state->contact_count++];
        n[0] = touching[i].manifold.normal.x * sign;
        n[1] = touching[i].manifold.normal.y * sign;
        n[2] = 0.0f;
    }
}
