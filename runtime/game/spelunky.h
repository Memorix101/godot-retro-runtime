/* Spelunky Classic port for godot4-retro (640x480). */
#ifndef SPELUNKY_H
#define SPELUNKY_H

#include <stdbool.h>
#include <stdint.h>

#include "../core/rr_scene.h"
#include "spelunky_sprites.h"

#define SP_TILE_SIZE      16
#define SP_VIRT_W         320
#define SP_VIRT_H         240
#define SP_MAX_COLS       42
#define SP_MAX_ROWS       56
#define SP_MAX_ENTITIES   256
#define SP_FIXED_DT       (1.0f / 30.0f)

#define SP_SAVE_NAME      "SPELUNKY"
#define SP_SAVE_MAGIC     0x53504C31u /* "SPL1" */

/* Z levels registered by ZAnchors in spelunky_title/spelunky_level. */
enum {
    SP_Z_BG = 0,
    SP_Z_BACK_TILE = 1,
    SP_Z_FG_TILE = 2,
    SP_Z_ITEM = 3,
    SP_Z_ENEMY = 4,
    SP_Z_PLAYER = 5,
    SP_Z_FX = 6,
    SP_Z_DARK = 7
};

/* Area themes matching GDSpelunky global.level_type */
enum {
    SP_AREA_MINES = 0,
    SP_AREA_LUSH = 1,
    SP_AREA_ICE = 2,
    SP_AREA_TEMPLE = 3,
    SP_AREA_OLMEC = 4,
    SP_AREA_TUTORIAL = 5
};

/* Foreground tile types */
enum {
    TILE_NONE = 0,
    TILE_BRICK,       /* destructible area dirt/stone */
    TILE_ROCK,        /* hard indestructible border / metal / stone */
    TILE_DARK,        /* indestructible dark/gold brick */
    TILE_GOLD,        /* embedded gold nugget */
    TILE_GOLD_BIG,    /* embedded big gold */
    TILE_LUSH,
    TILE_ICE,
    TILE_THIN_ICE,
    TILE_TEMPLE,
    TILE_TEMPLE_GOLD,
    TILE_SHOP_WALL,
    TILE_SHOP_BLOCK,
    TILE_SPIKES,
    TILE_WATER,
    TILE_LAVA,
    TILE_ARROW_TRAP_L,
    TILE_ARROW_TRAP_R,
    TILE_SPEAR_TRAP,
    TILE_SMASH_TRAP,
    TILE_SPRING_TRAP,
    TILE_THWOMP,
    TILE_TREE_TRUNK,
    TILE_TREE_TOP,
    TILE_TREE_BRANCH,
    TILE_ALTAR_L,
    TILE_ALTAR_R,
    TILE_SAC_ALTAR_L,
    TILE_SAC_ALTAR_R,
    TILE_GRAVE,
    TILE_ALIEN_FLOOR
};

/* Background / climbable / interactive tile layer */
enum {
    BG_NONE = 0,
    BG_LADDER,
    BG_LADDER_TOP,
    BG_VINE,
    BG_VINE_TOP,
    BG_ROPE,
    BG_ROPE_TOP,
    BG_ENTRANCE,
    BG_EXIT,
    BG_GOLD_DOOR,
    BG_SIGN_GENERAL,
    BG_SIGN_BOMB,
    BG_SIGN_WEAPON,
    BG_SIGN_CLOTHING,
    BG_SIGN_RARE,
    BG_SIGN_CRAPS,
    BG_SIGN_KISSING,
    BG_LAMP,
    BG_LAMP_RED,
    BG_TIKI_TORCH,
    BG_MOAI,
    BG_KALI_HEAD,
    BG_WEB,
    BG_MUSHROOM
};

enum {
    TF_INVINCIBLE = 1 << 0,
    TF_BLOODY     = 1 << 1,
    TF_SHOP       = 1 << 2,
    TF_FIRED      = 1 << 3,
    TF_LIT        = 1 << 4,
    TF_SOLID      = 1 << 5,
    TF_PLATFORM   = 1 << 6,
    TF_CLIMBABLE  = 1 << 7
};

