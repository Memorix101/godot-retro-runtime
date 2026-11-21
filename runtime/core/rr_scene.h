/* Runtime scene: the node tree built from a package, with its component instances.
 * This header is what game components include. */
#ifndef RR_SCENE_H
#define RR_SCENE_H

#include "rr_component.h"
#include "rr_package.h"

typedef struct RRScene RRScene;
typedef struct RRVolume RRVolume; /* rr_internal.h */

struct Node {
    const char *name;
    RRScene    *scene;
    Node       *parent; /* NULL for the root */
    uint8_t     type;   /* RR_NODE_* */
    uint8_t     flags;  /* RR_NODE_FLAG_*; clear RR_NODE_FLAG_VISIBLE to hide a subtree */
    uint16_t    tag_first, tag_count;
    uint16_t    comp_first, comp_count;
    uint16_t    frame; /* Sprite2D: the cell of its sprite sheet that is shown, as in Godot;
                        * Button: 1 while it is pressed (toggle mode) */
    int16_t     z;     /* 2D nodes and controls: the level it is drawn at (rr_package_node_level) */
    float       value; /* ProgressBar and Slider: the value shown; PathFollow: the progress
                        * along the curve */
    RRMat34     local; /* change this to move the node */
    RRMat34     world; /* recomputed from `local` after every update */
    const void *data;  /* type-specific package data (RRPkg...Node), read-only */
    const char *text;  /* Label and Button: the text that is drawn */
};

/* 2D nodes use the same matrices: X and Y of the origin are screen pixels, the basis
 * holds rotation and scale in the XY plane. Controls have a fixed, baked rectangle and
 * do not move with their parent. */

typedef struct {
    const RRComponentType *type;
    Node                  *node;
    void                  *data;
} RRComponentInstance;

/* A signal connection with its handler looked up. */
typedef struct {
    RRComponentInstance *target;
    const RRSignalDesc  *handler;
} RRResolvedConnection;

/* Playback state of one AnimationPlayer node. */
typedef struct {
    uint16_t node;
    int16_t  clip; /* index of the clip that plays or played last, -1 for none */
    bool     playing;
    float    time;
    float    last; /* the time up to which method keys were called, -1 before the start */
    float    speed; /* how fast the clips play: 1.0 = as made */
    /* The clip that played before, while the new one takes over from it. */
    int16_t  from_clip;
    float    from_time;
    float    blend_time, blend_left; /* seconds the change takes, and the part of it that is left, 1..0 */
} RRAnimState;

/* Playback state of one AnimatedSprite2D node. */
typedef struct {
    uint16_t node;
    uint16_t anim;  /* index of the animation that is shown */
    uint16_t frame;
    bool     playing, flip_h, flip_v;
    float    time;  /* time spent in the current frame, in units of 1 / fps */
} RRSpriteState;

/* State of one Timer node. */
typedef struct {
    uint16_t node;
    bool     running;
    float    wait; /* the time a full run takes */
    float    left;
} RRTimerState;

/* An area or a body as the trigger tests see it (rr_trigger.c). */
typedef struct {
    uint16_t node;
    uint16_t shape_first, shape_count; /* its shapes in `collider_shapes` */
    bool     area;                     /* else a body */
    bool     live;                     /* in the tree with a shape that counts */
    bool     fresh, forget;            /* see rr_trigger_forget */
    float    min[3], max[3];           /* the box around its shapes, this frame */
} RRCollider;

/* State of one AudioStreamPlayer node. */
typedef struct {
    uint16_t node;
    bool     playing;
    float    left; /* seconds of the sound until one that does not loop has run out */
    float    pitch; /* how fast it plays: 1.0 = as recorded */
    bool     changed; /* the pitch was set since the platform heard of it */
} RRAudioState;

/* Tweens: what to change, and how the change is spread over the time. The transition
 * and ease numbers are Godot's Tween.TRANS_* and Tween.EASE_*. */
enum { RR_TWEEN_POSITION = 0, RR_TWEEN_SCALE, RR_TWEEN_ROTATION_2D, RR_TWEEN_VALUE };
enum {
    RR_TRANS_LINEAR = 0,
    RR_TRANS_SINE,
    RR_TRANS_QUINT,
    RR_TRANS_QUART,
    RR_TRANS_QUAD,
    RR_TRANS_EXPO,
    RR_TRANS_ELASTIC,
    RR_TRANS_CUBIC,
    RR_TRANS_CIRC,
    RR_TRANS_BOUNCE,
    RR_TRANS_BACK
};
enum { RR_EASE_IN = 0, RR_EASE_OUT, RR_EASE_IN_OUT, RR_EASE_OUT_IN };

