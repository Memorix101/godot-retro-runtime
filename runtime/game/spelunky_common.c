/* Shared Spelunky state, sprite pools, tile collision, and pixel-step physics. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "spelunky.h"

SpRunState g_sp_run;

static int sp_floor_div16(float v)
{
    return (int)floorf(v / 16.0f);
}

void sp_run_init_once(void)
{
    if (g_sp_run.initialized) {
        return;
    }
    memset(&g_sp_run, 0, sizeof(g_sp_run));
    g_sp_run.initialized = true;
    g_sp_run.show_intro = true;
    g_sp_run.character = SP_CHAR_EXPLORER;
    g_sp_run.curr_level = 1;
    g_sp_run.plife = 4;
    g_sp_run.bombs = 4;
    g_sp_run.rope = 4;

    if (!rr_save_read(SP_SAVE_NAME, &g_sp_run.scores, sizeof(g_sp_run.scores)) ||
        g_sp_run.scores.magic != SP_SAVE_MAGIC) {
        memset(&g_sp_run.scores, 0, sizeof(g_sp_run.scores));
        g_sp_run.scores.magic = SP_SAVE_MAGIC;
        g_sp_run.scores.tunnel1 = 100000;
        g_sp_run.scores.tunnel2 = 200000;
    }
}

void sp_save_scores(void)
{
    g_sp_run.scores.magic = SP_SAVE_MAGIC;
    rr_save_write(SP_SAVE_NAME, &g_sp_run.scores, sizeof(g_sp_run.scores));
}

void sp_record_run_end(bool won)
{
    SpHighscores *s = &g_sp_run.scores;
    s->plays++;
    if (won) {
        s->wins++;
        if (s->best_time == 0 || g_sp_run.time_ticks < s->best_time) {
            s->best_time = g_sp_run.time_ticks;
        }
    } else {
        s->deaths++;
    }
    if (g_sp_run.money > s->top_money) {
        s->top_money = g_sp_run.money;
    }
    if (g_sp_run.kills > s->most_kills) {
        s->most_kills = g_sp_run.kills;
    }
    if (g_sp_run.damsels > s->most_saves) {
        s->most_saves = g_sp_run.damsels;
    }
    sp_save_scores();
}

void sp_run_reset_new_game(bool tutorial)
{
    sp_run_init_once();
    g_sp_run.in_run = true;
    g_sp_run.is_tutorial = tutorial;
    g_sp_run.game_over = false;
    g_sp_run.victory = false;
    g_sp_run.curr_level = tutorial ? 0 : 1;
    g_sp_run.level_type = tutorial ? SP_AREA_TUTORIAL : SP_AREA_MINES;
    g_sp_run.plife = (g_sp_run.character == SP_CHAR_TUNNEL_MAN) ? 2 : 4;
    g_sp_run.bombs = (g_sp_run.character == SP_CHAR_TUNNEL_MAN) ? 0 : 4;
    g_sp_run.rope = (g_sp_run.character == SP_CHAR_TUNNEL_MAN) ? 0 : 4;
    g_sp_run.arrows = 0;
    g_sp_run.money = 0;
    g_sp_run.xmoney = 0;
    g_sp_run.kills = 0;
    g_sp_run.damsels = 0;
    g_sp_run.xdamsels = 0;
    g_sp_run.time_ticks = 0;
    g_sp_run.xtime_ticks = 0;
    g_sp_run.blood_level = 0;
    g_sp_run.favor = 0;
    g_sp_run.kali_gift = 0;
    g_sp_run.kali_punish = 0;
    g_sp_run.thief_level = 0;
    g_sp_run.murderer = false;
    g_sp_run.passives = 0;
    g_sp_run.held_item_kind = ENT_NONE;
    g_sp_run.made_udjat_eye = false;
    g_sp_run.made_market_entrance = false;
    g_sp_run.made_black_market = false;
    g_sp_run.made_moai = false;
    g_sp_run.city_of_gold = false;
    g_sp_run.had_dark_level = false;
    g_sp_run.dark_level = false;
    g_sp_run.ghost_exists = false;
    g_sp_run.death_cause[0] = '\0';
}

uint32_t sp_rng_next(SpWorld *w)
{
    uint32_t x = w->rng;
    if (x == 0) {
        x = 0x6D2B79F5u;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    w->rng = x;
    return x;
}

int sp_rand_range(SpWorld *w, int lo, int hi)
{
    if (hi <= lo) {
        return lo;
    }
    return lo + (int)(sp_rng_next(w) % (uint32_t)(hi - lo + 1));
}

Node *sp_find_child(Node *parent, const char *name)
{
    Node *c = NULL;
    if (parent == NULL || name == NULL) {
        return NULL;
    }
    while ((c = rr_node_child(parent, c)) != NULL) {
        if (c->name != NULL && strcmp(c->name, name) == 0) {
            return c;
        }
    }
    return NULL;
}

void sp_set_visible(Node *node, bool visible)
{
    if (node == NULL) {
        return;
    }
    if (visible) {
        node->flags |= RR_NODE_FLAG_VISIBLE;
    } else {
        node->flags &= (uint8_t)~RR_NODE_FLAG_VISIBLE;
    }
}

void sp_play_sfx(Node *sfx)
{
    if (sfx != NULL) {
        rr_audio_play(sfx);
    }
}

void sp_pool_init(SpSpritePool *pool, const RRPackedScene *packed, int max_count, uint16_t cols, uint16_t rows)
{
    int i;
    pool->cap = max_count;
    pool->used = 0;
    pool->cols = cols;
    pool->rows = rows;
    if (packed == NULL || max_count <= 0) {
        pool->nodes = NULL;
        pool->cap = 0;
        return;
    }
    /* Allocate via static scratch or instantiate lazily? Wait: we can instantiate up front
     * or keep fixed pointer array inside SpWorld? Let's keep pool->nodes pointing to static
     * arrays or instantiate on demand? Better: caller supplies backing array or static buffer!
     * Actually, SpSpritePool uses rr_instantiate; let's pre-instantiate all slots into a static
     * table per sheet since there is only one active Spelunky scene at a time! */
    (void)i;
}