typedef struct {
    uint8_t  fg;
    uint8_t  bg;
    uint8_t  flags;
    uint8_t  variant;
    uint16_t fg_anim;
    uint16_t bg_anim;
} SpTile;

/* Entity kinds */
enum {
    ENT_NONE = 0,
    ENT_PLAYER,
    /* Enemies */
    ENT_SNAKE,
    ENT_BAT,
    ENT_SPIDER,
    ENT_GIANT_SPIDER,
    ENT_CAVEMAN,
    ENT_SKELETON,
    ENT_ZOMBIE,
    ENT_VAMPIRE,
    ENT_FROG,
    ENT_FIRE_FROG,
    ENT_PIRANHA,
    ENT_JAWS,
    ENT_MAN_TRAP,
    ENT_YETI,
    ENT_YETI_KING,
    ENT_ALIEN,
    ENT_UFO,
    ENT_HAWKMAN,
    ENT_MONKEY,
    ENT_TOMB_LORD,
    ENT_MAGMA_MAN,
    ENT_GHOST,
    ENT_SHOPKEEPER,
    ENT_DAMSEL,
    ENT_OLMEC,
    /* Interactive blocks / containers / holdables / items */
    ENT_PUSH_BLOCK,
    ENT_BOULDER,
    ENT_CRATE,
    ENT_CHEST,
    ENT_LOCKED_CHEST,
    ENT_ROCK,
    ENT_JAR,
    ENT_SKULL,
    ENT_KEY,
    ENT_GOLD_IDOL,
    ENT_CRYSTAL_SKULL,
    ENT_MACHETE,
    ENT_MATTOCK,
    ENT_PISTOL,
    ENT_SHOTGUN,
    ENT_BOW,
    ENT_WEB_CANNON,
    ENT_TELEPORTER,
    ENT_SCEPTRE,
    ENT_FLARE,
    ENT_DICE,
    ENT_SHOP_ITEM,
    ENT_TREASURE,
    /* Projectiles & effects */
    ENT_BOMB,
    ENT_ROPE_THROW,
    ENT_ARROW,
    ENT_BULLET,
    ENT_LASER,
    ENT_WEB_SHOT,
    ENT_EXPLOSION,
    ENT_FX
};

/* Treasure subtypes */
enum {
    TR_GOLD_CHUNK = 0,
    TR_GOLD_NUGGET,
    TR_GOLD_BAR,
    TR_GOLD_BARS,
    TR_EMERALD,
    TR_SAPPHIRE,
    TR_RUBY,
    TR_DIAMOND,
    TR_EMERALD_BIG,
    TR_SAPPHIRE_BIG,
    TR_RUBY_BIG,
    TR_SCARAB
};

/* Shop / Pickup item subtypes (for ENT_SHOP_ITEM) */
enum {
    ITEM_BOMB_BAG = 0,
    ITEM_BOMB_BOX,
    ITEM_ROPE_PILE,
    ITEM_PASTE,
    ITEM_COMPASS,
    ITEM_SPRING_SHOES,
    ITEM_SPIKE_SHOES,
    ITEM_GLOVES,
    ITEM_MITT,
    ITEM_SPECTACLES,
    ITEM_CAPE,
    ITEM_JETPACK,
    ITEM_PARACHUTE,
    ITEM_UDJAT_EYE,
    ITEM_ANKH,
    ITEM_CROWN,
    ITEM_KAPALA
};

/* FX subtypes */
enum {
    FX_BLOOD = 0,
    FX_RUBBLE,
    FX_SMOKE,
    FX_SPARK,
    FX_POOF,
    FX_SPLASH,
    FX_HEART,
    FX_BURN,
    FX_PSYCHIC
};

