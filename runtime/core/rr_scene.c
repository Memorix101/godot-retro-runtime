#include "rr_scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rr_internal.h"
#include "rr_platform.h"

#define ALIGN8(n) (((n) + 7u) & ~7u)

static const RRFieldDesc *find_field(const RRComponentType *type, const char *name)
{
    uint32_t i;
    for (i = 0; i < type->field_count; i++) {
        if (strcmp(type->fields[i].name, name) == 0) {
            return &type->fields[i];
        }
    }
    return NULL;
}

/* Copies exported values from the package into the struct of a component or a resource.
 * `owner` says whose values they are, for the messages. */
static bool fill_fields(RRScene *scene, const RRComponentType *type, void *data,
                        uint32_t field_first, uint32_t field_count, const char *owner)
{
    const RRPackage *pkg = scene->pkg;
    uint32_t         i;

    if (field_count != type->field_count) {
        rr_set_error("%s: '%s' has %u exported fields in the package but %u in the runtime; "
                     "the script and the C struct are out of sync",
                     owner, type->name, (unsigned)field_count, (unsigned)type->field_count);
        return false;
    }
    for (i = 0; i < field_count; i++) {
        const RRPkgField  *pf = &pkg->fields[field_first + i];
        const char        *name = rr_package_string(pkg, pf->name);
        const RRFieldDesc *desc = find_field(type, name);
        uint8_t           *dst;

        if (desc == NULL) {
            rr_set_error("%s: '%s' has no exported field '%s' in the runtime", owner, type->name,
                         name);
            return false;
        }
        if (desc->type != pf->type) {
            rr_set_error("%s: field '%s' of '%s' has a different type in the package than in "
                         "the runtime", owner, name, type->name);
            return false;
        }
        dst = (uint8_t *)data + desc->offset;
        switch (pf->type) {
        case RR_FIELD_FLOAT:
            *(float *)dst = pf->value.f;
            break;
        case RR_FIELD_INT:
            *(int32_t *)dst = pf->value.i;
            break;
        case RR_FIELD_BOOL:
            *(bool *)dst = pf->value.u != 0;
            break;
        case RR_FIELD_SCENE:
            *(const char **)dst = rr_package_string(pkg, pf->value.u);
            break;
        case RR_FIELD_PACKED_SCENE:
            *(const RRPackedScene **)dst =
                pf->value.u == RR_NONE32 ? NULL : &scene->packed[pf->value.u];
            break;
        case RR_FIELD_RESOURCE:
            if (pf->value.u != RR_NONE32 &&
                strcmp(rr_package_string(pkg, pkg->resources[pf->value.u].name), desc->resource) != 0) {
                rr_set_error("%s: field '%s' of '%s' holds a resource of type '%s', the runtime "
                             "expects '%s'", owner, name, type->name,
                             rr_package_string(pkg, pkg->resources[pf->value.u].name), desc->resource);
                return false;
            }
            *(const void **)dst = pf->value.u == RR_NONE32 ? NULL : scene->resources[pf->value.u];
            break;
        default:
            *(Node **)dst = pf->value.u == RR_NONE32 ? NULL : &scene->nodes[pf->value.u];
            break;
        }
    }
    return true;
}

static bool fill_component(RRScene *scene, const RRPkgComponent *pc, RRComponentInstance *inst)
{
    char owner[96];
    snprintf(owner, sizeof(owner), "node '%s'", inst->node->name);
    return fill_fields(scene, inst->type, inst->data, pc->field_first, pc->field_count, owner);
}

/* The saved state of the components of autoload nodes, which outlives a scene. */
#define RR_MAX_AUTOLOAD_STATES 16

static struct {
    char                   node[32];
    const RRComponentType *type;
    void                  *data;
} g_autoload[RR_MAX_AUTOLOAD_STATES];

static int find_autoload_state(const RRComponentInstance *inst)
{
    int i;
    for (i = 0; i < RR_MAX_AUTOLOAD_STATES && g_autoload[i].type != NULL; i++) {
        if (g_autoload[i].type == inst->type && strcmp(g_autoload[i].node, inst->node->name) == 0) {
            return i;
        }
    }
    return -1;
}

void rr_scene_keep_autoloads(const RRScene *scene)
{
    uint32_t c;

    for (c = 0; c < scene->comp_count; c++) {
        const RRComponentInstance *inst = &scene->comps[c];
        int                        i;

        if (!(inst->node->flags & RR_NODE_FLAG_AUTOLOAD)) {
            continue;
        }
        i = find_autoload_state(inst);
        if (i < 0) {
            for (i = 0; i < RR_MAX_AUTOLOAD_STATES && g_autoload[i].type != NULL; i++) {
            }
            if (i == RR_MAX_AUTOLOAD_STATES || strlen(inst->node->name) >= sizeof(g_autoload[i].node)) {
                continue; /* more autoload components than the table holds: this one starts anew */
            }
            g_autoload[i].data = malloc(inst->type->size > 0 ? inst->type->size : 1);
            if (g_autoload[i].data == NULL) {
                continue;
            }
            g_autoload[i].type = inst->type;
            snprintf(g_autoload[i].node, sizeof(g_autoload[i].node), "%s", inst->node->name);
        }
        memcpy(g_autoload[i].data, inst->data, inst->type->size);
    }
}

/* Finds the handler of every signal connection. A connection without one is an error,
 * because the signal would silently do nothing. */
