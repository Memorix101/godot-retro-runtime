/* Package validation. The format is described in docs/package-format.md. */
#include "rr_package.h"

#include <stdarg.h>
#include <string.h>

_Static_assert(sizeof(RRPkgHeader) == 136, "header layout");
_Static_assert(sizeof(RRPkgBus) == 12, "bus layout");
_Static_assert(sizeof(RRPkgNode) == 72, "node layout");
_Static_assert(sizeof(RRPkgParallaxNode) == 60, "parallax layout");
_Static_assert(sizeof(RRPkgPanelNode) == 40, "panel layout");
_Static_assert(sizeof(RRPkgTextureProgressNode) == 56, "texture progress layout");
_Static_assert(sizeof(RRPkgScrollNode) == 76, "scroll layout");
_Static_assert(sizeof(RRPkgRichTextNode) == 44, "rich text layout");
_Static_assert(sizeof(RRPkgAnimationPlayerNode) == 12, "animation player layout");
_Static_assert(sizeof(RRPkgAreaNode) == 12, "area layout");
_Static_assert(sizeof(RRPkgShapeNode) == 24, "shape layout");
_Static_assert(sizeof(RRPkgBodyNode) == 72, "body layout");
_Static_assert(sizeof(RRPkgAnimatedSpriteNode) == 28, "animated sprite layout");
_Static_assert(sizeof(RRPkgCamera2DNode) == 68, "2D camera layout");
_Static_assert(sizeof(RRPkgSpriteFrame) == 24, "sprite frame layout");
_Static_assert(sizeof(RRPkgSpriteAnim) == 20, "sprite animation layout");
_Static_assert(sizeof(RRPkgTimerNode) == 8, "timer layout");
_Static_assert(sizeof(RRPkgEnvironmentNode) == 44, "environment layout");
_Static_assert(sizeof(RRPkgTileMapNode) == 16, "tile map node layout");
_Static_assert(sizeof(RRPkgTileCell) == 12, "tile cell layout");
_Static_assert(sizeof(RRPkgTileFrame) == 8, "tile frame layout");
_Static_assert(sizeof(RRPkgTileAnim) == 12, "tile animation layout");
_Static_assert(sizeof(RRPkgTileBox) == 20, "tile box layout");
_Static_assert(sizeof(RRPkgTileMap) == 36, "tile map layout");
_Static_assert(sizeof(RRPkgSpriteNode) == 44, "sprite layout");
_Static_assert(sizeof(RRPkgLabelNode) == 32, "label layout");
_Static_assert(sizeof(RRPkgButtonNode) == 100, "button layout");
_Static_assert(sizeof(RRPkgSliderNode) == 72, "slider layout");
_Static_assert(sizeof(RRPkgTextureRectNode) == 24, "texture rect layout");
_Static_assert(sizeof(RRPkgAudioNode) == 40, "audio layout");
_Static_assert(sizeof(RRPkgMusic) == 8, "music layout");
_Static_assert(sizeof(RRPkgSprite3DNode) == 48, "3D sprite layout");
_Static_assert(sizeof(RRPkgLabel3DNode) == 28, "3D label layout");
_Static_assert(sizeof(RRPkgParticlesNode) == 84, "particles layout");
_Static_assert(sizeof(RRPkgKey) == 20, "key layout");
_Static_assert(sizeof(RRPkgTrack) == 12, "track layout");
_Static_assert(sizeof(RRPkgClip) == 20, "clip layout");
_Static_assert(sizeof(RRPkgComponent) == 12, "component layout");
_Static_assert(sizeof(RRPkgField) == 12, "field layout");
_Static_assert(sizeof(RRPkgConnection) == 12, "connection layout");
_Static_assert(sizeof(RRPkgPool) == 12, "pool layout");
_Static_assert(sizeof(RRPkgResource) == 12, "resource layout");
_Static_assert(sizeof(RRPkgMeshNode) == 24, "mesh node layout");
_Static_assert(sizeof(RRPkgInfluence) == 8, "skin influence layout");
_Static_assert(sizeof(RRVertexPC) == 24 && sizeof(RRVertexDC) == 24, "vertex layout");
_Static_assert(sizeof(RRPkgRayCastNode) == 40, "ray cast layout");
_Static_assert(sizeof(RRPkgPathFollowNode) == 16, "path follow layout");
_Static_assert(sizeof(RRPkgColorRectNode) == 20, "colour rect layout");
_Static_assert(sizeof(RRPkgProgressBarNode) == 48, "progress bar layout");
_Static_assert(sizeof(RRPkgNinePatchNode) == 60, "nine patch layout");
_Static_assert(sizeof(RRPkgCurvePoint) == 24, "curve point layout");
_Static_assert(offsetof(RRPkgCurve, points) == 12, "curve layout");
_Static_assert(sizeof(RRPkgAction) == 12, "action layout");
_Static_assert(sizeof(RRPkgBinding) == 4, "binding layout");
_Static_assert(sizeof(RRPkgAsset) == 16, "asset layout");
_Static_assert(sizeof(RRPkgSurface) == 24, "surface layout");
_Static_assert(offsetof(RRPkgMesh, surfaces) == 28, "mesh layout");
_Static_assert(sizeof(RRPkgMeshLod) == 8, "mesh LOD layout");
_Static_assert(sizeof(RRPkgTexture) == 12, "texture layout");
_Static_assert(sizeof(RRPkgGlyph) == 20, "glyph layout");
_Static_assert(sizeof(RRPkgSound) == 16, "sound layout");
_Static_assert(sizeof(RRVertexPC) == 24, "PC vertex layout");
_Static_assert(sizeof(RRVertexDC) == 24, "Dreamcast vertex layout");
_Static_assert(sizeof(RRVertexPSP) == 24 && offsetof(RRVertexPSP, pos) == 12, "PSP vertex layout");
_Static_assert(sizeof(RRVertexWii) == 24, "Wii vertex layout");

static char g_error[256];

void rr_set_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_error, sizeof(g_error), fmt, args);
    va_end(args);
}

const char *rr_last_error(void)
{
    return g_error;
}

const char *rr_platform_name(uint8_t platform)
{
    switch (platform) {
    case RR_PLATFORM_PC:        return "pc";
    case RR_PLATFORM_DREAMCAST: return "dreamcast";
    case RR_PLATFORM_PSP:       return "psp";
    case RR_PLATFORM_WII:       return "wii";
    default:                    return "unknown";
    }
}

static bool host_is_big_endian(void)
{
    const uint16_t probe = 1;
    return *(const uint8_t *)&probe == 0;
}

