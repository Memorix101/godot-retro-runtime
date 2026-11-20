/* AnimatedSprite2D: plays the animations of a SpriteFrames asset, one frame at a time.
 *
 * Timing follows Godot: a frame is shown for duration / (fps * speed_scale) seconds. An
 * animation that does not loop stops on its last frame and emits `animation_finished`. */
#include <string.h>

#include "rr_internal.h"

static const RRPkgSpriteFrames *frames_of(const Node *sprite)
{
    const RRPkgAnimatedSpriteNode *data = (const RRPkgAnimatedSpriteNode *)sprite->data;
    return (const RRPkgSpriteFrames *)rr_package_asset(sprite->scene->pkg, data->frames);
}

RRSpriteState *rr_sprite_state(const Node *sprite)
{
    RRScene       *scene = sprite->scene;
    const uint16_t index = (uint16_t)(sprite - scene->nodes);
    uint32_t       i;

    if (sprite->type != RR_NODE_ANIMATED_SPRITE) {
        return NULL;
    }
    for (i = 0; i < scene->sprite_count; i++) {
        if (scene->sprites[i].node == index) {
            return &scene->sprites[i];
        }
    }
    return NULL;
}

const RRPkgSpriteFrame *rr_sprite_frame(const Node *sprite, const RRSpriteState *state)
{
    const RRPkgSpriteFrames *set = frames_of(sprite);
    const RRPkgSpriteAnim   *anim = &set->anims[state->anim];
    return (const RRPkgSpriteFrame *)((const uint8_t *)set + anim->frames_off) + state->frame;
}

void rr_sprite_init(RRScene *scene, Node *sprite)
{
    RRSpriteState *state = &scene->sprites[scene->sprite_count++];

    state->node = (uint16_t)(sprite - scene->nodes);
    rr_sprite_reset(sprite);
}

void rr_sprite_reset(Node *sprite)
{
    const RRPkgAnimatedSpriteNode *data = (const RRPkgAnimatedSpriteNode *)sprite->data;
    RRSpriteState                 *state = rr_sprite_state(sprite);

    state->anim    = data->animation;
    state->frame   = 0;
    state->playing = data->playing != 0;
    state->flip_h  = data->flip_h != 0;
    state->flip_v  = data->flip_v != 0;
    state->time    = 0.0f;
}

void rr_sprites_update(RRScene *scene, float dt)
{
    uint32_t i;

    for (i = 0; i < scene->sprite_count; i++) {
        RRSpriteState                 *state = &scene->sprites[i];
        Node                          *node = &scene->nodes[state->node];
        const RRPkgAnimatedSpriteNode *data = (const RRPkgAnimatedSpriteNode *)node->data;
        const RRPkgSpriteFrames       *set = frames_of(node);
        const RRPkgSpriteAnim         *anim = &set->anims[state->anim];
        const RRPkgSpriteFrame        *frames =
            (const RRPkgSpriteFrame *)((const uint8_t *)set + anim->frames_off);
        const float                    speed = anim->fps * data->speed_scale;

        if (!state->playing || !(speed > 0.0f) || (node->flags & RR_NODE_FLAG_FREE)) {
            continue;
        }
        /* Time is counted in units of 1 / fps, the unit of the frame durations. */
        state->time += dt * speed;
        while (state->playing && state->time >= frames[state->frame].duration) {
            state->time -= frames[state->frame].duration;
            if (state->frame + 1u < anim->frame_count) {
                state->frame++;
            } else if (anim->loop) {
                state->frame = 0;
            } else {
                state->playing = false;
                state->time    = 0.0f;
                rr_emit(node, "animation_finished", NULL);
            }
        }
    }
}

void rr_sprite_play(Node *sprite, const char *animation)
{
    RRSpriteState           *state = rr_sprite_state(sprite);
    const RRPkgSpriteFrames *set;
    uint32_t                 i;

    if (state == NULL) {
        return;
    }
    set = frames_of(sprite);
    for (i = 0; i < set->anim_count; i++) {
        if (strcmp(rr_package_string(sprite->scene->pkg, set->anims[i].name), animation) == 0) {
            /* As in Godot: playing the animation that already runs does not restart it. */
            if (state->anim != i || !state->playing) {
                state->anim  = (uint16_t)i;
                state->frame = 0;
                state->time  = 0.0f;
            }
            state->playing = true;
            return;
        }
    }
}

void rr_sprite_stop(Node *sprite)
{
    RRSpriteState *state = rr_sprite_state(sprite);
    if (state != NULL) {
        state->playing = false;
        state->frame   = 0;
        state->time    = 0.0f;
    }
}

bool rr_sprite_is_playing(const Node *sprite)
{
    const RRSpriteState *state = rr_sprite_state(sprite);
    return state != NULL && state->playing;
}

const char *rr_sprite_animation(const Node *sprite)
{
    const RRSpriteState *state = rr_sprite_state(sprite);
    if (state == NULL) {
        return NULL;
    }
    return rr_package_string(sprite->scene->pkg, frames_of(sprite)->anims[state->anim].name);
}

void rr_sprite_set_flip_h(Node *sprite, bool flip)
{
    RRSpriteState *state = rr_sprite_state(sprite);
    if (state != NULL) {
        state->flip_h = flip;
    }
}

void rr_sprite_set_flip_v(Node *sprite, bool flip)
{
    RRSpriteState *state = rr_sprite_state(sprite);
    if (state != NULL) {
        state->flip_v = flip;
    }
}