static bool resolve_connections(RRScene *scene)
{
    const RRPackage *pkg = scene->pkg;
    uint32_t         i, c, s;

    for (i = 0; i < pkg->header->conns.count; i++) {
        const RRPkgConnection *pc = &pkg->conns[i];
        const char            *method = rr_package_string(pkg, pc->method);
        const Node            *target = &scene->nodes[pc->target];

        for (c = 0; c < target->comp_count && scene->conns[i].handler == NULL; c++) {
            RRComponentInstance *inst = &scene->comps[target->comp_first + c];
            for (s = 0; s < inst->type->signal_count; s++) {
                if (strcmp(inst->type->signals[s].method, method) == 0) {
                    scene->conns[i].target  = inst;
                    scene->conns[i].handler = &inst->type->signals[s];
                    break;
                }
            }
        }
        if (scene->conns[i].handler == NULL) {
            rr_set_error("signal '%s' of node '%s' is connected to '%s' on node '%s', but no "
                         "component there has a handler with that name",
                         rr_package_string(pkg, pc->signal), scene->nodes[pc->source].name,
                         method, target->name);
            return false;
        }
    }
    return true;
}

static bool has_transform(uint8_t type)
{
    return type == RR_NODE_NODE3D || type == RR_NODE_MESH || type == RR_NODE_CAMERA ||
           type == RR_NODE_AREA || type == RR_NODE_SHAPE || type == RR_NODE_NODE2D ||
           type == RR_NODE_SPRITE || type == RR_NODE_PARTICLES ||
           type == RR_NODE_STATIC_BODY || type == RR_NODE_CHARACTER_BODY ||
           type == RR_NODE_ANIMATED_SPRITE || type == RR_NODE_CAMERA2D ||
           type == RR_NODE_TILEMAP || type == RR_NODE_RAYCAST || type == RR_NODE_PATH ||
           type == RR_NODE_PATH_FOLLOW || type == RR_NODE_AUDIO_SPATIAL ||
           type == RR_NODE_SPRITE3D || type == RR_NODE_LABEL3D || type == RR_NODE_RIGID_BODY ||
           type == RR_NODE_PARALLAX;
}

void rr_node_world_now(const Node *node, RRMat34 *out)
{
    RRMat34 parent;

    if (!has_transform(node->type)) {
        rr_mat34_identity(out);
    } else if (node->parent == NULL) {
        *out = node->local;
    } else {
        rr_node_world_now(node->parent, &parent);
        rr_mat34_mul(out, &parent, &node->local);
    }
}

static void update_world_transforms(RRScene *scene)
{
    uint32_t i;
    /* Parents come before their children, so one forward pass is enough. Nodes without a
     * transform keep an identity world matrix, which restarts the chain below them
     * exactly like Godot does. */
    for (i = 0; i < scene->node_count; i++) {
        Node *n = &scene->nodes[i];
        if (n->type == RR_NODE_PATH_FOLLOW && !(n->flags & RR_NODE_FLAG_FREE)) {
            rr_path_follow_apply(n);
        }
        if (!has_transform(n->type)) {
            continue;
        }
        if (n->parent != NULL) {
            rr_mat34_mul(&n->world, &n->parent->world, &n->local);
        } else {
            n->world = n->local;
        }
    }
}

/* The text of a node as the package has it, or what it means in the locale that is set. */
static const char *node_text(const RRScene *scene, const RRPkgNode *pn, uint32_t text)
{
    const char *raw = rr_package_string(scene->pkg, text);
    return pn->translate ? rr_tr(scene, raw) : raw;
}

void rr_scene_update_subtree(RRScene *scene, Node *root)
{
    const Node *end = scene->nodes + scene->node_count;
    Node       *n;

    /* The nodes below `root` follow it directly: all those whose parent is `root` or
     * comes after it. */
    for (n = root; n < end && (n == root || n->parent >= root); n++) {
        if (!has_transform(n->type)) {
            continue;
        }
        if (n->parent != NULL) {
            rr_mat34_mul(&n->world, &n->parent->world, &n->local);
        } else {
            n->world = n->local;
        }
    }
}

/* Notes a draw level that is in use, keeping the list sorted. The loader made sure
 * there are no more than RR_MAX_Z_LEVELS. */
static void add_level(RRScene *scene, int16_t level)
{
    uint32_t i, k;

    for (i = 0; i < scene->level_count && scene->levels[i] < level; i++) {
    }
    if (i < scene->level_count && scene->levels[i] == level) {
        return;
    }
    for (k = scene->level_count; k > i; k--) {
        scene->levels[k] = scene->levels[k - 1];
    }
    scene->levels[i] = level;
    scene->level_count++;
}

