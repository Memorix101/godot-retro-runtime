/* Procedural level generation, room templates, autotiling, and sliding-window tile rendering. */
#include <math.h>
#include <string.h>

#include "spelunky.h"
#include "spelunky_rooms.h"

static void set_fg(SpWorld *w, int tx, int ty, uint8_t fg, uint8_t flags, uint16_t anim)
{
    if (!sp_tile_in_bounds(w, tx, ty)) {
        return;
    }
    w->grid[ty][tx].fg = fg;
    w->grid[ty][tx].flags = (w->grid[ty][tx].flags & TF_CLIMBABLE) | flags;
    w->grid[ty][tx].fg_anim = anim;
    w->grid[ty][tx].variant = 0;
}

static void set_bg(SpWorld *w, int tx, int ty, uint8_t bg, uint16_t anim, bool climbable, bool platform)
{
    if (!sp_tile_in_bounds(w, tx, ty)) {
        return;
    }
    w->grid[ty][tx].bg = bg;
    w->grid[ty][tx].bg_anim = anim;
    if (climbable) {
        w->grid[ty][tx].flags |= TF_CLIMBABLE;
    }
    if (platform) {
        w->grid[ty][tx].flags |= TF_PLATFORM;
    }
}

static void place_area_brick(SpWorld *w, int tx, int ty, uint8_t extra_flags)
{
    int area = g_sp_run.level_type;
    if (area == SP_AREA_LUSH) {
        set_fg(w, tx, ty, TILE_LUSH, TF_SOLID | extra_flags, SP_ANIM_LUSH);
    } else if (area == SP_AREA_ICE) {
        set_fg(w, tx, ty, TILE_ICE, TF_SOLID | extra_flags, SP_ANIM_ICE);
    } else if (area == SP_AREA_TEMPLE || area == SP_AREA_OLMEC) {
        set_fg(w, tx, ty, TILE_TEMPLE, TF_SOLID | extra_flags, SP_ANIM_TEMPLE);
    } else {
        if (sp_rand_range(w, 1, 10) == 1) {
            set_fg(w, tx, ty, TILE_ROCK, TF_SOLID | extra_flags, SP_ANIM_BLOCK);
        } else {
            set_fg(w, tx, ty, TILE_BRICK, TF_SOLID | extra_flags, SP_ANIM_BRICK);
        }
    }
}

static int spawn_treasure_at(SpWorld *w, uint8_t sub, float x, float y)
{
    int idx = sp_spawn_entity(w, ENT_TREASURE, x, y);
    if (idx < 0) {
        return -1;
    }
    SpEntity *e = &w->ents[idx];
    e->subtype = sub;
    e->bx = -5;
    e->by = -5;
    e->bw = 10;
    e->bh = 10;
    e->z = SP_Z_ITEM;
    switch (sub) {
    case TR_GOLD_CHUNK:   e->anim_id = SP_ANIM_GOLD_CHUNK;   e->value = 100;  break;
    case TR_GOLD_NUGGET:  e->anim_id = SP_ANIM_GOLD_NUGGET;  e->value = 500;  break;
    case TR_GOLD_BAR:     e->anim_id = SP_ANIM_GOLD_BAR;     e->value = 500;  break;
    case TR_GOLD_BARS:    e->anim_id = SP_ANIM_GOLD_BARS;    e->value = 1500; break;
    case TR_EMERALD:      e->anim_id = SP_ANIM_EMERALD;      e->value = 800;  break;
    case TR_SAPPHIRE:     e->anim_id = SP_ANIM_SAPPHIRE;     e->value = 1200; break;
    case TR_RUBY:         e->anim_id = SP_ANIM_RUBY;         e->value = 1600; break;
    case TR_DIAMOND:      e->anim_id = SP_ANIM_DIAMOND;      e->value = 5000; break;
    case TR_EMERALD_BIG:  e->anim_id = SP_ANIM_EMERALD_BIG;  e->value = 2400; break;
    case TR_SAPPHIRE_BIG: e->anim_id = SP_ANIM_SAPPHIRE_BIG; e->value = 3600; break;
    case TR_RUBY_BIG:     e->anim_id = SP_ANIM_RUBY_BIG;     e->value = 4800; break;
    case TR_SCARAB:       e->anim_id = SP_ANIM_SCARAB;       e->value = 5000; break;
    default:              e->anim_id = SP_ANIM_GOLD_CHUNK;   e->value = 100;  break;
    }
    return idx;
}

static int spawn_holdable(SpWorld *w, uint8_t kind, float x, float y, bool for_sale, int cost)
{
    int idx = sp_spawn_entity(w, kind, x, y);
    if (idx < 0) {
        return -1;
    }
    SpEntity *e = &w->ents[idx];
    e->for_sale = for_sale;
    e->cost = (int16_t)cost;
    e->bx = -5;
    e->by = -5;
    e->bw = 10;
    e->bh = 10;
    e->z = SP_Z_ITEM;
    switch (kind) {
    case ENT_ROCK:          e->anim_id = SP_ANIM_ROCK;          break;
    case ENT_JAR:           e->anim_id = SP_ANIM_JAR;           break;
    case ENT_SKULL:         e->anim_id = SP_ANIM_SKULL;         break;
    case ENT_KEY:           e->anim_id = SP_ANIM_KEY_RIGHT;     break;
    case ENT_GOLD_IDOL:     e->anim_id = SP_ANIM_GOLD_IDOL;     e->value = 5000; break;
    case ENT_CRYSTAL_SKULL: e->anim_id = SP_ANIM_CRYSTAL_SKULL; e->value = 15000; break;
    case ENT_MACHETE:       e->anim_id = SP_ANIM_MACHETE_RIGHT; break;
    case ENT_MATTOCK:       e->anim_id = SP_ANIM_MATTOCK_RIGHT; e->hp = 35; break;
    case ENT_PISTOL:        e->anim_id = SP_ANIM_PISTOL_RIGHT;  break;
    case ENT_SHOTGUN:       e->anim_id = SP_ANIM_SHOTGUN_RIGHT; break;
    case ENT_BOW:           e->anim_id = SP_ANIM_BOW_RIGHT;     break;
    case ENT_WEB_CANNON:    e->anim_id = SP_ANIM_WEB_CANNON_R;  break;
    case ENT_TELEPORTER:    e->anim_id = SP_ANIM_TELEPORTER;    break;
    case ENT_SCEPTRE:       e->anim_id = SP_ANIM_SCEPTRE_RIGHT; break;
    case ENT_FLARE:         e->anim_id = SP_ANIM_FLARE;         break;
    case ENT_DICE:          e->anim_id = SP_ANIM_DICE1;         break;
    default:                e->anim_id = SP_ANIM_ROCK;          break;
    }
    return idx;
}

static int spawn_shop_item(SpWorld *w, uint8_t sub, float x, float y, bool for_sale, int cost)
{
    int idx = sp_spawn_entity(w, ENT_SHOP_ITEM, x, y);
    if (idx < 0) {
        return -1;
    }
    SpEntity *e = &w->ents[idx];
    e->subtype = sub;
    e->for_sale = for_sale;
    e->cost = (int16_t)cost;
    e->bx = -6;
    e->by = -6;
    e->bw = 12;
    e->bh = 12;
    e->z = SP_Z_ITEM;
    switch (sub) {
    case ITEM_BOMB_BAG:     e->anim_id = SP_ANIM_BOMB_BAG;     break;
    case ITEM_BOMB_BOX:     e->anim_id = SP_ANIM_BOMB_BOX;     break;
    case ITEM_ROPE_PILE:    e->anim_id = SP_ANIM_ROPE_PILE;    break;
    case ITEM_PASTE:        e->anim_id = SP_ANIM_PASTE;        break;
    case ITEM_COMPASS:      e->anim_id = SP_ANIM_COMPASS;      break;
    case ITEM_SPRING_SHOES: e->anim_id = SP_ANIM_SPRING_SHOES; break;
    case ITEM_SPIKE_SHOES:  e->anim_id = SP_ANIM_SPIKE_SHOES;  break;
    case ITEM_GLOVES:       e->anim_id = SP_ANIM_GLOVE;        break;
    case ITEM_MITT:         e->anim_id = SP_ANIM_MITT;         break;
    case ITEM_SPECTACLES:   e->anim_id = SP_ANIM_SPECTACLES;   break;
    case ITEM_CAPE:         e->anim_id = SP_ANIM_CAPE;         break;
    case ITEM_JETPACK:      e->anim_id = SP_ANIM_JETPACK_LEFT; break;
    case ITEM_PARACHUTE:    e->anim_id = SP_ANIM_PARA_PICKUP;  break;
    case ITEM_UDJAT_EYE:    e->anim_id = SP_ANIM_UDJAT_EYE;    break;
    case ITEM_ANKH:         e->anim_id = SP_ANIM_ANKH;         break;
    case ITEM_CROWN:        e->anim_id = SP_ANIM_CROWN;        break;
    case ITEM_KAPALA:       e->anim_id = SP_ANIM_KAPALA;       break;
    default:                e->anim_id = SP_ANIM_BOMB_BAG;     break;
    }
    return idx;
}

