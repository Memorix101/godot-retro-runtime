/* SpelunkyLevel: level runner, 30 Hz fixed-timestep simulation, HUD, dark levels, transitions. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "spelunky.h"

typedef struct {
    const char          *title_scene;
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
    const RRPackedScene *slot_64a_scene;
    int32_t              slot_64a_scene_max;
    const RRPackedScene *slot_64b_scene;
    int32_t              slot_64b_scene_max;

    /* World children */
    Node *camera;
    Node *dark_sprite;
    Node *bg_cave;
    Node *bg_lush;
    Node *bg_ice;
    Node *bg_temple;

    /* HUD children */
    Node *life_label;
    Node *bomb_label;
    Node *rope_label;
    Node *money_label;
    Node *collect_label;
    Node *level_label;
    Node *time_label;
    Node *held_label;
    Node *compass_label;
    Node *msg1_label;
    Node *msg2_label;
    Node *bomb_icon;
    Node *sticky_bomb_icon;
    Node *icons[13];
    Node *pause_panel;
    Node *btn_resume;
    Node *summary_panel;
    Node *summary_title;
    Node *summary_body;
    Node *btn_next;
    Node *btn_quit_title;

    /* Music */
    Node *mus_cave;
    Node *mus_lush;
    Node *mus_ice;
    Node *mus_temple;
    Node *mus_boss;
    Node *mus_credits;

    /* Actions */
    int act_left, act_right, act_up, act_down;
    int act_jump, act_whip, act_bomb, act_rope, act_run, act_pay, act_pause, act_accept;

    SpWorld world;
    float   accum;
    bool    paused;
    bool    in_summary;
    bool    summary_is_death;
    bool    summary_is_win;

    /* Edge-triggered input latches between render frames and 30Hz ticks */
    bool lat_jump_p, lat_jump_r, lat_whip_p, lat_bomb_p, lat_rope_p, lat_pay_p, lat_accept_p;

    char life_buf[16];
    char bomb_buf[16];
    char rope_buf[16];
    char money_buf[24];
    char collect_buf[24];
    char level_buf[16];
    char time_buf[16];
    char held_buf[24];
    char compass_buf[16];
    char sum_body_buf[384];
} SpelunkyLevel;

static const char *held_kind_name(uint8_t k)
{
    switch (k) {
    case ENT_ROCK:          return "ROCK";
    case ENT_JAR:           return "JAR";
    case ENT_SKULL:         return "SKULL";
    case ENT_KEY:           return "KEY";
    case ENT_GOLD_IDOL:     return "IDOL";
    case ENT_CRYSTAL_SKULL: return "CRYSTAL";
    case ENT_MACHETE:       return "MACHETE";
    case ENT_MATTOCK:       return "MATTOCK";
    case ENT_PISTOL:        return "PISTOL";
    case ENT_SHOTGUN:       return "SHOTGUN";
    case ENT_BOW:           return "BOW";
    case ENT_WEB_CANNON:    return "WEBCAN";
    case ENT_TELEPORTER:    return "TELEPORT";
    case ENT_SCEPTRE:       return "SCEPTRE";
    case ENT_FLARE:         return "FLARE";
    case ENT_DICE:          return "DICE";
    case ENT_DAMSEL:        return "DAMSEL";
    default:                return "";
    }
}

static void start_area_music(SpelunkyLevel *lv)
{
    Node *m = lv->mus_cave;
    if (g_sp_run.level_type == SP_AREA_LUSH) m = lv->mus_lush;
    else if (g_sp_run.level_type == SP_AREA_ICE) m = lv->mus_ice;
    else if (g_sp_run.level_type == SP_AREA_TEMPLE) m = lv->mus_temple;
    else if (g_sp_run.level_type == SP_AREA_OLMEC) m = lv->mus_boss;
    if (m != NULL) {
        rr_audio_play(m);
    }
}