bool rr_scene_create(RRScene *scene, const RRPackage *pkg)
{
    const RRPkgHeader *h = pkg->header;
    uint32_t           i, data_size = 0, area_count = 0, solid_count = 0, total;
    uint32_t           shape_count, pair_capacity, off_colliders, off_cshapes, off_volumes;
    uint32_t           off_sweep, off_pairs;
    uint32_t           anim_count = 0, emitter_count = 0, particle_count = 0, sprite_count = 0;
    uint32_t           timer_count = 0, tilemap_count = 0, off_timers, off_tilemaps, off_packed;
    uint32_t           ray_count = 0, off_rays, off_resources, audio_count = 0, off_audios;
    uint32_t           off_tweens, body_count = 0, off_bodies;
    uint32_t           rigid_count = 0, off_rigids, char_count = 0, off_chars;
    uint32_t           skin_count = 0, skin_bytes = 0, off_skins;
    uint8_t           *skin_memory;
    uint32_t           off_anims, off_emitters, off_particles, off_solids, off_sprites;
    uint32_t           off_comps, off_conns, off_handles, off_areas, off_data;
    uint8_t           *block, *comp_data;

    memset(scene, 0, sizeof(*scene));

    /* First pass: resolve the component types and add up everything that needs memory. */
    for (i = 0; i < h->resources.count; i++) {
        const char            *name = rr_package_string(pkg, pkg->resources[i].name);
        const RRComponentType *type = rr_component_find(name);
        if (type == NULL) {
            rr_set_error("resource type '%s' is not registered in this runtime", name);
            return false;
        }
        data_size += ALIGN8(type->size);
    }
    for (i = 0; i < h->comps.count; i++) {
        const RRPkgComponent  *pc = &pkg->comps[i];
        const char            *name = rr_package_string(pkg, pc->name);
        const RRComponentType *type = rr_component_find(name);
        if (type == NULL) {
            rr_set_error("node '%s': component '%s' is not registered in this runtime",
                         rr_package_string(pkg, pkg->nodes[pc->node].name), name);
            return false;
        }
        data_size += ALIGN8(type->size);
    }
    for (i = 0; i < h->nodes.count; i++) {
        const RRPkgNode *pn = &pkg->nodes[i];
        if (pn->type == RR_NODE_AUDIO || pn->type == RR_NODE_AUDIO_SPATIAL) {
            audio_count++;
        }
        if (pn->type == RR_NODE_CHARACTER_BODY) {
            char_count++;
        }
        if (pn->type == RR_NODE_RIGID_BODY) {
            rigid_count++;
        }
        if (pn->type == RR_NODE_STATIC_BODY || pn->type == RR_NODE_CHARACTER_BODY ||
            pn->type == RR_NODE_RIGID_BODY) {
            body_count++;
        }
        if (pn->type == RR_NODE_MESH && ((const RRPkgMeshNode *)rr_package_node_data(pkg, pn))->skin != RR_NONE32) {
            /* A mesh with a skin gets a copy of its mesh to pose. */
            skin_count++;
            skin_bytes += ALIGN8(pkg->assets[((const RRPkgMeshNode *)rr_package_node_data(pkg, pn))->mesh].size);
        }
        if (pn->type == RR_NODE_RAYCAST) {
            ray_count++;
        }
        if (pn->type == RR_NODE_TIMER) {
            timer_count++;
        } else if (pn->type == RR_NODE_TILEMAP) {
            tilemap_count++;
        }
        if (pn->type == RR_NODE_ANIMATED_SPRITE) {
            sprite_count++;
        }
        if (pn->type == RR_NODE_ANIMATION_PLAYER) {
            anim_count++;
        } else if (pn->type == RR_NODE_PARTICLES) {
            emitter_count++;
            particle_count += ((const RRPkgParticlesNode *)rr_package_node_data(pkg, pn))->amount;
        }
        if (pn->type == RR_NODE_SHAPE && pkg->nodes[pn->parent].type == RR_NODE_STATIC_BODY) {
            solid_count++;
        }
        if (pn->type == RR_NODE_AREA) {
            area_count++;
        }
    }
    shape_count   = rr_trigger_shape_count(pkg);
    pair_capacity = rr_trigger_pair_capacity(area_count, body_count);

    off_comps    = ALIGN8(h->nodes.count * (uint32_t)sizeof(Node));
    off_conns    = off_comps + ALIGN8(h->comps.count * (uint32_t)sizeof(RRComponentInstance));
    off_handles  = off_conns + ALIGN8(h->conns.count * (uint32_t)sizeof(RRResolvedConnection));
    off_areas    = off_handles + ALIGN8(h->assets.count * 4u);
    off_colliders = off_areas + ALIGN8(area_count * 2u);
    off_cshapes   = off_colliders + ALIGN8((area_count + body_count) * (uint32_t)sizeof(RRCollider));
    off_volumes   = off_cshapes + ALIGN8(shape_count * 2u);
    off_sweep     = off_volumes + ALIGN8(shape_count * (uint32_t)sizeof(RRVolume));
    off_pairs     = off_sweep + ALIGN8((area_count + body_count) * 2u);
    off_anims     = off_pairs + ALIGN8(2u * pair_capacity * 4u);
    off_emitters  = off_anims + ALIGN8(anim_count * (uint32_t)sizeof(RRAnimState));
    off_particles = off_emitters + ALIGN8(emitter_count * (uint32_t)sizeof(RREmitter));
    off_solids    = off_particles + ALIGN8(particle_count * (uint32_t)sizeof(RRParticle));
    off_sprites   = off_solids + ALIGN8(solid_count * 2u);
    off_timers    = off_sprites + ALIGN8(sprite_count * (uint32_t)sizeof(RRSpriteState));
    off_tilemaps  = off_timers + ALIGN8(timer_count * (uint32_t)sizeof(RRTimerState));
    off_packed    = off_tilemaps + ALIGN8(tilemap_count * 2u);
    off_rays      = off_packed + ALIGN8(h->pools.count * (uint32_t)sizeof(RRPackedScene));
    off_resources = off_rays + ALIGN8(ray_count * (uint32_t)sizeof(RRRayState));
    off_audios    = off_resources + ALIGN8(h->resources.count * (uint32_t)sizeof(void *));
    off_tweens    = off_audios + ALIGN8(audio_count * (uint32_t)sizeof(RRAudioState));
    off_bodies    = off_tweens + ALIGN8(RR_MAX_TWEENS * (uint32_t)sizeof(RRTween));
    off_rigids    = off_bodies + ALIGN8(body_count * 2u);
    off_chars     = off_rigids + ALIGN8(rigid_count * (uint32_t)sizeof(RRRigidState));
    off_skins     = off_chars + ALIGN8(char_count * (uint32_t)sizeof(RRCharState));
    off_data      = off_skins + ALIGN8(skin_count * (uint32_t)sizeof(RRSkinState)) + skin_bytes;
    total        = off_data + data_size;

    block = (uint8_t *)calloc(1, total > 0 ? total : 1);
    if (block == NULL) {
        rr_set_error("out of memory: scene needs %u bytes", (unsigned)total);
        return false;
    }

    scene->pkg        = pkg;
    scene->memory     = block;
    scene->nodes      = (Node *)block;
    scene->node_count = h->nodes.count;
    scene->comps      = (RRComponentInstance *)(block + off_comps);
    scene->comp_count = h->comps.count;
    scene->conns      = (RRResolvedConnection *)(block + off_conns);
    scene->handles    = (uint32_t *)(block + off_handles);
    scene->areas      = (uint16_t *)(block + off_areas);
    scene->colliders  = (RRCollider *)(block + off_colliders);
    scene->collider_shapes = (uint16_t *)(block + off_cshapes);
    scene->volumes    = (RRVolume *)(block + off_volumes);
    scene->sweep      = (uint16_t *)(block + off_sweep);
    scene->pairs      = (uint32_t *)(block + off_pairs);
    scene->pairs_next = scene->pairs + pair_capacity;
    scene->pair_capacity = pair_capacity;
    scene->anims      = (RRAnimState *)(block + off_anims);
    scene->emitters   = (RREmitter *)(block + off_emitters);
    scene->particles  = (RRParticle *)(block + off_particles);
    scene->solids     = (uint16_t *)(block + off_solids);
    scene->sprites    = (RRSpriteState *)(block + off_sprites);
    scene->timers     = (RRTimerState *)(block + off_timers);
    scene->tilemaps   = (uint16_t *)(block + off_tilemaps);
    scene->packed     = (RRPackedScene *)(block + off_packed);
    scene->rays       = (RRRayState *)(block + off_rays);
    scene->resources  = (void **)(block + off_resources);
    scene->audios     = (RRAudioState *)(block + off_audios);
    scene->tweens     = (RRTween *)(block + off_tweens);
    scene->bodies     = (uint16_t *)(block + off_bodies);
    scene->rigids     = (RRRigidState *)(block + off_rigids);
    scene->chars      = (RRCharState *)(block + off_chars);
    scene->skins      = (RRSkinState *)(block + off_skins);
    skin_memory       = block + off_skins + ALIGN8(skin_count * (uint32_t)sizeof(RRSkinState));
    scene->rng        = 0x2545F491u;
    comp_data         = block + off_data;
    memset(scene->handles, 0xFF, h->assets.count * 4u);

    for (i = 0; i < h->nodes.count; i++) {
        const RRPkgNode *pn = &pkg->nodes[i];
        Node            *n = &scene->nodes[i];
        n->name       = rr_package_string(pkg, pn->name);
        n->scene      = scene;
        n->parent     = pn->parent == RR_NONE16 ? NULL : &scene->nodes[pn->parent];
        n->type       = pn->type;
        n->flags      = pn->flags & (RR_NODE_FLAG_VISIBLE | RR_NODE_FLAG_CURRENT_CAMERA |
                                     RR_NODE_FLAG_AUTOLOAD);
        n->tag_first  = pn->tag_first;
        n->tag_count  = pn->tag_count;
        n->comp_first = pn->comp_first;
        n->comp_count = pn->comp_count;
        n->local      = pn->xform;
        n->data       = pn->data == RR_NONE32 ? NULL : rr_package_node_data(pkg, pn);
        n->z          = (int16_t)rr_package_node_level(pn);
        add_level(scene, n->z);
        rr_mat34_identity(&n->world);

        if (n->type == RR_NODE_PROGRESS_BAR) {
            n->value = ((const RRPkgProgressBarNode *)n->data)->value;
        } else if (n->type == RR_NODE_PATH_FOLLOW) {
            n->value = ((const RRPkgPathFollowNode *)n->data)->progress;
        }
        if (n->type == RR_NODE_SPRITE3D) {
            n->frame = ((const RRPkgSprite3DNode *)n->data)->frame;
        } else if (n->type == RR_NODE_LABEL3D) {
            n->text = node_text(scene, pn, ((const RRPkgLabel3DNode *)n->data)->text);
        }
        if (n->type == RR_NODE_SPRITE) {
            n->frame = ((const RRPkgSpriteNode *)n->data)->frame;
        } else if (n->type == RR_NODE_LABEL) {
            n->text = node_text(scene, pn, ((const RRPkgLabelNode *)n->data)->text);
        } else if (n->type == RR_NODE_BUTTON) {
            n->text = node_text(scene, pn, ((const RRPkgButtonNode *)n->data)->text);
            n->frame = ((const RRPkgButtonNode *)n->data)->pressed;
            if (((const RRPkgButtonNode *)n->data)->disabled) {
                n->flags |= RR_NODE_FLAG_DISABLED;
            }
        } else if (n->type == RR_NODE_RICH_TEXT) {
            n->text = node_text(scene, pn, ((const RRPkgRichTextNode *)n->data)->text);
        } else if (n->type == RR_NODE_TEXTURE_PROGRESS) {
            n->value = ((const RRPkgTextureProgressNode *)n->data)->value;
        } else if (n->type == RR_NODE_SCROLL) {
            /* A control has no transform of its own; the place holds how far it is scrolled. */
            n->local.m[9]  = ((const RRPkgScrollNode *)n->data)->start[0];
            n->local.m[10] = ((const RRPkgScrollNode *)n->data)->start[1];
        } else if (n->type == RR_NODE_SLIDER) {
            n->value = ((const RRPkgSliderNode *)n->data)->value;
            if (!((const RRPkgSliderNode *)n->data)->editable) {
                n->flags |= RR_NODE_FLAG_DISABLED;
            }
        } else if (n->type == RR_NODE_SHAPE && n->parent->type == RR_NODE_STATIC_BODY) {
            scene->solids[scene->solid_count++] = (uint16_t)i;
        } else if (n->type == RR_NODE_STATIC_BODY || n->type == RR_NODE_CHARACTER_BODY ||
                   n->type == RR_NODE_RIGID_BODY) {
            scene->bodies[scene->body_count++] = (uint16_t)i;
            if (n->type == RR_NODE_RIGID_BODY) {
                rr_rigid_body_init(scene, n);
            } else if (n->type == RR_NODE_CHARACTER_BODY) {
                scene->chars[scene->char_count].node  = (uint16_t)i;
                scene->chars[scene->char_count].floor = RR_NONE16;
                scene->char_count++;
            }
        } else if (n->type == RR_NODE_AREA) {
            scene->areas[scene->area_count++] = (uint16_t)i;
        } else if (n->type == RR_NODE_TIMER) {
            rr_timer_init(scene, n);
        } else if (n->type == RR_NODE_TILEMAP) {
            scene->tilemaps[scene->tilemap_count++] = (uint16_t)i;
        } else if (n->type == RR_NODE_ENVIRONMENT) {
            if (scene->environment == NULL) {
                scene->environment = n;
            }
        } else if (n->type == RR_NODE_ANIMATED_SPRITE) {
            rr_sprite_init(scene, n);
        } else if (n->type == RR_NODE_CAMERA2D) {
            /* The first enabled one in the tree is the active camera. */
            if (scene->camera2d == NULL && ((const RRPkgCamera2DNode *)n->data)->enabled) {
                scene->camera2d = n;
            }
        } else if (n->type == RR_NODE_AUDIO || n->type == RR_NODE_AUDIO_SPATIAL) {
            rr_audio_init(scene, n);
        } else if (n->type == RR_NODE_MESH && ((const RRPkgMeshNode *)n->data)->skin != RR_NONE32) {
            rr_skin_init(scene, n, skin_memory);
            skin_memory += ALIGN8(pkg->assets[((const RRPkgMeshNode *)n->data)->mesh].size);
        } else if (n->type == RR_NODE_RAYCAST) {
            rr_raycast_init(scene, n);
        } else if (n->type == RR_NODE_ANIMATION_PLAYER) {
            rr_anim_init(scene, n);
        } else if (n->type == RR_NODE_PARTICLES) {
            rr_particles_init(scene, n);
        }

        /* The camera marked current wins, otherwise the first camera in the tree. */
        if (n->type == RR_NODE_CAMERA &&
            (scene->camera == NULL || ((n->flags & RR_NODE_FLAG_CURRENT_CAMERA) &&
                                       !(scene->camera->flags & RR_NODE_FLAG_CURRENT_CAMERA)))) {
            scene->camera = n;
        }
    }

    rr_trigger_init(scene);

    /* The instances of the pools wait outside the tree until rr_instantiate. */
    for (i = 0; i < h->pools.count; i++) {
        const RRPkgPool *pool = &pkg->pools[i];
        uint32_t         k;
        scene->packed[i].scene = scene;
        scene->packed[i].pool  = pool;
        for (k = 0; k < (uint32_t)pool->node_count * pool->instance_count; k++) {
            scene->nodes[pool->first_node + k].flags |= RR_NODE_FLAG_FREE;
        }
    }

    /* Resources: every struct gets its place first, because they refer to each other. */
    for (i = 0; i < h->resources.count; i++) {
        scene->resources[i] = comp_data;
        comp_data += ALIGN8(rr_component_find(rr_package_string(pkg, pkg->resources[i].name))->size);
    }
    for (i = 0; i < h->resources.count; i++) {
        const RRPkgResource *pr = &pkg->resources[i];
        char                 owner[96];
        snprintf(owner, sizeof(owner), "resource %u", (unsigned)i);
        if (!fill_fields(scene, rr_component_find(rr_package_string(pkg, pr->name)),
                         scene->resources[i], pr->field_first, pr->field_count, owner)) {
            rr_scene_destroy(scene);
            return false;
        }
    }

    for (i = 0; i < h->comps.count; i++) {
        const RRPkgComponent *pc = &pkg->comps[i];
        RRComponentInstance  *inst = &scene->comps[i];
        inst->type = rr_component_find(rr_package_string(pkg, pc->name));
        inst->node = &scene->nodes[pc->node];
        inst->data = comp_data;
        comp_data += ALIGN8(inst->type->size);
        if (inst->node->flags & RR_NODE_FLAG_AUTOLOAD) {
            /* What the component knew in the last scene; the exported values and
             * whatever the ready handler looks up are set again after it. */
            const int kept = find_autoload_state(inst);
            if (kept >= 0) {
                memcpy(inst->data, g_autoload[kept].data, inst->type->size);
            }
        }
        if (!fill_component(scene, pc, inst)) {
            rr_scene_destroy(scene);
            return false;
        }
    }
    if (!resolve_connections(scene)) {
        rr_scene_destroy(scene);
        return false;
    }

    scene->time = 0.0f;
    scene->camera2d_first = true;
    rr_ui_init(scene);
    update_world_transforms(scene);
    scene->solids_are_boxes = rr_body_solids_are_boxes(scene);
    rr_colliders_refresh(scene);
    return true;
}

