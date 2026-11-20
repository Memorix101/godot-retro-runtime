/* Timer nodes: count down and emit `timeout`, once or again and again. */
#include "rr_internal.h"

static RRTimerState *state_of(const Node *timer)
{
    RRScene       *scene = timer->scene;
    const uint16_t index = (uint16_t)(timer - scene->nodes);
    uint32_t       i;

    if (timer->type != RR_NODE_TIMER) {
        return NULL;
    }
    for (i = 0; i < scene->timer_count; i++) {
        if (scene->timers[i].node == index) {
            return &scene->timers[i];
        }
    }
    return NULL;
}

void rr_timer_init(RRScene *scene, Node *timer)
{
    const RRPkgTimerNode *data = (const RRPkgTimerNode *)timer->data;
    RRTimerState         *state = &scene->timers[scene->timer_count++];

    state->node    = (uint16_t)(timer - scene->nodes);
    state->running = data->autostart != 0;
    state->wait    = data->wait_time;
    state->left    = data->wait_time;
}

void rr_timer_reset(Node *timer)
{
    const RRPkgTimerNode *data = (const RRPkgTimerNode *)timer->data;
    RRTimerState         *state = state_of(timer);

    state->running = data->autostart != 0;
    state->wait    = data->wait_time;
    state->left    = data->wait_time;
}

void rr_timers_update(RRScene *scene, float dt)
{
    uint32_t i;

    for (i = 0; i < scene->timer_count; i++) {
        RRTimerState *state = &scene->timers[i];
        Node         *node = &scene->nodes[state->node];

        if (!state->running || (node->flags & RR_NODE_FLAG_FREE)) {
            continue;
        }
        state->left -= dt;
        if (state->left > 0.0f) {
            continue;
        }
        if (((const RRPkgTimerNode *)node->data)->one_shot) {
            state->running = false;
            state->left    = 0.0f;
        } else {
            /* Keeps the remainder, so the rate does not drift with the frame time. A wait
             * shorter than a frame still fires only once per frame. */
            state->left += state->wait;
            if (state->left <= 0.0f) {
                state->left = state->wait;
            }
        }
        rr_emit(node, "timeout", NULL);
    }
}

void rr_timer_start(Node *timer, float wait_time)
{
    RRTimerState *state = state_of(timer);
    if (state != NULL) {
        if (wait_time > 0.0f) {
            state->wait = wait_time;
        }
        state->left    = state->wait;
        state->running = true;
    }
}

void rr_timer_stop(Node *timer)
{
    RRTimerState *state = state_of(timer);
    if (state != NULL) {
        state->running = false;
        state->left    = 0.0f;
    }
}

bool rr_timer_is_stopped(const Node *timer)
{
    const RRTimerState *state = state_of(timer);
    return state == NULL || !state->running;
}

float rr_timer_time_left(const Node *timer)
{
    const RRTimerState *state = state_of(timer);
    return state != NULL && state->running ? state->left : 0.0f;
}