/* Player/character selection */
enum {
    SP_CHAR_EXPLORER = 0,
    SP_CHAR_DAMSEL = 1,
    SP_CHAR_TUNNEL_MAN = 2
};

/* Passive inventory flags */
enum {
    PASS_UDJAT     = 1 << 0,
    PASS_ANKH      = 1 << 1,
    PASS_CROWN     = 1 << 2,
    PASS_KAPALA    = 1 << 3,
    PASS_SPECTACLES= 1 << 4,
    PASS_GLOVES    = 1 << 5,
    PASS_MITT      = 1 << 6,
    PASS_SPRING    = 1 << 7,
    PASS_SPIKE     = 1 << 8,
    PASS_CAPE      = 1 << 9,
    PASS_JETPACK   = 1 << 10,
    PASS_COMPASS   = 1 << 11,
    PASS_PARACHUTE = 1 << 12,
    PASS_PASTE     = 1 << 13
};

enum {
    ST_IDLE = 0,
    ST_WALK,
    ST_JUMP,
    ST_FALL,
    ST_CLIMB,
    ST_HANG,
    ST_DUCK,
    ST_LOOK_UP,
    ST_ATTACK,
    ST_STUNNED,
    ST_DEAD,
    ST_PATROL,
    ST_ANGRY,
    ST_EXITING
};

typedef struct {
    bool     active;
    bool     alive;
    bool     held;
    bool     for_sale;
    bool     invincible;
    bool     safe;
    bool     armed;
    bool     on_ground;
    bool     flip_h;

    uint8_t  kind;
    uint8_t  subtype;
    uint8_t  state;
    int8_t   facing; /* -1 left, +1 right */

    float    x, y;
    float    vx, vy;
    float    x_rem, y_rem; /* subpixel accumulator */
    int16_t  bx, by, bw, bh; /* AABB relative to (x,y) */

    int16_t  hp;
    int16_t  timer;
    int16_t  timer2;
    int16_t  counter;
    int16_t  cost;
    int32_t  value;
    int16_t  holder; /* entity index holding this, or -1 */
    int16_t  held_ent; /* entity index held by this, or -1 */

    uint16_t anim_id;
    float    anim_frame;
    float    anim_speed;
    int16_t  z;
} SpEntity;

/* Saved high scores & stats (persisted via rr_save_write) */
typedef struct {
    uint32_t magic;
    int32_t  top_money;
    int32_t  best_time;  /* frames or seconds of fastest win, 0 if none */
    int32_t  most_kills;
    int32_t  most_saves;
    int32_t  plays;
    int32_t  deaths;
    int32_t  wins;
    int32_t  tunnel1;
    int32_t  tunnel2;
} SpHighscores;

/* Cross-scene run state */
typedef struct {
    bool     initialized;
    bool     in_run;
    bool     is_tutorial;
    bool     game_over;
    bool     victory;
    bool     show_intro;

    int32_t  character; /* SP_CHAR_* */
    int32_t  curr_level; /* 1..16 (0 = tutorial) */
    int32_t  level_type; /* SP_AREA_* */
    int32_t  plife;
    int32_t  bombs;
    int32_t  rope;
    int32_t  arrows;
    int32_t  money;
    int32_t  xmoney; /* earned this level */
    int32_t  kills;
    int32_t  damsels;
    int32_t  xdamsels;
    int32_t  time_ticks;  /* total run ticks at 30 Hz */
    int32_t  xtime_ticks; /* current level ticks */
    int32_t  blood_level; /* Kapala charge 0..7 */
    int32_t  favor;       /* Kali favor */
    int32_t  kali_gift;
    int32_t  kali_punish;
    int32_t  thief_level;
    bool     murderer;

    uint32_t passives;
    uint8_t  held_item_kind; /* carried between levels */

    bool     made_udjat_eye;
    bool     made_market_entrance;
    bool     made_black_market;
    bool     made_moai;
    bool     city_of_gold;
    bool     had_dark_level;
    bool     dark_level;
    bool     snake_pit;
    bool     cemetary;
    bool     lake;
    bool     yeti_lair;
    bool     alien_craft;
    bool     black_market;
    bool     sacrifice_pit;
    bool     ghost_exists;

    char     death_cause[64];

    SpHighscores scores;
} SpRunState;