void rr_scene_destroy(RRScene *scene)
{
    free(scene->memory);
    memset(scene, 0, sizeof(*scene));
}

void rr_scene_load_resources(RRScene *scene)
{
    const RRPackage *pkg = scene->pkg;
    uint32_t         i;
    for (i = 0; i < pkg->header->assets.count; i++) {
        if (pkg->assets[i].type == RR_ASSET_TEXTURE) {
            scene->handles[i] = rr_gfx_texture_load((const RRPkgTexture *)rr_package_asset(pkg, i));
        } else if (pkg->assets[i].type == RR_ASSET_SOUND) {
            scene->handles[i] = rr_snd_load((const RRPkgSound *)rr_package_asset(pkg, i));
        }
    }
}

void rr_scene_unload_resources(RRScene *scene)
{
    const RRPackage *pkg = scene->pkg;
    uint32_t         i;

    rr_music_stop();
    for (i = 0; i < pkg->header->assets.count; i++) {
        if (scene->handles[i] == RR_NONE32) {
            continue;
        }
        if (pkg->assets[i].type == RR_ASSET_TEXTURE) {
            rr_gfx_texture_unload(scene->handles[i]);
        } else if (pkg->assets[i].type == RR_ASSET_SOUND) {
            rr_snd_unload(scene->handles[i]);
        }
        scene->handles[i] = RR_NONE32;
    }
}

