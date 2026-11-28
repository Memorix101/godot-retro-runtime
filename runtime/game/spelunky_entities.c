/* Spelunky Classic entities: player physics, weapons, items, traps, enemies, Olmec, and FX. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "spelunky.h"

/* Forward helpers */
static void step_player(SpWorld *w, const SpInput *in, bool allow_exit);
static void step_enemies_and_items(SpWorld *w);
static void step_traps(SpWorld *w);

static bool is_enemy_kind(uint8_t k)
{
    return k >= ENT_SNAKE && k <= ENT_OLMEC && k != ENT_DAMSEL;
}

static bool is_holdable_kind(uint8_t k)
{
    return k == ENT_ROCK || k == ENT_JAR || k == ENT_SKULL || k == ENT_KEY ||
           k == ENT_GOLD_IDOL || k == ENT_CRYSTAL_SKULL || k == ENT_MACHETE ||
           k == ENT_MATTOCK || k == ENT_PISTOL || k == ENT_SHOTGUN || k == ENT_BOW ||
           k == ENT_WEB_CANNON || k == ENT_TELEPORTER || k == ENT_SCEPTRE ||
           k == ENT_FLARE || k == ENT_DICE || k == ENT_DAMSEL || k == ENT_SHOP_ITEM;
}

static uint16_t char_anim(uint16_t base_anim)
{
    if (g_sp_run.character == SP_CHAR_DAMSEL) {
        switch (base_anim) {
        case SP_ANIM_STAND_LEFT:  return SP_ANIM_DAMSEL_LEFT;
        case SP_ANIM_RUN_LEFT:    return SP_ANIM_DAMSEL_RUN_L;
        case SP_ANIM_JUMP_LEFT:   return SP_ANIM_DAMSEL_FALL_L;
        case SP_ANIM_FALL_LEFT:   return SP_ANIM_DAMSEL_FALL_L;
        case SP_ANIM_DUCK_LEFT:   return SP_ANIM_DAMSEL_DUCK_L;
        case SP_ANIM_CRAWL_LEFT:  return SP_ANIM_DAMSEL_CRAWL_L;
        case SP_ANIM_CLIMB_UP:    return SP_ANIM_DAMSEL_CLIMB;
        case SP_ANIM_HANG_LEFT:   return SP_ANIM_DAMSEL_HANG_L;
        case SP_ANIM_LOOK_LEFT:   return SP_ANIM_DAMSEL_LOOK_L;
        case SP_ANIM_ATTACK_LEFT: return SP_ANIM_DAMSEL_ATTACK_L;
        case SP_ANIM_STUN_L:      return SP_ANIM_DAMSEL_STUN_L;
        case SP_ANIM_DIE_L:       return SP_ANIM_DAMSEL_DIE_L;
        case SP_ANIM_DIE_LL:      return SP_ANIM_DAMSEL_DIE_LL;
        default: break;
        }
    } else if (g_sp_run.character == SP_CHAR_TUNNEL_MAN) {
        switch (base_anim) {
        case SP_ANIM_STAND_LEFT:  return SP_ANIM_TUNNEL_LEFT;
        case SP_ANIM_RUN_LEFT:    return SP_ANIM_TUNNEL_RUN_L;
        case SP_ANIM_JUMP_LEFT:   return SP_ANIM_TUNNEL_JUMP_L;
        case SP_ANIM_FALL_LEFT:   return SP_ANIM_TUNNEL_FALL_L;
        case SP_ANIM_DUCK_LEFT:   return SP_ANIM_TUNNEL_DUCK_L;
        case SP_ANIM_CRAWL_LEFT:  return SP_ANIM_TUNNEL_CRAWL_L;
        case SP_ANIM_CLIMB_UP:    return SP_ANIM_TUNNEL_CLIMB;
        case SP_ANIM_HANG_LEFT:   return SP_ANIM_TUNNEL_HANG_L;
        case SP_ANIM_LOOK_LEFT:   return SP_ANIM_TUNNEL_LOOK_L;
        case SP_ANIM_ATTACK_LEFT: return SP_ANIM_TUNNEL_ATTACK_L;
        case SP_ANIM_STUN_L:      return SP_ANIM_TUNNEL_STUN_L;
        case SP_ANIM_DIE_L:       return SP_ANIM_TUNNEL_DIE_L;
        case SP_ANIM_DIE_LL:      return SP_ANIM_TUNNEL_DIE_LL;
        default: break;
        }
    }
    return base_anim;
}

static void add_money(SpWorld *w, int amount)
{
    g_sp_run.money += amount;
    g_sp_run.xmoney += amount;
    w->collect_popup += amount;
    w->collect_timer = 45;
}

static void grant_passive(SpWorld *w, uint8_t item_sub)
{
    switch (item_sub) {
    case ITEM_BOMB_BAG:
        g_sp_run.bombs += 3;
        sp_set_message(w, "YOU GOT 3 MORE BOMBS!", "", 90);
        break;
    case ITEM_BOMB_BOX:
        g_sp_run.bombs += 12;
        sp_set_message(w, "YOU GOT 12 MORE BOMBS!", "", 90);
        break;
    case ITEM_ROPE_PILE:
        g_sp_run.rope += 3;
        sp_set_message(w, "YOU GOT 3 MORE ROPES!", "", 90);
        break;
    case ITEM_PASTE:
        g_sp_run.passives |= PASS_PASTE;
        sp_set_message(w, "YOU GOT STICKY BOMBS!", "", 90);
        break;
    case ITEM_COMPASS:
        g_sp_run.passives |= PASS_COMPASS;
        sp_set_message(w, "YOU GOT THE COMPASS!", "IT POINTS TO THE EXIT.", 90);
        break;
    case ITEM_SPRING_SHOES:
        g_sp_run.passives |= PASS_SPRING;
        sp_set_message(w, "YOU GOT SPRING SHOES!", "YOU CAN JUMP HIGHER.", 90);
        break;
    case ITEM_SPIKE_SHOES:
        g_sp_run.passives |= PASS_SPIKE;
        sp_set_message(w, "YOU GOT SPIKE SHOES!", "STOMP ENEMIES HARDER!", 90);
        break;
    case ITEM_GLOVES:
        g_sp_run.passives |= PASS_GLOVES;
        sp_set_message(w, "YOU GOT CLIMBING GLOVES!", "GRIP ANY WALL.", 90);
        break;
    case ITEM_MITT:
        g_sp_run.passives |= PASS_MITT;
        sp_set_message(w, "YOU GOT A PITCHER'S MITT!", "THROW FAST AND FAR.", 90);
        break;
    case ITEM_SPECTACLES:
        g_sp_run.passives |= PASS_SPECTACLES;
        sp_set_message(w, "YOU GOT SPECTACLES!", "SEE GEMS IN THE WALLS.", 90);
        break;
    case ITEM_CAPE:
        g_sp_run.passives = (g_sp_run.passives & ~PASS_JETPACK) | PASS_CAPE;
        sp_set_message(w, "YOU GOT A CAPE!", "HOLD JUMP TO GLIDE.", 90);
        break;
    case ITEM_JETPACK:
        g_sp_run.passives = (g_sp_run.passives & ~PASS_CAPE) | PASS_JETPACK;
        w->jetpack_fuel = 50;
        sp_set_message(w, "YOU GOT A JETPACK!", "HOLD JUMP TO FLY!", 90);
        break;
    case ITEM_PARACHUTE:
        g_sp_run.passives |= PASS_PARACHUTE;
        sp_set_message(w, "YOU GOT A PARACHUTE!", "", 90);
        break;
    case ITEM_UDJAT_EYE:
        g_sp_run.passives |= PASS_UDJAT;
        sp_set_message(w, "YOU GOT THE UDJAT EYE!", "YOU FEEL AWAKENED.", 120);
        break;
    case ITEM_ANKH:
        g_sp_run.passives |= PASS_ANKH;
        sp_set_message(w, "YOU GOT THE ANKH!", "YOU FEEL PROTECTED.", 120);
        break;
    case ITEM_CROWN:
        g_sp_run.passives |= PASS_CROWN;
        sp_set_message(w, "YOU GOT THE HEDJET!", "IT GLOWS BRILLIANTLY.", 120);
        break;
    case ITEM_KAPALA:
        g_sp_run.passives |= PASS_KAPALA;
        sp_set_message(w, "YOU GOT THE KAPALA!", "IT THIRSTS FOR BLOOD...", 120);
        break;
    default:
        break;
    }
    sp_play_sfx(w->sfx_pickup);
}