static void refresh_area_bg(SpelunkyLevel *lv)
{
    int area = g_sp_run.level_type;
    sp_set_visible(lv->bg_cave, area == SP_AREA_MINES || area == SP_AREA_TUTORIAL);
    sp_set_visible(lv->bg_lush, area == SP_AREA_LUSH);
    sp_set_visible(lv->bg_ice, area == SP_AREA_ICE);
    sp_set_visible(lv->bg_temple, area == SP_AREA_TEMPLE || area == SP_AREA_OLMEC);
}

static void start_current_level(SpelunkyLevel *lv)
{
    lv->in_summary = false;
    lv->paused = false;
    sp_set_visible(lv->pause_panel, false);
    sp_set_visible(lv->summary_panel, false);

    sp_generate_level(&lv->world);
    refresh_area_bg(lv);
    start_area_music(lv);
    if (g_sp_run.curr_level == 1 && !g_sp_run.is_tutorial) {
        sp_play_sfx(lv->world.sfx_letsexplore);
    }

    if (lv->camera != NULL) {
        lv->camera->local.m[9] = lv->world.cam_x;
        lv->camera->local.m[10] = lv->world.cam_y;
    }
    rr_camera2d_reset_smoothing(lv->world.scene);
}

static void open_summary(SpelunkyLevel *lv, bool is_death, bool is_win)
{
    lv->in_summary = true;
    lv->summary_is_death = is_death;
    lv->summary_is_win = is_win;
    sp_set_visible(lv->summary_panel, true);

    if (is_win) {
        int sec = g_sp_run.time_ticks / 30;
        sp_record_run_end(true);
        rr_node_set_text(lv->summary_title, "VICTORY! ESCAPED OLMEC!");
        rr_node_set_text(lv->btn_next, "Play Again");
        snprintf(lv->sum_body_buf, sizeof(lv->sum_body_buf),
                 "YOU DEFEATED OLMEC AND ESCAPED!\n\n"
                 "TOTAL MONEY:    $%d\n"
                 "TOTAL TIME:     %d:%02d\n"
                 "ENEMIES KILLED: %d\n"
                 "DAMSELS SAVED:  %d",
                 (int)g_sp_run.money, sec / 60, sec % 60, (int)g_sp_run.kills, (int)g_sp_run.damsels);
        if (lv->mus_credits != NULL) {
            rr_audio_play(lv->mus_credits);
        }
    } else if (is_death) {
        int sec = g_sp_run.time_ticks / 30;
        sp_record_run_end(false);
        rr_node_set_text(lv->summary_title, "GAME OVER");
        rr_node_set_text(lv->btn_next, "Try Again");
        snprintf(lv->sum_body_buf, sizeof(lv->sum_body_buf),
                 "%s\n\n"
                 "REACHED LEVEL:  %d-%d\n"
                 "TOTAL MONEY:    $%d\n"
                 "TOTAL TIME:     %d:%02d\n"
                 "KILLS: %d   SAVES: %d",
                 g_sp_run.death_cause,
                 (int)((g_sp_run.curr_level - 1) / 4 + 1), (int)((g_sp_run.curr_level - 1) % 4 + 1),
                 (int)g_sp_run.money, sec / 60, sec % 60, (int)g_sp_run.kills, (int)g_sp_run.damsels);
    } else {
        int lsec = g_sp_run.xtime_ticks / 30;
        int tsec = g_sp_run.time_ticks / 30;
        if (g_sp_run.is_tutorial) {
            rr_node_set_text(lv->summary_title, "TUTORIAL COMPLETED!");
            rr_node_set_text(lv->btn_next, "Start 1-1");
        } else {
            rr_node_set_text(lv->summary_title, "LEVEL COMPLETED!");
            rr_node_set_text(lv->btn_next, "Next Level");
        }
        snprintf(lv->sum_body_buf, sizeof(lv->sum_body_buf),
                 "LEVEL LOOT:     +$%d\n"
                 "TOTAL MONEY:    $%d\n"
                 "LEVEL TIME:     %d:%02d  (TOTAL %d:%02d)\n"
                 "DAMSELS SAVED:  %d\n"
                 "KILLS:          %d",
                 (int)g_sp_run.xmoney, (int)g_sp_run.money,
                 lsec / 60, lsec % 60, tsec / 60, tsec % 60,
                 (int)g_sp_run.damsels, (int)g_sp_run.kills);
    }
    rr_node_set_text(lv->summary_body, lv->sum_body_buf);
    rr_control_grab_focus(lv->btn_next);
}