static Node *g_slot_16a[832];
static Node *g_slot_16b[136];
static Node *g_slot_32a[68];
static Node *g_slot_32b[52];
static Node *g_slot_64a[28];
static Node *g_slot_64b[16];

void sp_pools_setup(SpWorld *w,
                    const RRPackedScene *s16a, int m16a,
                    const RRPackedScene *s16b, int m16b,
                    const RRPackedScene *s32a, int m32a,
                    const RRPackedScene *s32b, int m32b,
                    const RRPackedScene *s64a, int m64a,
                    const RRPackedScene *s64b, int m64b)
{
    struct {
        int id;
        const RRPackedScene *ps;
        Node **buf;
        int max_cap;
        int req;
        uint16_t cols, rows;
    } cfg[SP_SHEET_COUNT] = {
        { SP_SHEET_16A, s16a, g_slot_16a, 832, m16a, 32, 32 },
        { SP_SHEET_16B, s16b, g_slot_16b, 136, m16b, 32, 4  },
        { SP_SHEET_32A, s32a, g_slot_32a, 68,  m32a, 16, 16 },
        { SP_SHEET_32B, s32b, g_slot_32b, 52,  m32b, 16, 16 },
        { SP_SHEET_64A, s64a, g_slot_64a, 28,  m64a, 8,  8  },
        { SP_SHEET_64B, s64b, g_slot_64b, 16,  m64b, 8,  4  },
    };
    int s, i;
    for (s = 0; s < SP_SHEET_COUNT; s++) {
        SpSpritePool *p = &w->pools[s];
        int cap = cfg[s].req < cfg[s].max_cap ? cfg[s].req : cfg[s].max_cap;
        p->nodes = cfg[s].buf;
        p->cap = 0;
        p->used = 0;
        p->cols = cfg[s].cols;
        p->rows = cfg[s].rows;
        if (cfg[s].ps == NULL) {
            continue;
        }
        for (i = 0; i < cap; i++) {
            Node *n = rr_instantiate(cfg[s].ps);
            if (n == NULL) {
                break;
            }
            sp_set_visible(n, false);
            p->nodes[p->cap++] = n;
        }
    }
}

