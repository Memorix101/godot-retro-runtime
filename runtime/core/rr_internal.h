/* Functions shared between the core's own source files. Not for game code. */
#ifndef RR_INTERNAL_H
#define RR_INTERNAL_H

#include "rr_scene.h"

/* rr_input.c */
void rr_input_attach(const RRPackage *pkg); /* NULL detaches */
void rr_input_update(void);

/* rr_scene.c */
/* World transforms of a node and everything below it, from their local transforms. */
void rr_scene_update_subtree(RRScene *scene, Node *root);
/* Looks the texts of the nodes that translate theirs up again, after a change of locale. */
void rr_scene_retranslate(RRScene *scene);
/* World transform of a node from the local transforms as they are right now. `world` of
 * a node is one update old inside a component. */
void rr_node_world_now(const Node *node, RRMat34 *out);
/* Remembers the state of the autoload components before the scene goes away. */
void rr_scene_keep_autoloads(const RRScene *scene);

/* rr_audio.c */
void rr_audio_init(RRScene *scene, Node *player); /* during scene creation */
void rr_audios_update(RRScene *scene, float dt);
/* What a player sounds like from the camera right now: volume, and pan from -1 (left) to 1. */
void rr_audio_mix(const Node *player, float *volume, float *pan);

/* rr_app.c */
const char *rr_app_data_dir(void); /* where the packages and the music files are */

/* rr_tween.c */
void rr_tweens_update(RRScene *scene, float dt);

/* rr_body.c */
void rr_rigid_body_init(RRScene *scene, Node *body); /* during scene creation */
void rr_rigid_bodies_ready(RRScene *scene);          /* once all nodes of the scene are there */
void rr_rigid_body_reset(Node *body);
void rr_rigid_bodies_update(RRScene *scene, float dt);

/* rr_physics.c: the RigidBody3D nodes, which a physics engine moves */
void rr_physics_begin(const RRScene *scene); /* when a scene is made: empties the engine's world */
void rr_physics_body_init(RRScene *scene, Node *node, RRRigidState *state);
void rr_physics_body_reset(Node *node, RRRigidState *state);
void rr_physics_step(RRScene *scene, float dt);
void rr_physics_get_velocity(const RRRigidState *state, float *linear, float *angular); /* either may be NULL */
void rr_physics_set_velocity(RRRigidState *state, const float *linear, const float *angular);
/* `offset`: where the push is, from the middle of the body, in the world; NULL for the middle. */
void rr_physics_apply_impulse(RRRigidState *state, const float *impulse, const float *offset);
/* Stands a node where `world` says, whatever its parents are. */
void rr_node_set_world(Node *node, const RRMat34 *world);

/* rr_physics2d.c: the RigidBody2D nodes, which Box2D moves. As the functions above. */
void rr_physics2d_begin(const RRScene *scene);
void rr_physics2d_body_init(RRScene *scene, Node *node, RRRigidState *state);
void rr_physics2d_body_reset(Node *node, RRRigidState *state);
void rr_physics2d_step(RRScene *scene, float dt);
void rr_physics2d_get_velocity(const RRRigidState *state, float *linear, float *angular);
void rr_physics2d_set_velocity(RRRigidState *state, const float *linear, const float *angular);
void rr_physics2d_apply_impulse(RRRigidState *state, const float *impulse, const float *offset);
bool rr_body_solids_are_boxes(const RRScene *scene); /* needs the world transforms */
/* True if a box, sphere or capsule overlaps a solid: `normal` then points from the solid
 * towards it, and `depth` is how far the two overlap along it. */
bool rr_volume_contact(const RRVolume *mover, const RRVolume *solid, float *normal, float *depth);

/* rr_path.c */
void rr_path_follow_apply(Node *follow); /* sets `local` from the node's progress */

/* rr_raycast.c */
void rr_raycast_init(RRScene *scene, Node *ray); /* during scene creation */
void rr_raycasts_update(RRScene *scene);

/* rr_trigger.c */
/* A collision shape in world space. */
struct RRVolume {
    bool  sphere, capsule, used; /* used: by the trigger tests, false for a shape that is off */
    bool  one_way; /* stops only bodies that come down onto it (rr_body.c) */
    float center[3];
    float axis[3][3]; /* unit vectors (box only) */
    float half[3];    /* box: half extents; sphere: radius in half[0] */
    float radius, seg; /* capsule: radius, and half the length of its line along axis[1] */
    /* Height map (the ground of a HeightMapShape3D), NULL for the other shapes. Such a
     * shape is never rotated. Who does not know height maps sees the box around it. */
    const RRPkgHeightMap *map;
    float                 origin[3]; /* where point (0, 0) with height 0 is */
    float                 scale[3];  /* distance between two points along X and Z; Y: of the heights */
    /* Concave shape (ConcavePolygonShape3D), NULL for the others: its triangles in the
     * shape's space and the shape's world transform. center, axis and half are the box
     * around the triangles, which is what code that does not know such shapes sees. */
    const RRPkgTriangles *tris;
    bool                  backface; /* solid from behind its faces too */
    RRMat34               frame;
};

