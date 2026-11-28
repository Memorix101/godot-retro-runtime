/* SpelunkyTitle: Intro story + interactive Title Cave + Highscores + Pause Menu. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "spelunky.h"

typedef struct {
    const char          *level_scene;
    const char          *menu_scene;
    Node                *world_node;
    Node                *hud_node;
    Node                *audio_node;
    const RRPackedScene *slot_16a_scene;
    int32_t              slot_16a_scene_max;
    const RRPackedScene *slot_16b_scene;
    int32_t              slot_16b_scene_max;
    const RRPackedScene *slot_32a_scene;
    int32_t              slot_32a_scene_max;
    const RRPackedScene *slot_32b_scene;
    int32_t              slot_32b_scene_max;
    const RRPackedScene *slot_64b_scene;
    int32_t              slot_64b_scene_max;

    /* Looked-up HUD & world nodes */
    Node *camera;
    Node *title_art;
    Node *status_label;
    Node *prompt_label;
    Node *intro_panel;
    Node *intro_line1;
    Node *intro_line2;
    Node *intro_line3;
    Node *highscore_panel;
    Node *highscore_label;
    Node *pause_panel;
    Node *btn_resume;
    Node *btn_close_scores;
    Node *mus_title;

    /* Input actions */
    int act_left, act_right, act_up, act_down;
    int act_jump, act_whip, act_bomb, act_rope, act_run, act_pay, act_pause, act_accept;

    SpWorld world;
    float   accum;
    int     intro_stage;
    int     intro_timer;
    bool    showing_scores;
    bool    paused;
    char    hs_buf[384];
    char    status_buf[48];
} SpelunkyTitle;

static const char *const g_intro1[8] = {
    "AS THE MOON BURNED BRIGHT ABOVE,",
    "WITH THE DESERT STRETCHING BEHIND ME,",
    "AFTER I DOUBLE-CHECKED MY MAP,",
    "MY LIPS CRACKED AND COVERED IN SAND,",
    "WITH FATE GUIDING MY EVERY MOVE,",
    "PUTTING THE FADED PHOTO IN MY POCKET,",
    "AS I RECALLED MY FATHER'S LAST WORDS,",
    "MY MEMORY SLIPPING AWAY FROM ME,"
};
static const char *const g_intro2[8] = {
    "I STRODE VALIANTLY TOWARD MY DESTINY,",
    "I SQUEEZED THE WHIP AT MY SIDE,",
    "I DRAINED THE REST OF MY CANTEEN,",
    "I SPOTTED THE CAVE'S ENTRANCE,",
    "I FURROWED MY BROW,",
    "I PAID MY BEDOUIN GUIDE,",
    "I DISMOUNTED MY CAMEL,",
    "I SQUINTED INTO THE DARKNESS,"
};
static const char *const g_intro3[8] = {
    "AND THOUGHT OF HER ONE LAST TIME.",
    "AND HELD MY HAT AGAINST THE WIND.",
    "AND A COLD CHILL TOOK HOLD OF ME.",
    "AND WONDERED HOW LONG I'D BE BELOW.",
    "AND VOWED TO RETURN VICTORIOUS.",
    "AND SWORE I HEARD VOICES UP AHEAD.",
    "AND FELT THE GODS SMILING UPON ME.",
    "AND THAT'S WHEN IT ALL STARTED."
};

static void set_t(SpWorld *w, int tx, int ty, uint8_t fg, uint16_t fg_anim, uint8_t flags)
{
    w->grid[ty][tx].fg = fg;
    w->grid[ty][tx].fg_anim = fg_anim;
    w->grid[ty][tx].flags = flags;
}

static void set_b(SpWorld *w, int tx, int ty, uint8_t bg, uint16_t bg_anim, bool climb, bool plat)
{
    w->grid[ty][tx].bg = bg;
    w->grid[ty][tx].bg_anim = bg_anim;
    if (climb) w->grid[ty][tx].flags |= TF_CLIMBABLE;
    if (plat) w->grid[ty][tx].flags |= TF_PLATFORM;
}