void rr_scene_ready(RRScene *scene)
{
    uint32_t i;
    for (i = 0; i < scene->comp_count; i++) {
        RRComponentInstance *inst = &scene->comps[i];
        if (inst->type->on_ready != NULL && !(inst->node->flags & RR_NODE_FLAG_FREE)) {
            inst->type->on_ready(inst->node, inst->data);
        }
    }
    for (i = 0; i < scene->node_count; i++) {
        Node *n = &scene->nodes[i];
        if ((n->type == RR_NODE_AUDIO || n->type == RR_NODE_AUDIO_SPATIAL) &&
            ((const RRPkgAudioNode *)n->data)->autoplay &&
            !(n->flags & RR_NODE_FLAG_FREE)) {
            rr_audio_play(n);
        }
    }
    update_world_transforms(scene);
    rr_colliders_refresh(scene);
    scene->spawned = false;
    /* The view starts where the camera is now, before the first frame moves anything. */
    rr_canvas_update(scene, 0.0f);
    rr_skins_update(scene);
}

/* Takes the nodes that rr_queue_free marked, and everything below them, out of the tree. */
static void free_queued(RRScene *scene)
{
    const Node *end = scene->nodes + scene->node_count;
    Node       *n, *m;

    if (scene->queued == 0) {
        return;
    }
    for (n = scene->nodes; n < end; n++) {
        if (!(n->flags & RR_NODE_FLAG_QUEUED)) {
            continue;
        }
        /* Pre-order: the subtree follows its root and ends at the first node whose
         * parent comes before the root. */
        for (m = n; m < end && (m == n || m->parent >= n); m++) {
            m->flags &= (uint8_t)~RR_NODE_FLAG_QUEUED;
            m->flags |= RR_NODE_FLAG_FREE;
            if (m->type == RR_NODE_PARTICLES) {
                rr_particles_reset(m);
            }
        }
        n = m - 1;
    }
    scene->queued = 0;
    rr_colliders_refresh(scene); /* the freed ones are out */
}