void sp_pool_begin_frame(SpWorld *w)
{
    int s;
    for (s = 0; s < SP_SHEET_COUNT; s++) {
        w->pools[s].used = 0;
    }
}

void sp_pool_end_frame(SpWorld *w)
{
    int s, i;
    for (s = 0; s < SP_SHEET_COUNT; s++) {
        SpSpritePool *p = &w->pools[s];
        for (i = p->used; i < p->cap; i++) {
            sp_set_visible(p->nodes[i], false);
        }
    }
}

void sp_draw_anim_at(SpWorld *w, uint16_t anim_id, int frame_idx, float x, float y, bool flip_h, int16_t z, uint32_t modulate)
{
    const SpelunkyAnim *anim;
    const SpelunkyFrame *fr;
    SpSpritePool *pool;
    Node *n;
    float px, py;

    if (anim_id >= SP_ANIM_COUNT) {
        return;
    }
    anim = &g_sp_anims[anim_id];
    if (anim->frame_count == 0) {
        return;
    }
    if (frame_idx < 0) {
        frame_idx = 0;
    }
    fr = &g_sp_frames[anim->first_frame + (uint16_t)(frame_idx % anim->frame_count)];
    pool = &w->pools[fr->sheet];
    if (pool->used >= pool->cap) {
        return;
    }
    n = pool->nodes[pool->used++];
    n->frame = fr->frame;
    n->z = z;
    n->modulate = modulate;

    py = floorf(y - (float)fr->oy + 0.5f);
    if (!flip_h) {
        px = floorf(x - (float)fr->ox + 0.5f);
        n->local.m[0] = 1.0f;
        n->local.m[1] = 0.0f;
        n->local.m[3] = 0.0f;
        n->local.m[4] = 1.0f;
    } else {
        float pivot = (fr->ox == 0) ? (float)fr->w : (float)fr->ox;
        px = floorf(x + pivot + 0.5f);
        n->local.m[0] = -1.0f;
        n->local.m[1] = 0.0f;
        n->local.m[3] = 0.0f;
        n->local.m[4] = 1.0f;
    }
    n->local.m[9] = px;
    n->local.m[10] = py;
    sp_set_visible(n, true);
}

void sp_world_clear(SpWorld *w, RRScene *scene)
{
    SpSpritePool saved_pools[SP_SHEET_COUNT];
    uint32_t saved_rng = w->rng;
    memcpy(saved_pools, w->pools, sizeof(saved_pools));
    /* Preserve bound AudioStreamPlayer pointers (sfx_jump..sfx_blink1) and rng */
    memset(w, 0, offsetof(SpWorld, sfx_jump));
    memcpy(w->pools, saved_pools, sizeof(saved_pools));
    w->scene = scene;
    w->rng = saved_rng ? saved_rng : 0x13579BDFu;
    w->player_idx = -1;
    w->olmec_idx = -1;
}

int sp_spawn_entity(SpWorld *w, uint8_t kind, float x, float y)
{
    int i;
    for (i = 0; i < SP_MAX_ENTITIES; i++) {
        if (!w->ents[i].active) {
            SpEntity *e = &w->ents[i];
            memset(e, 0, sizeof(*e));
            e->active = true;
            e->alive = true;
            e->kind = kind;
            e->facing = 1;
            e->x = x;
            e->y = y;
            e->holder = -1;
            e->held_ent = -1;
            e->z = SP_Z_ITEM;
            e->bw = 16;
            e->bh = 16;
            e->anim_speed = 0.25f;
            return i;
        }
    }
    return -1;
}