static void build_title_cave(SpWorld *w)
{
    int tx, ty;
    sp_world_clear(w, w->scene);
    w->cols = 40; /* 2 screens of 20 tiles (640 x 240 world) */
    w->rows = 15;

    for (tx = 0; tx < w->cols; tx++) {
        set_t(w, tx, 0, TILE_ROCK, SP_ANIM_BLOCK, TF_SOLID | TF_INVINCIBLE);
        set_t(w, tx, 13, TILE_BRICK, SP_ANIM_CAVE_UP, TF_SOLID | TF_INVINCIBLE);
        set_t(w, tx, 14, TILE_BRICK, SP_ANIM_BRICK, TF_SOLID | TF_INVINCIBLE);
    }
    for (ty = 0; ty < w->rows; ty++) {
        set_t(w, 0, ty, TILE_ROCK, SP_ANIM_BLOCK, TF_SOLID | TF_INVINCIBLE);
        set_t(w, 39, ty, TILE_ROCK, SP_ANIM_BLOCK, TF_SOLID | TF_INVINCIBLE);
    }

    /* Left room (Shortcut House): Tunnel Man + character switch statue + wall dividing rooms with gap */
    for (ty = 1; ty <= 9; ty++) {
        set_t(w, 20, ty, TILE_BRICK, SP_ANIM_BRICK, TF_SOLID | TF_INVINCIBLE);
    }
    set_b(w, 3, 12, BG_SIGN_GENERAL, SP_ANIM_RESET_SIGN, false, false);
    set_b(w, 12, 12, BG_ENTRANCE, SP_ANIM_CHANGING_DOOR, false, false);

    /* Right room (Title Cave): Tutorial door, Highscores door, Start rope! */
    set_b(w, 23, 12, BG_SIGN_GENERAL, SP_ANIM_TUTORIAL_SIGN, false, false);
    set_b(w, 24, 12, BG_EXIT, SP_ANIM_EXIT, false, false); /* Tutorial door */

    set_b(w, 27, 11, BG_SIGN_GENERAL, SP_ANIM_SCORES_SIGN, false, false);
    set_b(w, 28, 12, BG_GOLD_DOOR, SP_ANIM_GOLD_DOOR, false, false); /* Highscores door */

    set_b(w, 33, 11, BG_SIGN_GENERAL, SP_ANIM_START_SIGN, false, false);
    for (ty = 6; ty <= 12; ty++) {
        set_b(w, 35, ty, ty == 6 ? BG_ROPE_TOP : BG_ROPE, ty == 6 ? SP_ANIM_ROPE_TOP : SP_ANIM_ROPE, true, ty == 6);
    }

    /* Spawn player inside Title Cave (x=320+280 -> tx=37? Let's spawn at tx=32, ty=12) */
    {
        int p = sp_spawn_entity(w, ENT_PLAYER, 32.0f * 16.0f, 12.0f * 16.0f + 8.0f);
        if (p >= 0) {
            w->ents[p].bx = -5;
            w->ents[p].by = -6;
            w->ents[p].bw = 10;
            w->ents[p].bh = 14;
            w->ents[p].z = SP_Z_PLAYER;
            w->ents[p].facing = -1;
            w->ents[p].hp = 4;
            w->player_idx = p;
        }
        w->cam_x = 320.0f;
        w->cam_y = 0.0f;
    }

    /* Decorative flare on the floor */
    {
        int fl = sp_spawn_entity(w, ENT_FLARE, 32.0f * 16.0f + 12.0f, 12.0f * 16.0f + 10.0f);
        if (fl >= 0) {
            w->ents[fl].anim_id = SP_ANIM_FLARE;
            w->ents[fl].bx = -4; w->ents[fl].by = -4; w->ents[fl].bw = 8; w->ents[fl].bh = 8;
        }
    }
}

static void refresh_status(SpelunkyTitle *t)
{
    const char *cname = (g_sp_run.character == SP_CHAR_DAMSEL) ? "DAMSEL"
                      : (g_sp_run.character == SP_CHAR_TUNNEL_MAN) ? "TUNNEL MAN"
                      : "EXPLORER";
    snprintf(t->status_buf, sizeof(t->status_buf), "CHAR: %s", cname);
    rr_node_set_text(t->status_label, t->status_buf);
}