/* Triangle `index` of a concave volume in the world, and the unit normal of its front
 * (Godot's: the side its corners go round clockwise). False if it has no area. */
bool rr_concave_triangle(const RRVolume *v, uint32_t index, float *a, float *b, float *c, float *normal);
/* The deepest place where a sphere, capsule or box overlaps the faces of a concave
 * volume: the way out of them (unit) and how deep. False if it overlaps none. */
bool rr_concave_contact(const RRVolume *concave, const RRVolume *other, float *normal, float *depth);

/* The height of a height map's ground below or above (x, z) in the world, and the normal
 * of the ground there if `normal` is not NULL. False if (x, z) is outside of the map. */
bool rr_height_at(const RRVolume *v, float x, float z, float *height, float *normal);

/* World-space volume of a CollisionShape node, with the node's scale applied. */
void rr_volume_from_shape(RRVolume *v, const Node *shape);
void rr_volume_make(RRVolume *v, const RRMat34 *world, const RRPkgShapeNode *shape);
void rr_triggers_update(RRScene *scene);
/* Collider lists, made during scene creation; the counts size the scene's memory. */
uint32_t rr_trigger_shape_count(const RRPackage *pkg);
uint32_t rr_trigger_pair_capacity(uint32_t areas, uint32_t bodies);
void     rr_trigger_init(RRScene *scene); /* after `areas` and `bodies` are filled */
/* Works out where every area and body is (their shapes in `volumes`, the boxes around
 * them, the sweep order). Rays and bodies use it as well as the trigger tests, so it
 * runs whenever the world transforms changed: after each transform pass, and after
 * nodes were freed. rr_triggers_update relies on it having run since the nodes moved. */
void     rr_colliders_refresh(RRScene *scene);
void     rr_volume_bounds(const RRVolume *v, float *min, float *max);
/* Forgets what an area or body overlaps, so that it enters everything it touches anew
 * (an instance that is handed out again). */
void rr_trigger_forget(RRScene *scene, const Node *node);

/* rr_anim.c */
void rr_anim_init(RRScene *scene, Node *player); /* during scene creation, in tree order */
void rr_anim_reset(Node *player);                /* back to the state after loading */
void rr_anims_update(RRScene *scene, float dt);

/* rr_sprite.c */
void                    rr_sprite_init(RRScene *scene, Node *sprite); /* during scene creation */
void                    rr_sprite_reset(Node *sprite);
void                    rr_sprites_update(RRScene *scene, float dt);
RRSpriteState          *rr_sprite_state(const Node *sprite);
const RRPkgSpriteFrame *rr_sprite_frame(const Node *sprite, const RRSpriteState *state);

/* rr_timer.c */
void rr_timer_init(RRScene *scene, Node *timer); /* during scene creation */
void rr_timer_reset(Node *timer);
void rr_timers_update(RRScene *scene, float dt);

/* rr_particles.c */
void rr_particles_init(RRScene *scene, Node *particles); /* during scene creation */
void rr_particles_reset(Node *particles); /* removes its particles as well */
void rr_particles_update(RRScene *scene, float dt);
void rr_particles_draw(const RRScene *scene); /* needs the scene's camera */
/* The particles of one 2D system; (ox, oy) and (zx, zy) are the canvas origin and zoom. */
void rr_particles_draw_2d(const RRScene *scene, const Node *node, float ox, float oy, float zx,
                          float zy);

/* rr_skin.c: mesh nodes with a copy of their mesh, for a skin or a colour that changes */
#define rr_mesh_node_has_copy(d) ((d)->skin != RR_NONE32 || (d)->tinted)
/* During scene creation: `memory` is where the node's copy of its mesh goes. */
void rr_skin_init(RRScene *scene, Node *node, uint8_t *memory);
/* Puts the vertices of every mesh with a skin where its bones are now. Needs the world
 * transforms. */
void rr_skins_update(RRScene *scene);
/* The copy of a mesh node with a skin or a tint (posed, coloured); NULL for any other
 * node. */
const RRPkgMesh *rr_skin_mesh(const RRScene *scene, const Node *node);

/* rr_canvas.c */
/* Works out where the Camera2D puts the view and places the Parallax2D nodes. Needs the
 * world transforms; call it once per frame. */
void rr_canvas_update(RRScene *scene, float dt);
/* The Parallax2D above a node that repeats what is below it: its repeat size and the
 * number of extra copies. NULL if the node is drawn once. */
const Node *rr_canvas_repeat(const Node *node, float *step, int *times);

/* rr_ui.c */
void rr_ui_init(RRScene *scene);
void rr_ui_update(RRScene *scene, float dt);

/* A node is shown only if it and all of its ancestors are visible and in the tree. */
static inline bool rr_node_visible_in_tree(const Node *n)
{
    for (; n != NULL; n = n->parent) {
        if ((n->flags & (RR_NODE_FLAG_VISIBLE | RR_NODE_FLAG_FREE)) != RR_NODE_FLAG_VISIBLE) {
            return false;
        }
    }
    return true;
}

#endif