/* Size of one vertex for a platform, 0 if this runtime does not know the layout. */
static uint32_t vertex_size(uint8_t platform)
{
    switch (platform) {
    case RR_PLATFORM_PC:        return sizeof(RRVertexPC);
    case RR_PLATFORM_DREAMCAST: return sizeof(RRVertexDC);
    case RR_PLATFORM_PSP:       return sizeof(RRVertexPSP);
    case RR_PLATFORM_WII:       return sizeof(RRVertexWii);
    default:                    return 0;
    }
}

uint32_t rr_vertex_position_offset(uint8_t platform)
{
    return platform == RR_PLATFORM_PSP ? (uint32_t)offsetof(RRVertexPSP, pos) : 0u;
}

void rr_vertex_color_bytes(uint8_t platform, uint8_t *offsets)
{
    /* PC: bytes r, g, b, a. Dreamcast: a little-endian 0xAARRGGBB. PSP: bytes r, g, b, a
     * after the texture coordinates. Wii: a big-endian 0xRRGGBBAA after the position. */
    static const uint8_t pc[4] = { 20, 21, 22, 23 }, dc[4] = { 22, 21, 20, 23 };
    static const uint8_t psp[4] = { 8, 9, 10, 11 }, wii[4] = { 12, 13, 14, 15 };
    const uint8_t       *o = platform == RR_PLATFORM_DREAMCAST ? dc
                           : platform == RR_PLATFORM_PSP     ? psp
                           : platform == RR_PLATFORM_WII     ? wii
                                                             : pc;
    memcpy(offsets, o, 4);
}

/* Size of the type-specific data of a node type, 0 if the type has none. */
static uint32_t node_data_size(uint8_t type)
{
    switch (type) {
    case RR_NODE_MESH:         return sizeof(RRPkgMeshNode);
    case RR_NODE_CAMERA:       return sizeof(RRPkgCameraNode);
    case RR_NODE_AREA:         return sizeof(RRPkgAreaNode);
    case RR_NODE_SHAPE:        return sizeof(RRPkgShapeNode);
    case RR_NODE_SPRITE:       return sizeof(RRPkgSpriteNode);
    case RR_NODE_CONTROL:      return sizeof(RRPkgControlNode);
    case RR_NODE_LABEL:        return sizeof(RRPkgLabelNode);
    case RR_NODE_BUTTON:       return sizeof(RRPkgButtonNode);
    case RR_NODE_TEXTURE_RECT: return sizeof(RRPkgTextureRectNode);
    case RR_NODE_AUDIO:
    case RR_NODE_AUDIO_SPATIAL: return sizeof(RRPkgAudioNode);
    case RR_NODE_SPRITE3D:     return sizeof(RRPkgSprite3DNode);
    case RR_NODE_LABEL3D:      return sizeof(RRPkgLabel3DNode);
    case RR_NODE_ANIMATION_PLAYER: return sizeof(RRPkgAnimationPlayerNode);
    case RR_NODE_PARTICLES:    return sizeof(RRPkgParticlesNode);
    case RR_NODE_STATIC_BODY:
    case RR_NODE_CHARACTER_BODY:
    case RR_NODE_RIGID_BODY:   return sizeof(RRPkgBodyNode);
    case RR_NODE_ANIMATED_SPRITE: return sizeof(RRPkgAnimatedSpriteNode);
    case RR_NODE_CAMERA2D:     return sizeof(RRPkgCamera2DNode);
    case RR_NODE_TIMER:        return sizeof(RRPkgTimerNode);
    case RR_NODE_ENVIRONMENT:  return sizeof(RRPkgEnvironmentNode);
    case RR_NODE_TILEMAP:      return sizeof(RRPkgTileMapNode);
    case RR_NODE_RAYCAST:      return sizeof(RRPkgRayCastNode);
    case RR_NODE_PATH:         return sizeof(RRPkgPathNode);
    case RR_NODE_PATH_FOLLOW:  return sizeof(RRPkgPathFollowNode);
    case RR_NODE_COLOR_RECT:   return sizeof(RRPkgColorRectNode);
    case RR_NODE_PROGRESS_BAR: return sizeof(RRPkgProgressBarNode);
    case RR_NODE_NINE_PATCH:   return sizeof(RRPkgNinePatchNode);
    case RR_NODE_SLIDER:       return sizeof(RRPkgSliderNode);
    case RR_NODE_PARALLAX:     return sizeof(RRPkgParallaxNode);
    case RR_NODE_PANEL:        return sizeof(RRPkgPanelNode);
    case RR_NODE_TEXTURE_PROGRESS: return sizeof(RRPkgTextureProgressNode);
    case RR_NODE_SCROLL:       return sizeof(RRPkgScrollNode);
    case RR_NODE_RICH_TEXT:    return sizeof(RRPkgRichTextNode);
    default:                   return 0;
    }
}

/* True if [off, off + count * elem) is 4-byte aligned and lies inside [lo, hi). */
static bool range_ok(uint32_t off, uint32_t count, uint32_t elem, uint32_t lo, uint32_t hi)
{
    uint64_t end = (uint64_t)off + (uint64_t)count * elem;
    return (off & 3u) == 0 && off >= lo && end <= hi;
}

static bool check_table(const RRPkgHeader *h, const char *what, RRPkgTable table, uint32_t elem)
{
    if (!range_ok(table.off, table.count, elem, sizeof(RRPkgHeader), h->file_size)) {
        rr_set_error("package is corrupt: %s table is out of range", what);
        return false;
    }
    return true;
}

static bool check_string(const RRPkgHeader *h, const char *what, uint32_t index, uint32_t off)
{
    if (off >= h->strings.count) {
        rr_set_error("package is corrupt: %s %u has a bad string offset", what, (unsigned)index);
        return false;
    }
    return true;
}

static bool corrupt_asset(uint32_t index, const char *what)
{
    rr_set_error("package is corrupt: asset %u has a bad %s", (unsigned)index, what);
    return false;
}