void sp_destroy_entity(SpWorld *w, int idx)
{
    if (idx >= 0 && idx < SP_MAX_ENTITIES) {
        w->ents[idx].active = false;
        w->ents[idx].alive = false;
    }
}

void sp_set_message(SpWorld *w, const char *m1, const char *m2, int ticks)
{
    snprintf(w->msg1, sizeof(w->msg1), "%s", m1 != NULL ? m1 : "");
    snprintf(w->msg2, sizeof(w->msg2), "%s", m2 != NULL ? m2 : "");
    w->msg_timer = ticks;
}

/* --- Tile & collision helpers --------------------------------------------------------- */

bool sp_tile_in_bounds(const SpWorld *w, int tx, int ty)
{
    return tx >= 0 && tx < w->cols && ty >= 0 && ty < w->rows;
}

bool sp_tile_is_solid(const SpWorld *w, int tx, int ty)
{
    if (!sp_tile_in_bounds(w, tx, ty)) {
        return true;
    }
    return (w->grid[ty][tx].flags & TF_SOLID) != 0;
}

bool sp_tile_is_platform(const SpWorld *w, int tx, int ty)
{
    if (!sp_tile_in_bounds(w, tx, ty)) {
        return false;
    }
    return (w->grid[ty][tx].flags & TF_PLATFORM) != 0;
}

bool sp_tile_is_ladder(const SpWorld *w, int tx, int ty)
{
    if (!sp_tile_in_bounds(w, tx, ty)) {
        return false;
    }
    return (w->grid[ty][tx].flags & TF_CLIMBABLE) != 0;
}

bool sp_tile_is_water(const SpWorld *w, int tx, int ty)
{
    if (!sp_tile_in_bounds(w, tx, ty)) {
        return false;
    }
    return w->grid[ty][tx].fg == TILE_WATER;
}

bool sp_tile_is_lava(const SpWorld *w, int tx, int ty)
{
    if (!sp_tile_in_bounds(w, tx, ty)) {
        return false;
    }
    return w->grid[ty][tx].fg == TILE_LAVA;
}

bool sp_rect_hits_solid(const SpWorld *w, float x0, float y0, float x1, float y1)
{
    int tx0 = sp_floor_div16(x0);
    int ty0 = sp_floor_div16(y0);
    int tx1 = sp_floor_div16(x1 - 0.001f);
    int ty1 = sp_floor_div16(y1 - 0.001f);
    int tx, ty, i;

    for (ty = ty0; ty <= ty1; ty++) {
        for (tx = tx0; tx <= tx1; tx++) {
            if (sp_tile_is_solid(w, tx, ty)) {
                return true;
            }
        }
    }
    /* Also check solid push blocks */
    for (i = 0; i < SP_MAX_ENTITIES; i++) {
        const SpEntity *b = &w->ents[i];
        if (!b->active || b->kind != ENT_PUSH_BLOCK) {
            continue;
        }
        if (x0 < b->x + b->bx + b->bw && x1 > b->x + b->bx &&
            y0 < b->y + b->by + b->bh && y1 > b->y + b->by) {
            return true;
        }
    }
    return false;
}

bool sp_rect_hits_platform(const SpWorld *w, float x0, float y0, float x1, float y1)
{
    int tx0 = sp_floor_div16(x0);
    int ty0 = sp_floor_div16(y0);
    int tx1 = sp_floor_div16(x1 - 0.001f);
    int ty1 = sp_floor_div16(y1 - 0.001f);
    int tx, ty;
    for (ty = ty0; ty <= ty1; ty++) {
        for (tx = tx0; tx <= tx1; tx++) {
            if (sp_tile_is_platform(w, tx, ty)) {
                return true;
            }
        }
    }
    return false;
}