static int spawn_enemy(SpWorld *w, uint8_t kind, float x, float y)
{
    int idx = sp_spawn_entity(w, kind, x, y);
    if (idx < 0) {
        return -1;
    }
    SpEntity *e = &w->ents[idx];
    e->z = SP_Z_ENEMY;
    e->facing = (sp_rand_range(w, 0, 1) == 0) ? -1 : 1;
    e->bx = 2; e->by = 2; e->bw = 12; e->bh = 14;
    e->hp = 1;
    switch (kind) {
    case ENT_SNAKE:
        e->anim_id = SP_ANIM_SNAKE_WALK_L;
        e->bx = 2; e->by = 2; e->bw = 12; e->bh = 14;
        break;
    case ENT_BAT:
        e->anim_id = SP_ANIM_BAT_HANG;
        e->state = ST_HANG;
        e->bx = 2; e->by = 2; e->bw = 12; e->bh = 12;
        break;
    case ENT_SPIDER:
        e->anim_id = SP_ANIM_SPIDER_HANG;
        e->state = ST_HANG;
        e->bx = 2; e->by = 4; e->bw = 12; e->bh = 12;
        break;
    case ENT_GIANT_SPIDER:
        e->anim_id = SP_ANIM_GIANT_SPIDER_HANG;
        e->state = ST_HANG;
        e->hp = 10;
        e->bx = 2; e->by = 2; e->bw = 28; e->bh = 28;
        break;
    case ENT_CAVEMAN:
        e->anim_id = SP_ANIM_CAVEMAN_RUN_LEFT;
        e->hp = 3;
        e->bx = 2; e->by = 2; e->bw = 12; e->bh = 14;
        break;
    case ENT_SKELETON:
        e->anim_id = SP_ANIM_SKELETON_WALK_LEFT;
        e->hp = 1;
        break;
    case ENT_ZOMBIE:
        e->anim_id = SP_ANIM_ZOMBIE_LEFT;
        e->hp = 3;
        break;
    case ENT_VAMPIRE:
        e->anim_id = SP_ANIM_VAMPIRE_RUN_L;
        e->hp = 6;
        break;
    case ENT_FROG:
        e->anim_id = SP_ANIM_FROG_LEFT;
        e->hp = 1;
        break;
    case ENT_FIRE_FROG:
        e->anim_id = SP_ANIM_FIRE_FROG_LEFT;
        e->hp = 2;
        break;
    case ENT_PIRANHA:
        e->anim_id = SP_ANIM_PIRANHA_LEFT;
        e->hp = 1;
        break;
    case ENT_MAN_TRAP:
        e->anim_id = SP_ANIM_MAN_TRAP_LEFT;
        e->hp = 3;
        break;
    case ENT_YETI:
        e->anim_id = SP_ANIM_YETI_RUN_LEFT;
        e->hp = 5;
        break;
    case ENT_YETI_KING:
        e->anim_id = SP_ANIM_YETI_KING_WALK_L;
        e->hp = 10;
        e->bx = 4; e->by = 2; e->bw = 24; e->bh = 30;
        break;
    case ENT_ALIEN:
        e->anim_id = SP_ANIM_ALIEN;
        e->hp = 1;
        break;
    case ENT_UFO:
        e->anim_id = SP_ANIM_UFO;
        e->hp = 2;
        break;
    case ENT_HAWKMAN:
        e->anim_id = SP_ANIM_HAWK_RUN_LEFT;
        e->hp = 3;
        break;
    case ENT_MONKEY:
        e->anim_id = SP_ANIM_MONKEY_WALK_L;
        e->hp = 1;
        break;
    case ENT_TOMB_LORD:
        e->anim_id = SP_ANIM_TOMB_LORD_LEFT;
        e->hp = 10;
        e->bx = 4; e->by = 4; e->bw = 24; e->bh = 28;
        break;
    case ENT_MAGMA_MAN:
        e->anim_id = SP_ANIM_MAGMA_MAN_LEFT;
        e->hp = 4;
        break;
    case ENT_SHOPKEEPER:
        e->anim_id = SP_ANIM_SHOP_LEFT;
        e->hp = 10;
        e->facing = -1;
        break;
    case ENT_DAMSEL:
        e->anim_id = SP_ANIM_DAMSEL_LEFT;
        e->hp = 3;
        break;
    case ENT_OLMEC:
        e->anim_id = SP_ANIM_OLMEC;
        e->hp = 999;
        e->invincible = true;
        e->bx = 2; e->by = 2; e->bw = 60; e->bh = 60;
        break;
    default:
        break;
    }
    return idx;
}

static void generate_shop_item(SpWorld *w, int shop_style, float x, float y)
{
    int price_mult = 1000 + g_sp_run.curr_level * 250;
    int r = sp_rand_range(w, 0, 5);
    if (shop_style == 1) { /* bomb shop */
        if (r == 0 && !(g_sp_run.passives & PASS_PASTE)) {
            spawn_shop_item(w, ITEM_PASTE, x, y, true, price_mult * 2);
        } else if (r <= 2) {
            spawn_shop_item(w, ITEM_BOMB_BAG, x, y, true, price_mult);
        } else {
            spawn_shop_item(w, ITEM_BOMB_BOX, x, y, true, price_mult * 3);
        }
    } else if (shop_style == 2) { /* weapon shop */
        switch (r) {
        case 0: spawn_holdable(w, ENT_SHOTGUN, x, y, true, price_mult * 4); break;
        case 1: spawn_holdable(w, ENT_PISTOL, x, y, true, price_mult * 2); break;
        case 2: spawn_holdable(w, ENT_MACHETE, x, y, true, price_mult * 2); break;
        case 3: spawn_holdable(w, ENT_MATTOCK, x, y, true, price_mult * 2); break;
        case 4: spawn_holdable(w, ENT_BOW, x, y, true, price_mult); break;
        default: spawn_shop_item(w, ITEM_BOMB_BAG, x, y, true, price_mult); break;
        }
    } else if (shop_style == 3) { /* clothing / gear shop */
        switch (r) {
        case 0: spawn_shop_item(w, ITEM_SPRING_SHOES, x, y, true, price_mult * 2); break;
        case 1: spawn_shop_item(w, ITEM_SPIKE_SHOES, x, y, true, price_mult * 2); break;
        case 2: spawn_shop_item(w, ITEM_GLOVES, x, y, true, price_mult * 2); break;
        case 3: spawn_shop_item(w, ITEM_MITT, x, y, true, price_mult * 2); break;
        case 4: spawn_shop_item(w, ITEM_CAPE, x, y, true, price_mult * 4); break;
        default: spawn_shop_item(w, ITEM_PARACHUTE, x, y, true, price_mult); break;
        }
    } else if (shop_style == 4) { /* rare shop */
        switch (r) {
        case 0: spawn_shop_item(w, ITEM_JETPACK, x, y, true, price_mult * 6); break;
        case 1: spawn_holdable(w, ENT_TELEPORTER, x, y, true, price_mult * 4); break;
        case 2: spawn_shop_item(w, ITEM_COMPASS, x, y, true, price_mult * 2); break;
        case 3: spawn_shop_item(w, ITEM_SPECTACLES, x, y, true, price_mult * 2); break;
        default: spawn_holdable(w, ENT_WEB_CANNON, x, y, true, price_mult * 3); break;
        }
    } else { /* general */
        switch (r) {
        case 0: spawn_shop_item(w, ITEM_ROPE_PILE, x, y, true, price_mult); break;
        case 1: spawn_shop_item(w, ITEM_BOMB_BAG, x, y, true, price_mult); break;
        case 2: spawn_shop_item(w, ITEM_COMPASS, x, y, true, price_mult * 2); break;
        case 3: spawn_shop_item(w, ITEM_PARACHUTE, x, y, true, price_mult); break;
        case 4: spawn_holdable(w, ENT_MATTOCK, x, y, true, price_mult * 2); break;
        default: spawn_shop_item(w, ITEM_ROPE_PILE, x, y, true, price_mult); break;
        }
    }
}