static bool check_mesh(const RRPackage *pkg, uint32_t index)
{
    const RRPkgAsset *a = &pkg->assets[index];
    const RRPkgMesh  *mesh = rr_package_mesh(pkg, index);
    const uint32_t    vsize = vertex_size(pkg->header->platform);
    uint32_t          s, i;

    if (a->size < offsetof(RRPkgMesh, surfaces) ||
        !range_ok(offsetof(RRPkgMesh, surfaces), mesh->surface_count, sizeof(RRPkgSurface), 4, a->size)) {
        return corrupt_asset(index, "surface table");
    }
    /* Written this way so that a NaN fails as well. */
    if (!(mesh->radius >= 0.0f)) {
        return corrupt_asset(index, "bounding sphere");
    }
    if (mesh->lod_count > 0) {
        const RRPkgMeshLod *lods = rr_mesh_lods(mesh);
        if (!range_ok(mesh->lods_off, mesh->lod_count, sizeof(RRPkgMeshLod), offsetof(RRPkgMesh, surfaces),
                      a->size)) {
            return corrupt_asset(index, "LOD table");
        }
        /* Each level is a mesh of its own, written (and checked) before this one, with no
         * levels of its own; the edge lengths grow. */
        for (i = 0; i < mesh->lod_count; i++) {
            if (lods[i].mesh >= index || pkg->assets[lods[i].mesh].type != RR_ASSET_MESH ||
                rr_package_mesh(pkg, lods[i].mesh)->lod_count != 0 || !(lods[i].edge >= 0.0f) ||
                (i > 0 && lods[i].edge < lods[i - 1].edge)) {
                return corrupt_asset(index, "LOD level");
            }
        }
    }
    for (s = 0; s < mesh->surface_count; s++) {
        const RRPkgSurface *surf = &mesh->surfaces[s];
        const uint16_t     *strips;
        uint64_t            total = 0;

        /* A texture is written before the mesh that uses it, so it has been checked. */
        if ((surf->texture != RR_NONE32 &&
             (surf->texture >= index || pkg->assets[surf->texture].type != RR_ASSET_TEXTURE)) ||
            surf->blend > RR_BLEND_TRANSLUCENT) {
            return corrupt_asset(index, "surface material");
        }
        if (!range_ok(surf->strips_off, surf->strip_count, 2, 4, a->size) ||
            !range_ok(surf->verts_off, surf->vertex_count, vsize, 4, a->size)) {
            return corrupt_asset(index, "surface range");
        }
        strips = rr_surface_strips(mesh, surf);
        for (i = 0; i < surf->strip_count; i++) {
            if (strips[i] < 3) {
                total = UINT64_MAX;
                break;
            }
            total += strips[i];
        }
        if (total != surf->vertex_count) {
            return corrupt_asset(index, "strip length");
        }
    }
    return true;
}

static bool check_texture(const RRPackage *pkg, uint32_t index)
{
    const RRPkgAsset   *a = &pkg->assets[index];
    const RRPkgTexture *t = (const RRPkgTexture *)rr_package_asset(pkg, index);
    uint32_t            bpp;

    if (a->size < sizeof(RRPkgTexture)) {
        return corrupt_asset(index, "texture header");
    }
    bpp = t->format == RR_TEX_RGBA8888 ? 4u : 2u;
    if (t->format < RR_TEX_RGBA8888 || t->format > RR_TEX_GX_IA8 ||
        t->filter > RR_FILTER_NEAREST || t->width == 0 ||
        t->height == 0 || t->content_width > t->width || t->content_height > t->height ||
        (uint64_t)sizeof(RRPkgTexture) + (uint64_t)t->width * t->height * bpp > a->size) {
        return corrupt_asset(index, "texture size or format");
    }
    return true;
}

static bool check_font(const RRPackage *pkg, uint32_t index)
{
    const RRPkgAsset *a = &pkg->assets[index];
    const RRPkgFont  *f = (const RRPkgFont *)rr_package_asset(pkg, index);

    if (a->size < 16 || !range_ok(16, f->glyph_count, sizeof(RRPkgGlyph), 16, a->size)) {
        return corrupt_asset(index, "glyph table");
    }
    /* The atlas is written before the font, so it has been checked already. */
    if (f->texture >= index || pkg->assets[f->texture].type != RR_ASSET_TEXTURE) {
        return corrupt_asset(index, "font atlas");
    }
    return true;
}

static bool check_sound(const RRPackage *pkg, uint32_t index)
{
    const RRPkgAsset *a = &pkg->assets[index];
    const RRPkgSound *s = (const RRPkgSound *)rr_package_asset(pkg, index);

    if (a->size < sizeof(RRPkgSound) || (uint64_t)sizeof(RRPkgSound) + s->data_size > a->size ||
        s->format < RR_SOUND_PCM16 || s->format > RR_SOUND_ADPCM || s->channels < 1 || s->channels > 2 ||
        s->sample_rate == 0) {
        return corrupt_asset(index, "sound header");
    }
    return true;
}

static bool check_sprite_frames(const RRPackage *pkg, uint32_t index)
{
    const RRPkgHeader       *h = pkg->header;
    const RRPkgAsset        *a = &pkg->assets[index];
    const RRPkgSpriteFrames *set = (const RRPkgSpriteFrames *)rr_package_asset(pkg, index);
    uint32_t                 i, f;

    if (a->size < 4 || set->anim_count == 0 ||
        !range_ok(4, set->anim_count, sizeof(RRPkgSpriteAnim), 4, a->size)) {
        return corrupt_asset(index, "sprite animation table");
    }
    for (i = 0; i < set->anim_count; i++) {
        const RRPkgSpriteAnim  *anim = &set->anims[i];
        const RRPkgSpriteFrame *frames = (const RRPkgSpriteFrame *)((const uint8_t *)set + anim->frames_off);

        if (anim->name >= h->strings.count || anim->frame_count == 0 || !(anim->fps >= 0.0f) ||
            !range_ok(anim->frames_off, anim->frame_count, sizeof(RRPkgSpriteFrame), 4, a->size)) {
            return corrupt_asset(index, "sprite animation");
        }
        for (f = 0; f < anim->frame_count; f++) {
            /* A texture is written before the asset that uses it, so it has been checked. */
            if (frames[f].texture >= index || pkg->assets[frames[f].texture].type != RR_ASSET_TEXTURE ||
                !(frames[f].duration > 0.0f)) {
                return corrupt_asset(index, "sprite frame");
            }
        }
    }
    return true;
}

static bool check_tilemap(const RRPackage *pkg, uint32_t index)
{
    const RRPkgAsset   *a = &pkg->assets[index];
    const RRPkgTileMap *map = (const RRPkgTileMap *)rr_package_asset(pkg, index);

    if (a->size < sizeof(RRPkgTileMap) ||
        !range_ok(map->cells_off, map->cell_count, sizeof(RRPkgTileCell), sizeof(RRPkgTileMap), a->size) ||
        !range_ok(map->boxes_off, map->box_count, sizeof(RRPkgTileBox), sizeof(RRPkgTileMap), a->size) ||
        !range_ok(map->anims_off, map->anim_count, sizeof(RRPkgTileAnim), sizeof(RRPkgTileMap), a->size)) {
        return corrupt_asset(index, "tile map tables");
    }
    {
        const RRPkgTileCell *cells = (const RRPkgTileCell *)((const uint8_t *)map + map->cells_off);
        const RRPkgTileAnim *anims = (const RRPkgTileAnim *)((const uint8_t *)map + map->anims_off);
        uint32_t             i;

        for (i = 0; i < map->cell_count; i++) {
            if (cells[i].anim != RR_NONE16 && cells[i].anim >= map->anim_count) {
                return corrupt_asset(index, "tile map cell animation");
            }
        }
        for (i = 0; i < map->anim_count; i++) {
            if (anims[i].frame_count == 0 || !(anims[i].duration > 0.0f) ||
                !range_ok(anims[i].frames_off, anims[i].frame_count, sizeof(RRPkgTileFrame),
                          sizeof(RRPkgTileMap), a->size)) {
                return corrupt_asset(index, "tile map animation");
            }
        }
    }
    /* A texture is written before the asset that uses it, so it has been checked. */
    if (map->texture >= index || pkg->assets[map->texture].type != RR_ASSET_TEXTURE ||
        !(map->tile_size[0] > 0.0f) || !(map->tile_size[1] > 0.0f)) {
        return corrupt_asset(index, "tile map texture or tile size");
    }
    return true;
}

