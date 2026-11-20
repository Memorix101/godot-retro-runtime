/* Focus and input of the controls that take it: buttons and sliders. The exporter baked
 * the focus neighbours Godot computed, so navigation is a table lookup: a direction
 * action moves the focus, the accept action presses the focused button, and on a slider
 * the directions along it change its value. */
#include "rr_internal.h"

/* A direction that is held changes a slider again and again: after this long, this often. */
#define REPEAT_DELAY    0.4f
#define REPEAT_INTERVAL 0.05f

/* How many controls that cannot take the focus are passed over in one move. */
#define MAX_SKIPPED 16

static const char *const g_action_names[RR_UI_ACTION_COUNT] = {
    "ui_left", "ui_up", "ui_right", "ui_down", "ui_accept"
};

static bool takes_focus(const Node *n)
{
    return (n->type == RR_NODE_BUTTON || n->type == RR_NODE_SLIDER) &&
           !(n->flags & RR_NODE_FLAG_DISABLED) && rr_node_visible_in_tree(n);
}

static const uint16_t *neighbors_of(const Node *n)
{
    return n->type == RR_NODE_SLIDER ? ((const RRPkgSliderNode *)n->data)->neighbors
                                     : ((const RRPkgButtonNode *)n->data)->neighbors;
}

static Node *first_focusable(RRScene *scene)
{
    uint32_t i;
    for (i = 0; i < scene->node_count; i++) {
        if (takes_focus(&scene->nodes[i])) {
            return &scene->nodes[i];
        }
    }
    return NULL;
}

void rr_ui_init(RRScene *scene)
{
    bool     any = false;
    uint32_t i;
    int      a;

    for (i = 0; i < scene->node_count; i++) {
        any = any || scene->nodes[i].type == RR_NODE_BUTTON || scene->nodes[i].type == RR_NODE_SLIDER;
    }
    scene->focus   = first_focusable(scene);
    scene->ui_hold = 0.0f;
    for (a = 0; a < RR_UI_ACTION_COUNT; a++) {
        scene->ui_actions[a] = any ? rr_action(scene, g_action_names[a]) : -1;
    }
}

/* Keeps how far a ScrollContainer is scrolled inside what it has to show. A control has
 * no transform, so the node's place holds the scroll position. */
static void set_scroll(Node *scroll, int axis, float value)
{
    const RRPkgScrollNode *s = (const RRPkgScrollNode *)scroll->data;
    const float            most = s->content[axis] - s->rect[2 + axis];

    value = value > most ? most : value;
    scroll->local.m[9 + axis] = value < 0.0f ? 0.0f : value;
}

/* Scrolls the ScrollContainers above a control that follow the focus so that the control
 * is in view, moving them no further than needed. */
static void scroll_into_view(const Node *control)
{
    const float *r = (const float *)control->data; /* every control starts with its rectangle */
    float        lo[2] = { r[0], r[1] }, hi[2] = { r[0] + r[2], r[1] + r[3] };
    Node        *at;
    int          a;

    for (at = control->parent; at != NULL; at = at->parent) {
        const RRPkgScrollNode *s = (const RRPkgScrollNode *)at->data;
        const uint8_t          enabled[2] = { at->type == RR_NODE_SCROLL ? s->scroll_h : 0,
                                              at->type == RR_NODE_SCROLL ? s->scroll_v : 0 };
        if (at->type != RR_NODE_SCROLL) {
            continue;
        }
        for (a = 0; a < 2; a++) {
            /* Where the control is now: its baked place, moved by the scrolling so far. */
            const float shift = at->local.m[9 + a] - s->start[a];
            if (s->follow_focus && enabled[a]) {
                if (lo[a] - shift < s->rect[a]) {
                    set_scroll(at, a, at->local.m[9 + a] + lo[a] - shift - s->rect[a]);
                } else if (hi[a] - shift > s->rect[a] + s->rect[2 + a]) {
                    set_scroll(at, a, at->local.m[9 + a] + hi[a] - shift - s->rect[a] - s->rect[2 + a]);
                }
            }
            /* For the containers further out, the control is where this one shows it. */
            lo[a] -= at->local.m[9 + a] - s->start[a];
            hi[a] -= at->local.m[9 + a] - s->start[a];
        }
    }
}

void rr_scroll_set(Node *scroll, float horizontal, float vertical)
{
    if (scroll->type == RR_NODE_SCROLL) {
        set_scroll(scroll, 0, horizontal);
        set_scroll(scroll, 1, vertical);
    }
}