void rr_scene_update(RRScene *scene, float dt)
{
    uint32_t i;

    rr_input_update();
    scene->time += dt;
    rr_ui_update(scene, dt);
    rr_anims_update(scene, dt);
    rr_sprites_update(scene, dt);
    rr_timers_update(scene, dt);
    for (i = 0; i < scene->comp_count; i++) {
        RRComponentInstance *inst = &scene->comps[i];
        if (inst->type->on_update != NULL && !(inst->node->flags & RR_NODE_FLAG_FREE)) {
            inst->type->on_update(inst->node, inst->data, dt);
        }
    }
    rr_tweens_update(scene, dt);
    rr_rigid_bodies_update(scene, dt);
    update_world_transforms(scene);
    rr_colliders_refresh(scene);
    scene->spawned = false;
    rr_raycasts_update(scene);
    rr_triggers_update(scene);
    if (scene->spawned) {
        /* A signal handler made an instance and placed it: it is drawn this frame. */
        update_world_transforms(scene);
        rr_colliders_refresh(scene);
        scene->spawned = false;
    }
    rr_canvas_update(scene, dt);
    rr_skins_update(scene);
    rr_audios_update(scene, dt);
    rr_particles_update(scene, dt);
    free_queued(scene);
}

Node *rr_instantiate(const RRPackedScene *packed)
{
    RRScene         *scene;
    const RRPackage *pkg;
    Node            *root = NULL;
    uint32_t         i, k, first, end;

    if (packed == NULL) {
        return NULL;
    }
    scene = packed->scene;
    pkg   = scene->pkg;
    for (i = 0; i < packed->pool->instance_count && root == NULL; i++) {
        Node *n = &scene->nodes[packed->pool->first_node + i * packed->pool->node_count];
        if (n->flags & RR_NODE_FLAG_FREE) {
            root = n;
        }
    }
    if (root == NULL) {
        return NULL;
    }
    first = (uint32_t)(root - scene->nodes);
    end   = first + packed->pool->node_count;

    /* Back to the state of the scene file, as after loading. */
    for (i = first; i < end; i++) {
        const RRPkgNode *pn = &pkg->nodes[i];
        Node            *n = &scene->nodes[i];

        n->flags = pn->flags & (RR_NODE_FLAG_VISIBLE | RR_NODE_FLAG_CURRENT_CAMERA |
                                RR_NODE_FLAG_AUTOLOAD);
        n->local = pn->xform;
        if (n->type == RR_NODE_PROGRESS_BAR) {
            n->value = ((const RRPkgProgressBarNode *)n->data)->value;
        } else if (n->type == RR_NODE_PATH_FOLLOW) {
            n->value = ((const RRPkgPathFollowNode *)n->data)->progress;
        }
        if (n->type == RR_NODE_SPRITE3D) {
            n->frame = ((const RRPkgSprite3DNode *)n->data)->frame;
        } else if (n->type == RR_NODE_LABEL3D) {
            n->text = node_text(scene, pn, ((const RRPkgLabel3DNode *)n->data)->text);
        }
        if (n->type == RR_NODE_SPRITE) {
            n->frame = ((const RRPkgSpriteNode *)n->data)->frame;
        } else if (n->type == RR_NODE_LABEL) {
            n->text = node_text(scene, pn, ((const RRPkgLabelNode *)n->data)->text);
        } else if (n->type == RR_NODE_BUTTON) {
            n->text = node_text(scene, pn, ((const RRPkgButtonNode *)n->data)->text);
            n->frame = ((const RRPkgButtonNode *)n->data)->pressed;
            if (((const RRPkgButtonNode *)n->data)->disabled) {
                n->flags |= RR_NODE_FLAG_DISABLED;
            }
        } else if (n->type == RR_NODE_RICH_TEXT) {
            n->text = node_text(scene, pn, ((const RRPkgRichTextNode *)n->data)->text);
        } else if (n->type == RR_NODE_TEXTURE_PROGRESS) {
            n->value = ((const RRPkgTextureProgressNode *)n->data)->value;
        } else if (n->type == RR_NODE_SCROLL) {
            /* A control has no transform of its own; the place holds how far it is scrolled. */
            n->local.m[9]  = ((const RRPkgScrollNode *)n->data)->start[0];
            n->local.m[10] = ((const RRPkgScrollNode *)n->data)->start[1];
        } else if (n->type == RR_NODE_SLIDER) {
            n->value = ((const RRPkgSliderNode *)n->data)->value;
            if (!((const RRPkgSliderNode *)n->data)->editable) {
                n->flags |= RR_NODE_FLAG_DISABLED;
            }
        } else if (n->type == RR_NODE_RIGID_BODY) {
            rr_rigid_body_reset(n);
        } else if (n->type == RR_NODE_TIMER) {
            rr_timer_reset(n);
        } else if (n->type == RR_NODE_ANIMATED_SPRITE) {
            rr_sprite_reset(n);
        } else if (n->type == RR_NODE_ANIMATION_PLAYER) {
            rr_anim_reset(n);
        } else if (n->type == RR_NODE_PARTICLES) {
            rr_particles_reset(n);
        }
        if (has_transform(n->type)) {
            rr_mat34_mul(&n->world, &n->parent->world, &n->local);
        }
        for (k = 0; k < n->comp_count; k++) {
            RRComponentInstance *inst = &scene->comps[n->comp_first + k];
            memset(inst->data, 0, inst->type->size);
            /* Cannot fail: the same fields were filled in when the scene was created. */
            fill_component(scene, &pkg->comps[n->comp_first + k], inst);
        }
    }
    scene->spawned = true;

    for (i = first; i < end; i++) {
        Node *n = &scene->nodes[i];
        /* All of the instance has its world transform now: the trigger tests take it
         * as new, and rays and bodies see it from here on. */
        if (n->type == RR_NODE_AREA || n->type == RR_NODE_STATIC_BODY ||
            n->type == RR_NODE_CHARACTER_BODY || n->type == RR_NODE_RIGID_BODY) {
            rr_trigger_forget(scene, n);
        }
        for (k = 0; k < n->comp_count; k++) {
            RRComponentInstance *inst = &scene->comps[n->comp_first + k];
            if (inst->type->on_ready != NULL) {
                inst->type->on_ready(n, inst->data);
            }
        }
        if ((n->type == RR_NODE_AUDIO || n->type == RR_NODE_AUDIO_SPATIAL) &&
            ((const RRPkgAudioNode *)n->data)->autoplay) {
            rr_audio_play(n);
        }
    }
    return root;
}