static void show_scores(SpelunkyTitle *t, bool show)
{
    const SpHighscores *s = &g_sp_run.scores;
    int sec = s->best_time / 30;
    t->showing_scores = show;
    sp_set_visible(t->highscore_panel, show);
    if (show) {
        snprintf(t->hs_buf, sizeof(t->hs_buf),
                 "TOP MONEY:   $%d\n"
                 "BEST TIME:   %d:%02d\n"
                 "MOST KILLS:  %d\n"
                 "MOST SAVES:  %d\n\n"
                 "PLAYS: %d   DEATHS: %d   WINS: %d",
                 (int)s->top_money, sec / 60, sec % 60, (int)s->most_kills, (int)s->most_saves,
                 (int)s->plays, (int)s->deaths, (int)s->wins);
        rr_node_set_text(t->highscore_label, t->hs_buf);
        rr_control_grab_focus(t->btn_close_scores);
    }
}

static void spelunky_title_ready(Node *self, SpelunkyTitle *t)
{
    sp_run_init_once();

    t->camera = sp_find_child(t->world_node, "Camera2D");
    t->title_art = sp_find_child(t->world_node, "TitleArt");

    t->status_label = sp_find_child(t->hud_node, "StatusLabel");
    t->prompt_label = sp_find_child(t->hud_node, "PromptLabel");
    t->intro_panel = sp_find_child(t->hud_node, "IntroPanel");
    t->intro_line1 = sp_find_child(t->intro_panel, "Line1");
    t->intro_line2 = sp_find_child(t->intro_panel, "Line2");
    t->intro_line3 = sp_find_child(t->intro_panel, "Line3");
    t->highscore_panel = sp_find_child(t->hud_node, "HighscorePanel");
    t->highscore_label = sp_find_child(t->highscore_panel, "HSBody");
    t->btn_close_scores = sp_find_child(t->highscore_panel, "BtnCloseScores");
    t->pause_panel = sp_find_child(t->hud_node, "PausePanel");
    t->btn_resume = sp_find_child(t->pause_panel, "BtnResume");
    sp_set_visible(t->highscore_panel, false);
    sp_set_visible(t->pause_panel, false);

    t->mus_title = sp_find_child(t->audio_node, "mus_title");
    t->world.sfx_jump = sp_find_child(t->audio_node, "sfx_jump");
    t->world.sfx_whip = sp_find_child(t->audio_node, "sfx_whip");
    t->world.sfx_pickup = sp_find_child(t->audio_node, "sfx_pickup");
    t->world.sfx_ignite = sp_find_child(t->audio_node, "sfx_ignite");
    t->world.sfx_click = sp_find_child(t->audio_node, "sfx_click");
    t->world.sfx_steps = sp_find_child(t->audio_node, "sfx_steps");
    t->world.sfx_land = sp_find_child(t->audio_node, "sfx_land");
    t->world.sfx_climb1 = sp_find_child(t->audio_node, "sfx_climb1");
    t->world.sfx_climb2 = sp_find_child(t->audio_node, "sfx_climb2");
    t->world.sfx_throw = sp_find_child(t->audio_node, "sfx_throw");
    t->world.sfx_bombready = sp_find_child(t->audio_node, "sfx_bombready");
    t->world.sfx_explosion = sp_find_child(t->audio_node, "sfx_explosion");

    t->act_left   = rr_action(self->scene, "sp_left");
    t->act_right  = rr_action(self->scene, "sp_right");
    t->act_up     = rr_action(self->scene, "sp_up");
    t->act_down   = rr_action(self->scene, "sp_down");
    t->act_jump   = rr_action(self->scene, "sp_jump");
    t->act_whip   = rr_action(self->scene, "sp_whip");
    t->act_bomb   = rr_action(self->scene, "sp_bomb");
    t->act_rope   = rr_action(self->scene, "sp_rope");
    t->act_run    = rr_action(self->scene, "sp_run");
    t->act_pay    = rr_action(self->scene, "sp_pay");
    t->act_pause  = rr_action(self->scene, "sp_pause");
    t->act_accept = rr_action(self->scene, "sp_accept");

    sp_pools_setup(&t->world,
                   t->slot_16a_scene, t->slot_16a_scene_max,
                   t->slot_16b_scene, t->slot_16b_scene_max,
                   t->slot_32a_scene, t->slot_32a_scene_max,
                   t->slot_32b_scene, t->slot_32b_scene_max,
                   NULL, 0,
                   t->slot_64b_scene, t->slot_64b_scene_max);

    t->world.scene = self->scene;
    t->world.rng = 0x9E3779B9u ^ (uint32_t)(g_sp_run.scores.plays * 2654435761u + 1u);
    build_title_cave(&t->world);
    refresh_status(t);

    if (g_sp_run.show_intro) {
        t->intro_stage = 0;
        t->intro_timer = 0;
        rr_node_set_text(t->intro_line1, g_intro1[sp_rand_range(&t->world, 0, 7)]);
        rr_node_set_text(t->intro_line2, g_intro2[sp_rand_range(&t->world, 0, 7)]);
        rr_node_set_text(t->intro_line3, g_intro3[sp_rand_range(&t->world, 0, 7)]);
        sp_set_visible(t->intro_panel, true);
        sp_set_visible(t->intro_line1, false);
        sp_set_visible(t->intro_line2, false);
        sp_set_visible(t->intro_line3, false);
    } else {
        t->intro_stage = 4;
        sp_set_visible(t->intro_panel, false);
        if (t->mus_title != NULL) {
            rr_audio_play(t->mus_title);
        }
    }

    if (t->camera != NULL) {
        t->camera->local.m[9] = t->world.cam_x;
        t->camera->local.m[10] = t->world.cam_y;
    }
    rr_camera2d_reset_smoothing(self->scene);
}