static bool check_animations(const RRPackage *pkg, uint32_t index)
{
    const RRPkgHeader     *h = pkg->header;
    const RRPkgAsset      *a = &pkg->assets[index];
    const RRPkgAnimations *set = (const RRPkgAnimations *)rr_package_asset(pkg, index);
    uint32_t               c, t, k;

    if (a->size < 8 || !range_ok(8, set->clip_count, sizeof(RRPkgClip), 8, a->size) ||
        (set->autoplay != RR_NONE32 && set->autoplay >= set->clip_count)) {
        return corrupt_asset(index, "clip table");
    }
    for (c = 0; c < set->clip_count; c++) {
        const RRPkgClip  *clip = &set->clips[c];
        const RRPkgTrack *tracks = (const RRPkgTrack *)((const uint8_t *)set + clip->tracks_off);

        if (clip->name >= h->strings.count || !(clip->length > 0.0f) ||
            !range_ok(clip->tracks_off, clip->track_count, sizeof(RRPkgTrack), 8, a->size)) {
            return corrupt_asset(index, "clip");
        }
        for (t = 0; t < clip->track_count; t++) {
            const RRPkgKey *keys = (const RRPkgKey *)((const uint8_t *)set + tracks[t].keys_off);
            if (tracks[t].node >= h->nodes.count || tracks[t].kind < RR_TRACK_POSITION ||
                tracks[t].kind > RR_TRACK_METHOD || tracks[t].interp > RR_INTERP_LINEAR ||
                tracks[t].key_count == 0 ||
                !range_ok(tracks[t].keys_off, tracks[t].key_count, sizeof(RRPkgKey), 8, a->size)) {
                return corrupt_asset(index, "track");
            }
            for (k = 0; k < tracks[t].key_count; k++) {
                if (k > 0 && keys[k].time < keys[k - 1].time) {
                    return corrupt_asset(index, "key order");
                }
                if (tracks[t].kind == RR_TRACK_METHOD &&
                    !(keys[k].value[0] >= 0.0f && keys[k].value[0] < (float)h->strings.count)) {
                    return corrupt_asset(index, "method name");
                }
            }
        }
    }
    return true;
}

static bool asset_is(const RRPackage *pkg, uint32_t asset, uint8_t type)
{
    return asset < pkg->header->assets.count && pkg->assets[asset].type == type;
}

/* The controls the focus can move to from a button or slider: buttons and sliders. */
static bool focus_neighbors_ok(const RRPackage *pkg, const uint16_t *neighbors)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (neighbors[i] != RR_NONE16 &&
            (neighbors[i] >= pkg->header->nodes.count ||
             (pkg->nodes[neighbors[i]].type != RR_NODE_BUTTON &&
              pkg->nodes[neighbors[i]].type != RR_NODE_SLIDER))) {
            return false;
        }
    }
    return true;
}