static void update_hud(SpelunkyLevel *lv)
{
    static const uint32_t pass_bits[13] = {
        PASS_UDJAT, PASS_ANKH, PASS_CROWN, PASS_KAPALA, PASS_SPECTACLES, PASS_GLOVES,
        PASS_MITT, PASS_SPRING, PASS_SPIKE, PASS_CAPE, PASS_JETPACK, PASS_COMPASS, PASS_PARACHUTE
    };
    int i;
    int sec = g_sp_run.time_ticks / 30;
    int life = g_sp_run.plife < 0 ? 0 : g_sp_run.plife;

    snprintf(lv->life_buf, sizeof(lv->life_buf), "%d", life);
    snprintf(lv->bomb_buf, sizeof(lv->bomb_buf), "%d", (int)g_sp_run.bombs);
    snprintf(lv->rope_buf, sizeof(lv->rope_buf), "%d", (int)g_sp_run.rope);
    snprintf(lv->money_buf, sizeof(lv->money_buf), "%d", (int)g_sp_run.money);
    if (lv->world.collect_popup > 0) {
        snprintf(lv->collect_buf, sizeof(lv->collect_buf), "+%d", (int)lv->world.collect_popup);
    } else {
        lv->collect_buf[0] = '\0';
    }
    if (g_sp_run.is_tutorial || g_sp_run.curr_level == 0) {
        snprintf(lv->level_buf, sizeof(lv->level_buf), "TUT");
    } else {
        snprintf(lv->level_buf, sizeof(lv->level_buf), "%d-%d",
                 (int)((g_sp_run.curr_level - 1) / 4 + 1), (int)((g_sp_run.curr_level - 1) % 4 + 1));
    }
    snprintf(lv->time_buf, sizeof(lv->time_buf), "%d:%02d", sec / 60, sec % 60);
    snprintf(lv->held_buf, sizeof(lv->held_buf), "%s", held_kind_name(g_sp_run.held_item_kind));

    rr_node_set_text(lv->life_label, lv->life_buf);
    rr_node_set_text(lv->bomb_label, lv->bomb_buf);
    rr_node_set_text(lv->rope_label, lv->rope_buf);
    rr_node_set_text(lv->money_label, lv->money_buf);
    rr_node_set_text(lv->collect_label, lv->collect_buf);
    rr_node_set_text(lv->level_label, lv->level_buf);
    rr_node_set_text(lv->time_label, lv->time_buf);
    rr_node_set_text(lv->held_label, lv->held_buf);

    sp_set_visible(lv->bomb_icon, (g_sp_run.passives & PASS_PASTE) == 0);
    sp_set_visible(lv->sticky_bomb_icon, (g_sp_run.passives & PASS_PASTE) != 0);

    for (i = 0; i < 13; i++) {
        sp_set_visible(lv->icons[i], (g_sp_run.passives & pass_bits[i]) != 0);
    }

    if ((g_sp_run.passives & PASS_COMPASS) && lv->world.exit_x > 0) {
        float ex = (float)lv->world.exit_x * 16.0f;
        float ey = (float)lv->world.exit_y * 16.0f;
        const char *dir = (ey > lv->world.cam_y + 240.0f)
            ? (ex < lv->world.cam_x ? "EXIT: DL" : (ex > lv->world.cam_x + 320.0f ? "EXIT: DR" : "EXIT: DOWN"))
            : (ex < lv->world.cam_x ? "EXIT: L" : (ex > lv->world.cam_x + 320.0f ? "EXIT: R" : "EXIT: NEAR"));
        snprintf(lv->compass_buf, sizeof(lv->compass_buf), "%s", dir);
    } else {
        lv->compass_buf[0] = '\0';
    }
    rr_node_set_text(lv->compass_label, lv->compass_buf);

    if (lv->world.msg_timer > 0) {
        rr_node_set_text(lv->msg1_label, lv->world.msg1);
        rr_node_set_text(lv->msg2_label, lv->world.msg2);
    } else {
        rr_node_set_text(lv->msg1_label, "");
        rr_node_set_text(lv->msg2_label, "");
    }

    /* Dark level dim overlay */
    if (lv->dark_sprite != NULL) {
        sp_set_visible(lv->dark_sprite, g_sp_run.dark_level);
        if (g_sp_run.dark_level) {
            lv->dark_sprite->local.m[9] = lv->world.cam_x;
            lv->dark_sprite->local.m[10] = lv->world.cam_y;
            lv->dark_sprite->modulate = 0xFFFFFFB8u;
        }
    }
}