void rr_queue_free(Node *node)
{
    if (node != NULL && node->parent != NULL &&
        !(node->flags & (RR_NODE_FLAG_FREE | RR_NODE_FLAG_QUEUED))) {
        node->flags |= RR_NODE_FLAG_QUEUED;
        node->scene->queued++;
    }
}

/* --- for game components ------------------------------------------------------------- */

bool rr_node_has_tag(const Node *node, const char *tag)
{
    const RRPackage *pkg = node->scene->pkg;
    uint32_t         i;
    for (i = 0; i < node->tag_count; i++) {
        if (strcmp(rr_package_string(pkg, pkg->tags[node->tag_first + i]), tag) == 0) {
            return true;
        }
    }
    return false;
}

Node *rr_autoload(RRScene *scene, const char *name)
{
    Node *n;
    for (n = rr_node_child(&scene->nodes[0], NULL); n != NULL; n = rr_node_child(&scene->nodes[0], n)) {
        if ((n->flags & RR_NODE_FLAG_AUTOLOAD) && strcmp(n->name, name) == 0) {
            return n;
        }
    }
    return NULL;
}

void *rr_node_component(Node *node, const char *type)
{
    uint32_t i;
    for (i = 0; node != NULL && i < node->comp_count; i++) {
        RRComponentInstance *inst = &node->scene->comps[node->comp_first + i];
        if (strcmp(inst->type->name, type) == 0) {
            return inst->data;
        }
    }
    return NULL;
}

