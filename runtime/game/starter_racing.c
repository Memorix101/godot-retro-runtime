/* Components of the Starter Kit Racing demo (scenes/starter_racing.tscn).
 * Placeholder scripts: components/starter_vehicle.gd and components/starter_view.gd.
 *
 * After Kenney's Starter Kit Racing (https://github.com/KenneyNL/Starter-Kit-Racing, MIT
 * licence, see assets/kenney/starter-kit-racing/License.txt): its scripts vehicle.gd and
 * view.gd, rewritten in C. The scenes are the kit's, with GPUParticles3D swapped for
 * CPUParticles3D and the input actions of this project.
 *
 * StarterVehicle: the car is a RigidBody3D sphere with a rough physics material. The gas
 * spins the sphere around the car's X axis, and the grip of the ground makes it roll.
 * The model is not part of the physics: it is put where the sphere is every frame, turned
 * by the steering, laid onto the ground that a RayCast3D finds below it, and its body
 * leans and tilts with the steering and the change of speed. Smoke and the skid sound
 * come when the speed of the model and that of the sphere differ (a slide), the engine
 * sound follows the speed, and a knock is heard when the sphere meets a body.
 *
 * StarterView: a rig that eases after the car, with a camera that backs off the faster
 * the car goes. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/rr_scene.h"

#define PI_F  3.14159265f
#define TAU_F 6.28318531f

typedef struct {
    Node *sphere, *ground, *container;                  /* exported */
    Node *vehicle_body, *wheel_fl, *wheel_fr, *wheel_bl; /* exported, optional */
    Node *wheel_br, *trail_left, *trail_right;           /* exported, optional */
    Node *screech_sound, *engine_sound, *impact_sound;   /* exported, optional */

    int   act_gas, act_brake, act_left, act_right;
    float input_x, input_z; /* steering and gas as the kit's `input.x` and `input.z` */
    float acceleration, angular_speed, linear_speed;
    bool  colliding;
    float linear_velocity[3], prev_position[3]; /* of the model, in the car's space */
    float calculated_lean;
    float body_rot_x, body_rot_z;      /* the body's rotation, Euler angles as in Godot */
    float wheel_spin, front_steer;     /* the wheels' rotation around X, the front's around Y */
} StarterVehicle;

/* --- small maths, as Godot has it -------------------------------------------------------- */

static float lerpf(float from, float to, float weight)
{
    return from + (to - from) * weight;
}