static void expand_obstacles(SpWorld *w, char buf[81])
{
    int i;
    for (i = 0; i < 80; i++) {
        char c = buf[i];
        const char *o1 = NULL, *o2 = NULL, *o3 = NULL;
        if (c == '8') {
            int r = sp_rand_range(w, 1, 6);
            switch (r) {
            case 1: o1 = "00000"; o2 = "01110"; o3 = "11111"; break;
            case 2: o1 = "00000"; o2 = "02120"; o3 = "02120"; break;
            case 3: o1 = "00000"; o2 = "00000"; o3 = "22222"; break;
            case 4: o1 = "00000"; o2 = "11001"; o3 = "10001"; break;
            case 5: o1 = "11111"; o2 = "10001"; o3 = "40004"; break;
            default: o1 = "00000"; o2 = "12021"; o3 = "12021"; break;
            }
        } else if (c == '5') {
            int r = sp_rand_range(w, 1, 10);
            switch (r) {
            case 1: o1 = "11111"; o2 = "00000"; o3 = "00000"; break;
            case 2: o1 = "00000"; o2 = "02220"; o3 = "71117"; break;
            case 3: o1 = "00000"; o2 = "02020"; o3 = "71717"; break;
            case 4: o1 = "00000"; o2 = "22200"; o3 = "11100"; break;
            case 5: o1 = "00000"; o2 = "02220"; o3 = "01110"; break;
            case 6: o1 = "00000"; o2 = "00222"; o3 = "00111"; break;
            case 7: o1 = "11100"; o2 = "22200"; o3 = "00000"; break;
            case 8: o1 = "01110"; o2 = "02220"; o3 = "00000"; break;
            case 9: o1 = "00111"; o2 = "00222"; o3 = "00000"; break;
            default: o1 = "00000"; o2 = "02220"; o3 = "21112"; break;
            }
        } else if (c == '6') {
            int r = sp_rand_range(w, 1, 8);
            switch (r) {
            case 1: o1 = "11111"; o2 = "00000"; o3 = "00000"; break;
            case 2: o1 = "22222"; o2 = "00000"; o3 = "00000"; break;
            case 3: o1 = "11100"; o2 = "22200"; o3 = "00000"; break;
            case 4: o1 = "01110"; o2 = "02220"; o3 = "00000"; break;
            case 5: o1 = "00111"; o2 = "00222"; o3 = "00000"; break;
            case 6: o1 = "00000"; o2 = "01110"; o3 = "00000"; break;
            case 7: o1 = "00000"; o2 = "01110"; o3 = "02220"; break;
            default: o1 = "00000"; o2 = "02220"; o3 = "01110"; break;
            }
        } else if (c == 'V') {
            o1 = "0P0P0";
            o2 = "0L0L0";
            o3 = "0L0L0";
        }
        if (o1 != NULL) {
            int col = i % 10;
            int row = i / 10;
            int k;
            for (k = 0; k < 5 && col + k < 10; k++) {
                if (row < 8) buf[row * 10 + col + k] = o1[k];
                if (row + 1 < 8) buf[(row + 1) * 10 + col + k] = o2[k];
                if (row + 2 < 8) buf[(row + 2) * 10 + col + k] = o3[k];
            }
        }
    }
}

static const char *pick_room_template(SpWorld *w, int rx, int ry, int *out_shop_style)
{
    int area = g_sp_run.level_type;
    int rp = w->room_path[rx][ry];
    int rp_above = (ry > 0) ? w->room_path[rx][ry - 1] : 0;

    *out_shop_style = sp_rand_range(w, 0, 4);

    if (rx == w->start_room_x && ry == w->start_room_y) {
        if (area == SP_AREA_MINES) {
            int base = (rp == 2) ? 4 : 0;
            return g_rooms_mines_start[base + sp_rand_range(w, 0, 3)];
        } else if (area == SP_AREA_LUSH) {
            int base = (rp == 2) ? 2 : 0;
            return g_rooms_lush_start[base + sp_rand_range(w, 0, 1)];
        } else if (area == SP_AREA_ICE) {
            return g_rooms_ice_start[(rp == 2 && G_ROOMS_ICE_START_COUNT > 1) ? 1 : 0];
        } else {
            return g_rooms_temple_start[(rp == 2 && G_ROOMS_TEMPLE_START_COUNT > 1) ? 1 : 0];
        }
    }

    if (rx == w->end_room_x && ry == w->end_room_y) {
        if (area == SP_AREA_MINES) {
            int idx = (rp_above == 2) ? sp_rand_range(w, 1, 3) : sp_rand_range(w, 2, 5);
            return g_rooms_mines_end[idx % G_ROOMS_MINES_END_COUNT];
        } else if (area == SP_AREA_LUSH) {
            return g_rooms_lush_end[sp_rand_range(w, 0, G_ROOMS_LUSH_END_COUNT - 1)];
        } else if (area == SP_AREA_ICE) {
            return g_rooms_ice_end[0];
        } else {
            return g_rooms_temple_end[0];
        }
    }

    if (rp == 4) {
        w->shop_spawned = true;
        w->shop_x0 = 1 + rx * 10; w->shop_y0 = 1 + ry * 8;
        w->shop_x1 = w->shop_x0 + 9; w->shop_y1 = w->shop_y0 + 7;
        if (area == SP_AREA_LUSH) return g_rooms_lush_shop_left[0];
        if (area == SP_AREA_ICE) return g_rooms_ice_shop_left[0];
        if (area == SP_AREA_TEMPLE) return g_rooms_temple_shop_left[0];
        return g_rooms_mines_shop_left[0];
    }
    if (rp == 5) {
        w->shop_spawned = true;
        w->shop_x0 = 1 + rx * 10; w->shop_y0 = 1 + ry * 8;
        w->shop_x1 = w->shop_x0 + 9; w->shop_y1 = w->shop_y0 + 7;
        if (area == SP_AREA_LUSH) return g_rooms_lush_shop_right[0];
        if (area == SP_AREA_ICE) return g_rooms_ice_shop_right[0];
        if (area == SP_AREA_TEMPLE) return g_rooms_temple_shop_right[0];
        return g_rooms_mines_shop_right[0];
    }

    if (rp == 6) {
        if (area == SP_AREA_ICE) return g_rooms_ice_special[sp_rand_range(w, 0, G_ROOMS_ICE_SPECIAL_COUNT - 1)];
        if (area == SP_AREA_TEMPLE) return g_rooms_temple_special[0];
    }
    if (rp == 7) {
        if (area == SP_AREA_LUSH) return g_rooms_lush_pit_top[sp_rand_range(w, 0, G_ROOMS_LUSH_PIT_TOP_COUNT - 1)];
        if (area == SP_AREA_ICE) return g_rooms_ice_pit_top[0];
        if (area == SP_AREA_TEMPLE) return g_rooms_temple_pit_top[0];
    }
    if (rp == 8) {
        if (area == SP_AREA_LUSH) return g_rooms_lush_pit_mid[sp_rand_range(w, 0, G_ROOMS_LUSH_PIT_MID_COUNT - 1)];
        if (area == SP_AREA_ICE) return g_rooms_ice_pit_mid[0];
        if (area == SP_AREA_TEMPLE) return g_rooms_temple_pit_mid[0];
        return g_rooms_mines_pit_mid[0];
    }
    if (rp == 9) {
        if (area == SP_AREA_LUSH) return g_rooms_lush_pit_bot[0];
        if (area == SP_AREA_ICE) return g_rooms_ice_pit_bot[0];
        if (area == SP_AREA_TEMPLE) return g_rooms_temple_pit_bot[0];
        return g_rooms_mines_pit_bot[0];
    }

    if (rp == 2) {
        if (area == SP_AREA_LUSH) return g_rooms_lush_drop[sp_rand_range(w, 0, G_ROOMS_LUSH_DROP_COUNT - 1)];
        if (area == SP_AREA_ICE) return g_rooms_ice_main[sp_rand_range(w, 0, G_ROOMS_ICE_MAIN_COUNT - 1)];
        if (area == SP_AREA_TEMPLE) return g_rooms_temple_drop[sp_rand_range(w, 0, G_ROOMS_TEMPLE_DROP_COUNT - 1)];
        return g_rooms_mines_drop[sp_rand_range(w, 0, G_ROOMS_MINES_DROP_COUNT - 1)];
    }

    if (rp == 3) {
        if (area == SP_AREA_LUSH) return g_rooms_lush_land[sp_rand_range(w, 0, G_ROOMS_LUSH_LAND_COUNT - 1)];
        if (area == SP_AREA_ICE) return g_rooms_ice_main[sp_rand_range(w, 0, G_ROOMS_ICE_MAIN_COUNT - 1)];
        if (area == SP_AREA_TEMPLE) return g_rooms_temple_land[sp_rand_range(w, 0, G_ROOMS_TEMPLE_LAND_COUNT - 1)];
        return g_rooms_mines_land[sp_rand_range(w, 0, G_ROOMS_MINES_LAND_COUNT - 1)];
    }

    if (rp == 0) {
        if (area == SP_AREA_MINES) {
            if (g_sp_run.curr_level > 1 && !w->altar_spawned && sp_rand_range(w, 1, 14) == 1) {
                w->altar_spawned = true;
                return g_rooms_mines_side[10]; /* altar */
            }
            if (!w->idol_spawned && ry < 3 && sp_rand_range(w, 1, 8) == 1) {
                w->idol_spawned = true;
                return g_rooms_mines_side[9]; /* idol */
            }
            return g_rooms_mines_side[sp_rand_range(w, 0, 8)];
        }
        if (area == SP_AREA_LUSH) return g_rooms_lush_side[sp_rand_range(w, 0, G_ROOMS_LUSH_SIDE_COUNT - 1)];
        if (area == SP_AREA_ICE) return g_rooms_ice_side[sp_rand_range(w, 0, G_ROOMS_ICE_SIDE_COUNT - 1)];
        return g_rooms_temple_side[sp_rand_range(w, 0, G_ROOMS_TEMPLE_SIDE_COUNT - 1)];
    }

    /* rp == 1 (main) */
    if (area == SP_AREA_LUSH) return g_rooms_lush_main[sp_rand_range(w, 0, G_ROOMS_LUSH_MAIN_COUNT - 1)];
    if (area == SP_AREA_ICE) return g_rooms_ice_main[sp_rand_range(w, 0, G_ROOMS_ICE_MAIN_COUNT - 1)];
    if (area == SP_AREA_TEMPLE) return g_rooms_temple_main[sp_rand_range(w, 0, G_ROOMS_TEMPLE_MAIN_COUNT - 1)];
    return g_rooms_mines_main[sp_rand_range(w, 0, G_ROOMS_MINES_MAIN_COUNT - 1)];
}