static bool rect_hits_solid_except(const SpWorld *w, float x0, float y0, float x1, float y1, int ignore_idx)
{
    int tx0 = sp_floor_div16(x0);
    int ty0 = sp_floor_div16(y0);
    int tx1 = sp_floor_div16(x1 - 0.001f);
    int ty1 = sp_floor_div16(y1 - 0.001f);
    int tx, ty, i;

    for (ty = ty0; ty <= ty1; ty++) {
        for (tx = tx0; tx <= tx1; tx++) {
            if (sp_tile_is_solid(w, tx, ty)) {
                return true;
            }
        }
    }
    for (i = 0; i < SP_MAX_ENTITIES; i++) {
        const SpEntity *b;
        if (i == ignore_idx) {
            continue;
        }
        b = &w->ents[i];
        if (!b->active || b->kind != ENT_PUSH_BLOCK) {
            continue;
        }
        if (x0 < b->x + b->bx + b->bw && x1 > b->x + b->bx &&
            y0 < b->y + b->by + b->bh && y1 > b->y + b->by) {
            return true;
        }
    }
    return false;
}

bool sp_entity_hits_solid(const SpWorld *w, const SpEntity *e, float dx, float dy)
{
    int self_idx = (int)(e - w->ents);
    float x0 = e->x + e->bx + dx;
    float y0 = e->y + e->by + dy;
    return rect_hits_solid_except(w, x0, y0, x0 + e->bw, y0 + e->bh, self_idx);
}

bool sp_entity_on_ground(const SpWorld *w, const SpEntity *e)
{
    if (sp_entity_hits_solid(w, e, 0.0f, 1.0f)) {
        return true;
    }
    {
        float x0 = e->x + e->bx;
        float x1 = x0 + e->bw;
        float foot = e->y + e->by + e->bh;
        if (!sp_rect_hits_platform(w, x0, foot - 0.5f, x1, foot) &&
            sp_rect_hits_platform(w, x0, foot, x1, foot + 1.0f)) {
            return true;
        }
    }
    return false;
}

bool sp_entities_overlap(const SpEntity *a, const SpEntity *b)
{
    float ax0 = a->x + a->bx, ay0 = a->y + a->by;
    float bx0 = b->x + b->bx, by0 = b->y + b->by;
    return ax0 < bx0 + b->bw && ax0 + a->bw > bx0 &&
           ay0 < by0 + b->bh && ay0 + a->bh > by0;
}

void sp_move_entity(SpWorld *w, int idx, float vx, float vy, bool collide_platforms, bool *hit_x, bool *hit_y)
{
    SpEntity *e = &w->ents[idx];
    float tx = e->x_rem + vx;
    float ty = e->y_rem + vy;
    int ix = (int)truncf(tx);
    int iy = (int)truncf(ty);
    int s, step;

    e->x_rem = tx - (float)ix;
    e->y_rem = ty - (float)iy;
    if (hit_x) *hit_x = false;
    if (hit_y) *hit_y = false;

    /* Move X 1 pixel at a time */
    s = ix > 0 ? 1 : -1;
    for (step = 0; step != ix; step += s) {
        if (!sp_entity_hits_solid(w, e, (float)s, 0.0f)) {
            e->x += (float)s;
        } else {
            if (hit_x) *hit_x = true;
            e->x_rem = 0.0f;
            break;
        }
    }

    /* Move Y 1 pixel at a time */
    s = iy > 0 ? 1 : -1;
    for (step = 0; step != iy; step += s) {
        if (sp_entity_hits_solid(w, e, 0.0f, (float)s)) {
            if (hit_y) *hit_y = true;
            e->y_rem = 0.0f;
            break;
        }
        if (collide_platforms && s > 0) {
            float x0 = e->x + e->bx;
            float x1 = x0 + e->bw;
            float foot = e->y + e->by + e->bh;
            /* Land on top of platform/ladder-top/tree-branch only if not already inside it */
            if (!sp_rect_hits_platform(w, x0, foot - 1.0f, x1, foot) &&
                sp_rect_hits_platform(w, x0, foot, x1, foot + 1.0f)) {
                if (hit_y) *hit_y = true;
                e->y_rem = 0.0f;
                break;
            }
        }
        e->y += (float)s;
    }
}

