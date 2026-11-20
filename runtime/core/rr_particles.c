/* Particle systems (CPUParticles3D nodes).
 *
 * Every system owns a fixed slice of the scene's particle pool, `amount` particles long,
 * so nothing is allocated while the game runs. Particle i of a system is born at a fixed
 * point of the emission cycle, which lasts one lifetime: spread evenly over the cycle,
 * or squeezed towards its start by the explosiveness. Particles live in world space and
 * are drawn as quads that face the camera. */
#include <math.h>

#include "rr_internal.h"
#include "rr_platform.h"

static const RRPkgParticlesNode *data_of(const Node *node)
{
    return (const RRPkgParticlesNode *)node->data;
}

static RREmitter *emitter_of(const Node *node)
{
    RRScene       *scene = node->scene;
    const uint16_t index = (uint16_t)(node - scene->nodes);
    uint32_t       i;

    if (node->type != RR_NODE_PARTICLES) {
        return NULL;
    }
    for (i = 0; i < scene->emitter_count; i++) {
        if (scene->emitters[i].node == index) {
            return &scene->emitters[i];
        }
    }
    return NULL;
}

static void clear(RRScene *scene, RREmitter *emitter)
{
    const uint32_t amount = data_of(&scene->nodes[emitter->node])->amount;
    uint32_t       i;
    for (i = 0; i < amount; i++) {
        scene->particles[emitter->first + i].age = -1.0f;
    }
    emitter->cycle = 0.0f;
}

void rr_particles_init(RRScene *scene, Node *particles)
{
    RREmitter *emitter = &scene->emitters[scene->emitter_count++];

    emitter->node     = (uint16_t)(particles - scene->nodes);
    emitter->emitting = data_of(particles)->emitting != 0;
    emitter->first    = scene->particle_count;
    scene->particle_count += data_of(particles)->amount;
    clear(scene, emitter);
}

void rr_particles_reset(Node *particles)
{
    RREmitter *emitter = emitter_of(particles);

    emitter->emitting = data_of(particles)->emitting != 0;
    clear(particles->scene, emitter);
}

/* 0..1, from a small generator that gives the same sequence on every platform. */
static float random01(RRScene *scene)
{
    scene->rng = scene->rng * 1664525u + 1013904223u;
    return (float)(scene->rng >> 8) * (1.0f / 16777216.0f);
}