static void stamp_room(SpWorld *w, int rx, int ry, const char *tpl, int shop_style)
{
    char buf[81];
    int tx0 = 1 + rx * 10;
    int ty0 = 1 + ry * 8;
    int i, j;

    memcpy(buf, tpl, 80);
    buf[80] = '\0';
    expand_obstacles(w, buf);

    for (j = 0; j < 8; j++) {
        for (i = 0; i < 10; i++) {
            char c = buf[j * 10 + i];
            int tx = tx0 + i;
            int ty = ty0 + j;
            float px = (float)tx * 16.0f;
            float py = (float)ty * 16.0f;

            switch (c) {
            case '1':
                place_area_brick(w, tx, ty, 0);
                break;
            case '2':
                if (sp_rand_range(w, 1, 2) == 1) {
                    place_area_brick(w, tx, ty, 0);
                }
                break;
            case '3':
                if (sp_rand_range(w, 1, 3) == 1) {
                    place_area_brick(w, tx, ty, 0);
                }
                break;
            case '.':
                place_area_brick(w, tx, ty, TF_SHOP);
                break;
            case ':':
                place_area_brick(w, tx, ty, TF_SHOP | TF_INVINCIBLE);
                break;
            case 'b':
                set_fg(w, tx, ty, TILE_SHOP_WALL, TF_SOLID | TF_SHOP, SP_ANIM_BRICK);
                break;
            case '+':
                set_fg(w, tx, ty, TILE_ICE, TF_SOLID | TF_SHOP, SP_ANIM_ICE_BLOCK);
                break;
            case 'L':
                if (g_sp_run.level_type == SP_AREA_LUSH) {
                    set_bg(w, tx, ty, BG_VINE, SP_ANIM_VINE, true, false);
                } else {
                    set_bg(w, tx, ty, BG_LADDER, SP_ANIM_LADDER, true, false);
                }
                break;
            case 'P':
                if (g_sp_run.level_type == SP_AREA_LUSH) {
                    set_bg(w, tx, ty, BG_VINE_TOP, SP_ANIM_VINE_TOP, true, true);
                } else {
                    set_bg(w, tx, ty, BG_LADDER_TOP, SP_ANIM_LADDER_TOP, true, true);
                }
                break;
            case 'G':
                set_bg(w, tx, ty, BG_LADDER, SP_ANIM_LADDER, true, false);
                break;
            case 'H':
                set_bg(w, tx, ty, BG_LADDER_TOP, SP_ANIM_LADDER_TOP, true, true);
                break;
            case '7':
                if (sp_rand_range(w, 1, 3) == 1) {
                    set_fg(w, tx, ty, TILE_SPIKES, 0, SP_ANIM_SPIKES);
                }
                break;
            case 's':
                if (g_sp_run.level_type == SP_AREA_MINES) {
                    if (sp_rand_range(w, 1, 10) == 1) spawn_enemy(w, ENT_SNAKE, px, py);
                    else if (sp_rand_range(w, 1, 2) == 1) place_area_brick(w, tx, ty, 0);
                } else {
                    set_fg(w, tx, ty, TILE_SPIKES, 0, SP_ANIM_SPIKES);
                }
                break;
            case 'S':
                spawn_enemy(w, ENT_SNAKE, px, py);
                break;
            case '4':
                if (sp_rand_range(w, 1, 3) == 1) {
                    int pb = sp_spawn_entity(w, ENT_PUSH_BLOCK, px, py);
                    if (pb >= 0) {
                        w->ents[pb].anim_id = SP_ANIM_BLOCK;
                        w->ents[pb].bx = 0; w->ents[pb].by = 0;
                        w->ents[pb].bw = 16; w->ents[pb].bh = 16;
                        w->ents[pb].z = SP_Z_FG_TILE;
                    }
                }
                break;
            case '9':
                if (rx == w->start_room_x && ry == w->start_room_y && w->entrance_x == 0) {
                    set_bg(w, tx, ty, BG_ENTRANCE, SP_ANIM_ENTRANCE, false, false);
                    w->entrance_x = tx;
                    w->entrance_y = ty;
                    place_area_brick(w, tx, ty + 1, TF_INVINCIBLE);
                } else {
                    set_bg(w, tx, ty, BG_EXIT, SP_ANIM_EXIT, false, false);
                    w->exit_x = tx;
                    w->exit_y = ty;
                    place_area_brick(w, tx, ty + 1, TF_INVINCIBLE);
                }
                break;
            case 'A':
                set_fg(w, tx, ty, TILE_ALTAR_L, TF_SOLID | TF_INVINCIBLE, SP_ANIM_ALTAR_LEFT);
                break;
            case 'x':
                set_fg(w, tx, ty, TILE_SAC_ALTAR_L, TF_SOLID | TF_INVINCIBLE, SP_ANIM_SAC_ALTAR_LEFT);
                set_fg(w, tx + 1, ty, TILE_SAC_ALTAR_R, TF_SOLID | TF_INVINCIBLE, SP_ANIM_SAC_ALTAR_RIGHT);
                set_bg(w, tx, ty - 2, BG_KALI_HEAD, SP_ANIM_KALI_HEAD1, false, false);
                break;
            case 'I':
                spawn_holdable(w, ENT_GOLD_IDOL, px + 8.0f, py + 8.0f, false, 0);
                break;
            case 'a': {
                int ch = sp_spawn_entity(w, ENT_CHEST, px + 8.0f, py + 8.0f);
                if (ch >= 0) {
                    w->ents[ch].anim_id = SP_ANIM_CHEST;
                    w->ents[ch].bx = -6; w->ents[ch].by = -6;
                    w->ents[ch].bw = 12; w->ents[ch].bh = 12;
                }
                break;
            }
            case 'T':
                spawn_treasure_at(w, TR_RUBY_BIG, px + 8.0f, py + 8.0f);
                break;
            case 'M':
                if (g_sp_run.level_type == SP_AREA_ICE) {
                    set_bg(w, tx, ty, BG_MOAI, SP_ANIM_MOAI, false, false);
                } else {
                    place_area_brick(w, tx, ty, 0);
                    spawn_holdable(w, ENT_MATTOCK, px + 8.0f, py + 8.0f, false, 0);
                }
                break;
            case 't':
                set_fg(w, tx, ty, TILE_LUSH, TF_SOLID, SP_ANIM_LUSH);
                break;
            case 'r':
                set_fg(w, tx, ty, TILE_ROCK, TF_SOLID | TF_INVINCIBLE, SP_ANIM_BLOCK);
                break;
            case 'i':
                if (g_sp_run.level_type == SP_AREA_ICE) {
                    set_fg(w, tx, ty, TILE_THIN_ICE, TF_SOLID, SP_ANIM_THIN_ICE1);
                } else {
                    generate_shop_item(w, shop_style, px + 8.0f, py + 8.0f);
                }
                break;
            case '$':
                generate_shop_item(w, shop_style, px + 8.0f, py + 8.0f);
                break;
            case 'K':
                spawn_enemy(w, ENT_SHOPKEEPER, px, py);
                break;
            case 'k':
                set_bg(w, tx, ty, BG_SIGN_GENERAL,
                       shop_style == 1 ? SP_ANIM_SIGN_BOMB
                     : shop_style == 2 ? SP_ANIM_SIGN_WEAPON
                     : shop_style == 3 ? SP_ANIM_SIGN_CLOTHING
                     : shop_style == 4 ? SP_ANIM_SIGN_RARE
                     : SP_ANIM_SIGN_GENERAL, false, false);
                break;
            case 'l':
                set_bg(w, tx, ty, BG_LAMP, SP_ANIM_LAMP, false, false);
                break;
            case 'D':
            case '!':
                if (!w->damsel_spawned) {
                    int dm = spawn_enemy(w, ENT_DAMSEL, px, py);
                    if (dm >= 0) {
                        w->ents[dm].for_sale = (c == '!');
                        w->damsel_spawned = true;
                    }
                }
                break;
            case 'd':
                spawn_holdable(w, ENT_DICE, px + 8.0f, py + 8.0f, false, 0);
                break;
            case 'w':
                set_fg(w, tx, ty, TILE_WATER, 0, SP_ANIM_WATER);
                break;
            case 'f':
                if (sp_rand_range(w, 1, 2) == 1) {
                    spawn_enemy(w, ENT_FROG, px, py);
                }
                break;
            case 'j':
                set_fg(w, tx, ty, TILE_ICE, TF_SOLID, SP_ANIM_ICE);
                break;
            case 'X':
                if (g_sp_run.level_type == SP_AREA_ICE) {
                    spawn_enemy(w, ENT_YETI_KING, px, py);
                } else if (g_sp_run.level_type == SP_AREA_TEMPLE) {
                    spawn_enemy(w, ENT_TOMB_LORD, px, py);
                }
                break;
            case 'm':
                set_fg(w, tx, ty, TILE_TEMPLE, TF_SOLID, SP_ANIM_TEMPLE);
                break;
            case 'v':
                set_fg(w, tx, ty, TILE_LAVA, 0, SP_ANIM_LAVA);
                break;
            case 'F':
                set_fg(w, tx, ty, TILE_ALIEN_FLOOR, TF_SOLID | TF_INVINCIBLE, SP_ANIM_ALIEN_FLOOR);
                break;
            case 'c':
                if (sp_rand_range(w, 1, 2) == 1) {
                    int cr = sp_spawn_entity(w, ENT_CRATE, px + 8.0f, py + 8.0f);
                    if (cr >= 0) {
                        w->ents[cr].anim_id = SP_ANIM_CRATE;
                        w->ents[cr].bx = -6; w->ents[cr].by = -6;
                        w->ents[cr].bw = 12; w->ents[cr].bh = 12;
                    }
                }
                break;
            default:
                break;
            }
        }
    }
}