#define RR_MAX_TWEENS 32 /* running at the same time, per scene */

typedef struct {
    bool     active, started;
    uint16_t node;
    uint8_t  property, trans, ease;
    float    from[3], to[3];
    float    duration, elapsed; /* elapsed is negative during the delay */
} RRTween;

/* What one CharacterBody stands on, so that a floor that moves can carry it. */
typedef struct {
    uint16_t node;
    uint16_t floor;        /* node index of the shape below it, RR_NONE16 in the air */
    float    floor_pos[3]; /* where that shape was at the body's last move */
} RRCharState;

/* State of one RigidBody node. */
#define RR_MAX_CONTACTS 8
typedef struct {
    uint16_t node;
    float    velocity[3]; /* RigidBody2D: world units per second */
    /* RigidBody3D (rr_physics.c): */
    void    *body;        /* its body in the physics engine, NULL if it has none */
    RRMat34  placed;      /* where the engine put the node last; anything else is a move from outside */
    bool     synced;      /* the body has been put where the node is */
    uint8_t  contact_max, contact_count; /* contacts it reports, and how many the last step had */
    float    contact_normals[RR_MAX_CONTACTS][3]; /* out of what was touched, world space */
} RRRigidState;

/* A mesh that bones deform: the copy of its mesh asset that holds it as it is posed. */
typedef struct {
    uint16_t   node;
    RRPkgMesh *mesh;
} RRSkinState;

/* What one RayCast node hit at its last update. */
typedef struct {
    uint16_t node;
    uint16_t collider; /* node index of the body, area or tile map, RR_NONE16 for none */
    float    point[3], normal[3]; /* world space */
    float    safe, unsafe; /* shape cast: how far it got without touching, and where it touches */
} RRRayState;

/* State of one particle system and its part of the particle pool. */
typedef struct {
    uint16_t node;
    bool     emitting;
    float    cycle; /* time inside the current emission cycle */
    uint32_t first; /* index of its first particle in the pool */
} RREmitter;

typedef struct {
    float pos[3]; /* world space */
    float vel[3];
    float age;    /* seconds since birth, negative while the particle is not in use */
    float size;   /* scale factor of the quad */
} RRParticle;

/* A PackedScene a component exported: the pool of its instances in this scene. */
struct RRPackedScene {
    RRScene         *scene;
    const RRPkgPool *pool;
};

enum { RR_UI_LEFT = 0, RR_UI_UP, RR_UI_RIGHT, RR_UI_DOWN, RR_UI_ACCEPT, RR_UI_ACTION_COUNT };