static void spelunky_level_ready(Node *self, SpelunkyLevel *lv)
{
    static const char *const icon_names[13] = {
        "Udjat", "Ankh", "Crown", "Kapala", "Spectacles", "Gloves",
        "Mitt", "Spring", "Spike", "Cape", "Jetpack", "Compass", "Parachute"
    };
    Node *icons_node;
    int i;

    sp_run_init_once();
    if (!g_sp_run.in_run) {
        sp_run_reset_new_game(false);
    }

    lv->camera = sp_find_child(lv->world_node, "Camera2D");
    lv->dark_sprite = sp_find_child(lv->world_node, "DarkSprite");
    lv->bg_cave = sp_find_child(lv->world_node, "BgCave");
    lv->bg_lush = sp_find_child(lv->world_node, "BgLush");
    lv->bg_ice = sp_find_child(lv->world_node, "BgIce");
    lv->bg_temple = sp_find_child(lv->world_node, "BgTemple");

    lv->life_label = sp_find_child(lv->hud_node, "LifeLabel");
    lv->bomb_label = sp_find_child(lv->hud_node, "BombLabel");
    lv->rope_label = sp_find_child(lv->hud_node, "RopeLabel");
    lv->money_label = sp_find_child(lv->hud_node, "MoneyLabel");
    lv->collect_label = sp_find_child(lv->hud_node, "CollectLabel");
    lv->level_label = sp_find_child(lv->hud_node, "LevelLabel");
    lv->time_label = sp_find_child(lv->hud_node, "TimeLabel");
    lv->held_label = sp_find_child(lv->hud_node, "HeldLabel");
    lv->compass_label = sp_find_child(lv->hud_node, "CompassLabel");
    lv->msg1_label = sp_find_child(lv->hud_node, "MsgLine1");
    lv->msg2_label = sp_find_child(lv->hud_node, "MsgLine2");
    lv->bomb_icon = sp_find_child(lv->hud_node, "BombIcon");
    lv->sticky_bomb_icon = sp_find_child(lv->hud_node, "StickyBombIcon");

    icons_node = sp_find_child(lv->hud_node, "Icons");
    for (i = 0; i < 13; i++) {
        lv->icons[i] = sp_find_child(icons_node, icon_names[i]);
    }

    lv->pause_panel = sp_find_child(lv->hud_node, "PausePanel");
    lv->btn_resume = sp_find_child(lv->pause_panel, "BtnResume");
    lv->summary_panel = sp_find_child(lv->hud_node, "SummaryPanel");
    lv->summary_title = sp_find_child(lv->summary_panel, "SumTitle");
    lv->summary_body = sp_find_child(lv->summary_panel, "SumBody");
    lv->btn_next = sp_find_child(lv->summary_panel, "BtnNext");
    lv->btn_quit_title = sp_find_child(lv->summary_panel, "BtnQuitTitle");

    lv->mus_cave = sp_find_child(lv->audio_node, "mus_cave");
    lv->mus_lush = sp_find_child(lv->audio_node, "mus_lush");
    lv->mus_ice = sp_find_child(lv->audio_node, "mus_ice");
    lv->mus_temple = sp_find_child(lv->audio_node, "mus_temple");
    lv->mus_boss = sp_find_child(lv->audio_node, "mus_boss");
    lv->mus_credits = sp_find_child(lv->audio_node, "mus_credits");

    lv->world.sfx_jump = sp_find_child(lv->audio_node, "sfx_jump");
    lv->world.sfx_bigjump = sp_find_child(lv->audio_node, "sfx_bigjump");
    lv->world.sfx_whip = sp_find_child(lv->audio_node, "sfx_whip");
    lv->world.sfx_throw = sp_find_child(lv->audio_node, "sfx_throw");
    lv->world.sfx_pickup = sp_find_child(lv->audio_node, "sfx_pickup");
    lv->world.sfx_coin = sp_find_child(lv->audio_node, "sfx_coin");
    lv->world.sfx_gem = sp_find_child(lv->audio_node, "sfx_gem");
    lv->world.sfx_hurt = sp_find_child(lv->audio_node, "sfx_hurt");
    lv->world.sfx_die = sp_find_child(lv->audio_node, "sfx_die");
    lv->world.sfx_hit = sp_find_child(lv->audio_node, "sfx_hit");
    lv->world.sfx_explosion = sp_find_child(lv->audio_node, "sfx_explosion");
    lv->world.sfx_smallexplode = sp_find_child(lv->audio_node, "sfx_smallexplode");
    lv->world.sfx_break = sp_find_child(lv->audio_node, "sfx_break");
    lv->world.sfx_land = sp_find_child(lv->audio_node, "sfx_land");
    lv->world.sfx_climb1 = sp_find_child(lv->audio_node, "sfx_climb1");
    lv->world.sfx_climb2 = sp_find_child(lv->audio_node, "sfx_climb2");
    lv->world.sfx_steps = sp_find_child(lv->audio_node, "sfx_steps");
    lv->world.sfx_splash = sp_find_child(lv->audio_node, "sfx_splash");
    lv->world.sfx_arrowtrap = sp_find_child(lv->audio_node, "sfx_arrowtrap");
    lv->world.sfx_shotgun = sp_find_child(lv->audio_node, "sfx_shotgun");
    lv->world.sfx_bowpull = sp_find_child(lv->audio_node, "sfx_bowpull");
    lv->world.sfx_bat = sp_find_child(lv->audio_node, "sfx_bat");
    lv->world.sfx_frog = sp_find_child(lv->audio_node, "sfx_frog");
    lv->world.sfx_ghost = sp_find_child(lv->audio_node, "sfx_ghost");
    lv->world.sfx_giantspider = sp_find_child(lv->audio_node, "sfx_giantspider");
    lv->world.sfx_spiderjump = sp_find_child(lv->audio_node, "sfx_spiderjump");
    lv->world.sfx_alien = sp_find_child(lv->audio_node, "sfx_alien");
    lv->world.sfx_laser = sp_find_child(lv->audio_node, "sfx_laser");
    lv->world.sfx_yetiyell = sp_find_child(lv->audio_node, "sfx_yetiyell");
    lv->world.sfx_zombie = sp_find_child(lv->audio_node, "sfx_zombie");
    lv->world.sfx_cavemandie = sp_find_child(lv->audio_node, "sfx_cavemandie");
    lv->world.sfx_alert = sp_find_child(lv->audio_node, "sfx_alert");
    lv->world.sfx_chestopen = sp_find_child(lv->audio_node, "sfx_chestopen");
    lv->world.sfx_kiss = sp_find_child(lv->audio_node, "sfx_kiss");
    lv->world.sfx_damsel = sp_find_child(lv->audio_node, "sfx_damsel");
    lv->world.sfx_jetpack = sp_find_child(lv->audio_node, "sfx_jetpack");
    lv->world.sfx_teleport = sp_find_child(lv->audio_node, "sfx_teleport");
    lv->world.sfx_slam = sp_find_child(lv->audio_node, "sfx_slam");
    lv->world.sfx_boing = sp_find_child(lv->audio_node, "sfx_boing");
    lv->world.sfx_trap = sp_find_child(lv->audio_node, "sfx_trap");
    lv->world.sfx_push = sp_find_child(lv->audio_node, "sfx_push");
    lv->world.sfx_thud = sp_find_child(lv->audio_node, "sfx_thud");
    lv->world.sfx_pause = sp_find_child(lv->audio_node, "sfx_pause");
    lv->world.sfx_click = sp_find_child(lv->audio_node, "sfx_click");
    lv->world.sfx_letsexplore = sp_find_child(lv->audio_node, "sfx_letsexplore");
    lv->world.sfx_mattockbreak = sp_find_child(lv->audio_node, "sfx_mattockbreak");
    lv->world.sfx_psychic = sp_find_child(lv->audio_node, "sfx_psychic");
    lv->world.sfx_ignite = sp_find_child(lv->audio_node, "sfx_ignite");
    lv->world.sfx_bombready = sp_find_child(lv->audio_node, "sfx_bombready");
    lv->world.sfx_blink1 = sp_find_child(lv->audio_node, "sfx_blink1");

    lv->act_left   = rr_action(self->scene, "sp_left");
    lv->act_right  = rr_action(self->scene, "sp_right");
    lv->act_up     = rr_action(self->scene, "sp_up");
    lv->act_down   = rr_action(self->scene, "sp_down");
    lv->act_jump   = rr_action(self->scene, "sp_jump");
    lv->act_whip   = rr_action(self->scene, "sp_whip");
    lv->act_bomb   = rr_action(self->scene, "sp_bomb");
    lv->act_rope   = rr_action(self->scene, "sp_rope");
    lv->act_run    = rr_action(self->scene, "sp_run");
    lv->act_pay    = rr_action(self->scene, "sp_pay");
    lv->act_pause  = rr_action(self->scene, "sp_pause");
    lv->act_accept = rr_action(self->scene, "sp_accept");

    sp_pools_setup(&lv->world,
                   lv->slot_16a_scene, lv->slot_16a_scene_max,
                   lv->slot_16b_scene, lv->slot_16b_scene_max,
                   lv->slot_32a_scene, lv->slot_32a_scene_max,
                   lv->slot_32b_scene, lv->slot_32b_scene_max,
                   lv->slot_64a_scene, lv->slot_64a_scene_max,
                   lv->slot_64b_scene, lv->slot_64b_scene_max);

    lv->world.scene = self->scene;
    lv->world.rng = 0x243F6A88u ^ (uint32_t)(g_sp_run.curr_level * 1315423911u + g_sp_run.scores.plays * 97u + 1u);
    start_current_level(lv);
    update_hud(lv);
}