static void spawn_treasure_and_entities(SpWorld *w)
{
    int tx, ty;
    int area = g_sp_run.level_type;
    int exit_room_x = w->exit_x / 10;
    int exit_room_y = w->exit_y / 8;
    (void)exit_room_x;
    (void)exit_room_y;

    /* Embedded gold/gems inside dirt blocks */
    for (ty = 1; ty < w->rows - 1; ty++) {
        for (tx = 1; tx < w->cols - 1; tx++) {
            SpTile *t = &w->grid[ty][tx];
            float cx = (float)tx * 16.0f + 8.0f;
            float cy = (float)ty * 16.0f + 8.0f;
            bool in_shop = (t->flags & TF_SHOP) != 0;
            bool in_start = (tx >= 1 + w->start_room_x * 10 && tx < 1 + (w->start_room_x + 1) * 10 &&
                             ty >= 1 + w->start_room_y * 8 && ty < 1 + (w->start_room_y + 1) * 8);

            if ((t->fg == TILE_BRICK || t->fg == TILE_LUSH || t->fg == TILE_TEMPLE) && !in_shop) {
                int r = sp_rand_range(w, 1, 45);
                if (r == 1) {
                    t->fg = (area == SP_AREA_TEMPLE) ? TILE_TEMPLE_GOLD : TILE_GOLD_BIG;
                } else if (r <= 4) {
                    t->fg = TILE_GOLD;
                }
            }

            if (t->fg == TILE_NONE || !(t->flags & TF_SOLID) || in_shop) {
                continue;
            }

            /* Loose treasure / crates / jars / rocks on top of solid tiles */
            if (ty > 1 && w->grid[ty - 1][tx].fg == TILE_NONE && w->grid[ty - 1][tx].bg == BG_NONE) {
                float px = (float)tx * 16.0f;
                float py = (float)(ty - 1) * 16.0f;
                int roll = sp_rand_range(w, 1, 100);
                if (!in_start && roll == 1) {
                    int cr = sp_spawn_entity(w, ENT_CRATE, px + 8.0f, py + 8.0f);
                    if (cr >= 0) {
                        w->ents[cr].anim_id = SP_ANIM_CRATE;
                        w->ents[cr].bx = -6; w->ents[cr].by = -6;
                        w->ents[cr].bw = 12; w->ents[cr].bh = 12;
                    }
                } else if (!in_start && roll == 2) {
                    int ch = sp_spawn_entity(w, ENT_CHEST, px + 8.0f, py + 8.0f);
                    if (ch >= 0) {
                        w->ents[ch].anim_id = SP_ANIM_CHEST;
                        w->ents[ch].bx = -6; w->ents[ch].by = -6;
                        w->ents[ch].bw = 12; w->ents[ch].bh = 12;
                    }
                } else if (roll <= 5 && !in_start) {
                    static const uint8_t tr_table[] = {
                        TR_GOLD_BAR, TR_GOLD_BARS, TR_EMERALD, TR_SAPPHIRE, TR_RUBY, TR_GOLD_NUGGET
                    };
                    spawn_treasure_at(w, tr_table[sp_rand_range(w, 0, 5)], px + 8.0f, py + 8.0f);
                } else if (roll == 6 && !in_start) {
                    spawn_holdable(w, ENT_JAR, px + 8.0f, py + 8.0f, false, 0);
                } else if (roll == 7) {
                    spawn_holdable(w, ENT_ROCK, px + 8.0f, py + 8.0f, false, 0);
                } else if (!in_start && !w->damsel_spawned && sp_rand_range(w, 1, 90) == 1 &&
                           (tx != w->exit_x || ty - 1 != w->exit_y)) {
                    spawn_enemy(w, ENT_DAMSEL, px, py);
                    w->damsel_spawned = true;
                }
            }

            /* Enemies & traps outside the start room */
            if (in_start) {
                continue;
            }

            /* Ceiling hanging enemies */
            if (ty < w->rows - 4 &&
                w->grid[ty + 1][tx].fg == TILE_NONE &&
                w->grid[ty + 2][tx].fg == TILE_NONE) {
                float ex = (float)tx * 16.0f;
                float ey = (float)(ty + 1) * 16.0f;
                int er = sp_rand_range(w, 1, 140);
                if (area == SP_AREA_MINES) {
                    if (er == 1 && tx < w->cols - 2 && w->grid[ty + 1][tx + 1].fg == TILE_NONE &&
                        w->grid[ty + 2][tx + 1].fg == TILE_NONE && sp_rand_range(w, 1, 3) == 1) {
                        spawn_enemy(w, ENT_GIANT_SPIDER, ex, ey);
                    } else if (er <= 3) {
                        spawn_enemy(w, ENT_BAT, ex, ey);
                    } else if (er == 4) {
                        spawn_enemy(w, ENT_SPIDER, ex, ey);
                    }
                } else if (area == SP_AREA_LUSH) {
                    if (er <= 2) spawn_enemy(w, ENT_BAT, ex, ey);
                    else if (er == 3) spawn_enemy(w, ENT_MONKEY, ex, ey);
                } else if (area == SP_AREA_ICE) {
                    if (er <= 2) spawn_enemy(w, ENT_UFO, ex, ey);
                }
            }

            /* Floor enemies */
            if (ty > 2 && w->grid[ty - 1][tx].fg == TILE_NONE && w->grid[ty - 1][tx].bg == BG_NONE) {
                float ex = (float)tx * 16.0f;
                float ey = (float)(ty - 1) * 16.0f;
                int er = sp_rand_range(w, 1, 120);
                if (area == SP_AREA_MINES) {
                    if (er <= 2) spawn_enemy(w, ENT_SNAKE, ex, ey);
                    else if (er == 3) spawn_enemy(w, ENT_CAVEMAN, ex, ey);
                    else if (er == 4) spawn_enemy(w, ENT_SKELETON, ex, ey);
                } else if (area == SP_AREA_LUSH) {
                    if (er <= 2) spawn_enemy(w, ENT_FROG, ex, ey);
                    else if (er == 3) spawn_enemy(w, ENT_FIRE_FROG, ex, ey);
                    else if (er == 4) spawn_enemy(w, ENT_MAN_TRAP, ex, ey);
                    else if (er == 5) spawn_enemy(w, ENT_CAVEMAN, ex, ey);
                } else if (area == SP_AREA_ICE) {
                    if (er <= 2) spawn_enemy(w, ENT_YETI, ex, ey);
                    else if (er == 3) spawn_enemy(w, ENT_ALIEN, ex, ey);
                } else if (area == SP_AREA_TEMPLE) {
                    if (er <= 2) spawn_enemy(w, ENT_HAWKMAN, ex, ey);
                    else if (er == 3) spawn_enemy(w, ENT_CAVEMAN, ex, ey);
                    else if (er == 4) spawn_enemy(w, ENT_ZOMBIE, ex, ey);
                }
            }

            /* Arrow traps (Mines & Lush & Temple) */
            if ((area == SP_AREA_MINES || area == SP_AREA_LUSH || area == SP_AREA_TEMPLE) &&
                t->fg == TILE_ROCK && !(t->flags & TF_INVINCIBLE) &&
                sp_rand_range(w, 1, 5) == 1) {
                if (tx > 3 && w->grid[ty][tx + 1].fg != TILE_NONE &&
                    w->grid[ty][tx - 1].fg == TILE_NONE && w->grid[ty][tx - 2].fg == TILE_NONE) {
                    set_fg(w, tx, ty, TILE_ARROW_TRAP_L, TF_SOLID, SP_ANIM_ARROW_TRAP_LEFT);
                } else if (tx < w->cols - 4 && w->grid[ty][tx - 1].fg != TILE_NONE &&
                           w->grid[ty][tx + 1].fg == TILE_NONE && w->grid[ty][tx + 2].fg == TILE_NONE) {
                    set_fg(w, tx, ty, TILE_ARROW_TRAP_R, TF_SOLID, SP_ANIM_ARROW_TRAP_RIGHT);
                }
            }
            (void)cx;
            (void)cy;
        }
    }

    /* Angry shopkeeper guarding exit if thief or murderer */
    if ((g_sp_run.murderer || g_sp_run.thief_level > 0) && w->exit_x > 0) {
        int sk = spawn_enemy(w, ENT_SHOPKEEPER, (float)w->exit_x * 16.0f, (float)w->exit_y * 16.0f);
        if (sk >= 0) {
            w->ents[sk].state = ST_ANGRY;
            w->shop_angry = true;
        }
    }

    /* Guarantee Udjat locked chest + key once in Mines (1-2..1-4) */
    if (area == SP_AREA_MINES && !g_sp_run.made_udjat_eye && g_sp_run.curr_level >= 2) {
        if (g_sp_run.curr_level == 4 || sp_rand_range(w, 1, 3) == 1) {
            int cx = (w->exit_x > 2) ? w->exit_x - 1 : w->exit_x + 1;
            int lc = sp_spawn_entity(w, ENT_LOCKED_CHEST, (float)cx * 16.0f + 8.0f, (float)w->exit_y * 16.0f + 8.0f);
            if (lc >= 0) {
                w->ents[lc].anim_id = SP_ANIM_LOCKED_CHEST;
                w->ents[lc].bx = -6; w->ents[lc].by = -6;
                w->ents[lc].bw = 12; w->ents[lc].bh = 12;
                spawn_holdable(w, ENT_KEY, (float)w->entrance_x * 16.0f + 24.0f, (float)w->entrance_y * 16.0f + 8.0f, false, 0);
                g_sp_run.made_udjat_eye = true;
            }
        }
    }
}