struct RRScene {
    const RRPackage      *pkg;
    void                 *memory; /* the one block that holds everything below */
    Node                 *nodes;
    uint32_t              node_count;
    RRComponentInstance  *comps;
    uint32_t              comp_count;
    RRResolvedConnection *conns;   /* one per package connection */
    uint32_t             *handles; /* one per asset: platform handle of textures and sounds */
    uint16_t             *areas;   /* node indices of all Area nodes */
    uint32_t              area_count;
    uint16_t             *bodies;   /* node indices of all StaticBody and CharacterBody nodes */
    uint32_t              body_count;
    RRCollider           *colliders; /* the areas, then the bodies */
    uint32_t              collider_count;
    uint16_t             *collider_shapes; /* node indices of their shapes */
    RRVolume             *volumes;   /* one per entry of collider_shapes, this frame */
    uint16_t             *sweep;     /* collider indices sorted along X */
    uint32_t             *pairs;     /* touching pairs, sorted: first << 16 | second */
    uint32_t             *pairs_next;
    uint32_t              pair_count, pair_capacity;
    bool                  pair_overflow; /* the warning about a full list was printed */
    uint16_t             *solids;   /* node indices of the shapes of all StaticBody nodes */
    uint32_t              solid_count;
    RRAnimState          *anims;    /* one per AnimationPlayer node */
    uint32_t              anim_count;
    RRSpriteState        *sprites;  /* one per AnimatedSprite2D node */
    uint32_t              sprite_count;
    RRTimerState         *timers;   /* one per Timer node */
    uint32_t              timer_count;
    RRTween              *tweens;   /* RR_MAX_TWEENS slots */
    RRCharState          *chars;    /* one per CharacterBody node */
    uint32_t              char_count;
    RRRigidState         *rigids;   /* one per RigidBody node */
    uint32_t              rigid_count;
    bool                  solids_are_boxes; /* no static shape is round or rotated */
    RRAudioState         *audios;   /* one per AudioStreamPlayer node */
    uint32_t              audio_count;
    RRRayState           *rays;     /* one per RayCast node */
    uint32_t              ray_count;
    RRSkinState          *skins;    /* one per mesh node with a skin */
    uint32_t              skin_count;
    uint16_t             *tilemaps; /* node indices of all TileMapLayer nodes */
    uint32_t              tilemap_count;
    RREmitter            *emitters; /* one per particle system */
    uint32_t              emitter_count;
    RRParticle           *particles; /* the pool all particle systems share */
    uint32_t              particle_count;
    RRPackedScene        *packed;   /* one per pool of the package */
    void                **resources; /* one per resource of the package: its struct */
    uint32_t              queued;   /* nodes that rr_queue_free marked this frame */
    bool                  spawned;  /* rr_instantiate ran since the last transform pass */
    uint32_t              rng;
    Node                 *camera;  /* active camera, NULL if the scene has none */
    Node                 *camera2d; /* active Camera2D, NULL if the scene has none */
    Node                 *environment; /* WorldEnvironment, NULL if the scene has none */
    Node                 *focus;    /* focused button or slider, NULL if the scene has none */
    float                 ui_hold;  /* how long a direction has been held on a slider */
    float                 time;     /* seconds since the scene was made: the clock of animated tiles */
    /* The 2D view (rr_canvas.c): the world position of the screen's top left corner, and
     * what the Parallax2D nodes count from. */
    float                 view[2], screen_offset[2];
    float                 camera2d_pos[2], camera2d_smooth[2]; /* where the view is heading for, and is */
    bool                  camera2d_first; /* the camera has not been looked at yet */
    int16_t               levels[RR_MAX_Z_LEVELS]; /* the draw levels in use, lowest first */
    uint32_t              level_count;
    int                   ui_actions[RR_UI_ACTION_COUNT];
};

/* Builds the scene with a single allocation. Fails with an error message if the package
 * uses a component that is not registered, whose fields do not match the C struct, or
 * that lacks a handler for a connected signal. The package must outlive the scene. */
bool rr_scene_create(RRScene *scene, const RRPackage *pkg);
void rr_scene_destroy(RRScene *scene);

/* Hands textures and sounds to the platform layer, and takes them back. */
void rr_scene_load_resources(RRScene *scene);
void rr_scene_unload_resources(RRScene *scene);

/* Calls every component's ready handler once and starts autoplay sounds. */
void rr_scene_ready(RRScene *scene);

/* One frame: input, UI focus, component updates, world transforms, triggers. */
void rr_scene_update(RRScene *scene, float dt);

/* Draws the scene through the platform graphics interface (rr_render.c). */
void rr_scene_draw(const RRScene *scene);

/* --- for game components ----------------------------------------------------------- */

/* Tags are the node's groups from the editor. */
bool rr_node_has_tag(const Node *node, const char *tag);

/* Next node with the tag after `after` in tree order (NULL starts at the root). */
Node *rr_scene_find_tagged(RRScene *scene, const char *tag, const Node *after);

/* Next direct child of `parent` after `after` in tree order (NULL starts at the first). */
Node *rr_node_child(Node *parent, const Node *after);

/* Scene instancing, after PackedScene.instantiate() and Node.queue_free().
 *
 * A component exports a PackedScene together with an int named `<field>_max`. The
 * exporter puts that many instances of the scene into the package, as children of the
 * scene's root that are not in the tree at first. rr_instantiate puts the next unused
 * one into the tree, in the state it has in its scene file, calls the ready handlers of
 * its components and returns its root node; the caller then places it through `local`.
 * It returns NULL if all instances are in use (or `packed` is NULL), so nothing is
 * allocated while the game runs.
 *
 * rr_queue_free removes a node and everything below it from the tree at the end of the
 * frame: no updates, signals, collisions or drawing any more. It works on any node but
 * the scene's root. An instance of a PackedScene can be handed out again afterwards; any
 * other node is gone until the scene is loaded again. Do not keep pointers to freed
 * nodes. */
Node *rr_instantiate(const RRPackedScene *packed);
void  rr_queue_free(Node *node);