void sp_hurt_player(SpWorld *w, int dmg, const char *cause, float knock_vx, float knock_vy)
{
    SpEntity *p;
    if (w->player_idx < 0) {
        return;
    }
    p = &w->ents[w->player_idx];
    if (!p->alive || w->invuln_timer > 0 || p->state == ST_EXITING) {
        return;
    }

    g_sp_run.plife -= dmg;
    p->hp = (int16_t)g_sp_run.plife;
    w->invuln_timer = 45;
    sp_spawn_blood(w, p->x, p->y, 4);

    if (g_sp_run.plife <= 0) {
        if ((g_sp_run.passives & PASS_ANKH) && g_sp_run.curr_level > 0) {
            g_sp_run.passives &= ~PASS_ANKH;
            g_sp_run.plife = 4;
            p->hp = 4;
            w->invuln_timer = 90;
            sp_set_message(w, "THE ANKH SHATTERS!", "YOU ARE REVIVED!", 120);
            sp_play_sfx(w->sfx_pickup);
            return;
        }
        g_sp_run.plife = 0;
        p->alive = false;
        p->state = ST_DEAD;
        p->vx = knock_vx;
        p->vy = knock_vy;
        p->timer = 90;
        if (cause != NULL && cause[0] != '\0') {
            snprintf(g_sp_run.death_cause, sizeof(g_sp_run.death_cause), "%s", cause);
        } else {
            snprintf(g_sp_run.death_cause, sizeof(g_sp_run.death_cause), "DIED IN THE CAVES");
        }
        sp_play_sfx(w->sfx_die);
    } else {
        w->stun_timer = 20;
        p->state = ST_STUNNED;
        p->vx = knock_vx;
        p->vy = knock_vy;
        sp_play_sfx(w->sfx_hurt);
    }
}

static void record_enemy_kill(uint8_t kind)
{
    g_sp_run.kills++;
    (void)kind;
    if (g_sp_run.passives & PASS_KAPALA) {
        g_sp_run.blood_level++;
        if (g_sp_run.blood_level >= 7) {
            g_sp_run.blood_level = 0;
            g_sp_run.plife++;
        }
    }
}

static void hurt_enemy(SpWorld *w, int idx, int dmg, float kvx, float kvy, bool kill_counts)
{
    SpEntity *e = &w->ents[idx];
    if (!e->active || !e->alive || e->invincible) {
        return;
    }
    e->hp -= (int16_t)dmg;
    e->vx = kvx;
    e->vy = kvy;
    if (e->kind == ENT_SHOPKEEPER) {
        w->shop_angry = true;
        e->state = ST_ANGRY;
    }
    sp_spawn_blood(w, e->x + 8.0f, e->y + 8.0f, 3);
    if (e->hp <= 0) {
        if (e->kind == ENT_FIRE_FROG) {
            sp_explode(w, e->x + 8.0f, e->y + 8.0f, 36, true);
        }
        if (e->kind == ENT_GIANT_SPIDER) {
            int gem = sp_spawn_entity(w, ENT_TREASURE, e->x + 16.0f, e->y + 16.0f);
            if (gem >= 0) {
                w->ents[gem].subtype = TR_RUBY_BIG;
                w->ents[gem].anim_id = SP_ANIM_RUBY_BIG;
                w->ents[gem].value = 4800;
                w->ents[gem].bx = -5; w->ents[gem].by = -5; w->ents[gem].bw = 10; w->ents[gem].bh = 10;
            }
        }
        if (e->kind == ENT_CAVEMAN) {
            sp_play_sfx(w->sfx_cavemandie);
        } else {
            sp_play_sfx(w->sfx_hit);
        }
        if (kill_counts && e->kind != ENT_DAMSEL) {
            record_enemy_kill(e->kind);
        }
        sp_destroy_entity(w, idx);
    } else {
        e->state = ST_STUNNED;
        e->timer = 90;
        sp_play_sfx(w->sfx_hit);
    }
}

static void open_crate(SpWorld *w, float x, float y)
{
    int r = sp_rand_range(w, 0, 12);
    sp_play_sfx(w->sfx_pickup);
    sp_spawn_fx(w, FX_POOF, x, y, 0.0f, 0.0f);
    switch (r) {
    case 0:
    case 1:
        g_sp_run.bombs += 3;
        sp_set_message(w, "FOUND 3 BOMBS!", "", 75);
        break;
    case 2:
    case 3:
        g_sp_run.rope += 3;
        sp_set_message(w, "FOUND 3 ROPES!", "", 75);
        break;
    case 4: {
        int s = sp_spawn_entity(w, ENT_SHOTGUN, x, y);
        if (s >= 0) {
            w->ents[s].anim_id = SP_ANIM_SHOTGUN_RIGHT;
            w->ents[s].bx = -5; w->ents[s].by = -5; w->ents[s].bw = 10; w->ents[s].bh = 10;
        }
        break;
    }
    case 5: {
        int m = sp_spawn_entity(w, ENT_MATTOCK, x, y);
        if (m >= 0) {
            w->ents[m].anim_id = SP_ANIM_MATTOCK_RIGHT;
            w->ents[m].hp = 35;
            w->ents[m].bx = -5; w->ents[m].by = -5; w->ents[m].bw = 10; w->ents[m].bh = 10;
        }
        break;
    }
    case 6: grant_passive(w, ITEM_PASTE); break;
    case 7: grant_passive(w, ITEM_SPRING_SHOES); break;
    case 8: grant_passive(w, ITEM_SPIKE_SHOES); break;
    case 9: grant_passive(w, ITEM_COMPASS); break;
    case 10: grant_passive(w, ITEM_CAPE); break;
    case 11: grant_passive(w, ITEM_GLOVES); break;
    default: grant_passive(w, ITEM_JETPACK); break;
    }
}

static void open_chest(SpWorld *w, int idx)
{
    SpEntity *e = &w->ents[idx];
    int i;
    sp_play_sfx(w->sfx_chestopen);
    for (i = 0; i < 3; i++) {
        int g = sp_spawn_entity(w, ENT_TREASURE, e->x, e->y - 4.0f);
        if (g >= 0) {
            static const uint8_t gems[] = { TR_EMERALD, TR_SAPPHIRE, TR_RUBY };
            uint8_t sub = gems[sp_rand_range(w, 0, 2)];
            w->ents[g].subtype = sub;
            w->ents[g].anim_id = (sub == TR_EMERALD) ? SP_ANIM_EMERALD : (sub == TR_SAPPHIRE ? SP_ANIM_SAPPHIRE : SP_ANIM_RUBY);
            w->ents[g].value = (sub == TR_EMERALD) ? 800 : (sub == TR_SAPPHIRE ? 1200 : 1600);
            w->ents[g].bx = -5; w->ents[g].by = -5; w->ents[g].bw = 10; w->ents[g].bh = 10;
            w->ents[g].vx = (float)sp_rand_range(w, -20, 20) * 0.1f;
            w->ents[g].vy = -3.0f;
        }
    }
    sp_destroy_entity(w, idx);
}

static void smash_jar(SpWorld *w, int idx)
{
    SpEntity *e = &w->ents[idx];
    int r = sp_rand_range(w, 0, 5);
    sp_play_sfx(w->sfx_break);
    sp_spawn_fx(w, FX_RUBBLE, e->x, e->y, -1.0f, -2.0f);
    sp_spawn_fx(w, FX_RUBBLE, e->x, e->y, 1.0f, -2.0f);
    if (r == 0) {
        int s = sp_spawn_entity(w, ENT_SNAKE, e->x - 8.0f, e->y - 8.0f);
        if (s >= 0) {
            w->ents[s].anim_id = SP_ANIM_SNAKE_LEFT;
            w->ents[s].bx = 2; w->ents[s].by = 2; w->ents[s].bw = 12; w->ents[s].bh = 14;
            w->ents[s].hp = 1;
            w->ents[s].z = SP_Z_ENEMY;
        }
    } else if (r == 1) {
        int sp = sp_spawn_entity(w, ENT_SPIDER, e->x - 8.0f, e->y - 8.0f);
        if (sp >= 0) {
            w->ents[sp].anim_id = SP_ANIM_SPIDER;
            w->ents[sp].state = ST_WALK;
            w->ents[sp].bx = 2; w->ents[sp].by = 4; w->ents[sp].bw = 12; w->ents[sp].bh = 12;
            w->ents[sp].hp = 1;
            w->ents[sp].z = SP_Z_ENEMY;
        }
    } else if (r <= 3) {
        int g = sp_spawn_entity(w, ENT_TREASURE, e->x, e->y - 4.0f);
        if (g >= 0) {
            w->ents[g].subtype = TR_SAPPHIRE;
            w->ents[g].anim_id = SP_ANIM_SAPPHIRE;
            w->ents[g].value = 1200;
            w->ents[g].bx = -5; w->ents[g].by = -5; w->ents[g].bw = 10; w->ents[g].bh = 10;
            w->ents[g].vy = -2.0f;
        }
    }
    sp_destroy_entity(w, idx);
}