void sp_autotile_world(SpWorld *w)
{
    int tx, ty;
    for (ty = 0; ty < w->rows; ty++) {
        for (tx = 0; tx < w->cols; tx++) {
            SpTile *t = &w->grid[ty][tx];
            bool up_same, dn_same;
            if (t->fg == TILE_NONE) {
                continue;
            }
            up_same = (ty == 0) || (w->grid[ty - 1][tx].fg != TILE_NONE && w->grid[ty - 1][tx].fg != TILE_SPIKES);
            dn_same = (ty == w->rows - 1) || (w->grid[ty + 1][tx].fg != TILE_NONE && w->grid[ty + 1][tx].fg != TILE_SPIKES);

            switch (t->fg) {
            case TILE_BRICK:
            case TILE_SHOP_WALL:
                if (!up_same && !dn_same) t->fg_anim = SP_ANIM_CAVE_UP2;
                else if (!up_same) t->fg_anim = SP_ANIM_CAVE_UP;
                else if (!dn_same) t->fg_anim = SP_ANIM_BRICK_DOWN;
                else t->fg_anim = SP_ANIM_BRICK;
                break;
            case TILE_LUSH:
                if (!up_same && !dn_same) t->fg_anim = SP_ANIM_LUSH_UP2;
                else if (!up_same) t->fg_anim = SP_ANIM_LUSH_UP;
                else if (!dn_same) t->fg_anim = SP_ANIM_LUSH_DOWN;
                else t->fg_anim = SP_ANIM_LUSH;
                break;
            case TILE_ICE:
                if (!up_same) t->fg_anim = SP_ANIM_ICE_UP;
                else if (!dn_same) t->fg_anim = SP_ANIM_ICE_DOWN;
                else t->fg_anim = SP_ANIM_ICE;
                break;
            case TILE_TEMPLE:
                if (!up_same) t->fg_anim = SP_ANIM_TEMPLE_UP;
                else if (!dn_same) t->fg_anim = SP_ANIM_TEMPLE_DOWN;
                else t->fg_anim = SP_ANIM_TEMPLE;
                break;
            case TILE_DARK:
                if (!up_same) t->fg_anim = SP_ANIM_DARK_UP;
                else if (!dn_same) t->fg_anim = SP_ANIM_DARK_DOWN;
                else t->fg_anim = SP_ANIM_DARK;
                break;
            case TILE_GOLD:
                t->fg_anim = (g_sp_run.level_type == SP_AREA_LUSH) ? SP_ANIM_LUSH_GOLD : SP_ANIM_GOLD;
                break;
            case TILE_GOLD_BIG:
                t->fg_anim = (g_sp_run.level_type == SP_AREA_LUSH) ? SP_ANIM_LUSH_GOLD_BIG : SP_ANIM_GOLD_BIG;
                break;
            case TILE_TEMPLE_GOLD:
                t->fg_anim = SP_ANIM_TEMPLE_GOLD;
                break;
            case TILE_ROCK:
                t->fg_anim = SP_ANIM_BLOCK;
                break;
            case TILE_WATER:
                t->fg_anim = (ty > 0 && w->grid[ty - 1][tx].fg == TILE_WATER) ? SP_ANIM_WATER : SP_ANIM_WATER_TOP;
                break;
            case TILE_LAVA:
                t->fg_anim = (ty > 0 && w->grid[ty - 1][tx].fg == TILE_LAVA) ? SP_ANIM_LAVA : SP_ANIM_LAVA_TOP;
                break;
            case TILE_SPIKES:
                t->fg_anim = (t->flags & TF_BLOODY) ? SP_ANIM_SPIKES_BLOOD : SP_ANIM_SPIKES;
                break;
            default:
                break;
            }
        }
    }
}