/* Autoloads, as set up in the Godot project settings: nodes that are part of every
 * scene, below its root. Their components keep their state from scene to scene, except
 * for the exported fields, which are set from the package each time; the ready handlers
 * run in every scene, so node pointers must be looked up there again.
 * rr_autoload finds one by its name, rr_node_component returns the struct of a node's
 * component of the given type (NULL if it has none). */
Node *rr_autoload(RRScene *scene, const char *name);
void *rr_node_component(Node *node, const char *type);

/* Saved games: a named block of bytes, on the memory card of a console or in a file on
 * the PC. The name has up to 12 characters, capital letters, digits and '_'.
 * rr_save_read fills `data` only if a block of exactly `size` bytes is there. */
bool rr_save_write(const char *name, const void *data, uint32_t size);
bool rr_save_read(const char *name, void *data, uint32_t size);

/* Calls the handler `method` of a node's component, the way a signal would: the ON_SIGNAL
 * and ON_SIGNAL_NODE handlers are what can be called by name. Returns false if the node
 * has no such handler. Method tracks of an AnimationPlayer use this. */
bool rr_node_call(Node *node, const char *method, Node *arg);

/* Emits a signal: calls the handlers connected to it in the editor. `arg` is passed to
 * ON_SIGNAL_NODE handlers and may be NULL. */
void rr_emit(Node *source, const char *signal, Node *arg);

/* Changes the text of a Label, Button or Label3D. The string is not copied and must stay
 * valid. */
void rr_node_set_text(Node *node, const char *text);

/* Translations, after Godot's tr() and TranslationServer.set_locale(). rr_tr returns the
 * message of a key in the locale that is set, from the project's translations (Project
 * Settings > Localization); a text that is no key comes back as it is. The texts of
 * labels and buttons are looked up by themselves, unless auto translate is off for them.
 * rr_set_locale changes the locale ("de", "pt_BR"; a locale of the same language will do)
 * and the texts on the screen with it; it stays set when the scene changes. Before the
 * game sets one, the locale is the language of the machine. */
const char *rr_tr(const RRScene *scene, const char *key);
void        rr_set_locale(RRScene *scene, const char *locale);
const char *rr_get_locale(void);

/* Buttons and sliders. The focus moves between them with the ui_left, ui_up, ui_right
 * and ui_down actions, along the neighbours Godot worked out; ui_accept presses the
 * focused button, which emits `pressed`. A button in toggle mode (a CheckBox, a
 * CheckButton) changes its state first and emits `toggled`. On a slider the two
 * directions along it change `value` by its step and emit `value_changed`; a direction
 * that is held repeats. The ON_SIGNAL_NODE handlers of `toggled` and `value_changed` get
 * the button or slider, to ask it for its new state.
 *
 * A disabled button (a slider that is not editable) is drawn in its disabled look, and
 * the focus passes over it. rr_button_set_disabled works on both. rr_button_set_pressed
 * sets the state of a toggle button without a signal. rr_control_grab_focus moves the
 * focus to a button or slider that can take it. */
void rr_button_set_disabled(Node *button, bool disabled);
bool rr_button_is_disabled(const Node *button);
void rr_button_set_pressed(Node *button, bool pressed);
bool rr_button_is_pressed(const Node *button);
void rr_control_grab_focus(Node *control);

/* ScrollContainer: how far it is scrolled, in pixels, after its properties
 * scroll_horizontal and scroll_vertical. The controls below it are drawn only inside its
 * rectangle. With follow_focus it scrolls by itself to the button or slider that gets the
 * focus. rr_scroll_set keeps the values inside what there is to scroll. */
void rr_scroll_set(Node *scroll, float horizontal, float vertical);
void rr_scroll_get(const Node *scroll, float *horizontal, float *vertical);

/* Plays or stops the sound of an AudioStreamPlayer, AudioStreamPlayer2D or
 * AudioStreamPlayer3D node. A player that does not loop emits `finished` when its sound
 * has run out. */
void rr_audio_play(Node *player);
void rr_audio_stop(Node *player);
bool rr_audio_is_playing(const Node *player);

/* How fast a player plays its sound, after Godot's pitch_scale: 2.0 is twice as fast and
 * an octave higher. It can be changed while the sound plays (an engine that revs up).
 * Music keeps its speed. */
void  rr_audio_set_pitch_scale(Node *player, float pitch);
float rr_audio_get_pitch_scale(const Node *player);