static void use_held_item(SpWorld *w, SpEntity *p, const SpInput *in)
{
    SpEntity *item;
    if (p->held_ent < 0) {
        return;
    }
    item = &w->ents[p->held_ent];

    if (item->kind == ENT_PISTOL || item->kind == ENT_SHOTGUN) {
        int pellets = (item->kind == ENT_SHOTGUN) ? 6 : 1;
        int k;
        if (w->firing_timer > 0) {
            return;
        }
        w->firing_timer = (item->kind == ENT_SHOTGUN) ? 35 : 18;
        sp_play_sfx(w->sfx_shotgun);
        for (k = 0; k < pellets; k++) {
            int b = sp_spawn_entity(w, ENT_BULLET, p->x + p->facing * 10.0f, p->y);
            if (b >= 0) {
                w->ents[b].anim_id = SP_ANIM_BULLET;
                w->ents[b].vx = p->facing * (9.0f + (float)sp_rand_range(w, -1, 1));
                w->ents[b].vy = (pellets > 1) ? (float)sp_rand_range(w, -15, 15) * 0.1f : 0.0f;
                w->ents[b].bx = -2; w->ents[b].by = -2; w->ents[b].bw = 4; w->ents[b].bh = 4;
                w->ents[b].timer = 24;
                w->ents[b].safe = true;
                w->ents[b].z = SP_Z_FX;
            }
        }
        p->vx -= p->facing * (item->kind == ENT_SHOTGUN ? 2.5f : 1.0f);
        return;
    }

    if (item->kind == ENT_BOW) {
        int a = sp_spawn_entity(w, ENT_ARROW, p->x + p->facing * 10.0f, p->y);
        if (a >= 0) {
            w->ents[a].anim_id = SP_ANIM_ARROW_RIGHT;
            w->ents[a].facing = p->facing;
            w->ents[a].vx = p->facing * 7.0f;
            w->ents[a].vy = -0.5f;
            w->ents[a].bx = -4; w->ents[a].by = -2; w->ents[a].bw = 8; w->ents[a].bh = 4;
            w->ents[a].safe = true;
            w->ents[a].timer = 15;
            w->ents[a].z = SP_Z_FX;
            sp_play_sfx(w->sfx_arrowtrap);
        }
        return;
    }

    if (item->kind == ENT_WEB_CANNON) {
        int ws = sp_spawn_entity(w, ENT_WEB_SHOT, p->x + p->facing * 10.0f, p->y);
        if (ws >= 0) {
            w->ents[ws].anim_id = SP_ANIM_WEB_BALL;
            w->ents[ws].vx = p->facing * 6.0f;
            w->ents[ws].vy = -1.0f;
            w->ents[ws].bx = -4; w->ents[ws].by = -4; w->ents[ws].bw = 8; w->ents[ws].bh = 8;
            w->ents[ws].z = SP_Z_FX;
        }
        return;
    }

    if (item->kind == ENT_TELEPORTER) {
        float dx = (float)(p->facing * sp_rand_range(w, 64, 96));
        float dy = in->up ? -64.0f : (in->down ? 64.0f : 0.0f);
        float nx = p->x + dx;
        float ny = p->y + dy;
        if (nx < 20.0f) nx = 20.0f;
        if (ny < 20.0f) ny = 20.0f;
        if (nx > (float)(w->cols - 2) * 16.0f) nx = (float)(w->cols - 2) * 16.0f;
        if (ny > (float)(w->rows - 2) * 16.0f) ny = (float)(w->rows - 2) * 16.0f;
        sp_spawn_fx(w, FX_POOF, p->x, p->y, 0.0f, 0.0f);
        p->x = nx;
        p->y = ny;
        sp_spawn_fx(w, FX_POOF, p->x, p->y, 0.0f, 0.0f);
        sp_play_sfx(w->sfx_teleport);
        if (sp_entity_hits_solid(w, p, 0.0f, 0.0f)) {
            sp_hurt_player(w, 99, "TELEFRAGGED INSIDE A WALL", 0.0f, 0.0f);
        }
        return;
    }

    if (item->kind == ENT_MACHETE) {
        w->whip_timer = 10;
        sp_play_sfx(w->sfx_whip);
        return;
    }

    if (item->kind == ENT_MATTOCK) {
        int tx = (int)floorf((p->x + p->facing * 12.0f) / 16.0f);
        int ty = (int)floorf((p->y + (in->down ? 12.0f : 0.0f)) / 16.0f);
        w->whip_timer = 10;
        if (sp_tile_in_bounds(w, tx, ty) && w->grid[ty][tx].fg != TILE_NONE &&
            !(w->grid[ty][tx].flags & TF_INVINCIBLE)) {
            sp_destroy_tile(w, tx, ty, true);
            sp_autotile_world(w);
            sp_play_sfx(w->sfx_break);
            item->hp--;
            if (item->hp <= 0) {
                sp_play_sfx(w->sfx_mattockbreak);
                sp_destroy_entity(w, p->held_ent);
                p->held_ent = -1;
                g_sp_run.held_item_kind = ENT_NONE;
            }
        }
        return;
    }

    if (item->kind == ENT_SCEPTRE) {
        int ws = sp_spawn_entity(w, ENT_LASER, p->x + p->facing * 12.0f, p->y);
        if (ws >= 0) {
            w->ents[ws].anim_id = SP_ANIM_PSYCHIC_WAVE;
            w->ents[ws].vx = p->facing * 5.0f;
            w->ents[ws].vy = 0.0f;
            w->ents[ws].timer = 45;
            w->ents[ws].safe = true;
            sp_play_sfx(w->sfx_psychic);
        }
        return;
    }

    /* Throw / drop holdable item */
    item->held = false;
    item->holder = -1;
    p->held_ent = -1;
    g_sp_run.held_item_kind = ENT_NONE;
    if (in->down) {
        item->vx = p->facing * 1.5f;
        item->vy = 0.0f;
    } else {
        float throw_spd = (g_sp_run.passives & PASS_MITT) ? 8.0f : 5.5f;
        item->vx = p->facing * throw_spd + p->vx * 0.5f;
        item->vy = in->up ? -5.5f : -2.5f;
    }
    item->armed = true; /* flying projectile */
    sp_play_sfx(w->sfx_throw);
}