static void spelunky_level_update(Node *self, SpelunkyLevel *lv, float dt)
{
    (void)self;

    if (!lv->in_summary && rr_action_pressed(lv->act_pause)) {
        lv->paused = !lv->paused;
        sp_set_visible(lv->pause_panel, lv->paused);
        sp_play_sfx(lv->world.sfx_pause);
        if (lv->paused) {
            rr_control_grab_focus(lv->btn_resume);
        }
    }

    if (!lv->paused && !lv->in_summary) {
        if (rr_action_pressed(lv->act_jump)) lv->lat_jump_p = true;
        if (!rr_action_down(lv->act_jump))   lv->lat_jump_r = true;
        if (rr_action_pressed(lv->act_whip)) lv->lat_whip_p = true;
        if (rr_action_pressed(lv->act_bomb)) lv->lat_bomb_p = true;
        if (rr_action_pressed(lv->act_rope)) lv->lat_rope_p = true;
        if (rr_action_pressed(lv->act_pay))  lv->lat_pay_p = true;
        if (rr_action_pressed(lv->act_accept)) lv->lat_accept_p = true;

        lv->accum += dt;
        if (lv->accum > SP_FIXED_DT * 2.0f) {
            lv->accum = SP_FIXED_DT * 2.0f;
        }
        while (lv->accum >= SP_FIXED_DT) {
            SpInput in;
            memset(&in, 0, sizeof(in));
            in.left          = rr_action_down(lv->act_left);
            in.right         = rr_action_down(lv->act_right);
            in.up            = rr_action_down(lv->act_up);
            in.down          = rr_action_down(lv->act_down);
            in.jump_down     = rr_action_down(lv->act_jump);
            in.jump_pressed  = lv->lat_jump_p;
            in.jump_released = lv->lat_jump_r;
            in.whip_pressed  = lv->lat_whip_p;
            in.whip_down     = rr_action_down(lv->act_whip);
            in.bomb_pressed  = lv->lat_bomb_p;
            in.rope_pressed  = lv->lat_rope_p;
            in.run_down      = rr_action_down(lv->act_run);
            in.pay_pressed   = lv->lat_pay_p;
            in.accept_pressed = lv->lat_accept_p;

            lv->lat_jump_p = lv->lat_jump_r = lv->lat_whip_p = false;
            lv->lat_bomb_p = lv->lat_rope_p = lv->lat_pay_p = lv->lat_accept_p = false;

            lv->accum -= SP_FIXED_DT;
            sp_step_world(&lv->world, &in, true);

            if (lv->world.player_idx >= 0) {
                const SpEntity *p = &lv->world.ents[lv->world.player_idx];
                if (p->state == ST_EXITING) {
                    if (g_sp_run.curr_level >= 16) {
                        open_summary(lv, false, true);
                    } else {
                        open_summary(lv, false, false);
                    }
                    break;
                } else if (!p->alive && p->timer <= 0) {
                    open_summary(lv, true, false);
                    break;
                }
            }
        }
    }

    if (lv->camera != NULL) {
        float sy = (lv->world.shake_ticks > 0) ? (lv->world.shake_toggle ? 3.0f : -3.0f) : 0.0f;
        lv->camera->local.m[9] = floorf(lv->world.cam_x + 0.5f);
        lv->camera->local.m[10] = floorf(lv->world.cam_y + sy + 0.5f);
    }

    sp_pool_begin_frame(&lv->world);
    sp_render_tiles(&lv->world);
    sp_render_entities(&lv->world);
    sp_pool_end_frame(&lv->world);

    update_hud(lv);
}

