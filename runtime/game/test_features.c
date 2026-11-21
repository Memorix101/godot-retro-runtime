/* Component of the self-checking test scene (scenes/features.tscn).
 * Placeholder script: components/tests/feature_test.gd.
 *
 * It changes a few things in its first frame, looks at what the nodes report two frames
 * later, prints "features: ok" or one line per failed check, and quits. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/rr_internal.h" /* rr_audio_mix: what a player sounds like, which only a test asks */
#include "../core/rr_platform.h" /* the size of the screen */
#include "../core/rr_scene.h"
#include "game_state.h"

/* A custom resource: components/tests/test_stats.gd. */
typedef struct TestStats TestStats;
struct TestStats {
    float            power; /* exported */
    int32_t          gears; /* exported */
    const TestStats *next;  /* exported */
};

RESOURCE_BEGIN(TestStats)
    EXPORT_FLOAT(power)
    EXPORT_INT(gears)
    EXPORT_RESOURCE(next, TestStats)
RESOURCE_END()

typedef struct {
    Node *ray_down, *ray_miss, *ray_2d;             /* exported */
    Node *track, *follower, *follower_loop, *follower_2d; /* exported */
    Node *bar;                                      /* exported */
    Node *speaker, *speaker_2d, *beep, *music; /* exported */
    Node *mover, *tween_target;                /* exported */
    Node *greeting;                            /* exported: a label with a key as its text */
    const TestStats *stats;      /* exported */
    const char      *self_scene; /* exported: this scene, to load it a second time */

    int  frame, failed;
    bool beep_finished, anim_finished;
    int  anim_marks;
    int  body_hits;
    Node *walker, *ball; /* found by their groups */
    float walker_fall, walker_start_x;
    bool  ball_bounced, ball_sank;
    Node *last_body;
    float since; /* seconds the tweens have been running */

    /* Started from the menu, the scene stays and shows what the checks found. */
    Node  *result; /* the label for the report, found by its group */
    bool   done;
    int    act_cancel;
    size_t report_length;
    char   report[480];
} FeatureTest;

static void report(FeatureTest *test, const char *line)
{
    const int written = snprintf(test->report + test->report_length,
                                 sizeof(test->report) - test->report_length, "%s\n", line);
    if (written > 0 && test->report_length + (size_t)written < sizeof(test->report)) {
        test->report_length += (size_t)written;
    }
}

static void check(FeatureTest *test, bool ok, const char *what)
{
    if (!ok) {
        printf("features: FAIL %s\n", what);
        report(test, what);
        test->failed++;
    }
}