static float lerp_angle(float from, float to, float weight)
{
    const float diff = fmodf(to - from, TAU_F);
    return from + (fmodf(2.0f * diff, TAU_F) - diff) * weight;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float remapf(float v, float in_lo, float in_hi, float out_lo, float out_hi)
{
    return out_lo + (v - in_lo) * (out_hi - out_lo) / (in_hi - in_lo);
}

static float signf(float v)
{
    return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f);
}

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cross3(const float *a, const float *b, float *out)
{
    const float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

static void normalize3(float *v)
{
    const float length = sqrtf(dot3(v, v));
    if (length > 0.0f) {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
}

/* A direction of a node's parent space into the world: by the parent's basis. */
static void basis_apply(const float *m, const float *v, float *out)
{
    int k;
    for (k = 0; k < 3; k++) {
        out[k] = m[k] * v[0] + m[3 + k] * v[1] + m[6 + k] * v[2];
    }
}

/* The other way: a world direction into a space whose basis is `m` (square axes). */
static void basis_unapply(const float *m, const float *v, float *out)
{
    const float x = dot3(&m[0], v) / dot3(&m[0], &m[0]), y = dot3(&m[3], v) / dot3(&m[3], &m[3]),
                z = dot3(&m[6], v) / dot3(&m[6], &m[6]);
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

/* Basis.orthonormalized(): Gram-Schmidt in the order X, Y, Z. */
static void orthonormalize(float *m)
{
    float *x = &m[0], *y = &m[3], *z = &m[6];
    float  d;
    int    k;

    normalize3(x);
    d = dot3(x, y);
    for (k = 0; k < 3; k++) {
        y[k] -= x[k] * d;
    }
    normalize3(y);
    d = dot3(x, z);
    for (k = 0; k < 3; k++) {
        z[k] -= x[k] * d;
    }
    d = dot3(y, z);
    for (k = 0; k < 3; k++) {
        z[k] -= y[k] * d;
    }
    normalize3(z);
}

/* The rotation of a basis with square axes of unit length as a quaternion (x, y, z, w),
 * and back. */
static void quat_from_basis(const float *m, float *q)
{
    const float m00 = m[0], m10 = m[1], m20 = m[2], m01 = m[3], m11 = m[4], m21 = m[5];
    const float m02 = m[6], m12 = m[7], m22 = m[8], trace = m00 + m11 + m22;
    float       s;

    if (trace > 0.0f) {
        s    = sqrtf(trace + 1.0f) * 2.0f;
        q[3] = 0.25f * s;
        q[0] = (m21 - m12) / s;
        q[1] = (m02 - m20) / s;
        q[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        s    = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q[3] = (m21 - m12) / s;
        q[0] = 0.25f * s;
        q[1] = (m01 + m10) / s;
        q[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        s    = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q[3] = (m02 - m20) / s;
        q[0] = (m01 + m10) / s;
        q[1] = 0.25f * s;
        q[2] = (m12 + m21) / s;
    } else {
        s    = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q[3] = (m10 - m01) / s;
        q[0] = (m02 + m20) / s;
        q[1] = (m12 + m21) / s;
        q[2] = 0.25f * s;
    }
}

static void basis_from_quat(const float *q, float *m)
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1.0f - 2.0f * (y * y + z * z);
    m[1] = 2.0f * (x * y + z * w);
    m[2] = 2.0f * (x * z - y * w);
    m[3] = 2.0f * (x * y - z * w);
    m[4] = 1.0f - 2.0f * (x * x + z * z);
    m[5] = 2.0f * (y * z + x * w);
    m[6] = 2.0f * (x * z + y * w);
    m[7] = 2.0f * (y * z - x * w);
    m[8] = 1.0f - 2.0f * (x * x + y * y);
}

/* Quaternion.slerp, the short way round. */
static void slerp(const float *a, const float *b_in, float t, float *out)
{
    float b[4], cosine = a[0] * b_in[0] + a[1] * b_in[1] + a[2] * b_in[2] + a[3] * b_in[3], wa, wb, length;
    int   k;

    for (k = 0; k < 4; k++) {
        b[k] = cosine < 0.0f ? -b_in[k] : b_in[k];
    }
    cosine = fabsf(cosine);
    if (1.0f - cosine > 0.0001f) {
        const float omega = acosf(cosine), sine = sinf(omega);
        wa = sinf((1.0f - t) * omega) / sine;
        wb = sinf(t * omega) / sine;
    } else {
        wa = 1.0f - t;
        wb = t;
    }
    for (k = 0; k < 4; k++) {
        out[k] = a[k] * wa + b[k] * wb;
    }
    length = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    for (k = 0; k < 4; k++) {
        out[k] /= length;
    }
}

/* Node3D.rotate_y: turns a basis around its parent's Y axis. */
static void rotate_y(float *m, float angle)
{
    const float c = cosf(angle), s = sinf(angle);
    int         i;

    for (i = 0; i < 3; i++) {
        float *axis = &m[i * 3];
        const float x = axis[0], z = axis[2];
        axis[0] = c * x + s * z;
        axis[2] = -s * x + c * z;
    }
}

/* A basis from Godot's Euler angles (order YXZ), for the parts of the model. */
static void basis_from_euler(float *m, float rx, float ry, float rz)
{
    const float cx = cosf(rx), sx = sinf(rx), cy = cosf(ry), sy = sinf(ry), cz = cosf(rz), sz = sinf(rz);
    /* Y * X * Z, the columns of the product. */
    m[0] = cy * cz + sy * sx * sz;
    m[1] = cx * sz;
    m[2] = -sy * cz + cy * sx * sz;
    m[3] = -cy * sz + sy * sx * cz;
    m[4] = cx * cz;
    m[5] = sy * sz + cy * sx * cz;
    m[6] = sy * cx;
    m[7] = -sx;
    m[8] = cy * cx;
}

/* --- StarterVehicle --------------------------------------------------------------------- */

static void starter_vehicle_ready(Node *self, StarterVehicle *v)
{
    v->act_gas   = rr_action(self->scene, "accelerate");
    v->act_brake = rr_action(self->scene, "brake");
    v->act_left  = rr_action(self->scene, "steer_left");
    v->act_right = rr_action(self->scene, "steer_right");
    if (v->container != NULL) {
        memcpy(v->prev_position, &v->container->local.m[9], sizeof(v->prev_position));
    }
}

/* The kit's handle_input: the stick and the gas count while the car is on the ground,
 * and the sphere is spun around the car's X axis as fast as the car is to go. */
static void handle_input(Node *self, StarterVehicle *v, float dt)
{
    float spin[3], axis[3];
    int   k;

    if (rr_raycast_is_colliding(v->ground)) {
        v->input_x = rr_action_strength(v->act_right) - rr_action_strength(v->act_left);
        v->input_z = rr_action_strength(v->act_gas) - rr_action_strength(v->act_brake);
    }
    /* The container's X axis in the world: the car's basis, then its own. */
    basis_apply(self->world.m, &v->container->local.m[0], axis);
    rr_rigid_body_get_angular_velocity(v->sphere, spin);
    for (k = 0; k < 3; k++) {
        spin[k] += axis[k] * (v->linear_speed * 100.0f) * dt;
    }
    rr_rigid_body_set_angular_velocity(v->sphere, spin);
}

/* The kit's align_with_y, then interpolate_with(.., 0.2): the model is laid a fifth of the
 * way onto the ground every frame. Done in the car's own space. */
static void lay_on_ground(Node *self, StarterVehicle *v, const float *world_normal)
{
    float *m = v->container->local.m;
    float  n[3], to[9], q_from[4], q_to[4], q[4];
    int    k;

    basis_unapply(self->world.m, world_normal, n);
    normalize3(n);
    memcpy(to, m, sizeof(to));
    /* basis.y = n; basis.x = -basis.z.cross(n); orthonormalized */
    for (k = 0; k < 3; k++) {
        to[3 + k] = n[k];
    }
    cross3(&to[6], n, &to[0]);
    for (k = 0; k < 3; k++) {
        to[k] = -to[k];
    }
    orthonormalize(to);
    quat_from_basis(m, q_from);
    quat_from_basis(to, q_to);
    slerp(q_from, q_to, 0.2f, q);
    basis_from_quat(q, m);
    orthonormalize(m);
}

static void effect_engine(StarterVehicle *v, float dt)
{
    const float speed = clampf(fabsf(v->linear_speed), 0.0f, 1.0f);
    const float throttle = clampf(fabsf(v->input_z), 0.0f, 1.0f);
    float       volume, pitch;

    if (v->engine_sound == NULL) {
        return;
    }
    volume = remapf(speed + throttle * 0.5f, 0.0f, 1.5f, -15.0f, -5.0f);
    rr_audio_set_volume_db(v->engine_sound, lerpf(rr_audio_get_volume_db(v->engine_sound), volume, dt * 5.0f));
    pitch = remapf(speed, 0.0f, 1.0f, 0.5f, 3.0f) + (throttle > 0.1f ? 0.2f : 0.0f);
    rr_audio_set_pitch_scale(v->engine_sound, lerpf(rr_audio_get_pitch_scale(v->engine_sound), pitch, dt * 2.0f));
}

static void effect_body(StarterVehicle *v, float dt)
{
    float *m;
    int    k;

    v->calculated_lean = lerp_angle(v->calculated_lean, -v->input_x / 5.0f * v->linear_speed, dt * 5.0f);
    if (v->vehicle_body == NULL) {
        return;
    }
    v->body_rot_x = lerp_angle(v->body_rot_x, -(v->linear_speed - v->acceleration) / 6.0f, dt * 10.0f);
    v->body_rot_z = v->calculated_lean;
    m             = v->vehicle_body->local.m;
    basis_from_euler(m, v->body_rot_x, 0.0f, v->body_rot_z);
    for (k = 0; k < 3; k++) {
        const float rest = k == 1 ? 0.2f : 0.0f;
        m[9 + k] = lerpf(m[9 + k], rest, dt * 5.0f);
    }
}

static void effect_wheels(StarterVehicle *v, float dt)
{
    Node *const wheels[4] = { v->wheel_fl, v->wheel_fr, v->wheel_bl, v->wheel_br };
    int         i;

    /* Turned by the acceleration every frame, and the front ones steered. */
    v->wheel_spin  = fmodf(v->wheel_spin + v->acceleration, TAU_F);
    v->front_steer = lerp_angle(v->front_steer, -v->input_x / 1.5f, dt * 10.0f);
    for (i = 0; i < 4; i++) {
        if (wheels[i] != NULL) {
            basis_from_euler(wheels[i]->local.m, v->wheel_spin, i < 2 ? v->front_steer : 0.0f, 0.0f);
        }
    }
}

static void effect_trails(StarterVehicle *v, float dt)
{
    const float drift = fabsf(v->linear_speed - v->acceleration) + fabsf(v->calculated_lean) * 2.0f;
    const bool  emit = drift > 0.25f;
    float       volume = -80.0f;

    if (v->trail_left != NULL) {
        rr_particles_set_emitting(v->trail_left, emit);
    }
    if (v->trail_right != NULL) {
        rr_particles_set_emitting(v->trail_right, emit);
    }
    if (v->screech_sound == NULL) {
        return;
    }
    if (emit) {
        volume = remapf(clampf(drift, 0.25f, 2.0f), 0.25f, 2.0f, -10.0f, 0.0f);
    }
    rr_audio_set_pitch_scale(v->screech_sound, lerpf(rr_audio_get_pitch_scale(v->screech_sound),
                                                     clampf(fabsf(v->linear_speed), 1.0f, 3.0f), 0.1f));
    rr_audio_set_volume_db(v->screech_sound, lerpf(rr_audio_get_volume_db(v->screech_sound), volume, 10.0f * dt));
}

static void starter_vehicle_update(Node *self, StarterVehicle *v, float dt)
{
    float direction, grip, target, target_speed, spin[3];
    float *pos;
    int    k;

    if (v->sphere == NULL || v->ground == NULL || v->container == NULL || dt <= 0.0f) {
        return;
    }
    handle_input(self, v, dt);

    direction = signf(v->linear_speed);
    if (direction == 0.0f) {
        direction = fabsf(v->input_z) > 0.1f ? signf(v->input_z) : 1.0f;
    }
    grip             = clampf(fabsf(v->linear_speed), 0.2f, 1.0f);
    target           = -v->input_x * grip * 4.0f * direction;
    v->angular_speed = lerpf(v->angular_speed, target, dt * 4.0f);
    rotate_y(v->container->local.m, v->angular_speed * dt);

    /* On the ground: land with a bounce of the body, and lie down the way the ground does,
     * unless it is steeper than 60 degrees against the car. */
    if (rr_raycast_is_colliding(v->ground)) {
        float normal[3], up[3];
        if (!v->colliding) {
            if (v->vehicle_body != NULL) {
                v->vehicle_body->local.m[9]  = 0.0f;
                v->vehicle_body->local.m[10] = 0.1f;
                v->vehicle_body->local.m[11] = 0.0f;
            }
            v->input_z = 0.0f;
        }
        rr_raycast_get_collision_normal(v->ground, normal);
        basis_apply(self->world.m, &v->container->local.m[3], up);
        normalize3(up);
        if (dot3(normal, up) > 0.5f) {
            lay_on_ground(self, v, normal);
        }
    }
    v->colliding = rr_raycast_is_colliding(v->ground);

    target_speed = v->input_z;
    if (target_speed < 0.0f && v->linear_speed > 0.01f) {
        v->linear_speed = lerpf(v->linear_speed, 0.0f, dt * 8.0f); /* braking */
    } else if (target_speed < 0.0f) {
        v->linear_speed = lerpf(v->linear_speed, target_speed / 2.0f, dt * 2.0f); /* reverse */
    } else {
        v->linear_speed = lerpf(v->linear_speed, target_speed, dt * 6.0f);
    }
    rr_rigid_body_get_angular_velocity(v->sphere, spin);
    v->acceleration = lerpf(v->acceleration,
                            v->linear_speed + fabsf(sqrtf(dot3(spin, spin)) * v->linear_speed) / 100.0f, dt);

    /* The model and the ray follow the sphere. */
    pos = &v->container->local.m[9];
    for (k = 0; k < 3; k++) {
        pos[k]                     = v->sphere->local.m[9 + k] - (k == 1 ? 0.65f : 0.0f);
        v->ground->local.m[9 + k] = v->sphere->local.m[9 + k];
        v->linear_velocity[k]      = (pos[k] - v->prev_position[k]) / dt;
        v->prev_position[k]        = pos[k];
    }

    effect_engine(v, dt);
    effect_body(v, dt);
    effect_wheels(v, dt);
    effect_trails(v, dt);
}

/* A knock when the sphere meets a body, the louder the faster the car went along its
 * nose. */
static void starter_vehicle_on_sphere(Node *self, StarterVehicle *v, Node *body)
{
    float speed;

    (void)self;
    (void)body;
    if (v->vehicle_body == NULL || v->impact_sound == NULL || rr_audio_is_playing(v->impact_sound)) {
        return;
    }
    {
        float nose[3];
        memcpy(nose, &v->vehicle_body->world.m[6], sizeof(nose));
        normalize3(nose);
        speed = fabsf(dot3(v->linear_velocity, nose));
    }
    rr_audio_set_volume_db(v->impact_sound, clampf(remapf(speed, 0.0f, 6.0f, -20.0f, 0.0f), -20.0f, 0.0f));
    rr_audio_play(v->impact_sound);
}

COMPONENT_BEGIN(StarterVehicle, "StarterVehicle")
    EXPORT_NODE(sphere)
    EXPORT_NODE(ground)
    EXPORT_NODE(container)
    EXPORT_NODE(vehicle_body)
    EXPORT_NODE(wheel_fl)
    EXPORT_NODE(wheel_fr)
    EXPORT_NODE(wheel_bl)
    EXPORT_NODE(wheel_br)
    EXPORT_NODE(trail_left)
    EXPORT_NODE(trail_right)
    EXPORT_NODE(screech_sound)
    EXPORT_NODE(engine_sound)
    EXPORT_NODE(impact_sound)
    ON_READY(starter_vehicle_ready)
    ON_UPDATE(starter_vehicle_update)
    ON_SIGNAL_NODE("_on_sphere_body_entered", starter_vehicle_on_sphere)
COMPONENT_END()

/* --- StarterView ------------------------------------------------------------------------ */

typedef struct {
    Node       *target, *camera; /* exported */
    const char *menu_scene;      /* exported */

    int act_leave;
} StarterView;

static void starter_view_ready(Node *self, StarterView *w)
{
    w->act_leave = rr_action(self->scene, "leave");
}

static void starter_view_update(Node *self, StarterView *w, float dt)
{
    const StarterVehicle *car;
    float                 speed;
    int                   k;

    if (rr_action_pressed(w->act_leave) && w->menu_scene != NULL && w->menu_scene[0] != '\0') {
        rr_change_scene(w->menu_scene);
        return;
    }
    car = w->target != NULL ? (const StarterVehicle *)rr_node_component(w->target, "StarterVehicle") : NULL;
    if (car == NULL || car->container == NULL) {
        return;
    }
    /* Eases towards the model; the rig stands in the scene's root, as in the kit. */
    for (k = 0; k < 3; k++) {
        self->local.m[9 + k] = lerpf(self->local.m[9 + k], car->container->world.m[9 + k], dt * 4.0f);
    }
    if (w->camera != NULL) {
        speed                 = clampf(fabsf(car->linear_speed), 0.0f, 1.0f);
        w->camera->local.m[11] = lerpf(w->camera->local.m[11], remapf(speed, 0.0f, 1.0f, 10.0f, 20.0f), dt * 0.5f);
    }
}

COMPONENT_BEGIN(StarterView, "StarterView")
    EXPORT_NODE(target)
    EXPORT_NODE(camera)
    EXPORT_SCENE(menu_scene)
    ON_READY(starter_view_ready)
    ON_UPDATE(starter_view_update)
COMPONENT_END()