static void spawn(RRScene *scene, const Node *node, RRParticle *p)
{
    const RRPkgParticlesNode *d = data_of(node);
    const float              *m = node->world.m;
    float                     dir[3], side[3], up[3], length, angle, turn, speed;
    int                       i;

    /* The direction in world space, and two axes at right angles to it. */
    for (i = 0; i < 3; i++) {
        dir[i] = m[i] * d->direction[0] + m[3 + i] * d->direction[1] + m[6 + i] * d->direction[2];
    }
    length = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (length < 0.0001f) {
        dir[0] = 0.0f;
        dir[1] = 1.0f;
        dir[2] = 0.0f;
        length = 1.0f;
    }
    for (i = 0; i < 3; i++) {
        dir[i] /= length;
    }
    if (fabsf(dir[1]) < 0.9f) {
        side[0] = dir[2];
        side[1] = 0.0f;
        side[2] = -dir[0];
    } else {
        side[0] = 0.0f;
        side[1] = -dir[2];
        side[2] = dir[1];
    }
    length = sqrtf(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
    for (i = 0; i < 3; i++) {
        side[i] /= length;
    }
    up[0] = dir[1] * side[2] - dir[2] * side[1];
    up[1] = dir[2] * side[0] - dir[0] * side[2];
    up[2] = dir[0] * side[1] - dir[1] * side[0];

    speed = d->velocity_min + (d->velocity_max - d->velocity_min) * random01(scene);
    if (d->two_d) {
        /* Turned away from the direction by up to the spread, inside the plane. */
        angle = d->spread * (random01(scene) * 2.0f - 1.0f);
        p->vel[0] = (dir[0] * cosf(angle) - dir[1] * sinf(angle)) * speed;
        p->vel[1] = (dir[0] * sinf(angle) + dir[1] * cosf(angle)) * speed;
        p->vel[2] = 0.0f;
        for (i = 0; i < 3; i++) {
            p->pos[i] = m[9 + i];
        }
    } else {
        /* Somewhere inside the cone around the direction. */
        angle = d->spread * random01(scene);
        turn  = 6.2831853f * random01(scene);
        for (i = 0; i < 3; i++) {
            const float v = dir[i] * cosf(angle) + (side[i] * cosf(turn) + up[i] * sinf(turn)) * sinf(angle);
            p->vel[i] = v * speed;
            p->pos[i] = m[9 + i];
        }
    }
    if (d->emission_radius > 0.0f) {
        /* A point in the cube around the sphere is close enough for sparks. */
        for (i = 0; i < (d->two_d ? 2 : 3); i++) {
            p->pos[i] += (random01(scene) * 2.0f - 1.0f) * d->emission_radius * 0.7f;
        }
    }
    p->size = d->scale_min + (d->scale_max - d->scale_min) * random01(scene);
    p->age  = 0.0f;
}

void rr_particles_update(RRScene *scene, float dt)
{
    uint32_t e, i;

    for (e = 0; e < scene->emitter_count; e++) {
        RREmitter                *emitter = &scene->emitters[e];
        const Node               *node = &scene->nodes[emitter->node];
        const RRPkgParticlesNode *d = data_of(node);
        RRParticle               *particles = &scene->particles[emitter->first];
        /* The part of the cycle in which particles are born. */
        const float               window = d->lifetime * (1.0f - d->explosiveness);

        if (node->flags & RR_NODE_FLAG_FREE) {
            continue;
        }

        for (i = 0; i < d->amount; i++) {
            RRParticle *p = &particles[i];
            if (p->age < 0.0f) {
                continue;
            }
            p->age += dt;
            if (p->age >= d->lifetime) {
                p->age = -1.0f;
                continue;
            }
            p->vel[0] += d->gravity[0] * dt;
            p->vel[1] += d->gravity[1] * dt;
            p->vel[2] += d->gravity[2] * dt;
            p->pos[0] += p->vel[0] * dt;
            p->pos[1] += p->vel[1] * dt;
            p->pos[2] += p->vel[2] * dt;
        }

        if (emitter->cycle < 0.0f) {
            /* Restarted during this frame, perhaps after being moved: wait for the next
             * frame, when the node's world transform is up to date. */
            emitter->cycle = 0.0f;
        } else if (emitter->emitting && rr_node_visible_in_tree(node)) {
            const float from = emitter->cycle;
            float       to = from + dt;
            bool        wrapped = false;

            if (to >= d->lifetime) {
                wrapped = true;
                if (d->one_shot) {
                    emitter->emitting = false;
                }
            }
            for (i = 0; i < d->amount; i++) {
                const float birth = window * (float)i / (float)d->amount;
                /* Born in this frame, or in the part of it that belongs to the next cycle. */
                if ((birth >= from && birth < to) ||
                    (wrapped && emitter->emitting && birth < to - d->lifetime)) {
                    spawn(scene, node, &particles[i]);
                }
            }
            emitter->cycle = wrapped ? to - d->lifetime : to;
        }
    }
}

static uint32_t blend_colors(uint32_t a, uint32_t b, float t)
{
    uint32_t out = 0;
    int      shift;
    for (shift = 0; shift < 32; shift += 8) {
        const float ca = (float)((a >> shift) & 0xFFu), cb = (float)((b >> shift) & 0xFFu);
        out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << shift;
    }
    return out;
}

void rr_particles_draw(const RRScene *scene)
{
    const float *cam = scene->camera->world.m; /* basis X is right, basis Y is up */
    uint32_t     e, i;
    int          k;

    for (e = 0; e < scene->emitter_count; e++) {
        const RREmitter          *emitter = &scene->emitters[e];
        const Node               *node = &scene->nodes[emitter->node];
        const RRPkgParticlesNode *d = data_of(node);
        const uint32_t            handle = d->texture != RR_NONE32 ? scene->handles[d->texture] : RR_NONE32;
        float                     u = 1.0f, v = 1.0f;

        if (d->two_d || !rr_node_visible_in_tree(node)) {
            continue; /* 2D systems are drawn with the 2D nodes */
        }
        if (d->texture != RR_NONE32) {
            const RRPkgTexture *t = (const RRPkgTexture *)rr_package_asset(scene->pkg, d->texture);
            u = (float)t->content_width / (float)t->width;
            v = (float)t->content_height / (float)t->height;
        }
        for (i = 0; i < d->amount; i++) {
            const RRParticle *p = &scene->particles[emitter->first + i];
            const float       hw = d->size[0] * p->size * 0.5f, hh = d->size[1] * p->size * 0.5f;
            const float       uv[8] = { 0.0f, 0.0f, u, 0.0f, u, v, 0.0f, v };
            float             xyz[12];

            if (p->age < 0.0f) {
                continue;
            }
            /* Corners: top left, top right, bottom right, bottom left. */
            for (k = 0; k < 3; k++) {
                const float right = cam[k] * hw, up = cam[3 + k] * hh;
                xyz[k]     = p->pos[k] - right + up;
                xyz[3 + k] = p->pos[k] + right + up;
                xyz[6 + k] = p->pos[k] + right - up;
                xyz[9 + k] = p->pos[k] - right - up;
            }
            rr_gfx_draw_quad_3d(handle, xyz, uv,
                                blend_colors(d->color_start, d->color_end, p->age / d->lifetime));
        }
    }
}

void rr_particles_draw_2d(const RRScene *scene, const Node *node, float ox, float oy, float zx,
                          float zy)
{
    const RREmitter          *emitter = emitter_of(node);
    const RRPkgParticlesNode *d = data_of(node);
    const uint32_t            handle = d->texture != RR_NONE32 ? scene->handles[d->texture] : RR_NONE32;
    float                     u = 1.0f, v = 1.0f;
    uint32_t                  i;

    if (d->texture != RR_NONE32) {
        const RRPkgTexture *t = (const RRPkgTexture *)rr_package_asset(scene->pkg, d->texture);
        u = (float)t->content_width / (float)t->width;
        v = (float)t->content_height / (float)t->height;
    }
    for (i = 0; i < d->amount; i++) {
        const RRParticle *p = &scene->particles[emitter->first + i];
        const float       hw = d->size[0] * p->size * 0.5f * zx, hh = d->size[1] * p->size * 0.5f * zy;
        const float       x = (p->pos[0] - ox) * zx, y = (p->pos[1] - oy) * zy;
        const float       xy[8] = { x - hw, y - hh, x + hw, y - hh, x + hw, y + hh, x - hw, y + hh };
        const float       uv[8] = { 0.0f, 0.0f, u, 0.0f, u, v, 0.0f, v };

        if (p->age >= 0.0f) {
            rr_gfx_draw_quad(handle, xy, uv, blend_colors(d->color_start, d->color_end, p->age / d->lifetime));
        }
    }
}

/* --- for game components -------------------------------------------------------------- */

void rr_particles_restart(Node *particles)
{
    RREmitter *emitter = emitter_of(particles);
    if (emitter != NULL) {
        clear(particles->scene, emitter);
        emitter->emitting = true;
        emitter->cycle    = -1.0f; /* starts with the next frame, see rr_particles_update */
    }
}

void rr_particles_set_emitting(Node *particles, bool emitting)
{
    RREmitter *emitter = emitter_of(particles);
    if (emitter != NULL) {
        if (emitting && !emitter->emitting) {
            emitter->cycle = 0.0f;
        }
        emitter->emitting = emitting;
    }
}