static bool near(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

static void feature_test_update(Node *self, FeatureTest *test, float dt)
{
    float  v[3], volume, pan;
    Node  *hit;
    GameState *game = game_state(self->scene);
    uint32_t   block[2] = { 0x52455452u, 77u }, back[2] = { 0u, 0u };

    if (test->done) {
        /* The checks are over: something to look at, until the player leaves. */
        test->follower_loop->value += 2.0f * dt;
        if (rr_action_pressed(test->act_cancel)) {
            game->from_menu = false;
            rr_change_scene("main_menu");
        }
        return;
    }
    test->frame++;
    /* A capsule walks up a ramp that is turned by 30 degrees; a ball falls on a pad. */
    if (test->frame == 1) {
        test->result     = rr_scene_find_tagged(self->scene, "result", NULL);
        test->act_cancel = rr_action(self->scene, "ui_cancel");
        test->walker         = rr_scene_find_tagged(self->scene, "walker", NULL);
        test->ball           = rr_scene_find_tagged(self->scene, "ball", NULL);
        test->walker_start_x = test->walker->local.m[9];
    }
    {
        float walk[3] = { 2.0f, test->walker_fall - 9.8f * dt, 0.0f }, speed[3];
        rr_body_move_and_slide(test->walker, walk, dt);
        test->walker_fall = walk[1];
        rr_rigid_body_get_linear_velocity(test->ball, speed);
        if (speed[1] > 0.5f) {
            test->ball_bounced = true;
        }
        if (test->ball->local.m[10] < 0.95f) {
            test->ball_sank = true; /* the pad's top is at 0.5, the ball's radius is 0.5 */
        }
    }
    if (test->frame == 1) {
        test->follower->value      = 25.0f; /* past the end of a path that does not loop */
        test->follower_loop->value = 12.0f; /* once around a path of length 10 */
        test->bar->value           = 75.0f;
        {
            const float to[3] = { 4.0f, 0.0f, 0.0f }, full = 100.0f;
            rr_tween_property(test->tween_target, RR_TWEEN_POSITION, to, 0.25f, 0.0f,
                              RR_TRANS_QUAD, RR_EASE_OUT);
            rr_tween_property(test->bar, RR_TWEEN_VALUE, &full, 0.2f, 0.1f, RR_TRANS_LINEAR,
                              RR_EASE_IN);
        }
        test->since = dt; /* the tweens are advanced by this frame time right after */
        return;
    }
    if (test->frame == 5 || test->frame == 8) {
        /* Where the tweens must be after the time that has passed: a quadratic ease-out
         * over 0.25 s, and a linear one over 0.2 s that waited 0.1 s first. */
        const float t = test->since < 0.25f ? test->since / 0.25f : 1.0f;
        const float late = test->since - 0.1f;
        const float u = late < 0.0f ? 0.0f : (late < 0.2f ? late / 0.2f : 1.0f);
        check(test, near(test->tween_target->local.m[9], 4.0f * (1.0f - (1.0f - t) * (1.0f - t))),
              "tween on its way");
        check(test, near(test->bar->value, 75.0f + 25.0f * u), "tween with a delay");
    }
    test->since += dt;
    if (test->frame != 40) {
        return;
    }

    /* RayCast3D */
    check(test, rr_raycast_is_colliding(test->ray_down), "RayDown is colliding");
    hit = rr_raycast_get_collider(test->ray_down);
    check(test, hit != NULL && strcmp(hit->name, "Floor") == 0, "RayDown hits Floor");
    rr_raycast_get_collision_point(test->ray_down, v);
    check(test, near(v[0], 0.0f) && near(v[1], 1.0f) && near(v[2], 0.0f), "RayDown point");
    rr_raycast_get_collision_normal(test->ray_down, v);
    check(test, near(v[0], 0.0f) && near(v[1], 1.0f) && near(v[2], 0.0f), "RayDown normal");
    check(test, !rr_raycast_is_colliding(test->ray_miss), "RayMiss hits nothing");
    test->ray_miss->local.m[9] = 1.0f; /* over the floor now */
    check(test, !rr_raycast_is_colliding(test->ray_miss), "RayMiss keeps its result until updated");
    rr_raycast_force_update(test->ray_miss);
    check(test, rr_raycast_is_colliding(test->ray_miss), "RayMiss hits after force_update");

    /* RayCast2D against an Area2D with a circle */
    hit = rr_raycast_get_collider(test->ray_2d);
    check(test, hit != NULL && strcmp(hit->name, "Bubble") == 0, "Ray2D hits Bubble");
    rr_raycast_get_collision_point(test->ray_2d, v);
    check(test, near(v[0], 40.0f) && near(v[1], 0.0f), "Ray2D point");
    rr_raycast_get_collision_normal(test->ray_2d, v);
    check(test, near(v[0], -1.0f) && near(v[1], 0.0f), "Ray2D normal");

    /* Path3D, PathFollow3D */
    check(test, near(rr_path_get_length(test->track), 10.0f), "Track length");
    check(test, near(test->follower->value, 10.0f), "Follower stops at the end");
    check(test, near(test->follower->local.m[9], 10.0f), "Follower position");
    check(test, near(test->follower_loop->value, 2.0f), "FollowerLoop wraps");
    check(test, near(test->follower_loop->local.m[9], 2.0f), "FollowerLoop position");
    /* -Z looks along the path, which runs along +X. */
    check(test, near(test->follower_loop->local.m[6], -1.0f) && near(test->follower_loop->local.m[4], 1.0f),
          "FollowerLoop rotation");
    check(test, near(test->follower_loop->world.m[9], 2.0f), "FollowerLoop world position");

    /* Path2D, PathFollow2D: X looks along the path, which runs down the screen. */
    check(test, near(test->follower_2d->local.m[9], 0.0f) && near(test->follower_2d->local.m[10], 30.0f),
          "Follower2D position");
    check(test, near(test->follower_2d->local.m[0], 0.0f) && near(test->follower_2d->local.m[1], 1.0f),
          "Follower2D rotation");

    /* Audio: a 3D player one unit size to the right of the camera, a 2D player half way
     * to its maximum distance and to the right edge of the screen. */
    rr_audio_mix(test->speaker, &volume, &pan);
    check(test, near(volume, 1.0f) && near(pan, 1.0f), "AudioStreamPlayer3D volume and pan");
    rr_audio_mix(test->speaker_2d, &volume, &pan);
    {
        /* The listener is the middle of the screen, which is not as wide on every
         * platform: on one of 640 pixels both values are 0.5. */
        const RRPkgAudioNode *a = (const RRPkgAudioNode *)test->speaker_2d->data;
        const float           half = (float)RR_SCREEN_WIDTH * 0.5f;
        const float           dx = test->speaker_2d->world.m[9] - half;
        check(test, near(test->speaker_2d->world.m[10], (float)RR_SCREEN_HEIGHT * 0.5f) &&
                        near(volume, 1.0f - fabsf(dx) / a->max_distance) && near(pan, dx / half),
              "AudioStreamPlayer2D volume and pan");
    }
    check(test, test->beep_finished && !rr_audio_is_playing(test->beep), "finished signal");
    check(test, rr_audio_is_playing(test->music), "music plays");
    {
        /* The buses of the project's bus layout, and how fast a player plays. */
        const int sfx = rr_audio_bus_index(self->scene, "SFX");
        check(test, sfx > 0 && rr_audio_bus_index(self->scene, "Nope") == -1, "audio bus by name");
        check(test, near(rr_audio_bus_get_volume(self->scene, sfx), 1.0f), "audio bus volume at the start");
        rr_audio_bus_set_volume(self->scene, sfx, 0.25f);
        rr_audio_bus_set_mute(self->scene, sfx, true);
        check(test, near(rr_audio_bus_get_volume(self->scene, sfx), 0.25f) &&
                    rr_audio_bus_is_mute(self->scene, sfx), "audio bus volume and mute");
        rr_audio_bus_set_volume(self->scene, sfx, 1.0f);
        rr_audio_bus_set_mute(self->scene, sfx, false);
        check(test, near(rr_audio_get_pitch_scale(test->beep), 1.0f), "pitch scale at the start");
        rr_audio_set_pitch_scale(test->beep, 2.0f);
        check(test, near(rr_audio_get_pitch_scale(test->beep), 2.0f), "pitch scale");
        rr_audio_set_pitch_scale(test->beep, 1.0f);
    }
    {
        /* Translations: the label's text is a key. Whatever language the machine has,
         * the checks start from English. */
        static const char not_a_key[] = "NOT_A_KEY";
        rr_set_locale(self->scene, "en");
        check(test, strcmp(rr_tr(self->scene, "FAREWELL"), "Goodbye") == 0, "tr of a key");
        check(test, rr_tr(self->scene, not_a_key) == not_a_key, "tr of a text that is no key");
        check(test, strcmp(test->greeting->text, "Hello") == 0, "label text is translated");
        rr_set_locale(self->scene, "de_AT");
        check(test, strcmp(test->greeting->text, "Hallo") == 0 &&
                    strcmp(rr_tr(self->scene, "FAREWELL"), "Auf Wiedersehen") == 0,
              "locale of the same language");
        rr_node_set_text(test->greeting, "FAREWELL");
        check(test, strcmp(test->greeting->text, "Auf Wiedersehen") == 0, "a new text is translated");
        rr_set_locale(self->scene, "en");
        check(test, strcmp(test->greeting->text, "Hello") == 0, "back to the first locale");
    }

    /* An Area3D that sits inside the floor's box sees that body once. */
    check(test, test->body_hits == 1 && test->last_body != NULL &&
                    strcmp(test->last_body->name, "Floor") == 0,
          "body_entered");

    /* Bodies with round shapes, a rotated static box and a rigid body */
    check(test, rr_body_is_on_floor(test->walker), "capsule stands on the ramp");
    check(test, test->walker->local.m[9] > test->walker_start_x + 0.8f, "capsule walks along the ramp");
    check(test, test->walker->local.m[10] > 1.6f, "capsule climbs the ramp");
    check(test, test->ball_bounced && !test->ball_sank, "rigid body bounces off the pad");
    check(test, near(test->ball->local.m[9], -8.0f), "rigid body falls straight");

    /* Tweens */
    check(test, near(test->tween_target->local.m[9], 4.0f) && !rr_tween_is_running(test->tween_target),
          "tween end");
    check(test, near(test->bar->value, 100.0f), "tween of a value");

    /* AnimationPlayer: 2D tracks, a method track and the finished signal */
    check(test, near(test->mover->local.m[9], 100.0f) && near(test->mover->local.m[10], 50.0f),
          "2D position track");
    check(test, near(test->mover->local.m[0], 0.0f) && near(test->mover->local.m[1], 1.0f),
          "2D rotation track");
    check(test, test->anim_marks == 1, "method track");
    check(test, test->anim_finished, "animation_finished signal");

    /* Custom resources */
    check(test, test->stats != NULL && near(test->stats->power, 2.5f) && test->stats->gears == 6,
          "resource values");
    check(test, test->stats != NULL && test->stats->next != NULL && test->stats->next->gears == 4 &&
                    test->stats->next->next == NULL,
          "resource inside a resource");

    /* Saved games */
    check(test, rr_save_write("RR_TEST", block, sizeof(block)), "save write");
    check(test, rr_save_read("RR_TEST", back, sizeof(back)) && back[0] == block[0] && back[1] == 77u,
          "save read");
    check(test, !rr_save_read("RR_TEST", back, sizeof(back) - 4), "save read with a wrong size");
    check(test, !rr_save_read("RR_NONE", back, sizeof(back)), "save read of a missing block");
    check(test, !rr_save_write("bad name", block, sizeof(block)), "save name is checked");

    /* Autoload: its state survives a scene change, its exports are set anew. */
    check(test, game != NULL, "autoload Game");
    if (game != NULL && game->marker != 1234) {
        /* First time through: leave a mark and load this scene once more. */
        game->marker        = 1234;
        game->marker_visits = game->visits;
        game->start_lives   = 99;
        if (test->failed == 0) {
            rr_change_scene(test->self_scene);
            return;
        }
    } else if (game != NULL) {
        check(test, game->visits == game->marker_visits + 1, "autoload keeps its state");
        check(test, game->start_lives == 3, "autoload exports are set again");
    }
    if (game != NULL) {
        game->marker = 0; /* the next run starts from the beginning */
    }

    if (test->failed == 0) {
        printf("features: ok\n");
        report(test, "ALL CHECKS OK");
    }
    fflush(stdout);
    if (game != NULL && game->from_menu) {
        if (test->result != NULL) {
            rr_node_set_text(test->result, test->report);
        }
        test->done = true;
    } else {
        rr_quit();
    }
}

static void feature_test_on_beep_finished(Node *self, FeatureTest *test)
{
    (void)self;
    test->beep_finished = true;
}

static void feature_test_on_sensor_body_entered(Node *self, FeatureTest *test, Node *body)
{
    (void)self;
    test->body_hits++;
    test->last_body = body;
}

static void feature_test_on_anim_mark(Node *self, FeatureTest *test)
{
    (void)self;
    test->anim_marks++;
}

static void feature_test_on_anim_finished(Node *self, FeatureTest *test)
{
    (void)self;
    test->anim_finished = true;
}

COMPONENT_BEGIN(FeatureTest, "FeatureTest")
    EXPORT_NODE(ray_down)
    EXPORT_NODE(ray_miss)
    EXPORT_NODE(ray_2d)
    EXPORT_NODE(track)
    EXPORT_NODE(follower)
    EXPORT_NODE(follower_loop)
    EXPORT_NODE(follower_2d)
    EXPORT_NODE(bar)
    EXPORT_NODE(speaker)
    EXPORT_NODE(speaker_2d)
    EXPORT_NODE(beep)
    EXPORT_NODE(music)
    EXPORT_NODE(mover)
    EXPORT_NODE(tween_target)
    EXPORT_NODE(greeting)
    EXPORT_RESOURCE(stats, TestStats)
    EXPORT_SCENE(self_scene)
    ON_UPDATE(feature_test_update)
    ON_SIGNAL("_on_beep_finished", feature_test_on_beep_finished)
    ON_SIGNAL_NODE("_on_sensor_body_entered", feature_test_on_sensor_body_entered)
    ON_SIGNAL("_on_anim_mark", feature_test_on_anim_mark)
    ON_SIGNAL("_on_anim_finished", feature_test_on_anim_finished)
COMPONENT_END()

/* --- TriggerStress: counts the overlaps of scenes/tests/stress_triggers.tscn ----------- */

typedef struct {
    Node *row; /* exported: moved away after the first count */
    Node *free_walker, *blocked_walker; /* exported */

    int frame, entered, exited;
} TriggerStress;

static void trigger_stress_update(Node *self, TriggerStress *t, float dt)
{
    float walk[3] = { 2.0f, 0.0f, 0.0f };

    rr_body_move_and_slide(t->free_walker, walk, dt);
    walk[0] = 2.0f;
    rr_body_move_and_slide(t->blocked_walker, walk, dt);
    t->frame++;
    if (t->frame == 3) {
        /* All signals of the start are in: two per overlapping pair. */
        printf("stress: entered %d\n", t->entered);
        t->row->local.m[10] -= 5.0f; /* breaks the 10 pairs between the first two rows */
    } else if (t->frame == 6) {
        printf("stress: exited %d\n", t->exited);
        {
            Node *ray = NULL;
            int   hits = 0, gaps = 0, rays = 0;
            while ((ray = rr_scene_find_tagged(self->scene, "hit_ray", ray)) != NULL) {
                const Node *hit = rr_raycast_get_collider(ray);
                float       point[3];
                rr_raycast_get_collision_point(ray, point);
                /* The body in line with the ray, at the front of its box. */
                hits += hit != NULL && fabsf(hit->world.m[9] - ray->world.m[9]) < 0.01f &&
                        fabsf(hit->world.m[10] - ray->world.m[10]) < 0.01f &&
                        fabsf(point[2] + 19.8f) < 0.01f;
                rays++;
            }
            while ((ray = rr_scene_find_tagged(self->scene, "gap_ray", ray)) != NULL) {
                gaps += !rr_raycast_is_colliding(ray);
            }
            printf("stress: rays %d/%d hit, %d/10 through the gaps\n", hits, rays, gaps);
        }
    } else if (t->frame == 30) {
        /* 30 steps of 2 m/s at 1/60 s from x = -1, and the other one stopped by the box
         * at x = 0 (half size 0.2) with its radius of 0.2. */
        printf("stress: walkers at %.2f and %.2f\n", (double)t->free_walker->local.m[9],
               (double)t->blocked_walker->local.m[9]);
        fflush(stdout);
    }
}

static void trigger_stress_on_entered(Node *self, TriggerStress *t, Node *area)
{
    (void)self;
    (void)area;
    t->entered++;
}

static void trigger_stress_on_exited(Node *self, TriggerStress *t, Node *area)
{
    (void)self;
    (void)area;
    t->exited++;
}

COMPONENT_BEGIN(TriggerStress, "TriggerStress")
    EXPORT_NODE(row)
    EXPORT_NODE(free_walker)
    EXPORT_NODE(blocked_walker)
    ON_UPDATE(trigger_stress_update)
    ON_SIGNAL_NODE("_on_area_entered", trigger_stress_on_entered)
    ON_SIGNAL_NODE("_on_area_exited", trigger_stress_on_exited)
COMPONENT_END()

/* --- UiTest: prints the signals of the controls in scenes/tests/ui_test.tscn ----------- */

typedef struct {
    Node *locked; /* exported: the disabled button */
    Node *engine; /* exported: a sound that loops on the SFX bus, to hear the controls work */
} UiTest;

static void ui_test_on_plain(Node *self, UiTest *t)
{
    (void)self;
    (void)t;
    printf("ui: pressed Plain\n");
}

static void ui_test_on_picture(Node *self, UiTest *t)
{
    (void)self;
    (void)t;
    printf("ui: pressed Picture\n");
}

static void ui_test_on_unlock(Node *self, UiTest *t)
{
    (void)self;
    rr_button_set_disabled(t->locked, false);
    printf("ui: pressed Unlock\n");
}

/* Only reachable once it was enabled; ends the test. */
static void ui_test_on_locked(Node *self, UiTest *t)
{
    (void)self;
    (void)t;
    printf("ui: pressed Locked\n");
    rr_quit();
}

static void ui_test_on_toggled(Node *self, UiTest *t, Node *button)
{
    printf("ui: toggled %s %s\n", button->name, rr_button_is_pressed(button) ? "on" : "off");
    if (strcmp(button->name, "Check") == 0) {
        /* The box mutes the bus of the sound, the switch plays it faster when it is off. */
        rr_audio_bus_set_mute(self->scene, rr_audio_bus_index(self->scene, "SFX"),
                              rr_button_is_pressed(button));
    } else {
        rr_audio_set_pitch_scale(t->engine, rr_button_is_pressed(button) ? 1.0f : 1.5f);
    }
}

/* The slider is the volume of the sound's bus. */
static void ui_test_on_value_changed(Node *self, UiTest *t, Node *slider)
{
    (void)t;
    printf("ui: value %s %.2f\n", slider->name, (double)slider->value);
    rr_audio_bus_set_volume(self->scene, rr_audio_bus_index(self->scene, "SFX"), slider->value / 10.0f);
}

COMPONENT_BEGIN(UiTest, "UiTest")
    EXPORT_NODE(locked)
    EXPORT_NODE(engine)
    ON_SIGNAL("_on_plain", ui_test_on_plain)
    ON_SIGNAL("_on_picture", ui_test_on_picture)
    ON_SIGNAL("_on_unlock", ui_test_on_unlock)
    ON_SIGNAL("_on_locked", ui_test_on_locked)
    ON_SIGNAL_NODE("_on_toggled", ui_test_on_toggled)
    ON_SIGNAL_NODE("_on_value_changed", ui_test_on_value_changed)
COMPONENT_END()

/* --- TileTest: one-way platforms in scenes/tests/tile_test.tscn ------------------------ */

typedef struct {
    Node *jumper, *roller; /* exported: up through the one-way tiles, then down onto them */
    Node *faller, *riser;  /* exported: onto the one-way ledge from above and from below */
    Node *sider;           /* exported: sideways through the one-way tiles */

    int   frame;
    float fall[4]; /* downward speed of the first four, pixels per second */
} TileTest;

static void tile_test_update(Node *self, TileTest *t, float dt)
{
    Node *const bodies[4] = { t->jumper, t->roller, t->faller, t->riser };
    float       walk[3] = { 200.0f, 0.0f, 0.0f };
    int         i;

    (void)self;
    t->frame++;
    for (i = 0; i < 4; i++) {
        float velocity[3] = { 0.0f, 0.0f, 0.0f };
        /* The ones that start below are thrown up for a third of a second; after that
         * everything falls. Down is +Y in 2D. */
        if (i != 2 && t->frame <= 20) {
            t->fall[i] = -400.0f;
        } else {
            t->fall[i] += 900.0f * dt;
        }
        velocity[1] = t->fall[i];
        rr_body_move_and_slide(bodies[i], velocity, dt);
        if (rr_body_is_on_floor(bodies[i])) {
            t->fall[i] = 0.0f;
        }
    }
    if (t->frame <= 60) {
        rr_body_move_and_slide(t->sider, walk, dt);
    }
    if (t->frame == 120) {
        printf("tiles: jumper %.1f roller %.1f faller %.1f riser %.1f sider %.1f\n",
               (double)t->jumper->local.m[10], (double)t->roller->local.m[10],
               (double)t->faller->local.m[10], (double)t->riser->local.m[10],
               (double)t->sider->local.m[9]);
        fflush(stdout);
    }
}

COMPONENT_BEGIN(TileTest, "TileTest")
    EXPORT_NODE(jumper)
    EXPORT_NODE(roller)
    EXPORT_NODE(faller)
    EXPORT_NODE(riser)
    EXPORT_NODE(sider)
    ON_UPDATE(tile_test_update)
COMPONENT_END()

/* --- CanvasTest: the 2D view in scenes/tests/canvas_test.tscn -------------------------- */

typedef struct {
    Node *target;              /* exported: moves; the Camera2D is its child */
    Node *far, *strip, *bound; /* exported: Parallax2D nodes */

    int frame;
} CanvasTest;

/* The same as tools/canvas_reference.gd does in Godot: the two lines must be equal. */
static void canvas_test_update(Node *self, CanvasTest *t, float dt)
{
    const RRScene *scene = self->scene;

    if (t->frame == 150) {
        /* What the previous frame left: the view after 150 frames. */
        printf("canvas: center %.1f %.1f far %.1f %.1f strip %.1f %.1f bound %.1f %.1f\n",
               (double)(scene->screen_offset[0] + (float)RR_SCREEN_WIDTH * 0.5f),
               (double)(scene->screen_offset[1] + (float)RR_SCREEN_HEIGHT * 0.5f),
               (double)t->far->local.m[9], (double)t->far->local.m[10], (double)t->strip->local.m[9],
               (double)t->strip->local.m[10], (double)t->bound->local.m[9],
               (double)t->bound->local.m[10]);
        fflush(stdout);
    }
    t->frame++;
    if (t->frame <= 90) {
        t->target->local.m[9]  += 240.0f * dt;
        t->target->local.m[10] += 60.0f * dt;
    }
}

COMPONENT_BEGIN(CanvasTest, "CanvasTest")
    EXPORT_NODE(target)
    EXPORT_NODE(far)
    EXPORT_NODE(strip)
    EXPORT_NODE(bound)
    ON_UPDATE(canvas_test_update)
COMPONENT_END()

/* --- ScrollTest: the ScrollContainer in scenes/tests/scroll_test.tscn ------------------ */

typedef struct {
    Node *list; /* exported: the ScrollContainer */

    int frame;
} ScrollTest;

/* The scripted input moves the focus down to the last item by frame 100 and up again by
 * five by frame 160. tools/scroll_reference.gd does the same in Godot. */
static void scroll_test_update(Node *self, ScrollTest *t, float dt)
{
    static float bottom;
    float        h, v;

    (void)self;
    (void)dt;
    t->frame++;
    rr_scroll_get(t->list, &h, &v);
    if (t->frame == 100) {
        bottom = v;
    } else if (t->frame == 160) {
        printf("scroll: bottom %d back %d\n", (int)(bottom + 0.5f), (int)(v + 0.5f));
        fflush(stdout);
    }
}

COMPONENT_BEGIN(ScrollTest, "ScrollTest")
    EXPORT_NODE(list)
    ON_UPDATE(scroll_test_update)
COMPONENT_END()

/* --- AnimTest: speed and blending in scenes/tests/anim_test.tscn ----------------------- */

typedef struct {
    Node *mover; /* exported: what the clips move */
    Node *anim;  /* exported: their player */

    int frame;
} AnimTest;

/* The same as tools/anim_reference.gd does in Godot. `frame` counts the frames that are
 * done, as that script does. */
static void anim_test_update(Node *self, AnimTest *t, float dt)
{
    const float *m = t->mover->local.m;

    (void)self;
    (void)dt;
    if (t->frame == 10 || t->frame == 26 || t->frame == 36 || t->frame == 70) {
        const float length = sqrtf(m[6] * m[6] + m[7] * m[7] + m[8] * m[8]);
        printf("anim: frame %d at %.2f %.2f %.2f facing %.2f %.2f %.2f\n", t->frame, (double)m[9],
               (double)m[10], (double)m[11], (double)(m[6] / length), (double)(m[7] / length),
               (double)(m[8] / length));
        fflush(stdout);
    }
    if (t->frame == 20) {
        rr_anim_play(t->anim, "b");
    }
    t->frame++;
}

COMPONENT_BEGIN(AnimTest, "AnimTest")
    EXPORT_NODE(mover)
    EXPORT_NODE(anim)
    ON_UPDATE(anim_test_update)
COMPONENT_END()

/* --- GridTest: a GridMap's floor and wall in scenes/tests/grid_test.tscn --------------- */

typedef struct {
    Node *walker; /* exported */

    int   frame;
    float fall; /* upward speed */
} GridTest;

/* The same as tools/grid_reference.gd does in Godot with its own physics. */
static void grid_test_update(Node *self, GridTest *t, float dt)
{
    float velocity[3];

    (void)self;
    if (t->frame == 480) {
        printf("grid: walker at %.1f %.1f %.1f\n", (double)t->walker->local.m[9],
               (double)t->walker->local.m[10], (double)t->walker->local.m[11]);
        fflush(stdout);
    }
    t->frame++;
    t->fall -= 9.8f * dt;
    velocity[0] = 3.0f;
    velocity[1] = t->fall;
    velocity[2] = 0.0f;
    rr_body_move_and_slide(t->walker, velocity, dt);
    if (rr_body_is_on_floor(t->walker)) {
        t->fall = 0.0f;
    }
}

COMPONENT_BEGIN(GridTest, "GridTest")
    EXPORT_NODE(walker)
    ON_UPDATE(grid_test_update)
COMPONENT_END()

/* --- CastTest: the shape casts in scenes/tests/cast_test.tscn -------------------------- */

typedef struct {
    Node *sphere_cast, *box_cast, *capsule_cast, *miss_cast, *circle_cast; /* exported */

    int frame;
} CastTest;

/* The same lines tools/cast_reference.gd prints from Godot's physics. */
static void cast_test_update(Node *self, CastTest *t, float dt)
{
    Node *const casts[5] = { t->sphere_cast, t->box_cast, t->capsule_cast, t->miss_cast, t->circle_cast };
    int         i;

    (void)self;
    (void)dt;
    if (++t->frame != 3) {
        return;
    }
    for (i = 0; i < 5; i++) {
        const Node *hit = rr_raycast_get_collider(casts[i]);
        float       normal[3];
        rr_raycast_get_collision_normal(casts[i], normal);
        /* The sum with 0 turns a negative zero into a plain one. */
        printf("cast: %s hits %s at %.1f to %.1f normal %.1f %.1f %.1f\n", casts[i]->name,
               hit != NULL ? hit->name : "nothing", (double)rr_shapecast_get_safe_fraction(casts[i]),
               (double)rr_shapecast_get_unsafe_fraction(casts[i]), (double)(normal[0] + 0.0f),
               (double)(normal[1] + 0.0f), (double)(normal[2] + 0.0f));
    }
    fflush(stdout);
}

COMPONENT_BEGIN(CastTest, "CastTest")
    EXPORT_NODE(sphere_cast)
    EXPORT_NODE(box_cast)
    EXPORT_NODE(capsule_cast)
    EXPORT_NODE(miss_cast)
    EXPORT_NODE(circle_cast)
    ON_UPDATE(cast_test_update)
COMPONENT_END()

/* --- SkinTest: a mesh that two bones bend, in scenes/tests/skin_test.tscn -------------- */

typedef struct {
    Node *tube; /* exported: the mesh with the skin */

    int frame;
} SkinTest;

/* Where the vertex of a posed mesh is that is nearest to `probe` in the mesh at rest. */
static void skin_test_vertex(const RRPkgMesh *rest, const RRPkgMesh *posed, const float *probe, float *out)
{
    const uint32_t position = rr_vertex_position_offset(rr_plat_id());
    float          best = 1.0e30f;
    uint32_t       s, v;
    int            a;

    for (s = 0; s < rest->surface_count; s++) {
        const uint8_t *from = (const uint8_t *)rr_surface_verts(rest, &rest->surfaces[s]) + position;
        const uint8_t *to = (const uint8_t *)rr_surface_verts(posed, &posed->surfaces[s]) + position;
        for (v = 0; v < rest->surfaces[s].vertex_count; v++, from += RR_VERTEX_SIZE, to += RR_VERTEX_SIZE) {
            const float *p = (const float *)(const void *)from, *q = (const float *)(const void *)to;
            float        d = 0.0f;
            for (a = 0; a < 3; a++) {
                d += (p[a] - probe[a]) * (p[a] - probe[a]);
            }
            if (d < best) {
                best = d;
                for (a = 0; a < 3; a++) {
                    out[a] = q[a];
                }
            }
        }
    }
}

/* The same line tools/skin_reference.gd works out from the pose of Godot's skeleton. */
static void skin_test_update(Node *self, SkinTest *t, float dt)
{
    static const float probes[2][3] = { { 0.3f, 2.0f, 0.0f }, { 0.3f, 1.0f, 0.0f } };
    const RRPkgMesh   *rest = rr_package_mesh(self->scene->pkg, ((const RRPkgMeshNode *)t->tube->data)->mesh);
    const RRPkgMesh   *posed = rr_skin_mesh(self->scene, t->tube);
    float              at[2][3];
    int                i;

    (void)dt;
    if (++t->frame != 80 || posed == NULL) {
        return;
    }
    for (i = 0; i < 2; i++) {
        skin_test_vertex(rest, posed, probes[i], at[i]);
    }
    /* The sum with 0 turns a negative zero into a plain one. */
    printf("skin: %.2f %.2f %.2f %.2f %.2f %.2f\n", (double)(at[0][0] + 0.0f), (double)(at[0][1] + 0.0f),
           (double)(at[0][2] + 0.0f), (double)(at[1][0] + 0.0f), (double)(at[1][1] + 0.0f),
           (double)(at[1][2] + 0.0f));
    fflush(stdout);
}

COMPONENT_BEGIN(SkinTest, "SkinTest")
    EXPORT_NODE(tube)
    ON_UPDATE(skin_test_update)
COMPONENT_END()