/* Checks the references inside the type-specific data of a node. */
static bool check_node_data(const RRPackage *pkg, uint32_t index)
{
    const RRPkgHeader *h = pkg->header;
    const RRPkgNode   *n = &pkg->nodes[index];
    const void        *data = rr_package_node_data(pkg, n);
    bool               ok = true;
    int                i;

    switch (n->type) {
    case RR_NODE_MESH: {
        const RRPkgMeshNode *m = (const RRPkgMeshNode *)data;
        ok = asset_is(pkg, m->mesh, RR_ASSET_MESH);
        if (ok && m->skin != RR_NONE32) {
            /* The skin has one entry for every vertex of the mesh. */
            const RRPkgMesh *mesh = rr_package_mesh(pkg, m->mesh);
            uint32_t         vertices = 0, s;
            for (s = 0; s < mesh->surface_count; s++) {
                vertices += mesh->surfaces[s].vertex_count;
            }
            ok = asset_is(pkg, m->skin, RR_ASSET_SKIN) &&
                 ((const RRPkgSkin *)rr_package_asset(pkg, m->skin))->vertex_count == vertices;
        }
        break;
    }
    case RR_NODE_SHAPE: {
        const RRPkgShapeNode *s = (const RRPkgShapeNode *)data;
        /* A shape belongs to the area or body directly above it. */
        const uint8_t         owner = index > 0 ? pkg->nodes[n->parent].type : RR_NODE_NODE;
        /* Capsules are for the bodies that move. */
        ok = ((s->kind == RR_SHAPE_BOX || s->kind == RR_SHAPE_SPHERE) &&
              (owner == RR_NODE_AREA || owner == RR_NODE_STATIC_BODY)) ||
             ((s->kind >= RR_SHAPE_BOX && s->kind <= RR_SHAPE_CAPSULE) &&
              (owner == RR_NODE_CHARACTER_BODY || owner == RR_NODE_RIGID_BODY)) ||
             /* A height map is ground: it does not move. Concave shapes are for static
              * bodies only, as in Godot. */
             (s->kind == RR_SHAPE_HEIGHT_MAP && owner == RR_NODE_STATIC_BODY &&
              asset_is(pkg, s->map, RR_ASSET_HEIGHT_MAP)) ||
             (s->kind == RR_SHAPE_CONCAVE && owner == RR_NODE_STATIC_BODY &&
              asset_is(pkg, s->map, RR_ASSET_TRIANGLES));
        break;
    }
    case RR_NODE_SPRITE:
        ok = asset_is(pkg, ((const RRPkgSpriteNode *)data)->texture, RR_ASSET_TEXTURE) &&
             ((const RRPkgSpriteNode *)data)->hframes >= 1 &&
             ((const RRPkgSpriteNode *)data)->vframes >= 1;
        break;
    case RR_NODE_LABEL: {
        const RRPkgLabelNode *l = (const RRPkgLabelNode *)data;
        ok = l->text < h->strings.count && asset_is(pkg, l->font, RR_ASSET_FONT) &&
             l->h_align <= RR_ALIGN_END && l->v_align <= RR_ALIGN_END && l->wrap <= RR_WRAP_WORD_SMART;
        break;
    }
    case RR_NODE_BUTTON: {
        const RRPkgButtonNode *b = (const RRPkgButtonNode *)data;
        ok = b->text < h->strings.count &&
             (b->font == RR_NONE32 || asset_is(pkg, b->font, RR_ASSET_FONT)) &&
             (b->icon == RR_NONE32 || asset_is(pkg, b->icon, RR_ASSET_TEXTURE)) &&
             (b->icon_pressed == RR_NONE32 || asset_is(pkg, b->icon_pressed, RR_ASSET_TEXTURE)) &&
             b->h_align <= RR_ALIGN_END && focus_neighbors_ok(pkg, b->neighbors);
        for (i = 0; i < 4; i++) {
            if (b->textures[i] != RR_NONE32 && !asset_is(pkg, b->textures[i], RR_ASSET_TEXTURE)) {
                ok = false;
            }
        }
        break;
    }
    case RR_NODE_SLIDER: {
        const RRPkgSliderNode *s = (const RRPkgSliderNode *)data;
        ok = s->max >= s->min && s->step >= 0.0f && asset_is(pkg, s->grabber, RR_ASSET_TEXTURE) &&
             asset_is(pkg, s->focus_grabber, RR_ASSET_TEXTURE) &&
             asset_is(pkg, s->disabled_grabber, RR_ASSET_TEXTURE) &&
             focus_neighbors_ok(pkg, s->neighbors);
        break;
    }
    case RR_NODE_TEXTURE_RECT:
        ok = asset_is(pkg, ((const RRPkgTextureRectNode *)data)->texture, RR_ASSET_TEXTURE);
        break;
    case RR_NODE_AUDIO_SPATIAL:
    case RR_NODE_AUDIO: {
        const RRPkgAudioNode *a = (const RRPkgAudioNode *)data;
        /* Only sounds that are in memory can be placed in the world. */
        ok = a->mode <= RR_AUDIO_3D && a->model <= RR_ATTENUATION_DISABLED &&
             (asset_is(pkg, a->sound, RR_ASSET_SOUND) ||
              (a->mode == RR_AUDIO_PLAIN && asset_is(pkg, a->sound, RR_ASSET_MUSIC))) &&
             (a->mode != RR_AUDIO_PLAIN) == (n->type == RR_NODE_AUDIO_SPATIAL) &&
             a->pitch > 0.0f && a->bus < h->buses.count;
        break;
    }
    case RR_NODE_TILEMAP:
        ok = asset_is(pkg, ((const RRPkgTileMapNode *)data)->tiles, RR_ASSET_TILEMAP);
        break;
    case RR_NODE_TIMER:
        ok = ((const RRPkgTimerNode *)data)->wait_time > 0.0f;
        break;
    case RR_NODE_SPRITE3D: {
        const RRPkgSprite3DNode *s = (const RRPkgSprite3DNode *)data;
        ok = asset_is(pkg, s->texture, RR_ASSET_TEXTURE) && s->hframes >= 1 && s->vframes >= 1 &&
             s->billboard <= RR_BILLBOARD_Y && s->axis <= 2;
        break;
    }
    case RR_NODE_LABEL3D: {
        const RRPkgLabel3DNode *l = (const RRPkgLabel3DNode *)data;
        ok = l->text < h->strings.count && asset_is(pkg, l->font, RR_ASSET_FONT) &&
             l->billboard <= RR_BILLBOARD_Y;
        break;
    }
    case RR_NODE_PATH:
        ok = asset_is(pkg, ((const RRPkgPathNode *)data)->curve, RR_ASSET_CURVE);
        break;
    case RR_NODE_PATH_FOLLOW:
        /* Follows the curve of the Path directly above it. */
        ok = index > 0 && pkg->nodes[n->parent].type == RR_NODE_PATH &&
             ((const RRPkgPathFollowNode *)data)->rotation <= RR_PATH_ROTATION_ORIENTED;
        break;
    case RR_NODE_PROGRESS_BAR: {
        const RRPkgProgressBarNode *p = (const RRPkgProgressBarNode *)data;
        ok = p->fill_mode <= RR_FILL_BOTTOM_TO_TOP &&
             (!p->show_percentage || asset_is(pkg, p->font, RR_ASSET_FONT));
        break;
    }
    case RR_NODE_NINE_PATCH:
        ok = asset_is(pkg, ((const RRPkgNinePatchNode *)data)->texture, RR_ASSET_TEXTURE);
        break;
    case RR_NODE_ENVIRONMENT:
        ok = ((const RRPkgEnvironmentNode *)data)->fog_mode <= RR_FOG_DEPTH;
        for (i = 0; i < 6; i++) {
            const uint32_t face = ((const RRPkgEnvironmentNode *)data)->sky[i];
            /* All six or none. */
            if (face == RR_NONE32 ? ((const RRPkgEnvironmentNode *)data)->sky[0] != RR_NONE32
                                  : !asset_is(pkg, face, RR_ASSET_TEXTURE)) {
                ok = false;
            }
        }
        break;
    case RR_NODE_ANIMATED_SPRITE: {
        const RRPkgAnimatedSpriteNode *s = (const RRPkgAnimatedSpriteNode *)data;
        ok = asset_is(pkg, s->frames, RR_ASSET_SPRITE_FRAMES) &&
             s->animation < ((const RRPkgSpriteFrames *)rr_package_asset(pkg, s->frames))->anim_count;
        break;
    }
    case RR_NODE_CAMERA2D: {
        const RRPkgCamera2DNode *c = (const RRPkgCamera2DNode *)data;
        ok = c->zoom[0] > 0.0f && c->zoom[1] > 0.0f && c->smoothing_speed >= 0.0f;
        break;
    }
    case RR_NODE_PARALLAX: {
        const RRPkgParallaxNode *p = (const RRPkgParallaxNode *)data;
        ok = p->repeat_size[0] >= 0.0f && p->repeat_size[1] >= 0.0f && p->repeat_times <= 16;
        break;
    }
    case RR_NODE_TEXTURE_PROGRESS: {
        const RRPkgTextureProgressNode *p = (const RRPkgTextureProgressNode *)data;
        ok = p->fill_mode <= RR_FILL_BOTTOM_TO_TOP &&
             (p->under == RR_NONE32 || asset_is(pkg, p->under, RR_ASSET_TEXTURE)) &&
             (p->progress == RR_NONE32 || asset_is(pkg, p->progress, RR_ASSET_TEXTURE)) &&
             (p->over == RR_NONE32 || asset_is(pkg, p->over, RR_ASSET_TEXTURE));
        break;
    }
    case RR_NODE_RAYCAST:
        /* A ray, or a box, sphere or capsule that is cast. */
        ok = ((const RRPkgRayCastNode *)data)->shape <= RR_SHAPE_CAPSULE;
        break;
    case RR_NODE_RICH_TEXT: {
        const RRPkgRichTextNode *r = (const RRPkgRichTextNode *)data;
        ok = r->text < h->strings.count && asset_is(pkg, r->fonts[0], RR_ASSET_FONT) &&
             r->wrap <= RR_WRAP_WORD_SMART && r->v_align <= RR_ALIGN_END;
        for (i = 1; i < 4; i++) {
            if (r->fonts[i] != RR_NONE32 && !asset_is(pkg, r->fonts[i], RR_ASSET_FONT)) {
                ok = false;
            }
        }
        break;
    }
    case RR_NODE_SCROLL: {
        const RRPkgScrollNode *s = (const RRPkgScrollNode *)data;
        ok = s->content[0] >= 0.0f && s->content[1] >= 0.0f;
        break;
    }
    case RR_NODE_ANIMATION_PLAYER:
        ok = asset_is(pkg, ((const RRPkgAnimationPlayerNode *)data)->animations, RR_ASSET_ANIMATION) &&
             ((const RRPkgAnimationPlayerNode *)data)->speed_scale >= 0.0f &&
             ((const RRPkgAnimationPlayerNode *)data)->blend_time >= 0.0f;
        break;
    case RR_NODE_PARTICLES: {
        const RRPkgParticlesNode *p = (const RRPkgParticlesNode *)data;
        ok = (p->texture == RR_NONE32 || asset_is(pkg, p->texture, RR_ASSET_TEXTURE)) &&
             p->amount > 0 && p->amount <= RR_MAX_PARTICLES && p->lifetime > 0.0f &&
             p->explosiveness >= 0.0f && p->explosiveness <= 1.0f;
        break;
    }
    default:
        break;
    }
    if (!ok) {
        rr_set_error("package is corrupt: node %u '%s' has bad type data", (unsigned)index,
                     rr_package_string(pkg, n->name));
    }
    return ok;
}