static void start_adventure(SpelunkyTitle *t, bool tutorial)
{
    sp_play_sfx(t->world.sfx_click);
    g_sp_run.show_intro = false;
    sp_run_reset_new_game(tutorial);
    rr_change_scene(t->level_scene);
}

static void spelunky_title_update(Node *self, SpelunkyTitle *t, float dt)
{
    (void)self;

    if (t->intro_stage < 4) {
        t->intro_timer++;
        if (t->intro_timer > 18) sp_set_visible(t->intro_line1, true);
        if (t->intro_timer > 54) sp_set_visible(t->intro_line2, true);
        if (t->intro_timer > 90) sp_set_visible(t->intro_line3, true);
        if (t->intro_timer > 220 ||
            rr_action_pressed(t->act_accept) ||
            rr_action_pressed(t->act_jump) ||
            rr_action_pressed(t->act_whip) ||
            rr_action_pressed(t->act_pause)) {
            t->intro_stage = 4;
            g_sp_run.show_intro = false;
            sp_set_visible(t->intro_panel, false);
            if (t->mus_title != NULL) {
                rr_audio_play(t->mus_title);
            }
        }
        return;
    }

    if (t->showing_scores) {
        if (rr_action_pressed(t->act_pause)) {
            show_scores(t, false);
        }
    } else if (rr_action_pressed(t->act_pause)) {
        t->paused = !t->paused;
        sp_set_visible(t->pause_panel, t->paused);
        sp_play_sfx(t->world.sfx_click);
        if (t->paused) {
            rr_control_grab_focus(t->btn_resume);
        }
    }

    if (!t->paused && !t->showing_scores) {
        SpInput in;
        memset(&in, 0, sizeof(in));
        in.left          = rr_action_down(t->act_left);
        in.right         = rr_action_down(t->act_right);
        in.up            = rr_action_down(t->act_up);
        in.down          = rr_action_down(t->act_down);
        in.jump_down     = rr_action_down(t->act_jump);
        in.jump_pressed  = rr_action_pressed(t->act_jump);
        in.jump_released = !in.jump_down;
        in.whip_pressed  = rr_action_pressed(t->act_whip);
        in.whip_down     = rr_action_down(t->act_whip);
        in.run_down      = rr_action_down(t->act_run);
        in.pay_pressed   = rr_action_pressed(t->act_pay);
        in.accept_pressed = rr_action_pressed(t->act_accept);

        t->accum += dt;
        if (t->accum > SP_FIXED_DT * 2.0f) {
            t->accum = SP_FIXED_DT * 2.0f;
        }
        if (t->accum >= SP_FIXED_DT) {
            t->accum -= SP_FIXED_DT;
            sp_step_world(&t->world, &in, false);

            /* Camera smoothly switches between left room (x=0) and right room (x=320) */
            if (t->world.player_idx >= 0) {
                SpEntity *p = &t->world.ents[t->world.player_idx];
                int ptx = (int)floorf(p->x / 16.0f);
                int pty = (int)floorf(p->y / 16.0f);
                float target_cam = (p->x <= 320.0f) ? 0.0f : 320.0f;
                t->world.cam_x += (target_cam - t->world.cam_x) * 0.18f;
                t->world.cam_y = 0.0f;

                /* Interactive Title Cave triggers */
                if (ptx == 35 && pty <= 7) {
                    start_adventure(t, false);
                    return;
                } else if (ptx == 24 && pty == 12) {
                    rr_node_set_text(t->prompt_label, "UP / ENTER: TUTORIAL CAVE");
                    if (in.up || in.accept_pressed) {
                        start_adventure(t, true);
                        return;
                    }
                } else if (ptx == 28 && pty == 12) {
                    rr_node_set_text(t->prompt_label, "UP / ENTER: HIGH SCORES");
                    if (in.up || in.accept_pressed) {
                        show_scores(t, true);
                    }
                } else if (ptx == 12 && pty == 12) {
                    rr_node_set_text(t->prompt_label, "UP / E: SWITCH CHARACTER");
                    if (in.up || in.pay_pressed || in.accept_pressed) {
                        g_sp_run.character = (g_sp_run.character + 1) % 3;
                        refresh_status(t);
                        sp_play_sfx(t->world.sfx_pickup);
                    }
                } else if (ptx == 35 && pty >= 10) {
                    rr_node_set_text(t->prompt_label, "CLIMB ROPE TO START ADVENTURE!");
                    if (in.accept_pressed) {
                        start_adventure(t, false);
                        return;
                    }
                } else {
                    rr_node_set_text(t->prompt_label, "");
                }
            }
        }
    }

    if (t->camera != NULL) {
        t->camera->local.m[9] = t->world.cam_x;
        t->camera->local.m[10] = 0.0f;
    }

    sp_pool_begin_frame(&t->world);
    sp_render_tiles(&t->world);
    sp_render_entities(&t->world);
    sp_pool_end_frame(&t->world);
}