static void generate_tutorial(SpWorld *w)
{
    int tx, ty;
    w->cols = 42;
    w->rows = 34;

    for (ty = 0; ty < w->rows; ty++) {
        for (tx = 0; tx < w->cols; tx++) {
            if (tx == 0 || tx == w->cols - 1 || ty == 0 || ty >= 20) {
                set_fg(w, tx, ty, TILE_ROCK, TF_SOLID | TF_INVINCIBLE, SP_ANIM_BLOCK);
            }
        }
    }
    /* Upper tutorial course */
    for (tx = 1; tx < w->cols - 1; tx++) {
        if (tx < 12 || (tx >= 15 && tx < 25) || tx >= 28) {
            set_fg(w, tx, 15, TILE_BRICK, TF_SOLID, SP_ANIM_BRICK);
            set_fg(w, tx, 16, TILE_BRICK, TF_SOLID, SP_ANIM_BRICK);
        } else {
            set_fg(w, tx, 19, TILE_SPIKES, 0, SP_ANIM_SPIKES);
        }
    }
    for (ty = 9; ty <= 14; ty++) {
        set_bg(w, 20, ty, ty == 9 ? BG_LADDER_TOP : BG_LADDER, ty == 9 ? SP_ANIM_LADDER_TOP : SP_ANIM_LADDER, true, ty == 9);
    }
    for (tx = 20; tx <= 28; tx++) {
        set_fg(w, tx, 9, TILE_BRICK, TF_SOLID, SP_ANIM_BRICK);
    }
    set_bg(w, 3, 14, BG_ENTRANCE, SP_ANIM_ENTRANCE, false, false);
    w->entrance_x = 3;
    w->entrance_y = 14;
    set_bg(w, 37, 14, BG_EXIT, SP_ANIM_EXIT, false, false);
    w->exit_x = 37;
    w->exit_y = 14;

    spawn_holdable(w, ENT_ROCK, 7.0f * 16.0f + 8.0f, 14.0f * 16.0f + 8.0f, false, 0);
    spawn_holdable(w, ENT_JAR, 9.0f * 16.0f + 8.0f, 14.0f * 16.0f + 8.0f, false, 0);
    {
        int cr = sp_spawn_entity(w, ENT_CRATE, 23.0f * 16.0f + 8.0f, 8.0f * 16.0f + 8.0f);
        if (cr >= 0) {
            w->ents[cr].anim_id = SP_ANIM_CRATE;
            w->ents[cr].bx = -6; w->ents[cr].by = -6; w->ents[cr].bw = 12; w->ents[cr].bh = 12;
        }
    }
    spawn_enemy(w, ENT_SNAKE, 30.0f * 16.0f, 14.0f * 16.0f);
    spawn_treasure_at(w, TR_GOLD_BARS, 26.0f * 16.0f + 8.0f, 8.0f * 16.0f + 8.0f);
    sp_set_message(w, "TUTORIAL CAVE", "Z:JUMP X:WHIP C:BOMB V:ROPE UP:DOOR", 240);
}

static void generate_olmec_level(SpWorld *w)
{
    int rx, ry, tx, ty;
    w->cols = 42;
    w->rows = 55;

    for (ty = 0; ty < w->rows; ty++) {
        for (tx = 0; tx < w->cols; tx++) {
            if (tx == 0 || tx == w->cols - 1 || ty == 0 || ty == w->rows - 1) {
                set_fg(w, tx, ty, TILE_ROCK, TF_SOLID | TF_INVINCIBLE, SP_ANIM_BLOCK);
            }
        }
    }

    for (ry = 0; ry < 4; ry++) {
        for (rx = 0; rx < 4; rx++) {
            int ss = 0;
            const char *tpl = (ry < 3)
                ? g_rooms_olmec_boss_top[sp_rand_range(w, 0, G_ROOMS_OLMEC_BOSS_TOP_COUNT - 1)]
                : g_rooms_olmec_boss_bot[sp_rand_range(w, 0, G_ROOMS_OLMEC_BOSS_BOT_COUNT - 1)];
            stamp_room(w, rx, ry, tpl, ss);
        }
    }

    /* Arena floor + lava pit below */
    for (tx = 1; tx < w->cols - 1; tx++) {
        for (ty = 25; ty <= 36; ty++) {
            if (ty <= 31) {
                set_fg(w, tx, ty, TILE_TEMPLE, TF_SOLID, SP_ANIM_TEMPLE);
            } else {
                set_fg(w, tx, ty, TILE_LAVA, 0, SP_ANIM_LAVA);
            }
        }
        /* Clear upper fight arena */
        for (ty = 16; ty <= 24; ty++) {
            w->grid[ty][tx].fg = TILE_NONE;
            w->grid[ty][tx].flags = 0;
        }
    }

    /* Entrance on left, golden exit on right */
    set_bg(w, 3, 24, BG_ENTRANCE, SP_ANIM_ENTRANCE, false, false);
    w->entrance_x = 3;
    w->entrance_y = 24;
    set_fg(w, 3, 25, TILE_ROCK, TF_SOLID | TF_INVINCIBLE, SP_ANIM_BLOCK);

    set_bg(w, 38, 24, BG_GOLD_DOOR, SP_ANIM_GOLD_DOOR, false, false);
    w->exit_x = 38;
    w->exit_y = 24;
    set_fg(w, 38, 25, TILE_ROCK, TF_SOLID | TF_INVINCIBLE, SP_ANIM_BLOCK);

    w->olmec_idx = spawn_enemy(w, ENT_OLMEC, 19.0f * 16.0f, 18.0f * 16.0f);
    sp_set_message(w, "OLMEC'S LAIR!", "LURE OLMEC INTO THE LAVA BELOW!", 180);
}