Node *rr_scene_find_tagged(RRScene *scene, const char *tag, const Node *after)
{
    uint32_t i = after == NULL ? 0 : (uint32_t)(after - scene->nodes) + 1;
    for (; i < scene->node_count; i++) {
        if (!(scene->nodes[i].flags & RR_NODE_FLAG_FREE) && rr_node_has_tag(&scene->nodes[i], tag)) {
            return &scene->nodes[i];
        }
    }
    return NULL;
}

Node *rr_node_child(Node *parent, const Node *after)
{
    const Node *end = parent->scene->nodes + parent->scene->node_count;
    Node       *n;
    /* Nodes are stored in pre-order: the subtree of `parent` follows it directly, and it
     * ends at the first node whose parent comes before `parent`. */
    for (n = after == NULL ? parent + 1 : (Node *)after + 1; n < end && n->parent >= parent; n++) {
        if (n->parent == parent && !(n->flags & RR_NODE_FLAG_FREE)) {
            return n;
        }
    }
    return NULL;
}

bool rr_node_call(Node *node, const char *method, Node *arg)
{
    uint32_t c, s;

    if (node->flags & RR_NODE_FLAG_FREE) {
        return false;
    }
    for (c = 0; c < node->comp_count; c++) {
        RRComponentInstance *inst = &node->scene->comps[node->comp_first + c];
        for (s = 0; s < inst->type->signal_count; s++) {
            const RRSignalDesc *handler = &inst->type->signals[s];
            if (strcmp(handler->method, method) != 0) {
                continue;
            }
            if (handler->takes_node) {
                ((RRSignalNodeFn)handler->fn)(node, inst->data, arg);
            } else {
                ((RRSignalFn)handler->fn)(node, inst->data);
            }
            return true;
        }
    }
    return false;
}

void rr_emit(Node *source, const char *signal, Node *arg)
{
    RRScene         *scene = source->scene;
    const RRPackage *pkg = scene->pkg;
    const uint32_t   index = (uint32_t)(source - scene->nodes);
    uint32_t         i;

    for (i = 0; i < pkg->header->conns.count; i++) {
        const RRPkgConnection      *pc = &pkg->conns[i];
        const RRResolvedConnection *rc = &scene->conns[i];
        if (pc->source != index || strcmp(rr_package_string(pkg, pc->signal), signal) != 0 ||
            (rc->target->node->flags & RR_NODE_FLAG_FREE)) {
            continue;
        }
        if (rc->handler->takes_node) {
            ((RRSignalNodeFn)rc->handler->fn)(rc->target->node, rc->target->data, arg);
        } else {
            ((RRSignalFn)rc->handler->fn)(rc->target->node, rc->target->data);
        }
    }
}

/* Where the text of a node is in the package, RR_NONE32 for a node that has none. */
static uint32_t text_of(const Node *n)
{
    switch (n->type) {
    case RR_NODE_LABEL:     return ((const RRPkgLabelNode *)n->data)->text;
    case RR_NODE_BUTTON:    return ((const RRPkgButtonNode *)n->data)->text;
    case RR_NODE_LABEL3D:   return ((const RRPkgLabel3DNode *)n->data)->text;
    case RR_NODE_RICH_TEXT: return ((const RRPkgRichTextNode *)n->data)->text;
    default:                return RR_NONE32;
    }
}

void rr_scene_retranslate(RRScene *scene)
{
    uint32_t i;

    /* The texts the scene came with, in the locale that is set now. A text a component
     * has put in since is replaced as well. */
    for (i = 0; i < scene->node_count; i++) {
        Node          *n = &scene->nodes[i];
        const uint32_t text = text_of(n);
        if (text != RR_NONE32 && scene->pkg->nodes[i].translate) {
            n->text = rr_tr(scene, rr_package_string(scene->pkg, text));
        }
    }
}

void rr_node_set_text(Node *node, const char *text)
{
    if (text_of(node) != RR_NONE32) {
        /* A node that translates its text takes the new one as a key too, as in Godot. */
        node->text = node->scene->pkg->nodes[node - node->scene->nodes].translate ? rr_tr(node->scene, text)
                                                                                  : text;
    }
}