static void on_start_run(Node *self, SpelunkyTitle *t) { (void)self; start_adventure(t, false); }
static void on_tutorial(Node *self, SpelunkyTitle *t)  { (void)self; start_adventure(t, true); }
static void on_resume(Node *self, SpelunkyTitle *t)
{
    (void)self;
    t->paused = false;
    sp_set_visible(t->pause_panel, false);
    sp_play_sfx(t->world.sfx_click);
}
static void on_toggle_char(Node *self, SpelunkyTitle *t)
{
    (void)self;
    g_sp_run.character = (g_sp_run.character + 1) % 3;
    refresh_status(t);
    sp_play_sfx(t->world.sfx_pickup);
}
static void on_back_menu(Node *self, SpelunkyTitle *t)
{
    (void)self;
    rr_change_scene(t->menu_scene);
}
static void on_close_scores(Node *self, SpelunkyTitle *t)
{
    (void)self;
    show_scores(t, false);
    sp_play_sfx(t->world.sfx_click);
}

COMPONENT_BEGIN(SpelunkyTitle, "SpelunkyTitle")
    EXPORT_SCENE(level_scene)
    EXPORT_SCENE(menu_scene)
    EXPORT_NODE(world_node)
    EXPORT_NODE(hud_node)
    EXPORT_NODE(audio_node)
    EXPORT_PACKED_SCENE(slot_16a_scene)
    EXPORT_INT(slot_16a_scene_max)
    EXPORT_PACKED_SCENE(slot_16b_scene)
    EXPORT_INT(slot_16b_scene_max)
    EXPORT_PACKED_SCENE(slot_32a_scene)
    EXPORT_INT(slot_32a_scene_max)
    EXPORT_PACKED_SCENE(slot_32b_scene)
    EXPORT_INT(slot_32b_scene_max)
    EXPORT_PACKED_SCENE(slot_64b_scene)
    EXPORT_INT(slot_64b_scene_max)
    ON_READY(spelunky_title_ready)
    ON_UPDATE(spelunky_title_update)
    ON_SIGNAL("_on_start_run_pressed", on_start_run)
    ON_SIGNAL("_on_tutorial_pressed", on_tutorial)
    ON_SIGNAL("_on_resume_pressed", on_resume)
    ON_SIGNAL("_on_toggle_char_pressed", on_toggle_char)
    ON_SIGNAL("_on_back_menu_pressed", on_back_menu)
    ON_SIGNAL("_on_close_scores_pressed", on_close_scores)
COMPONENT_END()