static void step_player(SpWorld *w, const SpInput *in, bool allow_exit)
{
    SpEntity *p;
    int tx, ty;
    bool on_ground, in_water, on_climbable;
    float max_run;

    if (w->player_idx < 0) {
        return;
    }
    p = &w->ents[w->player_idx];

    if (!p->alive) {
        p->vy += 0.5f;
        if (p->vy > 6.0f) p->vy = 6.0f;
        sp_move_entity(w, w->player_idx, p->vx, p->vy, true, NULL, NULL);
        p->vx *= 0.85f;
        p->anim_id = char_anim(p->on_ground ? SP_ANIM_DIE_LL : SP_ANIM_DIE_L);
        if (p->timer > 0) p->timer--;
        return;
    }

    if (w->invuln_timer > 0) w->invuln_timer--;
    if (w->firing_timer > 0) w->firing_timer--;
    if (w->whip_timer > 0) w->whip_timer--;
    if (w->collect_timer > 0) {
        w->collect_timer--;
        if (w->collect_timer == 0) w->collect_popup = 0;
    }
    if (w->msg_timer > 0) w->msg_timer--;

    /* Udjat Eye blinking near secrets / gems */
    if (g_sp_run.passives & PASS_UDJAT) {
        w->udjat_timer++;
        if (w->udjat_timer >= 20) {
            w->udjat_timer = 0;
            w->udjat_blink = !w->udjat_blink;
        }
    }

    if (w->stun_timer > 0) {
        w->stun_timer--;
        p->vy += 0.6f;
        if (p->vy > 6.0f) p->vy = 6.0f;
        bool hx = false, hy = false;
        sp_move_entity(w, w->player_idx, p->vx, p->vy, true, &hx, &hy);
        if (hx) p->vx = -p->vx * 0.5f;
        if (hy && p->vy > 0.0f) {
            p->vx *= 0.7f;
            p->vy = -p->vy * 0.3f;
            if (fabsf(p->vy) < 0.8f) p->vy = 0.0f;
        }
        p->anim_id = char_anim(SP_ANIM_STUN_L);
        p->anim_frame += 0.25f;
        return;
    }

    tx = (int)floorf(p->x / 16.0f);
    ty = (int)floorf(p->y / 16.0f);
    on_ground = sp_entity_on_ground(w, p);
    p->on_ground = on_ground;
    in_water = sp_tile_is_water(w, tx, ty);
    on_climbable = sp_tile_is_ladder(w, tx, ty);

    /* Thin ice crack when standing on it */
    if (on_ground) {
        int bty = (int)floorf((p->y + p->by + p->bh + 2.0f) / 16.0f);
        if (sp_tile_in_bounds(w, tx, bty) && w->grid[bty][tx].fg == TILE_THIN_ICE) {
            w->grid[bty][tx].variant++;
            if (w->grid[bty][tx].variant > 18) {
                sp_destroy_tile(w, tx, bty, true);
            }
        }
    }

    /* Enter door / exit level */
    if (allow_exit && in->up && on_ground && sp_tile_in_bounds(w, tx, ty) &&
        (w->grid[ty][tx].bg == BG_EXIT || w->grid[ty][tx].bg == BG_GOLD_DOOR)) {
        p->state = ST_EXITING;
        if (p->held_ent >= 0 && w->ents[p->held_ent].kind == ENT_DAMSEL) {
            g_sp_run.damsels++;
            g_sp_run.xdamsels++;
            g_sp_run.plife++;
            sp_play_sfx(w->sfx_kiss);
            sp_destroy_entity(w, p->held_ent);
            p->held_ent = -1;
            g_sp_run.held_item_kind = ENT_NONE;
        } else if (p->held_ent >= 0 && w->ents[p->held_ent].kind == ENT_GOLD_IDOL) {
            add_money(w, 5000);
        }
        return;
    }

    /* Lava instant death */
    if (sp_tile_is_lava(w, tx, ty)) {
        sp_hurt_player(w, 99, "ROASTED IN LAVA", 0.0f, -4.0f);
        return;
    }

    /* Spikes check (falling into spikes kills player!) */
    if (sp_tile_in_bounds(w, tx, ty) && w->grid[ty][tx].fg == TILE_SPIKES &&
        p->vy > 1.5f && !(g_sp_run.passives & PASS_CAPE)) {
        w->grid[ty][tx].flags |= TF_BLOODY;
        sp_autotile_world(w);
        sp_hurt_player(w, 99, "IMPALED ON SPIKES", 0.0f, -2.0f);
        return;
    }

    /* Fall damage tracking */
    if (on_ground || p->state == ST_CLIMB || p->state == ST_HANG || in_water) {
        if (on_ground && w->fall_timer >= 24 && !(g_sp_run.passives & (PASS_CAPE | PASS_PARACHUTE))) {
            sp_hurt_player(w, 1, "FELL FROM A GREAT HEIGHT", 0.0f, -2.0f);
            w->fall_timer = 0;
            return;
        }
        if (on_ground && w->fall_timer > 4) {
            sp_play_sfx(w->sfx_land);
        }
        w->fall_timer = 0;
        if (g_sp_run.passives & PASS_JETPACK) {
            w->jetpack_fuel = 50;
        }
    } else if (p->vy > 0.0f) {
        w->fall_timer++;
    } else {
        w->fall_timer = 0;
    }

    /* Climbing ladders/vines/ropes */
    if (p->state != ST_CLIMB && on_climbable && (in->up || (in->down && !on_ground))) {
        p->state = ST_CLIMB;
        p->x = (float)tx * 16.0f + 8.0f;
        p->vx = 0.0f;
        p->vy = 0.0f;
    }

    if (p->state == ST_CLIMB) {
        if (!on_climbable || in->jump_pressed) {
            p->state = ST_FALL;
            if (in->jump_pressed) {
                p->vy = (g_sp_run.passives & PASS_SPRING) ? -8.5f : -6.2f;
                sp_play_sfx((g_sp_run.passives & PASS_SPRING) ? w->sfx_bigjump : w->sfx_jump);
            }
        } else {
            p->vx = 0.0f;
            p->vy = in->up ? -2.2f : (in->down ? 2.5f : 0.0f);
            sp_move_entity(w, w->player_idx, 0.0f, p->vy, false, NULL, NULL);
            if (in->left) p->facing = -1;
            if (in->right) p->facing = 1;
            p->anim_id = char_anim(SP_ANIM_CLIMB_UP);
            if (p->vy != 0.0f) p->anim_frame += 0.25f;
        }
    } else if (p->state == ST_HANG) {
        /* Hanging on a ledge */
        if (in->jump_pressed) {
            p->state = ST_JUMP;
            p->vy = in->down ? 0.5f : ((g_sp_run.passives & PASS_SPRING) ? -8.5f : -6.2f);
            if (!in->down) {
                sp_play_sfx((g_sp_run.passives & PASS_SPRING) ? w->sfx_bigjump : w->sfx_jump);
            }
        } else if (in->down && in->whip_pressed) {
            p->state = ST_FALL;
        } else {
            p->vx = 0.0f;
            p->vy = 0.0f;
            p->anim_id = char_anim(SP_ANIM_HANG_LEFT);
        }
    } else {
        /* Standard horizontal movement */
        max_run = in->run_down ? 4.4f : 2.9f;
        if (in->down && on_ground) {
            max_run = 1.4f; /* crawl */
        }
        if (in->left) {
            p->facing = -1;
            p->vx -= on_ground ? 1.1f : 0.65f;
            if (p->vx < -max_run) p->vx = -max_run;
        } else if (in->right) {
            p->facing = 1;
            p->vx += on_ground ? 1.1f : 0.65f;
            if (p->vx > max_run) p->vx = max_run;
        } else {
            float fric = (on_ground && g_sp_run.level_type == SP_AREA_ICE) ? 0.92f : (on_ground ? 0.65f : 0.88f);
            p->vx *= fric;
            if (fabsf(p->vx) < 0.15f) p->vx = 0.0f;
        }

        /* Jump */
        if (in->jump_pressed && (on_ground || in_water)) {
            p->vy = (g_sp_run.passives & PASS_SPRING) ? -8.6f : -6.4f;
            sp_play_sfx((g_sp_run.passives & PASS_SPRING) ? w->sfx_bigjump : w->sfx_jump);
        } else if (in->jump_released && p->vy < -2.4f) {
            p->vy = -2.4f;
        }

        /* Cape / Jetpack / Parachute */
        if (!on_ground && in->jump_down && (g_sp_run.passives & PASS_JETPACK) && w->jetpack_fuel > 0) {
            p->vy -= 1.05f;
            if (p->vy < -4.5f) p->vy = -4.5f;
            w->jetpack_fuel--;
            if ((w->jetpack_fuel & 3) == 0) {
                sp_spawn_fx(w, FX_SMOKE, p->x, p->y + 8.0f, 0.0f, 1.0f);
                sp_play_sfx(w->sfx_jetpack);
            }
        } else {
            float grav = in_water ? 0.25f : 0.58f;
            float term = in_water ? 2.5f : 6.0f;
            if (!on_ground && p->vy > 0.0f) {
                if ((g_sp_run.passives & PASS_CAPE) && in->jump_down) {
                    term = 1.6f;
                } else if ((g_sp_run.passives & PASS_PARACHUTE) && w->fall_timer > 14) {
                    term = 1.8f;
                }
            }
            p->vy += grav;
            if (p->vy > term) p->vy = term;
        }

        {
            bool hx = false, hy = false;
            sp_move_entity(w, w->player_idx, p->vx, p->vy, !(in->down && in->jump_down), &hx, &hy);
            if (hx) {
                /* Push block check */
                int i;
                for (i = 0; i < SP_MAX_ENTITIES; i++) {
                    SpEntity *b = &w->ents[i];
                    if (b->active && b->kind == ENT_PUSH_BLOCK &&
                        fabsf(b->y + 8.0f - p->y) < 12.0f &&
                        fabsf((b->x + 8.0f) - (p->x + p->facing * 14.0f)) < 10.0f) {
                        bool bhx = false;
                        sp_move_entity(w, i, (float)p->facing * 1.0f, 0.0f, true, &bhx, NULL);
                    }
                }
                p->vx = 0.0f;
            }
            if (hy) {
                p->vy = 0.0f;
            }
        }

        /* Ledge grab: falling near a wall edge */
        if (!on_ground && p->vy >= 0.0f && (in->left || in->right || (g_sp_run.passives & PASS_GLOVES))) {
            int dir = in->left ? -1 : (in->right ? 1 : p->facing);
            int wall_tx = (int)floorf((p->x + dir * 7.0f) / 16.0f);
            int head_ty = (int)floorf((p->y - 6.0f) / 16.0f);
            if (sp_tile_is_solid(w, wall_tx, head_ty) &&
                ((g_sp_run.passives & PASS_GLOVES) || !sp_tile_is_solid(w, wall_tx, head_ty - 1))) {
                p->state = ST_HANG;
                p->facing = (int8_t)dir;
                p->vy = 0.0f;
            }
        }

        /* Select player sprite animation */
        if (w->whip_timer > 0) {
            p->anim_id = char_anim(SP_ANIM_ATTACK_LEFT);
            p->anim_frame = (float)(12 - w->whip_timer);
        } else if (!on_ground) {
            p->anim_id = char_anim(p->vy < 0.0f ? SP_ANIM_JUMP_LEFT : SP_ANIM_FALL_LEFT);
            p->anim_frame = 0.0f;
        } else if (in->down) {
            if (fabsf(p->vx) > 0.2f) {
                p->anim_id = char_anim(SP_ANIM_CRAWL_LEFT);
                p->anim_frame += fabsf(p->vx) * 0.25f;
            } else {
                p->anim_id = char_anim(SP_ANIM_DUCK_LEFT);
                p->anim_frame = 0.0f;
            }
        } else if (in->up) {
            p->anim_id = char_anim(SP_ANIM_LOOK_LEFT);
            p->anim_frame = 0.0f;
        } else if (fabsf(p->vx) > 0.2f) {
            p->anim_id = char_anim(SP_ANIM_RUN_LEFT);
            p->anim_frame += fabsf(p->vx) * 0.22f;
        } else {
            p->anim_id = char_anim(SP_ANIM_STAND_LEFT);
            p->anim_frame = 0.0f;
        }
    }

    /* Whip / pickup / use item */
    if (in->whip_pressed && w->whip_timer == 0) {
        if (in->down && p->held_ent < 0) {
            /* Pick up nearby holdable or open crate/chest */
            int i;
            bool picked = false;
            for (i = 0; i < SP_MAX_ENTITIES; i++) {
                SpEntity *e = &w->ents[i];
                int etx, ety;
                if (!e->active || !e->alive || e->held) {
                    continue;
                }
                if (fabsf(e->x - p->x) >= 16.0f || fabsf(e->y - p->y) >= 16.0f) {
                    continue;
                }
                etx = (int)floorf(e->x / 16.0f);
                ety = (int)floorf(e->y / 16.0f);
                if (sp_tile_is_solid(w, etx, ety)) {
                    continue; /* still buried inside solid dirt/rock */
                }
                if (is_holdable_kind(e->kind)) {
                    if (e->kind == ENT_SHOP_ITEM && !e->for_sale) {
                        grant_passive(w, e->subtype);
                        sp_destroy_entity(w, i);
                    } else {
                        e->held = true;
                        e->holder = (int16_t)w->player_idx;
                        p->held_ent = (int16_t)i;
                        g_sp_run.held_item_kind = e->kind;
                        sp_play_sfx(w->sfx_pickup);
                    }
                    picked = true;
                    break;
                } else if (e->kind == ENT_CRATE) {
                    open_crate(w, e->x, e->y);
                    sp_destroy_entity(w, i);
                    picked = true;
                    break;
                } else if (e->kind == ENT_CHEST) {
                    open_chest(w, i);
                    picked = true;
                    break;
                }
            }
            if (!picked) {
                w->whip_timer = 12;
                sp_play_sfx(w->sfx_whip);
            }
        } else if (p->held_ent >= 0) {
            use_held_item(w, p, in);
        } else {
            w->whip_timer = 12;
            sp_play_sfx(w->sfx_whip);
        }
    }

    /* Pay / interact (E / P / LB) */
    if (in->pay_pressed) {
        int i;
        if (p->held_ent >= 0 && w->ents[p->held_ent].for_sale) {
            SpEntity *it = &w->ents[p->held_ent];
            if (g_sp_run.money >= it->cost) {
                g_sp_run.money -= it->cost;
                it->for_sale = false;
                it->cost = 0;
                sp_play_sfx(w->sfx_coin);
                if (it->kind == ENT_SHOP_ITEM) {
                    grant_passive(w, it->subtype);
                    sp_destroy_entity(w, p->held_ent);
                    p->held_ent = -1;
                    g_sp_run.held_item_kind = ENT_NONE;
                } else if (it->kind == ENT_DAMSEL) {
                    sp_set_message(w, "YOU HIRED THE DAMSEL!", "", 60);
                } else {
                    sp_set_message(w, "THANK YOU FOR YOUR PURCHASE!", "", 60);
                }
            } else {
                sp_set_message(w, "NOT ENOUGH GOLD!", "", 60);
                sp_play_sfx(w->sfx_click);
            }
        } else {
            /* Open chest / crate nearby */
            for (i = 0; i < SP_MAX_ENTITIES; i++) {
                SpEntity *e = &w->ents[i];
                if (!e->active) continue;
                if (fabsf(e->x - p->x) < 16.0f && fabsf(e->y - p->y) < 16.0f) {
                    if (e->kind == ENT_CRATE) {
                        open_crate(w, e->x, e->y);
                        sp_destroy_entity(w, i);
                        break;
                    } else if (e->kind == ENT_CHEST) {
                        open_chest(w, i);
                        break;
                    }
                }
            }
        }
    }

    /* Place / throw bomb */
    if (in->bomb_pressed && g_sp_run.bombs > 0) {
        int b = sp_spawn_entity(w, ENT_BOMB, p->x, p->y);
        if (b >= 0) {
            SpEntity *be = &w->ents[b];
            g_sp_run.bombs--;
            be->anim_id = SP_ANIM_BOMB_ARMED;
            be->armed = true;
            be->timer = 75;
            be->bx = -4; be->by = -4; be->bw = 8; be->bh = 8;
            if (in->down) {
                be->vx = p->facing * 1.2f;
                be->vy = 0.0f;
            } else {
                float spd = (g_sp_run.passives & PASS_MITT) ? 7.0f : 4.5f;
                be->vx = p->facing * spd;
                be->vy = in->up ? -5.0f : -2.5f;
            }
            sp_play_sfx(w->sfx_throw);
        }
    }

    /* Throw rope */
    if (in->rope_pressed && g_sp_run.rope > 0) {
        int r = sp_spawn_entity(w, ENT_ROPE_THROW, (float)tx * 16.0f + 8.0f, p->y);
        if (r >= 0) {
            g_sp_run.rope--;
            w->ents[r].anim_id = SP_ANIM_ROPE_TOP;
            w->ents[r].vy = in->down ? 4.0f : -8.0f;
            w->ents[r].timer = in->down ? 2 : 7; /* tiles to unfurl */
            w->ents[r].subtype = in->down ? 1 : 0;
            sp_play_sfx(w->sfx_throw);
        }
    }

    /* Keep held item aligned to player and check shop theft */
    if (p->held_ent >= 0) {
        SpEntity *it = &w->ents[p->held_ent];
        it->x = p->x + p->facing * 4.0f;
        it->y = p->y + 1.0f;
        it->facing = p->facing;
        if (it->for_sale && w->shop_spawned &&
            (tx < w->shop_x0 || tx > w->shop_x1 || ty < w->shop_y0 || ty > w->shop_y1)) {
            it->for_sale = false;
            it->cost = 0;
            w->shop_angry = true;
            g_sp_run.thief_level++;
            sp_set_message(w, "THIEF! STOP RIGHT THERE!", "", 90);
            sp_play_sfx(w->sfx_alert);
        }
    }

    /* Active whip hitbox check */
    if (w->whip_timer > 4 && w->whip_timer <= 11 && p->held_ent < 0) {
        int i;
        float wx0 = (w->whip_timer > 8) ? (p->x - p->facing * 14.0f) : (p->x + p->facing * 4.0f);
        float wx1 = wx0 + p->facing * 18.0f;
        float minx = wx0 < wx1 ? wx0 : wx1;
        float maxx = wx0 > wx1 ? wx0 : wx1;
        float miny = p->y - 8.0f;
        float maxy = p->y + 8.0f;
        for (i = 0; i < SP_MAX_ENTITIES; i++) {
            SpEntity *e = &w->ents[i];
            float ex0, ey0, ex1, ey1;
            if (!e->active || !e->alive || i == w->player_idx) continue;
            ex0 = e->x + e->bx; ey0 = e->y + e->by;
            ex1 = ex0 + e->bw;   ey1 = ey0 + e->bh;
            if (minx < ex1 && maxx > ex0 && miny < ey1 && maxy > ey0) {
                if (is_enemy_kind(e->kind)) {
                    hurt_enemy(w, i, 1, p->facing * 3.0f, -2.5f, true);
                } else if (e->kind == ENT_JAR) {
                    smash_jar(w, i);
                } else if (e->kind == ENT_CRATE) {
                    open_crate(w, e->x, e->y);
                    sp_destroy_entity(w, i);
                } else if (e->kind == ENT_CHEST) {
                    open_chest(w, i);
                }
            }
        }
    }
}