/* Audio buses, after Godot's AudioServer: the buses of the project's bus layout, which
 * the players name in their `bus` property. A player is heard at its own volume times
 * the volume of its bus and of every bus that one plays into, up to Master (bus 0).
 * Volumes are linear: 1.0 is unchanged, 0.0 silent. rr_audio_bus_index returns -1 for a
 * name that is no bus. What is set here stays when the scene changes. */
int   rr_audio_bus_index(const RRScene *scene, const char *name);
void  rr_audio_bus_set_volume(RRScene *scene, int bus, float volume);
float rr_audio_bus_get_volume(const RRScene *scene, int bus);
void  rr_audio_bus_set_mute(RRScene *scene, int bus, bool mute);
bool  rr_audio_bus_is_mute(const RRScene *scene, int bus);

/* Camera2D with position smoothing: lets the view jump to where it is heading for, as
 * Godot's reset_smoothing() does. Call it after moving the camera a long way at once. */
void rr_camera2d_reset_smoothing(RRScene *scene);

/* Animations of an AnimationPlayer node. rr_anim_play starts the clip from its beginning;
 * a clip that does not loop stops on its last frame and emits `animation_finished`.
 * rr_anim_current returns the name of
 * the clip that plays or played last, NULL before the first one. */
void        rr_anim_play(Node *player, const char *clip);
void        rr_anim_stop(Node *player);
bool        rr_anim_playing(const Node *player);
const char *rr_anim_current(const Node *player);
/* How fast a player plays its clips, after Godot's speed_scale; 0 holds the clip still.
 * And the time a clip takes to take over from the one that plays when it is started,
 * after playback_default_blend_time: the two are blended over that many seconds. A part
 * the clip before did not move changes at once. */
void        rr_anim_set_speed_scale(Node *player, float speed);
float       rr_anim_get_speed_scale(const Node *player);
void        rr_anim_set_blend_time(Node *player, float seconds);

/* Animations of an AnimatedSprite2D node, after Godot's methods of the same names.
 * rr_sprite_play starts an animation at its first frame; asking for the one that
 * already plays changes nothing. An animation that does not loop stops on its last
 * frame and emits `animation_finished`. */
void        rr_sprite_play(Node *sprite, const char *animation);
void        rr_sprite_stop(Node *sprite);
bool        rr_sprite_is_playing(const Node *sprite);
const char *rr_sprite_animation(const Node *sprite);
void        rr_sprite_set_flip_h(Node *sprite, bool flip);
void        rr_sprite_set_flip_v(Node *sprite, bool flip);

/* Timer nodes, after Godot's methods. rr_timer_start begins a full run; a wait_time
 * above 0 replaces the one from the inspector, as Timer.start(time_sec) does. */
void  rr_timer_start(Node *timer, float wait_time);
void  rr_timer_stop(Node *timer);
bool  rr_timer_is_stopped(const Node *timer);
float rr_timer_time_left(const Node *timer);

/* Tweens, after Godot's Tween.tween_property(). The property of the node goes from the
 * value it has when the delay is over to `to`, in `duration` seconds:
 *   RR_TWEEN_POSITION     local position, three floats (Z is 0 for a 2D node)
 *   RR_TWEEN_SCALE        local scale, three floats
 *   RR_TWEEN_ROTATION_2D  rotation of a 2D node in radians, one float
 *   RR_TWEEN_VALUE        the node's `value` (ProgressBar, PathFollow), one float
 * Returns false if all RR_MAX_TWEENS slots of the scene are busy. rr_tween_kill stops all
 * tweens of a node where they are. */
bool rr_tween_property(Node *node, int property, const float *to, float duration, float delay,
                       int trans, int ease);
bool rr_tween_is_running(const Node *node);
void rr_tween_kill(Node *node);

/* RayCast nodes (2D and 3D), after Godot's methods. An enabled ray is updated once per
 * frame, after the components ran; rr_raycast_force_update casts it right away from where
 * its node is now. The collider is the body, area or tile map that was hit. Point and
 * normal are three floats in world space (Z is 0 for a 2D ray). */
bool  rr_raycast_is_colliding(const Node *ray);
Node *rr_raycast_get_collider(const Node *ray);
void  rr_raycast_get_collision_point(const Node *ray, float *out);
void  rr_raycast_get_collision_normal(const Node *ray, float *out);
void  rr_raycast_force_update(Node *ray);