void sp_spawn_fx(SpWorld *w, uint8_t fx_kind, float x, float y, float vx, float vy)
{
    int idx = sp_spawn_entity(w, ENT_FX, x, y);
    if (idx < 0) {
        return;
    }
    SpEntity *e = &w->ents[idx];
    e->subtype = fx_kind;
    e->vx = vx;
    e->vy = vy;
    e->z = SP_Z_FX;
    e->bw = 8;
    e->bh = 8;
    if (fx_kind == FX_BLOOD) {
        e->anim_id = SP_ANIM_BLOOD;
        e->timer = 24;
    } else if (fx_kind == FX_RUBBLE) {
        e->anim_id = (g_sp_run.level_type == SP_AREA_LUSH) ? SP_ANIM_RUBBLE_LUSH
                   : (g_sp_run.level_type == SP_AREA_TEMPLE) ? SP_ANIM_RUBBLE_TAN
                   : (g_sp_run.level_type == SP_AREA_ICE) ? SP_ANIM_RUBBLE_DARK
                   : SP_ANIM_RUBBLE;
        e->timer = 28;
    } else if (fx_kind == FX_SMOKE) {
        e->anim_id = SP_ANIM_SMOKE_PUFF;
        e->timer = 18;
    } else if (fx_kind == FX_SPARK) {
        e->anim_id = SP_ANIM_FLARE_SPARK;
        e->timer = 14;
    } else if (fx_kind == FX_POOF) {
        e->anim_id = SP_ANIM_POOF;
        e->timer = 14;
    } else if (fx_kind == FX_SPLASH) {
        e->anim_id = SP_ANIM_SPLASH;
        e->timer = 14;
    } else if (fx_kind == FX_HEART) {
        e->anim_id = SP_ANIM_SMOOCH_HEART;
        e->timer = 30;
    } else if (fx_kind == FX_BURN) {
        e->anim_id = SP_ANIM_BURN;
        e->timer = 16;
    } else if (fx_kind == FX_PSYCHIC) {
        e->anim_id = SP_ANIM_PSYCHIC_WAVE;
        e->timer = 12;
    }
}

void sp_spawn_blood(SpWorld *w, float x, float y, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        float vx = (float)sp_rand_range(w, -30, 30) * 0.1f;
        float vy = (float)sp_rand_range(w, -40, -5) * 0.1f;
        sp_spawn_fx(w, FX_BLOOD, x, y, vx, vy);
    }
}

void sp_destroy_tile(SpWorld *w, int tx, int ty, bool spawn_rubble)
{
    SpTile *t;
    float cx, cy;
    if (!sp_tile_in_bounds(w, tx, ty)) {
        return;
    }
    t = &w->grid[ty][tx];
    if (t->fg == TILE_NONE || (t->flags & TF_INVINCIBLE)) {
        return;
    }
    cx = (float)tx * 16.0f + 8.0f;
    cy = (float)ty * 16.0f + 8.0f;

    /* Embedded gold drops nuggets */
    if (t->fg == TILE_GOLD || t->fg == TILE_GOLD_BIG || t->fg == TILE_TEMPLE_GOLD) {
        int g = sp_spawn_entity(w, ENT_TREASURE, cx, cy);
        if (g >= 0) {
            SpEntity *ge = &w->ents[g];
            ge->subtype = (t->fg == TILE_GOLD_BIG || t->fg == TILE_TEMPLE_GOLD) ? TR_GOLD_NUGGET : TR_GOLD_CHUNK;
            ge->anim_id = (ge->subtype == TR_GOLD_NUGGET) ? SP_ANIM_GOLD_NUGGET : SP_ANIM_GOLD_CHUNK;
            ge->value = (ge->subtype == TR_GOLD_NUGGET) ? 500 : 100;
            ge->bx = -4; ge->by = -4; ge->bw = 8; ge->bh = 8;
            ge->vy = -2.0f;
        }
    }

    if (t->flags & TF_SHOP) {
        w->shop_angry = true;
    }

    t->fg = TILE_NONE;
    t->flags &= (uint8_t)~(TF_SOLID | TF_PLATFORM | TF_BLOODY | TF_FIRED);
    t->fg_anim = SP_ANIM_COUNT;

    if (spawn_rubble) {
        int i;
        for (i = 0; i < 4; i++) {
            float vx = (float)sp_rand_range(w, -25, 25) * 0.1f;
            float vy = (float)sp_rand_range(w, -35, -10) * 0.1f;
            sp_spawn_fx(w, FX_RUBBLE, cx, cy, vx, vy);
        }
    }
}