void sp_generate_level(SpWorld *w)
{
    int rx, ry, tx, ty;
    int area;

    sp_world_clear(w, w->scene);

    /* Determine area from curr_level */
    if (g_sp_run.is_tutorial || g_sp_run.curr_level == 0) {
        g_sp_run.level_type = SP_AREA_TUTORIAL;
    } else if (g_sp_run.curr_level <= 4) {
        g_sp_run.level_type = SP_AREA_MINES;
    } else if (g_sp_run.curr_level <= 8) {
        g_sp_run.level_type = SP_AREA_LUSH;
    } else if (g_sp_run.curr_level <= 12) {
        g_sp_run.level_type = SP_AREA_ICE;
    } else if (g_sp_run.curr_level <= 15) {
        g_sp_run.level_type = SP_AREA_TEMPLE;
    } else {
        g_sp_run.level_type = SP_AREA_OLMEC;
    }
    area = g_sp_run.level_type;

    g_sp_run.dark_level = false;
    g_sp_run.ghost_exists = false;
    g_sp_run.xmoney = 0;
    g_sp_run.xdamsels = 0;
    g_sp_run.xtime_ticks = 0;

    if (area == SP_AREA_TUTORIAL) {
        generate_tutorial(w);
        goto spawn_player_done;
    }
    if (area == SP_AREA_OLMEC) {
        generate_olmec_level(w);
        goto spawn_player_done;
    }

    w->cols = 42;
    w->rows = 34;

    /* Outer border */
    for (ty = 0; ty < w->rows; ty++) {
        for (tx = 0; tx < w->cols; tx++) {
            if (tx == 0 || tx == w->cols - 1 || ty == 0 || ty == w->rows - 1) {
                set_fg(w, tx, ty, TILE_ROCK, TF_SOLID | TF_INVINCIBLE, SP_ANIM_BLOCK);
            }
        }
    }

    /* Path solver (matches scr_level_gen) */
    w->start_room_x = sp_rand_range(w, 0, 3);
    w->start_room_y = 0;
    rx = w->start_room_x;
    ry = 0;
    w->room_path[rx][ry] = 1;
    while (ry < 4) {
        int prev_x = rx, prev_y = ry;
        int n = (rx == 0) ? sp_rand_range(w, 3, 5)
              : (rx == 3) ? sp_rand_range(w, 5, 7)
              : sp_rand_range(w, 1, 5);
        bool moved_down = false;
        if (n < 3 || n > 5) {
            if (rx > 0 && w->room_path[rx - 1][ry] == 0) rx--;
            else if (rx < 3 && w->room_path[rx + 1][ry] == 0) rx++;
            else n = 5;
        } else if (n == 3 || n == 4) {
            if (rx < 3 && w->room_path[rx + 1][ry] == 0) rx++;
            else if (rx > 0 && w->room_path[rx - 1][ry] == 0) rx--;
            else n = 5;
        }
        if (n == 5) {
            ry++;
            moved_down = true;
            if (ry < 4) {
                w->room_path[prev_x][prev_y] = 2;
                w->room_path[rx][ry] = 3;
            } else {
                w->end_room_x = rx;
                w->end_room_y = ry - 1;
            }
        }
        if (!moved_down) {
            w->room_path[rx][ry] = 1;
        }
    }

    /* Optional shop in a side room */
    if (g_sp_run.curr_level >= 2 && sp_rand_range(w, 1, 2) == 1) {
        int sx, sy;
        bool placed = false;
        for (sy = 0; sy < 4 && !placed; sy++) {
            for (sx = 0; sx < 4 && !placed; sx++) {
                if (w->room_path[sx][sy] != 0) continue;
                if (sx < 3 && (w->room_path[sx + 1][sy] == 1 || w->room_path[sx + 1][sy] == 2)) {
                    w->room_path[sx][sy] = 4;
                    placed = true;
                } else if (sx > 0 && (w->room_path[sx - 1][sy] == 1 || w->room_path[sx - 1][sy] == 2)) {
                    w->room_path[sx][sy] = 5;
                    placed = true;
                }
            }
        }
    }

    /* Dark level chance (1-2..4-3, not Ice Caves) */
    if (!g_sp_run.had_dark_level && g_sp_run.curr_level > 1 && area != SP_AREA_ICE &&
        sp_rand_range(w, 1, 10) == 1) {
        g_sp_run.dark_level = true;
        g_sp_run.had_dark_level = true;
    }

    /* Stamp all 16 rooms */
    for (ry = 0; ry < 4; ry++) {
        for (rx = 0; rx < 4; rx++) {
            int shop_style = 0;
            const char *tpl = pick_room_template(w, rx, ry, &shop_style);
            stamp_room(w, rx, ry, tpl, shop_style);
        }
    }

    /* Guarantee entrance & exit exist even if a template omitted '9' */
    if (w->entrance_x == 0) {
        w->entrance_x = 1 + w->start_room_x * 10 + 4;
        w->entrance_y = 1 + w->start_room_y * 8 + 6;
        set_bg(w, w->entrance_x, w->entrance_y, BG_ENTRANCE, SP_ANIM_ENTRANCE, false, false);
        w->grid[w->entrance_y][w->entrance_x].fg = TILE_NONE;
        w->grid[w->entrance_y][w->entrance_x].flags = 0;
        place_area_brick(w, w->entrance_x, w->entrance_y + 1, TF_INVINCIBLE);
    }
    if (w->exit_x == 0) {
        w->exit_x = 1 + w->end_room_x * 10 + 4;
        w->exit_y = 1 + w->end_room_y * 8 + 6;
        set_bg(w, w->exit_x, w->exit_y, BG_EXIT, SP_ANIM_EXIT, false, false);
        w->grid[w->exit_y][w->exit_x].fg = TILE_NONE;
        w->grid[w->exit_y][w->exit_x].flags = 0;
        place_area_brick(w, w->exit_x, w->exit_y + 1, TF_INVINCIBLE);
    }

    spawn_treasure_and_entities(w);

    if (g_sp_run.dark_level) {
        sp_set_message(w, "IT'S SO DARK!", "CAN HARDLY SEE A THING...", 120);
    } else if (area == SP_AREA_LUSH && g_sp_run.curr_level == 5) {
        sp_set_message(w, "AREA 2: THE LUSH JUNGLE", "WATCH FOR FROGS AND MAN-TRAPS!", 120);
    } else if (area == SP_AREA_ICE && g_sp_run.curr_level == 9) {
        sp_set_message(w, "AREA 3: THE ICE CAVES", "SLIPPERY ICE AND YETIS AHEAD!", 120);
    } else if (area == SP_AREA_TEMPLE && g_sp_run.curr_level == 13) {
        sp_set_message(w, "AREA 4: THE TEMPLE", "THE FINAL CHALLENGES AWAIT!", 120);
    }

spawn_player_done:
    sp_autotile_world(w);

    /* Spawn player at entrance */
    {
        float px = (float)w->entrance_x * 16.0f + 8.0f;
        float py = (float)w->entrance_y * 16.0f + 8.0f;
        int p = sp_spawn_entity(w, ENT_PLAYER, px, py);
        if (p >= 0) {
            SpEntity *pe = &w->ents[p];
            pe->bx = -5;
            pe->by = -6;
            pe->bw = 10;
            pe->bh = 14;
            pe->z = SP_Z_PLAYER;
            pe->facing = 1;
            pe->hp = (int16_t)g_sp_run.plife;
            w->player_idx = p;

            if (g_sp_run.held_item_kind != ENT_NONE) {
                int hi = spawn_holdable(w, g_sp_run.held_item_kind, px, py, false, 0);
                if (hi >= 0) {
                    w->ents[hi].held = true;
                    w->ents[hi].holder = (int16_t)p;
                    pe->held_ent = (int16_t)hi;
                }
            }
        }
        w->cam_x = px - 160.0f;
        w->cam_y = py - 120.0f;
        if (w->cam_x < 0.0f) w->cam_x = 0.0f;
        if (w->cam_y < 0.0f) w->cam_y = 0.0f;
        if (w->cam_x > (float)(w->cols * 16 - SP_VIRT_W)) w->cam_x = (float)(w->cols * 16 - SP_VIRT_W);
        if (w->cam_y > (float)(w->rows * 16 - SP_VIRT_H)) w->cam_y = (float)(w->rows * 16 - SP_VIRT_H);
    }
}

void sp_render_tiles(SpWorld *w)
{
    int tx0 = (int)floorf(w->cam_x / 16.0f) - 1;
    int ty0 = (int)floorf(w->cam_y / 16.0f) - 1;
    int tx1 = tx0 + 22;
    int ty1 = ty0 + 17;
    int tx, ty;

    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    if (tx1 >= w->cols) tx1 = w->cols - 1;
    if (ty1 >= w->rows) ty1 = w->rows - 1;

    for (ty = ty0; ty <= ty1; ty++) {
        for (tx = tx0; tx <= tx1; tx++) {
            const SpTile *t = &w->grid[ty][tx];
            float wx = (float)tx * 16.0f;
            float wy = (float)ty * 16.0f;
            if (t->bg != BG_NONE && t->bg_anim < SP_ANIM_COUNT) {
                sp_draw_anim_at(w, t->bg_anim, 0, wx, wy, false, SP_Z_BACK_TILE, 0xFFFFFFFFu);
            }
            if (t->fg != TILE_NONE && t->fg_anim < SP_ANIM_COUNT) {
                uint32_t col = (t->fg == TILE_WATER || t->fg == TILE_LAVA) ? 0xFFFFFFD8u : 0xFFFFFFFFu;
                sp_draw_anim_at(w, t->fg_anim, 0, wx, wy, false, SP_Z_FG_TILE, col);
            }
        }
    }
}