bool rr_package_open(RRPackage *pkg, const void *data, uint32_t size, uint8_t platform)
{
    const RRPkgHeader *h = (const RRPkgHeader *)data;
    uint32_t           i, k;

    memset(pkg, 0, sizeof(*pkg));

    if (((uintptr_t)data & 3u) != 0) {
        rr_set_error("package data must be 4-byte aligned in memory");
        return false;
    }
    /* The first six bytes are single bytes, so they can be checked in any byte order. */
    if (size < 8 || memcmp(h->magic, "RPKG", 4) != 0) {
        rr_set_error("not a package file (bad magic)");
        return false;
    }
    if (platform != RR_PLATFORM_ANY && h->platform != platform) {
        rr_set_error("package was baked for platform '%s' (id %u), this runtime needs '%s' (id %u)",
                     rr_platform_name(h->platform), (unsigned)h->platform,
                     rr_platform_name(platform), (unsigned)platform);
        return false;
    }
    if ((h->big_endian != 0) != host_is_big_endian()) {
        rr_set_error("package is %s-endian (platform '%s'), this machine is not",
                     h->big_endian ? "big" : "little", rr_platform_name(h->platform));
        return false;
    }
    if (h->version != RR_PKG_VERSION) {
        rr_set_error("package has format version %u, this runtime reads version %u",
                     (unsigned)h->version, (unsigned)RR_PKG_VERSION);
        return false;
    }
    if (vertex_size(h->platform) == 0) {
        rr_set_error("package platform id %u has no known vertex layout", (unsigned)h->platform);
        return false;
    }
    if (size < sizeof(RRPkgHeader) || h->file_size != size) {
        rr_set_error("package is truncated: header says %u bytes, file has %u",
                     size < sizeof(RRPkgHeader) ? 0u : (unsigned)h->file_size, (unsigned)size);
        return false;
    }

    if (!check_table(h, "string", h->strings, 1) ||
        !check_table(h, "node", h->nodes, sizeof(RRPkgNode)) ||
        !check_table(h, "node data", h->node_data, 1) ||
        !check_table(h, "tag", h->tags, 4) ||
        !check_table(h, "component", h->comps, sizeof(RRPkgComponent)) ||
        !check_table(h, "field", h->fields, sizeof(RRPkgField)) ||
        !check_table(h, "connection", h->conns, sizeof(RRPkgConnection)) ||
        !check_table(h, "action", h->actions, sizeof(RRPkgAction)) ||
        !check_table(h, "binding", h->bindings, sizeof(RRPkgBinding)) ||
        !check_table(h, "asset", h->assets, sizeof(RRPkgAsset)) ||
        !check_table(h, "data", h->data, 1) ||
        !check_table(h, "pool", h->pools, sizeof(RRPkgPool)) ||
        !check_table(h, "resource", h->resources, sizeof(RRPkgResource)) ||
        !check_table(h, "bus", h->buses, sizeof(RRPkgBus)) ||
        !check_table(h, "translation", h->translations, 1)) {
        return false;
    }

    pkg->base      = (const uint8_t *)data;
    pkg->header    = h;
    pkg->strings   = (const char *)(pkg->base + h->strings.off);
    pkg->nodes     = (const RRPkgNode *)(pkg->base + h->nodes.off);
    pkg->node_data = pkg->base + h->node_data.off;
    pkg->tags      = (const uint32_t *)(pkg->base + h->tags.off);
    pkg->comps     = (const RRPkgComponent *)(pkg->base + h->comps.off);
    pkg->fields    = (const RRPkgField *)(pkg->base + h->fields.off);
    pkg->conns     = (const RRPkgConnection *)(pkg->base + h->conns.off);
    pkg->actions   = (const RRPkgAction *)(pkg->base + h->actions.off);
    pkg->bindings  = (const RRPkgBinding *)(pkg->base + h->bindings.off);
    pkg->assets    = (const RRPkgAsset *)(pkg->base + h->assets.off);
    pkg->pools     = (const RRPkgPool *)(pkg->base + h->pools.off);
    pkg->resources = (const RRPkgResource *)(pkg->base + h->resources.off);
    pkg->buses     = (const RRPkgBus *)(pkg->base + h->buses.off);
    pkg->translations = h->translations.count != 0
                            ? (const RRPkgTranslations *)(pkg->base + h->translations.off) : NULL;

    if (h->strings.count == 0 || pkg->strings[0] != '\0' ||
        pkg->strings[h->strings.count - 1] != '\0') {
        rr_set_error("package is corrupt: string table is not terminated");
        return false;
    }
    if (h->nodes.count == 0 || h->nodes.count >= RR_NONE16) {
        rr_set_error("package is corrupt: node count %u is not allowed", (unsigned)h->nodes.count);
        return false;
    }

    /* Assets first, because nodes refer to them. */
    for (i = 0; i < h->assets.count; i++) {
        const RRPkgAsset *a = &pkg->assets[i];
        bool              ok;
        if (!check_string(h, "asset", i, a->name)) {
            return false;
        }
        if (!range_ok(a->offset, a->size, 1, h->data.off, h->data.off + h->data.count)) {
            rr_set_error("package is corrupt: asset %u is outside the data region", (unsigned)i);
            return false;
        }
        switch (a->type) {
        case RR_ASSET_MESH:    ok = check_mesh(pkg, i); break;
        case RR_ASSET_TEXTURE: ok = check_texture(pkg, i); break;
        case RR_ASSET_FONT:    ok = check_font(pkg, i); break;
        case RR_ASSET_SOUND:   ok = check_sound(pkg, i); break;
        case RR_ASSET_ANIMATION: ok = check_animations(pkg, i); break;
        case RR_ASSET_SPRITE_FRAMES: ok = check_sprite_frames(pkg, i); break;
        case RR_ASSET_TILEMAP: ok = check_tilemap(pkg, i); break;
        case RR_ASSET_MUSIC:
            ok = a->size >= sizeof(RRPkgMusic) &&
                 ((const RRPkgMusic *)rr_package_asset(pkg, i))->file < h->strings.count;
            if (!ok) {
                rr_set_error("package is corrupt: music asset %u is malformed", (unsigned)i);
            }
            break;
        case RR_ASSET_SKIN: {
            const RRPkgSkin *s = (const RRPkgSkin *)rr_package_asset(pkg, i);
            ok = a->size >= sizeof(RRPkgSkin) && s->bone_count >= 1 && s->bone_count <= 256 &&
                 range_ok(s->bones_off, s->bone_count, 2, sizeof(RRPkgSkin), a->size) &&
                 range_ok(s->binds_off, s->bone_count, sizeof(RRMat34), sizeof(RRPkgSkin), a->size) &&
                 range_ok(s->influences_off, s->vertex_count, sizeof(RRPkgInfluence), sizeof(RRPkgSkin), a->size);
            if (ok) {
                const uint16_t       *bones = (const uint16_t *)((const uint8_t *)s + s->bones_off);
                const RRPkgInfluence *inf = (const RRPkgInfluence *)((const uint8_t *)s + s->influences_off);
                for (k = 0; k < s->bone_count; k++) {
                    ok = ok && bones[k] < h->nodes.count;
                }
                for (k = 0; k < s->vertex_count; k++) {
                    ok = ok && inf[k].bone[0] < s->bone_count && inf[k].bone[1] < s->bone_count &&
                         inf[k].bone[2] < s->bone_count && inf[k].bone[3] < s->bone_count;
                }
            }
            if (!ok) {
                rr_set_error("package is corrupt: skin asset %u is malformed", (unsigned)i);
            }
            break;
        }
        case RR_ASSET_HEIGHT_MAP: {
            const RRPkgHeightMap *m = (const RRPkgHeightMap *)rr_package_asset(pkg, i);
            ok = a->size >= offsetof(RRPkgHeightMap, heights) + 4 * sizeof(float) &&
                 m->width >= 2 && m->depth >= 2 && m->min <= m->max &&
                 (uint64_t)offsetof(RRPkgHeightMap, heights) +
                         (uint64_t)m->width * m->depth * sizeof(float) <= a->size;
            if (!ok) {
                rr_set_error("package is corrupt: height map asset %u is malformed", (unsigned)i);
            }
            break;
        }
        case RR_ASSET_TRIANGLES: {
            const RRPkgTriangles *t = (const RRPkgTriangles *)rr_package_asset(pkg, i);
            ok = a->size >= offsetof(RRPkgTriangles, corners) + 9 * sizeof(float) && t->count >= 1 &&
                 (uint64_t)offsetof(RRPkgTriangles, corners) + (uint64_t)t->count * 9 * sizeof(float) <= a->size &&
                 t->min[0] <= t->max[0] && t->min[1] <= t->max[1] && t->min[2] <= t->max[2];
            if (!ok) {
                rr_set_error("package is corrupt: triangles asset %u is malformed", (unsigned)i);
            }
            break;
        }
        case RR_ASSET_CURVE: {
            const RRPkgCurve *c = (const RRPkgCurve *)rr_package_asset(pkg, i);
            ok = a->size >= offsetof(RRPkgCurve, points) + 2 * sizeof(RRPkgCurvePoint) &&
                 c->point_count >= 2 && c->spacing > 0.0f &&
                 (uint64_t)offsetof(RRPkgCurve, points) +
                         (uint64_t)c->point_count * sizeof(RRPkgCurvePoint) <= a->size;
            if (!ok) {
                rr_set_error("package is corrupt: curve asset %u is malformed", (unsigned)i);
            }
            break;
        }
        default:
            rr_set_error("asset %u '%s' has type %u, which this runtime does not support",
                         (unsigned)i, rr_package_string(pkg, a->name), (unsigned)a->type);
            ok = false;
            break;
        }
        if (!ok) {
            return false;
        }
    }

    for (i = 0; i < h->nodes.count; i++) {
        const RRPkgNode *n = &pkg->nodes[i];
        uint32_t         dsize;
        if (!check_string(h, "node", i, n->name)) {
            return false;
        }
        if (i == 0 ? n->parent != RR_NONE16 : n->parent >= i) {
            rr_set_error("package is corrupt: node %u has a bad parent index", (unsigned)i);
            return false;
        }
        if (n->type >= RR_NODE_TYPE_COUNT) {
            rr_set_error("node %u '%s' has type %u, which this runtime does not support",
                         (unsigned)i, rr_package_string(pkg, n->name), (unsigned)n->type);
            return false;
        }
        if ((uint32_t)n->tag_first + n->tag_count > h->tags.count ||
            (uint32_t)n->comp_first + n->comp_count > h->comps.count) {
            rr_set_error("package is corrupt: node %u has a bad tag or component range",
                         (unsigned)i);
            return false;
        }
        for (k = 0; k < n->comp_count; k++) {
            if (pkg->comps[n->comp_first + k].node != i) {
                rr_set_error("package is corrupt: node %u lists a component it does not own",
                             (unsigned)i);
                return false;
            }
        }
        dsize = node_data_size(n->type);
        if (dsize == 0 ? n->data != RR_NONE32
                       : !range_ok(n->data, dsize, 1, 0, h->node_data.count)) {
            rr_set_error("package is corrupt: node %u has a bad data offset", (unsigned)i);
            return false;
        }
        if (dsize != 0 && !check_node_data(pkg, i)) {
            return false;
        }
    }
    {
        /* The scene is drawn level by level, so there must not be too many of them. */
        int32_t  seen[RR_MAX_Z_LEVELS];
        uint32_t count = 0;
        for (i = 0; i < h->nodes.count; i++) {
            const int32_t level = rr_package_node_level(&pkg->nodes[i]);
            for (k = 0; k < count && seen[k] != level; k++) {
            }
            if (k == count) {
                if (count == RR_MAX_Z_LEVELS) {
                    rr_set_error("package has more than %d draw levels (z_index)", RR_MAX_Z_LEVELS);
                    return false;
                }
                seen[count++] = level;
            }
        }
    }

    for (i = 0; i < h->tags.count; i++) {
        if (!check_string(h, "tag", i, pkg->tags[i])) {
            return false;
        }
    }

    for (i = 0; i < h->comps.count; i++) {
        const RRPkgComponent *c = &pkg->comps[i];
        if (!check_string(h, "component", i, c->name)) {
            return false;
        }
        if (c->node >= h->nodes.count ||
            (uint64_t)c->field_first + c->field_count > h->fields.count) {
            rr_set_error("package is corrupt: component %u has a bad node or field range",
                         (unsigned)i);
            return false;
        }
    }

    for (i = 0; i < h->fields.count; i++) {
        const RRPkgField *f = &pkg->fields[i];
        if (!check_string(h, "field", i, f->name)) {
            return false;
        }
        if (f->type < RR_FIELD_FLOAT || f->type > RR_FIELD_RESOURCE) {
            rr_set_error("field %u '%s' has type %u, which this runtime does not support",
                         (unsigned)i, rr_package_string(pkg, f->name), (unsigned)f->type);
            return false;
        }
        if ((f->type == RR_FIELD_NODE && f->value.u != RR_NONE32 && f->value.u >= h->nodes.count) ||
            (f->type == RR_FIELD_SCENE && f->value.u >= h->strings.count) ||
            (f->type == RR_FIELD_PACKED_SCENE && f->value.u != RR_NONE32 &&
             f->value.u >= h->pools.count) ||
            (f->type == RR_FIELD_RESOURCE && f->value.u != RR_NONE32 &&
             f->value.u >= h->resources.count)) {
            rr_set_error("package is corrupt: field %u has a bad reference", (unsigned)i);
            return false;
        }
    }

    for (i = 0; i < h->conns.count; i++) {
        const RRPkgConnection *c = &pkg->conns[i];
        if (c->source >= h->nodes.count || c->target >= h->nodes.count ||
            c->signal >= h->strings.count || c->method >= h->strings.count) {
            rr_set_error("package is corrupt: connection %u is out of range", (unsigned)i);
            return false;
        }
    }

    for (i = 0; i < h->pools.count; i++) {
        const RRPkgPool *p = &pkg->pools[i];
        const uint32_t   end = (uint32_t)p->first_node + (uint32_t)p->node_count * p->instance_count;
        if (!check_string(h, "pool", i, p->name)) {
            return false;
        }
        if (p->first_node == 0 || p->node_count == 0 || p->instance_count == 0 ||
            end > h->nodes.count) {
            rr_set_error("package is corrupt: pool %u has a bad node range", (unsigned)i);
            return false;
        }
        /* Every instance is one whole subtree below the root. */
        for (k = p->first_node; k < end; k++) {
            const uint32_t root = k - (k - p->first_node) % p->node_count;
            const uint16_t parent = pkg->nodes[k].parent;
            if (k == root ? parent != 0 : parent < root) {
                rr_set_error("package is corrupt: pool %u is not made of whole subtrees",
                             (unsigned)i);
                return false;
            }
        }
        if (end < h->nodes.count && pkg->nodes[end].parent >= p->first_node) {
            rr_set_error("package is corrupt: pool %u is not made of whole subtrees", (unsigned)i);
            return false;
        }
    }

    for (i = 0; i < h->resources.count; i++) {
        const RRPkgResource *r = &pkg->resources[i];
        if (!check_string(h, "resource", i, r->name)) {
            return false;
        }
        if ((uint64_t)r->field_first + r->field_count > h->fields.count) {
            rr_set_error("package is corrupt: resource %u has a bad field range", (unsigned)i);
            return false;
        }
    }

    if (pkg->translations != NULL) {
        const RRPkgTranslations *t = pkg->translations;
        uint64_t                 entries;

        if (h->translations.count < 12) {
            rr_set_error("package is corrupt: translation table");
            return false;
        }
        entries = (uint64_t)t->locale_count + t->key_count + (uint64_t)t->locale_count * t->key_count;
        if (t->locale_count == 0 || t->fallback >= t->locale_count ||
            12u + entries * 4u != h->translations.count) {
            rr_set_error("package is corrupt: translation table");
            return false;
        }
        for (i = 0; i < (uint32_t)entries; i++) {
            if (t->entries[i] >= h->strings.count) {
                rr_set_error("package is corrupt: translation %u", (unsigned)i);
                return false;
            }
        }
    }

    if (h->buses.count > RR_MAX_BUSES) {
        rr_set_error("package has %u audio buses, the limit is %d", (unsigned)h->buses.count,
                     RR_MAX_BUSES);
        return false;
    }
    for (i = 0; i < h->buses.count; i++) {
        const RRPkgBus *b = &pkg->buses[i];
        if (!check_string(h, "bus", i, b->name)) {
            return false;
        }
        /* A bus plays into one before it, so the way to Master always ends. */
        if (!(b->volume >= 0.0f) || (i == 0 ? b->send != RR_NONE16 : b->send >= i)) {
            rr_set_error("package is corrupt: audio bus %u", (unsigned)i);
            return false;
        }
    }

    for (i = 0; i < h->actions.count; i++) {
        const RRPkgAction *a = &pkg->actions[i];
        if (!check_string(h, "action", i, a->name)) {
            return false;
        }
        if ((uint32_t)a->binding_first + a->binding_count > h->bindings.count) {
            rr_set_error("package is corrupt: action %u has a bad binding range", (unsigned)i);
            return false;
        }
    }
    for (i = 0; i < h->bindings.count; i++) {
        const RRPkgBinding *b = &pkg->bindings[i];
        if (b->kind < RR_BIND_KEY || b->kind > RR_BIND_PAD_AXIS) {
            rr_set_error("package is corrupt: binding %u has a bad kind", (unsigned)i);
            return false;
        }
    }

    return true;
}