void sp_explode(SpWorld *w, float cx, float cy, int radius_px, bool hurt_player)
{
    int tx0 = sp_floor_div16(cx - (float)radius_px);
    int ty0 = sp_floor_div16(cy - (float)radius_px);
    int tx1 = sp_floor_div16(cx + (float)radius_px);
    int ty1 = sp_floor_div16(cy + (float)radius_px);
    int tx, ty, i;
    int exp_idx;

    sp_play_sfx(w->sfx_explosion);
    w->shake_ticks = 12;

    exp_idx = sp_spawn_entity(w, ENT_EXPLOSION, cx, cy);
    if (exp_idx >= 0) {
        SpEntity *ex = &w->ents[exp_idx];
        ex->anim_id = SP_ANIM_EXPLOSION;
        ex->timer = 14;
        ex->z = SP_Z_FX;
    }

    for (ty = ty0; ty <= ty1; ty++) {
        for (tx = tx0; tx <= tx1; tx++) {
            float bx = (float)tx * 16.0f + 8.0f;
            float by = (float)ty * 16.0f + 8.0f;
            float dx = bx - cx, dy = by - cy;
            if (dx * dx + dy * dy <= (float)(radius_px * radius_px)) {
                sp_destroy_tile(w, tx, ty, true);
            }
        }
    }
    sp_autotile_world(w);

    for (i = 0; i < SP_MAX_ENTITIES; i++) {
        SpEntity *e = &w->ents[i];
        float ex, ey, dx, dy;
        if (!e->active || !e->alive) {
            continue;
        }
        ex = e->x + e->bx + e->bw * 0.5f;
        ey = e->y + e->by + e->bh * 0.5f;
        dx = ex - cx;
        dy = ey - cy;
        if (dx * dx + dy * dy > (float)((radius_px + 12) * (radius_px + 12))) {
            continue;
        }
        if (e->kind == ENT_PLAYER) {
            if (hurt_player) {
                sp_hurt_player(w, 10, "KILLED BY AN EXPLOSION", dx >= 0 ? 4.0f : -4.0f, -5.0f);
            }
        } else if (e->kind == ENT_BOMB) {
            if (e->armed && e->timer > 2) {
                e->timer = 2; /* chain detonation */
            }
        } else if (e->kind == ENT_PUSH_BLOCK || e->kind == ENT_CRATE || e->kind == ENT_JAR) {
            sp_destroy_entity(w, i);
        } else if (e->kind == ENT_OLMEC) {
            /* Olmec is impervious to bombs */
        } else if (e->kind >= ENT_SNAKE && e->kind <= ENT_DAMSEL) {
            e->hp -= 10;
            e->vx = dx >= 0 ? 4.0f : -4.0f;
            e->vy = -4.0f;
            if (e->kind == ENT_SHOPKEEPER) {
                w->shop_angry = true;
            }
        }
    }
}