static void step_traps(SpWorld *w)
{
    int tx, ty, i;
    if (w->player_idx < 0) {
        return;
    }
    for (ty = 1; ty < w->rows - 1; ty++) {
        for (tx = 1; tx < w->cols - 1; tx++) {
            SpTile *t = &w->grid[ty][tx];
            if ((t->fg == TILE_ARROW_TRAP_L || t->fg == TILE_ARROW_TRAP_R) && !(t->flags & TF_FIRED)) {
                int dir = (t->fg == TILE_ARROW_TRAP_R) ? 1 : -1;
                float ry = (float)ty * 16.0f + 8.0f;
                for (i = 0; i < SP_MAX_ENTITIES; i++) {
                    const SpEntity *e = &w->ents[i];
                    if (!e->active || !e->alive || (e->kind != ENT_PLAYER && !is_enemy_kind(e->kind))) continue;
                    if (fabsf(e->y - ry) < 10.0f) {
                        float dx = (e->x - (float)tx * 16.0f) * (float)dir;
                        if (dx > 0.0f && dx < 96.0f) {
                            int ar = sp_spawn_entity(w, ENT_ARROW, (float)tx * 16.0f + (dir > 0 ? 16.0f : 0.0f), ry);
                            t->flags |= TF_FIRED;
                            if (ar >= 0) {
                                w->ents[ar].anim_id = (dir > 0) ? SP_ANIM_ARROW_RIGHT : SP_ANIM_ARROW_LEFT;
                                w->ents[ar].facing = (int8_t)dir;
                                w->ents[ar].vx = (float)dir * 7.5f;
                                w->ents[ar].vy = 0.0f;
                                w->ents[ar].bx = -4; w->ents[ar].by = -2;
                                w->ents[ar].bw = 8; w->ents[ar].bh = 4;
                                w->ents[ar].safe = false;
                                w->ents[ar].z = SP_Z_FX;
                                sp_play_sfx(w->sfx_arrowtrap);
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
}

static void step_enemies_and_items(SpWorld *w)
{
    int i;
    SpEntity *pl = (w->player_idx >= 0) ? &w->ents[w->player_idx] : NULL;

    for (i = 0; i < SP_MAX_ENTITIES; i++) {
        SpEntity *e = &w->ents[i];
        if (!e->active || i == w->player_idx) {
            continue;
        }
        e->anim_frame += e->anim_speed;

        /* FX particles */
        if (e->kind == ENT_FX || e->kind == ENT_EXPLOSION) {
            e->x += e->vx;
            e->y += e->vy;
            if (e->subtype == FX_BLOOD || e->subtype == FX_RUBBLE) {
                e->vy += 0.25f;
            }
            if (--e->timer <= 0) {
                sp_destroy_entity(w, i);
            }
            continue;
        }

        /* Armed bomb */
        if (e->kind == ENT_BOMB) {
            bool hx = false, hy = false;
            if (g_sp_run.passives & PASS_PASTE) {
                /* Sticky bomb stops on first contact */
                if (!sp_entity_hits_solid(w, e, e->vx, e->vy)) {
                    e->vy += 0.45f;
                    sp_move_entity(w, i, e->vx, e->vy, true, &hx, &hy);
                } else {
                    e->vx = 0.0f;
                    e->vy = 0.0f;
                }
            } else {
                e->vy += 0.45f;
                if (e->vy > 6.0f) e->vy = 6.0f;
                sp_move_entity(w, i, e->vx, e->vy, true, &hx, &hy);
                if (hx) e->vx = -e->vx * 0.5f;
                if (hy) { e->vx *= 0.7f; e->vy = -e->vy * 0.3f; if (fabsf(e->vy) < 0.7f) e->vy = 0.0f; }
            }
            if (--e->timer <= 0) {
                float ex = e->x, ey = e->y;
                sp_destroy_entity(w, i);
                sp_explode(w, ex, ey, 44, true);
            }
            continue;
        }

        /* Rope unfurl */
        if (e->kind == ENT_ROPE_THROW) {
            int tx = (int)floorf(e->x / 16.0f);
            int ty = (int)floorf(e->y / 16.0f);
            if (e->subtype == 0) {
                e->y += e->vy;
                ty = (int)floorf(e->y / 16.0f);
                if (!sp_tile_in_bounds(w, tx, ty) || sp_tile_is_solid(w, tx, ty) || --e->timer <= 0) {
                    int k;
                    if (sp_tile_is_solid(w, tx, ty)) ty++;
                    for (k = 0; k < 7 && sp_tile_in_bounds(w, tx, ty + k) && !sp_tile_is_solid(w, tx, ty + k); k++) {
                        w->grid[ty + k][tx].bg = (k == 0) ? BG_ROPE_TOP : BG_ROPE;
                        w->grid[ty + k][tx].bg_anim = (k == 0) ? SP_ANIM_ROPE_TOP : SP_ANIM_ROPE;
                        w->grid[ty + k][tx].flags |= TF_CLIMBABLE;
                    }
                    sp_destroy_entity(w, i);
                }
            } else {
                int k;
                for (k = 1; k <= 7 && sp_tile_in_bounds(w, tx, ty + k) && !sp_tile_is_solid(w, tx, ty + k); k++) {
                    w->grid[ty + k][tx].bg = (k == 1) ? BG_ROPE_TOP : BG_ROPE;
                    w->grid[ty + k][tx].bg_anim = (k == 1) ? SP_ANIM_ROPE_TOP : SP_ANIM_ROPE;
                    w->grid[ty + k][tx].flags |= TF_CLIMBABLE;
                }
                sp_destroy_entity(w, i);
            }
            continue;
        }

        /* Projectiles (bullet, arrow, laser, web shot) */
        if (e->kind == ENT_BULLET || e->kind == ENT_ARROW || e->kind == ENT_LASER || e->kind == ENT_WEB_SHOT) {
            bool hx = false, hy = false;
            int j;
            if (e->kind == ENT_ARROW || e->kind == ENT_WEB_SHOT) {
                e->vy += 0.15f;
            }
            if (e->timer > 0) e->timer--;
            sp_move_entity(w, i, e->vx, e->vy, false, &hx, &hy);
            if (hx || hy) {
                if (e->kind == ENT_LASER) {
                    float ex = e->x, ey = e->y;
                    sp_destroy_entity(w, i);
                    sp_explode(w, ex, ey, 28, true);
                } else if (e->kind == ENT_ARROW && (fabsf(e->vx) > 2.0f || fabsf(e->vy) > 2.0f)) {
                    sp_play_sfx(w->sfx_thud);
                    e->vx = 0.0f; e->vy = 0.0f;
                    sp_destroy_entity(w, i);
                } else {
                    sp_destroy_entity(w, i);
                }
                continue;
            }
            for (j = 0; j < SP_MAX_ENTITIES; j++) {
                SpEntity *t = &w->ents[j];
                if (!t->active || !t->alive || j == i) continue;
                if (!sp_entities_overlap(e, t)) continue;
                if (t->kind == ENT_PLAYER && (!e->safe || e->timer == 0)) {
                    sp_hurt_player(w, (e->kind == ENT_ARROW || e->kind == ENT_BULLET) ? 2 : 1,
                                   e->kind == ENT_ARROW ? "SHOT BY AN ARROW" : "SHOT BY A BULLET",
                                   e->vx > 0 ? 3.0f : -3.0f, -3.0f);
                    sp_destroy_entity(w, i);
                    break;
                } else if (is_enemy_kind(t->kind) && (e->safe || e->kind == ENT_ARROW)) {
                    hurt_enemy(w, j, (e->kind == ENT_ARROW) ? 2 : 3, e->vx > 0 ? 3.0f : -3.0f, -2.5f, true);
                    sp_destroy_entity(w, i);
                    break;
                }
            }
            if (e->kind == ENT_BULLET && e->timer <= 0) {
                sp_destroy_entity(w, i);
            }
            continue;
        }

        /* Collectibles / Treasure / Crates / Chests / Holdables */
        if (e->kind == ENT_TREASURE || e->kind == ENT_CRATE || e->kind == ENT_CHEST ||
            e->kind == ENT_LOCKED_CHEST || is_holdable_kind(e->kind)) {
            if (e->held) {
                continue;
            }
            if (sp_tile_is_solid(w, (int)floorf(e->x / 16.0f), (int)floorf(e->y / 16.0f))) {
                e->vx = 0.0f;
                e->vy = 0.0f;
                continue; /* buried inside dirt/rock until bombed or mined */
            }
            e->vy += 0.45f;
            if (e->vy > 6.0f) e->vy = 6.0f;
            {
                bool hx = false, hy = false;
                sp_move_entity(w, i, e->vx, e->vy, true, &hx, &hy);
                if (hx) {
                    if (e->kind == ENT_JAR && e->armed) {
                        smash_jar(w, i);
                        continue;
                    }
                    e->vx = -e->vx * 0.4f;
                }
                if (hy) {
                    if (e->kind == ENT_JAR && e->armed && e->vy > 2.5f) {
                        smash_jar(w, i);
                        continue;
                    }
                    e->vx *= 0.65f;
                    e->vy = 0.0f;
                    e->armed = false;
                }
            }

            /* Thrown holdable hits enemies */
            if (e->armed && (fabsf(e->vx) > 2.0f || fabsf(e->vy) > 2.0f)) {
                int j;
                for (j = 0; j < SP_MAX_ENTITIES; j++) {
                    SpEntity *t = &w->ents[j];
                    if (!t->active || !t->alive || !is_enemy_kind(t->kind)) continue;
                    if (sp_entities_overlap(e, t)) {
                        hurt_enemy(w, j, 1, e->vx > 0 ? 3.0f : -3.0f, -2.5f, true);
                        if (e->kind == ENT_JAR) {
                            smash_jar(w, i);
                        } else {
                            e->vx = -e->vx * 0.4f;
                            e->armed = false;
                        }
                        break;
                    }
                }
            }

            if (!e->active) continue;

            /* Player touch pickup */
            if (pl && pl->alive && sp_entities_overlap(e, pl)) {
                if (e->kind == ENT_TREASURE) {
                    add_money(w, e->value);
                    sp_play_sfx((e->subtype <= TR_GOLD_BARS) ? w->sfx_coin : w->sfx_gem);
                    sp_destroy_entity(w, i);
                } else if (e->kind == ENT_SHOP_ITEM && !e->for_sale) {
                    grant_passive(w, e->subtype);
                    sp_destroy_entity(w, i);
                } else if (e->kind == ENT_LOCKED_CHEST && pl->held_ent >= 0 &&
                           w->ents[pl->held_ent].kind == ENT_KEY) {
                    sp_destroy_entity(w, pl->held_ent);
                    pl->held_ent = -1;
                    g_sp_run.held_item_kind = ENT_NONE;
                    grant_passive(w, ITEM_UDJAT_EYE);
                    sp_destroy_entity(w, i);
                } else if (e->kind == ENT_GOLD_IDOL && !e->armed && e->timer == 0) {
                    e->timer = 1;
                    w->shake_ticks = 20;
                    sp_set_message(w, "YOU TRIGGERED AN ANCIENT TRAP!", "", 90);
                    sp_play_sfx(w->sfx_slam);
                }
            }
            continue;
        }

        /* Olmec Boss */
        if (e->kind == ENT_OLMEC) {
            int ptx, pty;
            e->vy += 0.45f;
            if (e->vy > 6.0f) e->vy = 6.0f;
            if (e->timer > 0) {
                e->timer--;
            } else if (pl && pl->alive) {
                e->vx = (pl->x < e->x + 32.0f) ? -1.4f : 1.4f;
                if (sp_entity_on_ground(w, e) && fabsf(pl->x - (e->x + 32.0f)) < 48.0f) {
                    e->vy = -7.5f;
                    e->timer = 45;
                }
            }
            {
                bool hx = false, hy = false;
                sp_move_entity(w, i, e->vx, e->vy, false, &hx, &hy);
                if (hx) e->vx = 0.0f;
                if (hy && e->vy > 2.5f) {
                    /* Olmec crushes blocks underneath! */
                    int cx0 = (int)floorf(e->x / 16.0f);
                    int cy = (int)floorf((e->y + 65.0f) / 16.0f);
                    int k;
                    w->shake_ticks = 10;
                    sp_play_sfx(w->sfx_slam);
                    for (k = 0; k < 4; k++) {
                        sp_destroy_tile(w, cx0 + k, cy, true);
                    }
                    sp_autotile_world(w);
                    e->vy = 0.0f;
                    e->vx = 0.0f;
                }
            }
            ptx = (int)floorf((e->x + 32.0f) / 16.0f);
            pty = (int)floorf((e->y + 48.0f) / 16.0f);
            if (sp_tile_is_lava(w, ptx, pty)) {
                e->vx = 0.0f;
                e->vy = 0.35f;
                e->y += 0.35f;
                if (!e->armed) {
                    e->armed = true;
                    sp_set_message(w, "OLMEC IS SINKING!", "HEAD FOR THE GOLDEN DOOR!", 150);
                    sp_play_sfx(w->sfx_explosion);
                }
                if (e->y > (float)pty * 16.0f + 40.0f) {
                    sp_destroy_entity(w, i);
                }
            } else if (pl && pl->alive && sp_entities_overlap(e, pl)) {
                sp_hurt_player(w, 99, "CRUSHED BY OLMEC", 0.0f, -2.0f);
            }
            continue;
        }

        /* Ghost */
        if (e->kind == ENT_GHOST) {
            if (pl && pl->alive) {
                float dx = pl->x - (e->x + 16.0f);
                float dy = pl->y - (e->y + 16.0f);
                float d = sqrtf(dx * dx + dy * dy);
                if (d > 1.0f) {
                    e->x += (dx / d) * 1.1f;
                    e->y += (dy / d) * 1.1f;
                }
                e->facing = (dx >= 0.0f) ? 1 : -1;
                e->anim_id = (e->facing < 0) ? SP_ANIM_GHOST_LEFT : SP_ANIM_GHOST_RIGHT;
                if (sp_entities_overlap(e, pl)) {
                    sp_hurt_player(w, 99, "TOUCHED BY THE GHOST", 0.0f, -2.0f);
                }
            }
            continue;
        }

        /* Standard enemies & NPCs */
        if (!e->alive) {
            continue;
        }
        if (e->state == ST_STUNNED) {
            e->vy += 0.5f;
            if (e->vy > 6.0f) e->vy = 6.0f;
            bool hx = false, hy = false;
            sp_move_entity(w, i, e->vx, e->vy, true, &hx, &hy);
            if (hx) e->vx = -e->vx * 0.5f;
            if (hy) { e->vx *= 0.7f; e->vy = 0.0f; }
            if (--e->timer <= 0) {
                e->state = ST_WALK;
            }
            continue;
        }

        /* Flying enemies: Bat, UFO, Vampire-bat */
        if (e->kind == ENT_BAT || e->kind == ENT_UFO) {
            if (e->state == ST_HANG && pl && pl->alive) {
                float dx = pl->x - (e->x + 8.0f);
                float dy = pl->y - (e->y + 8.0f);
                if (fabsf(dx) < 88.0f && dy > 0.0f && dy < 110.0f) {
                    e->state = ST_WALK;
                    sp_play_sfx(e->kind == ENT_BAT ? w->sfx_bat : w->sfx_alien);
                }
            } else if (pl && pl->alive) {
                if (e->kind == ENT_BAT) {
                    float dx = pl->x - (e->x + 8.0f);
                    float dy = pl->y - (e->y + 8.0f);
                    float d = sqrtf(dx * dx + dy * dy);
                    if (d > 1.0f) {
                        e->vx = (dx / d) * 1.4f;
                        e->vy = (dy / d) * 1.4f;
                    }
                    e->facing = (e->vx >= 0.0f) ? 1 : -1;
                    e->anim_id = (e->facing < 0) ? SP_ANIM_BAT_LEFT : SP_ANIM_BAT_RIGHT;
                } else {
                    if (e->vx == 0.0f) e->vx = 1.6f;
                    if (++e->timer >= 90) {
                        int ls = sp_spawn_entity(w, ENT_LASER, e->x + 8.0f, e->y + 16.0f);
                        e->timer = 0;
                        if (ls >= 0) {
                            w->ents[ls].anim_id = SP_ANIM_LASER;
                            w->ents[ls].vy = 4.5f;
                            w->ents[ls].bx = -3; w->ents[ls].by = -3; w->ents[ls].bw = 6; w->ents[ls].bh = 6;
                            w->ents[ls].z = SP_Z_FX;
                            sp_play_sfx(w->sfx_laser);
                        }
                    }
                }
                bool hx = false, hy = false;
                sp_move_entity(w, i, e->vx, e->vy, false, &hx, &hy);
                if (hx) e->vx = -e->vx;
                if (hy) e->vy = -e->vy;
            }
        } else if (e->kind == ENT_SPIDER || e->kind == ENT_GIANT_SPIDER) {
            if (e->state == ST_HANG && pl && pl->alive) {
                if (fabsf(pl->x - (e->x + e->bw * 0.5f)) < 24.0f && pl->y > e->y && pl->y - e->y < 110.0f) {
                    e->state = ST_WALK;
                    e->vy = 1.0f;
                    sp_play_sfx(e->kind == ENT_GIANT_SPIDER ? w->sfx_giantspider : w->sfx_spiderjump);
                }
            } else {
                e->vy += 0.45f;
                if (e->vy > 5.5f) e->vy = 5.5f;
                bool hx = false, hy = false;
                sp_move_entity(w, i, e->vx, e->vy, true, &hx, &hy);
                if (hx) e->vx = -e->vx;
                if (hy) {
                    e->vy = 0.0f;
                    e->vx = 0.0f;
                    if (++e->timer >= 35 && pl && pl->alive) {
                        e->timer = 0;
                        e->vx = (pl->x < e->x) ? -2.2f : 2.2f;
                        e->vy = -5.2f;
                        sp_play_sfx(e->kind == ENT_GIANT_SPIDER ? w->sfx_giantspider : w->sfx_spiderjump);
                    }
                }
                e->anim_id = (e->kind == ENT_GIANT_SPIDER)
                    ? (hy ? SP_ANIM_GIANT_SPIDER : SP_ANIM_GIANT_SPIDER_JUMP)
                    : SP_ANIM_SPIDER;
            }
        } else {
            /* Ground patrolling & charging enemies (Snake, Caveman, Frog, Yeti, Shopkeeper, Damsel...) */
            float speed = 1.0f;
            bool angry = (e->state == ST_ANGRY);
            if (e->kind == ENT_DAMSEL) {
                speed = 0.0f;
            } else if (e->kind == ENT_SHOPKEEPER) {
                speed = angry ? 3.0f : 0.0f;
                if (w->shop_angry && !angry) {
                    e->state = ST_ANGRY;
                    angry = true;
                }
                if (angry && pl && pl->alive) {
                    e->facing = (pl->x < e->x) ? -1 : 1;
                    if (++e->timer >= 45 && fabsf(pl->y - e->y) < 32.0f) {
                        int b = sp_spawn_entity(w, ENT_BULLET, e->x + 8.0f + e->facing * 8.0f, e->y + 8.0f);
                        e->timer = 0;
                        if (b >= 0) {
                            w->ents[b].anim_id = SP_ANIM_BULLET;
                            w->ents[b].vx = e->facing * 7.5f;
                            w->ents[b].bx = -2; w->ents[b].by = -2; w->ents[b].bw = 4; w->ents[b].bh = 4;
                            w->ents[b].timer = 20;
                            w->ents[b].z = SP_Z_FX;
                            sp_play_sfx(w->sfx_shotgun);
                        }
                    }
                    if (sp_entity_on_ground(w, e) && sp_rand_range(w, 1, 35) == 1) {
                        e->vy = -5.5f;
                    }
                }
            } else if (e->kind == ENT_CAVEMAN || e->kind == ENT_YETI || e->kind == ENT_HAWKMAN || e->kind == ENT_VAMPIRE) {
                if (pl && pl->alive && fabsf(pl->y - e->y) < 20.0f && fabsf(pl->x - e->x) < 110.0f) {
                    e->state = ST_ANGRY;
                    e->facing = (pl->x < e->x) ? -1 : 1;
                }
                speed = (e->state == ST_ANGRY) ? 2.4f : 1.0f;
            } else if (e->kind == ENT_FROG || e->kind == ENT_FIRE_FROG) {
                speed = 0.0f;
                if (sp_entity_on_ground(w, e) && ++e->timer >= 40) {
                    e->timer = 0;
                    if (pl && fabsf(pl->x - e->x) < 96.0f) e->facing = (pl->x < e->x) ? -1 : 1;
                    else e->facing = (sp_rand_range(w, 0, 1) == 0) ? -1 : 1;
                    e->vx = (float)e->facing * 2.0f;
                    e->vy = -4.8f;
                    sp_play_sfx(w->sfx_frog);
                }
            }

            if (speed > 0.0f) {
                int ahead_tx = (int)floorf((e->x + 8.0f + e->facing * 10.0f) / 16.0f);
                int foot_ty = (int)floorf((e->y + 18.0f) / 16.0f);
                if (!angry && !sp_tile_is_solid(w, ahead_tx, foot_ty)) {
                    e->facing = -e->facing;
                }
                e->vx = (float)e->facing * speed;
            } else if (e->kind != ENT_FROG && e->kind != ENT_FIRE_FROG) {
                e->vx *= 0.8f;
            }

            e->vy += 0.5f;
            if (e->vy > 6.0f) e->vy = 6.0f;
            {
                bool hx = false, hy = false;
                sp_move_entity(w, i, e->vx, e->vy, true, &hx, &hy);
                if (hx) {
                    e->facing = -e->facing;
                    e->vx = 0.0f;
                }
                if (hy) {
                    e->vy = 0.0f;
                    if (e->kind == ENT_FROG || e->kind == ENT_FIRE_FROG) e->vx = 0.0f;
                }
            }
        }

        /* Player <-> Enemy collision: stomp from above or take damage! */
        if (pl && pl->alive && e->kind != ENT_DAMSEL && sp_entities_overlap(e, pl)) {
            bool stomping = (pl->vy > 0.5f) && (pl->y + 4.0f < e->y + e->bh * 0.5f);
            if (stomping && e->kind != ENT_SHOPKEEPER) {
                int stomp_dmg = (g_sp_run.passives & PASS_SPIKE) ? 3 : 1;
                pl->vy = -5.6f;
                hurt_enemy(w, i, stomp_dmg, (float)pl->facing * 1.5f, -2.0f, true);
            } else if (w->invuln_timer == 0) {
                const char *cause = (e->kind == ENT_SNAKE) ? "BITTEN BY A SNAKE"
                                  : (e->kind == ENT_BAT) ? "BITTEN BY A BAT"
                                  : (e->kind == ENT_SPIDER || e->kind == ENT_GIANT_SPIDER) ? "EATEN BY A SPIDER"
                                  : (e->kind == ENT_SHOPKEEPER) ? "BLASTED BY THE SHOPKEEPER"
                                  : "KILLED BY A MONSTER";
                sp_hurt_player(w, 1, cause, (pl->x < e->x + 8.0f) ? -3.0f : 3.0f, -4.0f);
            }
        }
    }
}

void sp_step_world(SpWorld *w, const SpInput *in, bool allow_exit)
{
    g_sp_run.time_ticks++;
    g_sp_run.xtime_ticks++;

    /* Spawn the Ghost after 2:30 (4500 ticks at 30Hz) in normal levels */
    if (!g_sp_run.is_tutorial && g_sp_run.curr_level > 0 && !g_sp_run.ghost_exists &&
        g_sp_run.xtime_ticks >= 4500) {
        int gh;
        g_sp_run.ghost_exists = true;
        gh = sp_spawn_entity(w, ENT_GHOST, -16.0f, w->cam_y + 120.0f);
        if (gh >= 0) {
            w->ents[gh].anim_id = SP_ANIM_GHOST_RIGHT;
            w->ents[gh].bx = 4; w->ents[gh].by = 4; w->ents[gh].bw = 24; w->ents[gh].bh = 24;
            w->ents[gh].invincible = true;
            w->ents[gh].z = SP_Z_FX;
        }
        sp_set_message(w, "A CHILL RUNS UP YOUR SPINE...", "THE GHOST IS COMING!", 120);
        sp_play_sfx(w->sfx_ghost);
    }

    step_player(w, in, allow_exit);
    step_traps(w);
    step_enemies_and_items(w);

    /* Camera smoothly tracks player */
    if (w->player_idx >= 0) {
        const SpEntity *p = &w->ents[w->player_idx];
        float target_x = p->x - 160.0f;
        float target_y = p->y - 120.0f;
        float max_x = (float)(w->cols * 16 - SP_VIRT_W);
        float max_y = (float)(w->rows * 16 - SP_VIRT_H);

        if (target_x < 0.0f) target_x = 0.0f;
        if (target_y < 0.0f) target_y = 0.0f;
        if (target_x > max_x) target_x = max_x;
        if (target_y > max_y) target_y = max_y;

        w->cam_x += (target_x - w->cam_x) * 0.25f;
        w->cam_y += (target_y - w->cam_y) * 0.25f;
    }

    if (w->shake_ticks > 0) {
        w->shake_ticks--;
        w->shake_toggle = !w->shake_toggle;
    }
}

void sp_render_entities(SpWorld *w)
{
    int i;
    for (i = 0; i < SP_MAX_ENTITIES; i++) {
        const SpEntity *e = &w->ents[i];
        uint32_t mod = 0xFFFFFFFFu;
        bool flip = (e->facing < 0);
        if (!e->active || e->anim_id >= SP_ANIM_COUNT) {
            continue;
        }
        /* GDSpelunky's player & enemy _left sprites face LEFT natively, so flip when facing RIGHT (+1)! */
        if (e->kind == ENT_PLAYER || e->kind == ENT_SNAKE || e->kind == ENT_CAVEMAN ||
            e->kind == ENT_SKELETON || e->kind == ENT_ZOMBIE || e->kind == ENT_VAMPIRE ||
            e->kind == ENT_FROG || e->kind == ENT_FIRE_FROG || e->kind == ENT_MAN_TRAP ||
            e->kind == ENT_YETI || e->kind == ENT_HAWKMAN || e->kind == ENT_MONKEY ||
            e->kind == ENT_SHOPKEEPER || e->kind == ENT_DAMSEL) {
            flip = (e->facing > 0);
        }
        if (e->kind == ENT_PLAYER && (w->invuln_timer & 2)) {
            mod = 0xFFFFFF66u;
        }
        if ((e->kind == ENT_TREASURE || is_holdable_kind(e->kind)) && !e->held) {
            if (sp_tile_is_solid(w, (int)floorf(e->x / 16.0f), (int)floorf(e->y / 16.0f))) {
                if (!(g_sp_run.passives & (PASS_SPECTACLES | PASS_UDJAT))) {
                    continue; /* hidden inside solid rock until mined/bombed or wearing glasses/udjat */
                }
                mod = 0xFFFFFF88u;
            }
        }
        {
            const SpelunkyAnim *an = &g_sp_anims[e->anim_id];
            float dx = e->x;
            float dy = e->y;
            if (e->bx < 0 && an->frame_count > 0) {
                const SpelunkyFrame *fr = &g_sp_frames[an->first_frame];
                if (fr->ox == 0 && fr->oy == 0) {
                    dx -= (float)fr->w * 0.5f;
                    dy -= (float)fr->h * 0.5f;
                }
            }
            sp_draw_anim_at(w, e->anim_id, (int)e->anim_frame, dx, dy, flip, e->z, mod);
        }

        /* Draw whip arc alongside player */
        if (e->kind == ENT_PLAYER && w->whip_timer > 4 && e->held_ent < 0) {
            bool pre = (w->whip_timer > 8);
            uint16_t wa = pre
                ? (e->facing > 0 ? SP_ANIM_WHIP_PRE_L : SP_ANIM_WHIP_PRE_R)
                : (e->facing > 0 ? SP_ANIM_WHIP_RIGHT : SP_ANIM_WHIP_LEFT);
            float wx = pre ? (e->x - e->facing * 12.0f) : (e->x + e->facing * 14.0f);
            sp_draw_anim_at(w, wa, 12 - w->whip_timer, wx, e->y, false, SP_Z_PLAYER, 0xFFFFFFFFu);
        }
    }
}