void rr_scroll_get(const Node *scroll, float *horizontal, float *vertical)
{
    *horizontal = scroll->type == RR_NODE_SCROLL ? scroll->local.m[9] : 0.0f;
    *vertical   = scroll->type == RR_NODE_SCROLL ? scroll->local.m[10] : 0.0f;
}

/* Moves the focus to the next control in a direction that can take it. Controls on the
 * way that are disabled or hidden are passed over. */
static void move_focus(RRScene *scene, int side)
{
    const Node *at = scene->focus;
    int         hop;

    for (hop = 0; hop < MAX_SKIPPED; hop++) {
        const uint16_t next = neighbors_of(at)[side];
        if (next == RR_NONE16 || &scene->nodes[next] == scene->focus) {
            return;
        }
        at = &scene->nodes[next];
        if (takes_focus(at)) {
            scene->focus = &scene->nodes[next];
            scroll_into_view(scene->focus);
            return;
        }
    }
}

/* One step of a slider: -1 or 1 times its step, kept inside its range. */
static void step_slider(Node *slider, float direction)
{
    const RRPkgSliderNode *s = (const RRPkgSliderNode *)slider->data;
    float                  value = slider->value + direction * s->step;

    value = value < s->min ? s->min : (value > s->max ? s->max : value);
    if (value != slider->value) {
        slider->value = value;
        rr_emit(slider, "value_changed", slider);
    }
}

void rr_ui_update(RRScene *scene, float dt)
{
    int side, lower = -1, raise = -1;

    if (scene->focus == NULL || !takes_focus(scene->focus)) {
        scene->focus = first_focusable(scene);
        if (scene->focus == NULL) {
            return;
        }
    }
    if (scene->focus->type == RR_NODE_SLIDER) {
        /* The directions along the slider belong to its value. */
        const bool vertical = ((const RRPkgSliderNode *)scene->focus->data)->vertical != 0;
        lower = vertical ? RR_UI_DOWN : RR_UI_LEFT;
        raise = vertical ? RR_UI_UP : RR_UI_RIGHT;
        if (rr_action_pressed(scene->ui_actions[lower]) || rr_action_pressed(scene->ui_actions[raise])) {
            step_slider(scene->focus, rr_action_pressed(scene->ui_actions[raise]) ? 1.0f : -1.0f);
            scene->ui_hold = 0.0f;
        } else if (rr_action_down(scene->ui_actions[lower]) != rr_action_down(scene->ui_actions[raise])) {
            scene->ui_hold += dt;
            while (scene->ui_hold >= REPEAT_DELAY + REPEAT_INTERVAL) {
                scene->ui_hold -= REPEAT_INTERVAL;
                step_slider(scene->focus, rr_action_down(scene->ui_actions[raise]) ? 1.0f : -1.0f);
            }
        } else {
            scene->ui_hold = 0.0f;
        }
    }
    /* RR_UI_LEFT..RR_UI_DOWN are in the same order as RR_SIDE_LEFT..RR_SIDE_BOTTOM. */
    for (side = 0; side < 4; side++) {
        if (side != lower && side != raise && rr_action_pressed(scene->ui_actions[side])) {
            move_focus(scene, side);
            break;
        }
    }
    if (scene->focus->type == RR_NODE_BUTTON && rr_action_pressed(scene->ui_actions[RR_UI_ACCEPT])) {
        Node *button = scene->focus;
        if (((const RRPkgButtonNode *)button->data)->toggle) {
            button->frame = button->frame ? 0 : 1;
            rr_emit(button, "toggled", button);
        }
        rr_emit(button, "pressed", NULL);
    }
}

void rr_button_set_disabled(Node *button, bool disabled)
{
    if (button->type == RR_NODE_BUTTON || button->type == RR_NODE_SLIDER) {
        button->flags = (uint8_t)(disabled ? button->flags | RR_NODE_FLAG_DISABLED
                                           : button->flags & ~RR_NODE_FLAG_DISABLED);
    }
}

bool rr_button_is_disabled(const Node *button)
{
    return (button->type == RR_NODE_BUTTON || button->type == RR_NODE_SLIDER) &&
           (button->flags & RR_NODE_FLAG_DISABLED) != 0;
}

void rr_button_set_pressed(Node *button, bool pressed)
{
    if (button->type == RR_NODE_BUTTON) {
        button->frame = pressed ? 1 : 0;
    }
}

bool rr_button_is_pressed(const Node *button)
{
    return button->type == RR_NODE_BUTTON && button->frame != 0;
}

void rr_control_grab_focus(Node *control)
{
    if (takes_focus(control)) {
        control->scene->focus = control;
        scroll_into_view(control);
    }
}