/* Pool of reusable Sprite2D nodes for one atlas sheet */
typedef struct {
    Node   **nodes;
    int      cap;
    int      used;
    uint16_t cols;
    uint16_t rows;
} SpSpritePool;

/* Complete level/world runtime state */
typedef struct {
    RRScene     *scene;
    Node        *camera;
    Node        *dark_sprite;
    Node        *bg_nodes[4];

    SpSpritePool pools[SP_SHEET_COUNT];

    int          cols, rows;
    int          room_path[4][5];
    int          start_room_x, start_room_y;
    int          end_room_x, end_room_y;
    int          entrance_x, entrance_y;
    int          exit_x, exit_y;
    bool         idol_spawned;
    bool         altar_spawned;
    bool         shop_spawned;
    bool         damsel_spawned;
    bool         locked_chest_spawned;
    bool         key_spawned;
    bool         shop_angry;
    int          shop_x0, shop_y0, shop_x1, shop_y1;

    SpTile       grid[SP_MAX_ROWS][SP_MAX_COLS];
    SpEntity     ents[SP_MAX_ENTITIES];
    int          player_idx;
    int          olmec_idx;

    float        cam_x, cam_y;
    int          shake_ticks;
    bool         shake_toggle;
    int          look_off;

    /* Player action state */
    int          whip_timer;
    int          stun_timer;
    int          invuln_timer;
    int          fall_timer;
    int          jetpack_fuel;
    int          firing_timer;
    int          collect_popup;
    int          collect_timer;
    int          udjat_timer;
    bool         udjat_blink;

    /* Messages */
    char         msg1[64];
    char         msg2[64];
    int          msg_timer;

    /* Audio nodes (lookup by name from audio_node) */
    Node        *sfx_jump;
    Node        *sfx_bigjump;
    Node        *sfx_whip;
    Node        *sfx_throw;
    Node        *sfx_pickup;
    Node        *sfx_coin;
    Node        *sfx_gem;
    Node        *sfx_hurt;
    Node        *sfx_die;
    Node        *sfx_hit;
    Node        *sfx_explosion;
    Node        *sfx_smallexplode;
    Node        *sfx_break;
    Node        *sfx_land;
    Node        *sfx_climb1;
    Node        *sfx_climb2;
    Node        *sfx_steps;
    Node        *sfx_splash;
    Node        *sfx_arrowtrap;
    Node        *sfx_shotgun;
    Node        *sfx_bowpull;
    Node        *sfx_bat;
    Node        *sfx_frog;
    Node        *sfx_ghost;
    Node        *sfx_giantspider;
    Node        *sfx_spiderjump;
    Node        *sfx_alien;
    Node        *sfx_laser;
    Node        *sfx_yetiyell;
    Node        *sfx_zombie;
    Node        *sfx_cavemandie;
    Node        *sfx_alert;
    Node        *sfx_chestopen;
    Node        *sfx_kiss;
    Node        *sfx_damsel;
    Node        *sfx_jetpack;
    Node        *sfx_teleport;
    Node        *sfx_slam;
    Node        *sfx_boing;
    Node        *sfx_trap;
    Node        *sfx_push;
    Node        *sfx_thud;
    Node        *sfx_pause;
    Node        *sfx_click;
    Node        *sfx_letsexplore;
    Node        *sfx_mattockbreak;
    Node        *sfx_psychic;
    Node        *sfx_ignite;
    Node        *sfx_bombready;
    Node        *sfx_blink1;

    uint32_t     rng;
} SpWorld;