/* ShapeCast nodes (2D and 3D) are asked with the same functions: they move a box, sphere
 * or capsule along their line and report the first thing it touches, one result. How far
 * along the line that is, from 0 to 1, as Godot's get_closest_collision_safe_fraction()
 * and get_closest_collision_unsafe_fraction(): the shape is free up to the first and
 * touches at the second. Both are 1 if nothing is in the way. */
float rr_shapecast_get_safe_fraction(const Node *cast);
float rr_shapecast_get_unsafe_fraction(const Node *cast);

/* Path nodes: the length of the curve. A PathFollow child moves along it by its `value`,
 * the distance from the start of the curve (Godot's `progress`). */
float rr_path_get_length(const Node *path);

/* Particle systems. rr_particles_restart removes the particles of a system and starts
 * its emission again, which is how a one-shot burst is fired. */
void rr_particles_restart(Node *particles);
void rr_particles_set_emitting(Node *particles, bool emitting);

/* CharacterBody nodes (2D and 3D), after Godot's move_and_slide(): moves the body by
 * `velocity` (three floats, world units per second; Z is 0 for a 2D body) for `dt`
 * seconds and stops it at the box shapes of the StaticBody nodes its collision mask
 * selects. A component of the velocity that ran into something is set to 0. The body's
 * own shapes are its CollisionShape children. A body that stands on a static body which
 * something moves (a platform on a PathFollow, say) is carried along with it.
 * In scenes made of boxes that are not rotated, all boxes count as their axis-aligned bounds
 * in world space, so bodies and their shapes should not be rotated.
 *
 * The queries tell what the last move touched. The floor is below the body: -Y in 3D,
 * +Y (down the screen) in 2D. */
void rr_body_move_and_slide(Node *body, float *velocity, float dt);
bool rr_body_is_on_floor(const Node *body);
bool rr_body_is_on_wall(const Node *body);
bool rr_body_is_on_ceiling(const Node *body);

/* RigidBody nodes, after Godot's: bodies that move by themselves, with the gravity of the
 * project and their physics material. Velocities and impulses are three floats in world
 * units (Z is 0 for a 2D body).
 *
 * A RigidBody3D is moved by a physics engine (picophysics): it falls, turns, rubs,
 * bounces, is kept out of the static bodies and pushes other rigid bodies. Its shapes are
 * boxes and spheres. Game code may move its node; the body follows. The angular velocity
 * is an axis in the world times radians per second. rr_rigid_body_apply_impulse_at is
 * Godot's apply_impulse(impulse, position): `position` is where the push is, as an offset
 * from the body's origin in the world's directions, and a push away from the middle also
 * turns the body. A body with contact_monitor on and max_contacts_reported above 0 is
 * told what it touched in the last step: how many contacts, and for each the direction
 * out of what was touched, as get_contact_local_normal of Godot's PhysicsDirectBodyState3D.
 *
 * A RigidBody2D falls, bounces and rubs against static bodies and tile maps; it does not
 * turn and does not push other bodies, and the angular and contact functions do nothing
 * for it. */
void rr_rigid_body_get_linear_velocity(const Node *body, float *out);
void rr_rigid_body_set_linear_velocity(Node *body, const float *velocity);
void rr_rigid_body_get_angular_velocity(const Node *body, float *out);
void rr_rigid_body_set_angular_velocity(Node *body, const float *velocity);
void rr_rigid_body_apply_impulse(Node *body, const float *impulse);
void rr_rigid_body_apply_impulse_at(Node *body, const float *impulse, const float *position);
int  rr_rigid_body_get_contact_count(const Node *body);
void rr_rigid_body_get_contact_normal(const Node *body, int index, float *out);

/* Input actions from the Input Map. Look the id up once (in the ready handler); the
 * queries accept the -1 of an unknown action and report it as not pressed. */
int   rr_action(const RRScene *scene, const char *name);
bool  rr_action_down(int action);     /* held */
bool  rr_action_pressed(int action);  /* went down this frame */
float rr_action_strength(int action); /* 0..1, analogue where the binding is */

/* Rumble of the first pad, after Input.start_joy_vibration(): the strength of the light
 * and of the heavy motor, 0..1, for so many seconds (0: until it is stopped). A Dreamcast
 * rumble pack has one motor; it takes the stronger of the two. */
void rr_input_vibrate(float weak, float strong, float seconds);
void rr_input_stop_vibration(void);

/* Loads another scene by name at the end of the frame; rr_quit ends the program. */
void rr_change_scene(const char *name);
void rr_quit(void);

#endif