static void on_lv_resume(Node *self, SpelunkyLevel *lv)
{
    (void)self;
    lv->paused = false;
    sp_set_visible(lv->pause_panel, false);
    sp_play_sfx(lv->world.sfx_click);
}

static void on_lv_restart(Node *self, SpelunkyLevel *lv)
{
    (void)self;
    sp_play_sfx(lv->world.sfx_click);
    sp_run_reset_new_game(false);
    start_current_level(lv);
}

static void on_lv_title(Node *self, SpelunkyLevel *lv)
{
    (void)self;
    g_sp_run.show_intro = false;
    rr_change_scene(lv->title_scene);
}

static void on_lv_main_menu(Node *self, SpelunkyLevel *lv)
{
    (void)self;
    rr_change_scene(lv->menu_scene);
}

static void on_lv_next_level(Node *self, SpelunkyLevel *lv)
{
    (void)self;
    sp_play_sfx(lv->world.sfx_click);
    if (lv->summary_is_death || lv->summary_is_win) {
        sp_run_reset_new_game(false);
    } else if (g_sp_run.is_tutorial) {
        sp_run_reset_new_game(false);
    } else {
        g_sp_run.curr_level++;
        if (g_sp_run.thief_level > 0) {
            g_sp_run.thief_level--;
        }
    }
    start_current_level(lv);
}

COMPONENT_BEGIN(SpelunkyLevel, "SpelunkyLevel")
    EXPORT_SCENE(title_scene)
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
    EXPORT_PACKED_SCENE(slot_64a_scene)
    EXPORT_INT(slot_64a_scene_max)
    EXPORT_PACKED_SCENE(slot_64b_scene)
    EXPORT_INT(slot_64b_scene_max)
    ON_READY(spelunky_level_ready)
    ON_UPDATE(spelunky_level_update)
    ON_SIGNAL("_on_resume_pressed", on_lv_resume)
    ON_SIGNAL("_on_restart_pressed", on_lv_restart)
    ON_SIGNAL("_on_title_pressed", on_lv_title)
    ON_SIGNAL("_on_main_menu_pressed", on_lv_main_menu)
    ON_SIGNAL("_on_next_level_pressed", on_lv_next_level)
COMPONENT_END()
