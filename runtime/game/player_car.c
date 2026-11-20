/* PlayerCar: a drivable car with a follow camera and a speed display. The run ends when
 * the car's hitbox enters the finish area. Placeholder script: components/player_car.gd. */
#include <stdio.h>

#include "../core/rr_scene.h"

typedef struct {
    float       max_speed;        /* exported */
    float       acceleration;     /* exported */
    float       steer_speed;      /* exported */
    float       track_half_width; /* exported */
    Node       *camera;           /* exported */
    Node       *speed_label;      /* exported */
    const char *menu_scene;       /* exported */

    int   act_accelerate, act_brake, act_left, act_right;
    float speed;
    float start_x;
    float camera_offset[3];
    char  speed_text[16];
} PlayerCar;

/* Share of the acceleration that slows the car down when no pedal is pressed. */
#define COASTING_DRAG 0.3f

/* The car and its camera are moved through their local transforms, so the camera offset
 * is only right while both have the same parent. */
static void player_car_ready(Node *self, PlayerCar *car)
{
    int i;

    car->act_accelerate = rr_action(self->scene, "accelerate");
    car->act_brake      = rr_action(self->scene, "brake");
    car->act_left       = rr_action(self->scene, "steer_left");
    car->act_right      = rr_action(self->scene, "steer_right");
    car->start_x        = self->local.m[9];
    for (i = 0; i < 3; i++) {
        if (car->camera != NULL) {
            car->camera_offset[i] = car->camera->local.m[9 + i] - self->local.m[9 + i];
        }
    }
}

static void player_car_update(Node *self, PlayerCar *car, float dt)
{
    const float pedal = rr_action_strength(car->act_accelerate) - rr_action_strength(car->act_brake);
    const float steer = rr_action_strength(car->act_right) - rr_action_strength(car->act_left);
    float       x;
    int         i;

    car->speed += (pedal != 0.0f ? pedal : -COASTING_DRAG) * car->acceleration * dt;
    if (car->speed < 0.0f) {
        car->speed = 0.0f;
    } else if (car->speed > car->max_speed) {
        car->speed = car->max_speed;
    }

    /* Forward is the node's local -Z axis; steering slides the car sideways, more the
     * faster it goes. */
    for (i = 0; i < 3; i++) {
        self->local.m[9 + i] -= self->local.m[6 + i] * car->speed * dt;
    }
    x = self->local.m[9] + steer * car->steer_speed * (car->speed / car->max_speed) * dt;
    if (x < car->start_x - car->track_half_width) {
        x = car->start_x - car->track_half_width;
    } else if (x > car->start_x + car->track_half_width) {
        x = car->start_x + car->track_half_width;
    }
    self->local.m[9] = x;

    if (car->camera != NULL) {
        for (i = 0; i < 3; i++) {
            car->camera->local.m[9 + i] = self->local.m[9 + i] + car->camera_offset[i];
        }
    }
    if (car->speed_label != NULL) {
        snprintf(car->speed_text, sizeof(car->speed_text), "%d km/h", (int)(car->speed * 3.6f));
        rr_node_set_text(car->speed_label, car->speed_text);
    }
}

/* Connected to the finish area's `area_entered`. Other areas may cross the finish too,
 * so only the car's own hitbox (group "player") ends the run. */
static void player_car_on_finish(Node *self, PlayerCar *car, Node *area)
{
    (void)self;
    if (area != NULL && rr_node_has_tag(area, "player")) {
        rr_change_scene(car->menu_scene);
    }
}

COMPONENT_BEGIN(PlayerCar, "PlayerCar")
    EXPORT_FLOAT(max_speed)
    EXPORT_FLOAT(acceleration)
    EXPORT_FLOAT(steer_speed)
    EXPORT_FLOAT(track_half_width)
    EXPORT_NODE(camera)
    EXPORT_NODE(speed_label)
    EXPORT_SCENE(menu_scene)
    ON_READY(player_car_ready)
    ON_UPDATE(player_car_update)
    ON_SIGNAL_NODE("_on_finish_area_entered", player_car_on_finish)
COMPONENT_END()