/* Input snapshot (sampled each render frame, consumed in fixed 30Hz ticks) */
typedef struct {
    bool left, right, up, down;
    bool jump_down, jump_pressed, jump_released;
    bool whip_pressed, whip_down;
    bool bomb_pressed;
    bool rope_pressed;
    bool run_down;
    bool pay_pressed;
    bool accept_pressed;
    bool pause_pressed;
} SpInput;

extern SpRunState g_sp_run;

/* spelunky_common.c */
void     sp_run_init_once(void);
void     sp_run_reset_new_game(bool tutorial);
void     sp_save_scores(void);
void     sp_record_run_end(bool won);

uint32_t sp_rng_next(SpWorld *w);
int      sp_rand_range(SpWorld *w, int lo, int hi);

Node    *sp_find_child(Node *parent, const char *name);
void     sp_set_visible(Node *node, bool visible);
void     sp_play_sfx(Node *sfx);

void     sp_pool_init(SpSpritePool *pool, const RRPackedScene *packed, int max_count, uint16_t cols, uint16_t rows);
void     sp_pools_setup(SpWorld *w,
                        const RRPackedScene *s16a, int m16a,
                        const RRPackedScene *s16b, int m16b,
                        const RRPackedScene *s32a, int m32a,
                        const RRPackedScene *s32b, int m32b,
                        const RRPackedScene *s64a, int m64a,
                        const RRPackedScene *s64b, int m64b);
void     sp_pool_begin_frame(SpWorld *w);
void     sp_pool_end_frame(SpWorld *w);
void     sp_draw_anim_at(SpWorld *w, uint16_t anim_id, int frame_idx, float x, float y, bool flip_h, int16_t z, uint32_t modulate);

void     sp_world_clear(SpWorld *w, RRScene *scene);
int      sp_spawn_entity(SpWorld *w, uint8_t kind, float x, float y);
void     sp_destroy_entity(SpWorld *w, int idx);
void     sp_set_message(SpWorld *w, const char *m1, const char *m2, int ticks);

/* Tile & collision queries */
bool     sp_tile_in_bounds(const SpWorld *w, int tx, int ty);
bool     sp_tile_is_solid(const SpWorld *w, int tx, int ty);
bool     sp_tile_is_platform(const SpWorld *w, int tx, int ty);
bool     sp_tile_is_ladder(const SpWorld *w, int tx, int ty);
bool     sp_tile_is_water(const SpWorld *w, int tx, int ty);
bool     sp_tile_is_lava(const SpWorld *w, int tx, int ty);
bool     sp_rect_hits_solid(const SpWorld *w, float x0, float y0, float x1, float y1);
bool     sp_rect_hits_platform(const SpWorld *w, float x0, float y0, float x1, float y1);
bool     sp_entity_hits_solid(const SpWorld *w, const SpEntity *e, float dx, float dy);
bool     sp_entity_on_ground(const SpWorld *w, const SpEntity *e);
bool     sp_entities_overlap(const SpEntity *a, const SpEntity *b);
void     sp_move_entity(SpWorld *w, int idx, float vx, float vy, bool collide_platforms, bool *hit_x, bool *hit_y);
void     sp_destroy_tile(SpWorld *w, int tx, int ty, bool spawn_rubble);
void     sp_explode(SpWorld *w, float cx, float cy, int radius_px, bool hurt_player);
void     sp_spawn_fx(SpWorld *w, uint8_t fx_kind, float x, float y, float vx, float vy);
void     sp_spawn_blood(SpWorld *w, float x, float y, int count);

/* spelunky_levelgen.c */
void     sp_generate_level(SpWorld *w);
void     sp_autotile_world(SpWorld *w);
void     sp_render_tiles(SpWorld *w);

/* spelunky_entities.c */
void     sp_step_world(SpWorld *w, const SpInput *in, bool allow_exit);
void     sp_render_entities(SpWorld *w);
void     sp_hurt_player(SpWorld *w, int dmg, const char *cause, float knock_vx, float knock_vy);

#endif
