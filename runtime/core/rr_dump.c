/* Text dump of a package, used by tools/rpkdump and by the runtime's --dump flag.
 * The output is compared against expected files in tests/, so keep it stable. */
#include "rr_package.h"

static const char *const g_node_type_names[RR_NODE_TYPE_COUNT] = {
    "Node", "Node3D", "MeshInstance3D", "Camera3D", "Area", "CollisionShape", "Node2D",
    "Sprite2D", "Control", "Label", "Button", "TextureRect", "AudioStreamPlayer",
    "AnimationPlayer", "CPUParticles", "StaticBody", "CharacterBody", "AnimatedSprite2D",
    "Camera2D", "Timer", "WorldEnvironment", "TileMapLayer", "RayCast", "Path", "PathFollow",
    "ColorRect", "ProgressBar", "NinePatchRect", "AudioStreamPlayer (spatial)", "Sprite3D",
    "Label3D", "RigidBody", "Slider", "Parallax2D", "Panel", "TextureProgressBar",
    "ScrollContainer", "RichTextLabel"
};

static const char *const g_blend_names[] = { "opaque", "punch-through", "translucent" };
static const char *const g_align_names[] = { "begin", "center", "end" };
static const char *const g_wrap_names[] = { "off", "arbitrary", "word", "word-smart" };
static const char *const g_bind_names[] = { "", "key", "button", "axis" };
static const char *const g_texture_formats[] = { "", "rgba8888", "rgb565", "argb1555", "argb4444" };
static const char *const g_sound_formats[] = { "", "pcm16", "adpcm" };

/* Rounds to three decimals and avoids printing "-0.000". */
static double tidy(float v)
{
    return (v > -0.0005f && v < 0.0005f) ? 0.0 : (double)v;
}

static void indent(FILE *out, uint32_t depth)
{
    while (depth--) {
        fputs("  ", out);
    }
}

static void dump_vec3(FILE *out, const float *v)
{
    fprintf(out, "(%.3f %.3f %.3f)", tidy(v[0]), tidy(v[1]), tidy(v[2]));
}

static void dump_rect(FILE *out, const float *r)
{
    fprintf(out, "rect (%.1f %.1f %.1f %.1f)", tidy(r[0]), tidy(r[1]), tidy(r[2]), tidy(r[3]));
}

static const char *node_name(const RRPackage *pkg, uint32_t index)
{
    return rr_package_string(pkg, pkg->nodes[index].name);
}

/* Where the focus goes from a button or slider. */
static void dump_neighbors(FILE *out, const RRPackage *pkg, const uint16_t *neighbors)
{
    int i;
    fputs("neighbors left top right bottom:", out);
    for (i = 0; i < 4; i++) {
        if (neighbors[i] == RR_NONE16) {
            fputs(" none", out);
        } else {
            fprintf(out, " [%u] %s", (unsigned)neighbors[i], node_name(pkg, neighbors[i]));
        }
    }
    fputc('\n', out);
}

static uint32_t node_depth(const RRPackage *pkg, uint32_t index)
{
    uint32_t depth = 0;
    while (pkg->nodes[index].parent != RR_NONE16) {
        index = pkg->nodes[index].parent;
        depth++;
    }
    return depth;
}

/* The line with the type-specific data of a node. */
static void dump_node_data(const RRPackage *pkg, const RRPkgNode *n, uint32_t depth, FILE *out)
{
    const void *data = rr_package_node_data(pkg, n);
    int         i;

    indent(out, depth);
    switch (n->type) {
    case RR_NODE_MESH: {
        const RRPkgMeshNode *m = (const RRPkgMeshNode *)data;
        fprintf(out, "mesh asset %u", (unsigned)m->mesh);
        if (m->skin != RR_NONE32) {
            fprintf(out, " skin asset %u", (unsigned)m->skin);
        }
        if (m->range_begin > 0.0f || m->range_end > 0.0f) {
            fprintf(out, " visible from %.3f to %.3f", tidy(m->range_begin), tidy(m->range_end));
        }
        fputc('\n', out);
        break;
    }
    case RR_NODE_SPRITE3D: {
        static const char *const billboards[] = { "", " billboard", " billboard-y" };
        const RRPkgSprite3DNode *s = (const RRPkgSprite3DNode *)data;
        fprintf(out, "texture asset %u color %08lx region (%.1f %.1f %.1f %.1f) offset (%.1f %.1f) "
                     "pixel %.4f frames %ux%u frame %u axis %c%s%s%s%s\n",
                (unsigned)s->texture, (unsigned long)s->color, tidy(s->region[0]), tidy(s->region[1]),
                tidy(s->region[2]), tidy(s->region[3]), tidy(s->offset[0]), tidy(s->offset[1]),
                tidy(s->pixel_size), (unsigned)s->hframes, (unsigned)s->vframes, (unsigned)s->frame,
                "xyz"[s->axis], s->centered ? " centered" : "", s->flip_h ? " flip_h" : "",
                s->flip_v ? " flip_v" : "", billboards[s->billboard]);
        break;
    }
    case RR_NODE_LABEL3D: {
        static const char *const billboards[] = { "", " billboard", " billboard-y" };
        const RRPkgLabel3DNode *l = (const RRPkgLabel3DNode *)data;
        fprintf(out, "text \"%s\" font asset %u color %08lx pixel %.4f offset (%.1f %.1f) align %s %s%s\n",
                rr_package_string(pkg, l->text), (unsigned)l->font, (unsigned long)l->color,
                tidy(l->pixel_size), tidy(l->offset[0]), tidy(l->offset[1]), g_align_names[l->h_align],
                g_align_names[l->v_align], billboards[l->billboard]);
        break;
    }
    case RR_NODE_RAYCAST: {
        const RRPkgRayCastNode *r = (const RRPkgRayCastNode *)data;
        fputs("target ", out);
        dump_vec3(out, r->target);
        fprintf(out, " mask %u%s%s%s%s", (unsigned)r->mask, r->enabled ? " enabled" : "",
                r->collide_areas ? " areas" : "", r->collide_bodies ? " bodies" : "",
                r->exclude_parent ? " exclude-parent" : "");
        if (r->shape != 0) {
            static const char *const kinds[] = { "", "box", "sphere", "capsule" };
            fprintf(out, " casts a %s ", kinds[r->shape % 4]);
            dump_vec3(out, r->shape_size);
        }
        fputc('\n', out);
        break;
    }
    case RR_NODE_PATH:
        fprintf(out, "curve asset %u\n", (unsigned)((const RRPkgPathNode *)data)->curve);
        break;
    case RR_NODE_PATH_FOLLOW: {
        static const char *const rotations[] = { "none", "y", "forward", "oriented" };
        const RRPkgPathFollowNode *p = (const RRPkgPathFollowNode *)data;
        fprintf(out, "progress %.3f offset (%.3f %.3f) rotation %s%s%s\n", tidy(p->progress),
                tidy(p->h_offset), tidy(p->v_offset), rotations[p->rotation], p->loop ? " loop" : "",
                p->two_d ? " 2d" : "");
        break;
    }
    case RR_NODE_COLOR_RECT: {
        const RRPkgColorRectNode *c = (const RRPkgColorRectNode *)data;
        dump_rect(out, c->rect);
        fprintf(out, " color %08lx\n", (unsigned long)c->color);
        break;
    }
    case RR_NODE_PROGRESS_BAR: {
        static const char *const fills[] = { "begin-to-end", "end-to-begin", "top-to-bottom",
                                             "bottom-to-top" };
        const RRPkgProgressBarNode *p = (const RRPkgProgressBarNode *)data;
        dump_rect(out, p->rect);
        fprintf(out, " min %.3f max %.3f value %.3f bg %08lx fill %08lx %s", tidy(p->min),
                tidy(p->max), tidy(p->value), (unsigned long)p->bg_color,
                (unsigned long)p->fill_color, fills[p->fill_mode]);
        if (p->show_percentage) {
            fprintf(out, " percentage font asset %u color %08lx", (unsigned)p->font,
                    (unsigned long)p->text_color);
        }
        fputc('\n', out);
        break;
    }
    case RR_NODE_NINE_PATCH: {
        const RRPkgNinePatchNode *p = (const RRPkgNinePatchNode *)data;
        dump_rect(out, p->rect);
        fprintf(out, " texture asset %u color %08lx region (%.1f %.1f %.1f %.1f) margins (%.1f %.1f %.1f %.1f)%s\n",
                (unsigned)p->texture, (unsigned long)p->color, tidy(p->region[0]),
                tidy(p->region[1]), tidy(p->region[2]), tidy(p->region[3]), tidy(p->margin[0]),
                tidy(p->margin[1]), tidy(p->margin[2]), tidy(p->margin[3]),
                p->draw_center ? "" : " no-center");
        break;
    }
    case RR_NODE_CAMERA: {
        const RRPkgCameraNode *c = (const RRPkgCameraNode *)data;
        if (c->size > 0.0f) {
            fprintf(out, "orthogonal size %.3f near %.3f far %.3f\n", tidy(c->size), tidy(c->z_near),
                    tidy(c->z_far));
        } else {
            fprintf(out, "fov %.3f near %.3f far %.3f\n", tidy(c->fov), tidy(c->z_near), tidy(c->z_far));
        }
        break;
    }
    case RR_NODE_AREA: {
        const RRPkgAreaNode *a = (const RRPkgAreaNode *)data;
        fprintf(out, "layer %u mask %u%s%s\n", (unsigned)a->layer, (unsigned)a->mask,
                a->monitoring ? " monitoring" : "", a->monitorable ? " monitorable" : "");
        break;
    }
    case RR_NODE_ANIMATED_SPRITE: {
        const RRPkgAnimatedSpriteNode *s = (const RRPkgAnimatedSpriteNode *)data;
        const RRPkgSpriteFrames       *set = (const RRPkgSpriteFrames *)rr_package_asset(pkg, s->frames);
        fprintf(out, "sprite frames asset %u animation \"%s\"%s color %08lx offset (%.1f %.1f) speed %.3f%s%s%s\n",
                (unsigned)s->frames, rr_package_string(pkg, set->anims[s->animation].name),
                s->playing ? " playing" : "", (unsigned long)s->color, tidy(s->offset[0]),
                tidy(s->offset[1]), tidy(s->speed_scale), s->centered ? " centered" : "",
                s->flip_h ? " flip_h" : "", s->flip_v ? " flip_v" : "");
        break;
    }
    case RR_NODE_CAMERA2D: {
        const RRPkgCamera2DNode *c = (const RRPkgCamera2DNode *)data;
        fprintf(out, "zoom (%.3f %.3f) offset (%.1f %.1f) limits left %.1f top %.1f right %.1f bottom %.1f%s%s\n",
                tidy(c->zoom[0]), tidy(c->zoom[1]), tidy(c->offset[0]), tidy(c->offset[1]),
                tidy(c->limit[RR_SIDE_LEFT]), tidy(c->limit[RR_SIDE_TOP]), tidy(c->limit[RR_SIDE_RIGHT]),
                tidy(c->limit[RR_SIDE_BOTTOM]), c->top_left ? " top-left" : "",
                c->enabled ? "" : " disabled");
        if (c->smoothing || c->drag_h || c->drag_v || c->drag_offset[0] != 0.0f || c->drag_offset[1] != 0.0f) {
            indent(out, depth);
            if (c->smoothing) {
                fprintf(out, "smoothing speed %.3f%s ", tidy(c->smoothing_speed),
                        c->limit_smoothed ? " limit smoothed" : "");
            }
            fprintf(out, "drag%s%s margins left %.3f top %.3f right %.3f bottom %.3f offset (%.3f %.3f)\n",
                    c->drag_h ? " horizontal" : "", c->drag_v ? " vertical" : "",
                    tidy(c->drag_margin[RR_SIDE_LEFT]), tidy(c->drag_margin[RR_SIDE_TOP]),
                    tidy(c->drag_margin[RR_SIDE_RIGHT]), tidy(c->drag_margin[RR_SIDE_BOTTOM]),
                    tidy(c->drag_offset[0]), tidy(c->drag_offset[1]));
        }
        break;
    }
    case RR_NODE_PANEL: {
        const RRPkgPanelNode *p = (const RRPkgPanelNode *)data;
        dump_rect(out, p->rect);
        fprintf(out, " color %08lx border %08lx left %.1f top %.1f right %.1f bottom %.1f\n",
                (unsigned long)p->color, (unsigned long)p->border_color, tidy(p->border[RR_SIDE_LEFT]),
                tidy(p->border[RR_SIDE_TOP]), tidy(p->border[RR_SIDE_RIGHT]),
                tidy(p->border[RR_SIDE_BOTTOM]));
        break;
    }
    case RR_NODE_TEXTURE_PROGRESS: {
        static const char *const fills[] = { "left-to-right", "right-to-left", "top-to-bottom",
                                             "bottom-to-top" };
        static const char *const names[] = { "under", "progress", "over" };
        const RRPkgTextureProgressNode *p = (const RRPkgTextureProgressNode *)data;
        const uint32_t                  textures[3] = { p->under, p->progress, p->over };
        const uint32_t                  tints[3] = { p->tint_under, p->tint_progress, p->tint_over };
        dump_rect(out, p->rect);
        fprintf(out, " min %.3f max %.3f value %.3f %s\n", tidy(p->min), tidy(p->max), tidy(p->value),
                fills[p->fill_mode % 4]);
        indent(out, depth);
        fputs("textures", out);
        for (i = 0; i < 3; i++) {
            if (textures[i] != RR_NONE32) {
                fprintf(out, " %s asset %u tint %08lx", names[i], (unsigned)textures[i],
                        (unsigned long)tints[i]);
            }
        }
        fputc('\n', out);
        break;
    }
    case RR_NODE_RICH_TEXT: {
        static const char *const styles[] = { "normal", "bold", "italic", "bold-italic" };
        const RRPkgRichTextNode *r = (const RRPkgRichTextNode *)data;
        dump_rect(out, r->rect);
        fputs(" fonts", out);
        for (i = 0; i < 4; i++) {
            if (r->fonts[i] != RR_NONE32) {
                fprintf(out, " %s asset %u", styles[i], (unsigned)r->fonts[i]);
            }
        }
        fprintf(out, " color %08lx align %s wrap %s%s\n", (unsigned long)r->color,
                g_align_names[r->v_align % 3], g_wrap_names[r->wrap % 4], r->bbcode ? " bbcode" : "");
        indent(out, depth);
        fprintf(out, "text \"%s\"\n", rr_package_string(pkg, r->text));
        break;
    }
    case RR_NODE_SCROLL: {
        const RRPkgScrollNode *s = (const RRPkgScrollNode *)data;
        dump_rect(out, s->rect);
        fprintf(out, " content (%.1f %.1f) start (%.1f %.1f)%s%s%s\n", tidy(s->content[0]),
                tidy(s->content[1]), tidy(s->start[0]), tidy(s->start[1]),
                s->scroll_h ? " horizontal" : "", s->scroll_v ? " vertical" : "",
                s->follow_focus ? " follows the focus" : "");
        if (s->bar_h[2] > 0.0f || s->bar_v[2] > 0.0f) {
            indent(out, depth);
            fprintf(out, "bars colors %08lx grabber %08lx", (unsigned long)s->bar_color,
                    (unsigned long)s->grabber_color);
            if (s->bar_h[2] > 0.0f) {
                fputs(" horizontal ", out);
                dump_rect(out, s->bar_h);
            }
            if (s->bar_v[2] > 0.0f) {
                fputs(" vertical ", out);
                dump_rect(out, s->bar_v);
            }
            fputc('\n', out);
        }
        break;
    }
    case RR_NODE_PARALLAX: {
        const RRPkgParallaxNode *p = (const RRPkgParallaxNode *)data;
        fprintf(out, "scroll scale (%.3f %.3f) offset (%.1f %.1f) repeat size (%.1f %.1f) times %u "
                     "autoscroll (%.1f %.1f)%s%s\n",
                tidy(p->scroll_scale[0]), tidy(p->scroll_scale[1]), tidy(p->scroll_offset[0]),
                tidy(p->scroll_offset[1]), tidy(p->repeat_size[0]), tidy(p->repeat_size[1]),
                (unsigned)p->repeat_times, tidy(p->autoscroll[0]), tidy(p->autoscroll[1]),
                p->follow_viewport ? "" : " does not follow the viewport",
                p->ignore_camera_scroll ? " ignores the camera" : "");
        indent(out, depth);
        fprintf(out, "limits begin (%.1f %.1f) end (%.1f %.1f) screen offset (%.1f %.1f)\n",
                tidy(p->limit_begin[0]), tidy(p->limit_begin[1]), tidy(p->limit_end[0]),
                tidy(p->limit_end[1]), tidy(p->screen_offset[0]), tidy(p->screen_offset[1]));
        break;
    }
    case RR_NODE_TIMER: {
        const RRPkgTimerNode *t = (const RRPkgTimerNode *)data;
        fprintf(out, "wait %.3f%s%s\n", tidy(t->wait_time), t->one_shot ? " one-shot" : "",
                t->autostart ? " autostart" : "");
        break;
    }
    case RR_NODE_ENVIRONMENT: {
        const RRPkgEnvironmentNode *e = (const RRPkgEnvironmentNode *)data;
        if (e->sky[0] != RR_NONE32) {
            fprintf(out, "sky texture assets %u %u %u %u %u %u, ", (unsigned)e->sky[0],
                    (unsigned)e->sky[1], (unsigned)e->sky[2], (unsigned)e->sky[3],
                    (unsigned)e->sky[4], (unsigned)e->sky[5]);
        }
        if (!e->fog_enabled) {
            fputs("no fog\n", out);
        } else if (e->fog_mode == RR_FOG_DEPTH) {
            fprintf(out, "fog color %08lx depth %.3f..%.3f\n", (unsigned long)e->fog_color,
                    tidy(e->fog_begin), tidy(e->fog_end));
        } else {
            fprintf(out, "fog color %08lx density %.4f\n", (unsigned long)e->fog_color,
                    (double)e->fog_density);
        }
        break;
    }
    case RR_NODE_TILEMAP: {
        const RRPkgTileMapNode *t = (const RRPkgTileMapNode *)data;
        fprintf(out, "tile map asset %u color %08lx layer %u mask %u\n", (unsigned)t->tiles,
                (unsigned long)t->color, (unsigned)t->layer, (unsigned)t->mask);
        break;
    }
    case RR_NODE_STATIC_BODY:
    case RR_NODE_CHARACTER_BODY:
    case RR_NODE_RIGID_BODY: {
        const RRPkgBodyNode *b = (const RRPkgBodyNode *)data;
        fprintf(out, "layer %u mask %u%s bounce %.3f friction %.3f", (unsigned)b->layer,
                (unsigned)b->mask, b->two_d ? " 2d" : "", tidy(b->bounce), tidy(b->friction));
        if (n->type == RR_NODE_RIGID_BODY) {
            fprintf(out, " mass %.3f damp %.3f gravity ", tidy(b->mass), tidy(b->linear_damp));
            dump_vec3(out, b->gravity);
            fputs(" velocity ", out);
            dump_vec3(out, b->velocity);
        }
        fputc('\n', out);
        break;
    }
    case RR_NODE_SHAPE: {
        const RRPkgShapeNode *s = (const RRPkgShapeNode *)data;
        if (s->kind == RR_SHAPE_BOX) {
            fputs("box half extents ", out);
            dump_vec3(out, s->size);
        } else if (s->kind == RR_SHAPE_CAPSULE) {
            fprintf(out, "capsule radius %.3f half length %.3f", tidy(s->size[0]), tidy(s->size[1]));
        } else if (s->kind == RR_SHAPE_HEIGHT_MAP) {
            fprintf(out, "height map asset %u", (unsigned)s->map);
        } else {
            fprintf(out, "sphere radius %.3f", tidy(s->size[0]));
        }
        fprintf(out, "%s%s\n", s->one_way ? " one-way" : "", s->disabled ? " disabled" : "");
        break;
    }
    case RR_NODE_SPRITE: {
        const RRPkgSpriteNode *s = (const RRPkgSpriteNode *)data;
        fprintf(out, "texture asset %u color %08lx ", (unsigned)s->texture, (unsigned long)s->color);
        fprintf(out, "offset (%.1f %.1f) region (%.1f %.1f %.1f %.1f) frames %ux%u frame %u%s%s%s\n",
                tidy(s->offset[0]), tidy(s->offset[1]), tidy(s->region[0]), tidy(s->region[1]),
                tidy(s->region[2]), tidy(s->region[3]), (unsigned)s->hframes, (unsigned)s->vframes,
                (unsigned)s->frame, s->centered ? " centered" : "", s->flip_h ? " flip_h" : "",
                s->flip_v ? " flip_v" : "");
        break;
    }
    case RR_NODE_CONTROL:
        dump_rect(out, ((const RRPkgControlNode *)data)->rect);
        fputc('\n', out);
        break;
    case RR_NODE_LABEL: {
        const RRPkgLabelNode *l = (const RRPkgLabelNode *)data;
        dump_rect(out, l->rect);
        fprintf(out, " font asset %u color %08lx align %s %s", (unsigned)l->font,
                (unsigned long)l->color, g_align_names[l->h_align % 3], g_align_names[l->v_align % 3]);
        if (l->wrap != RR_WRAP_OFF) {
            fprintf(out, " wrap %s", g_wrap_names[l->wrap % 4]);
        }
        fputc('\n', out);
        indent(out, depth);
        fprintf(out, "text \"%s\"\n", rr_package_string(pkg, l->text));
        break;
    }
    case RR_NODE_BUTTON: {
        static const char *const states[] = { "normal", "pressed", "focused", "disabled" };
        const RRPkgButtonNode *b = (const RRPkgButtonNode *)data;
        bool                   textures = false;
        dump_rect(out, b->rect);
        if (b->font != RR_NONE32) {
            fprintf(out, " font asset %u", (unsigned)b->font);
        }
        fprintf(out, " colors text %08lx focus text %08lx bg %08lx focus %08lx\n",
                (unsigned long)b->text_color, (unsigned long)b->focus_text_color,
                (unsigned long)b->bg_color, (unsigned long)b->focus_color);
        indent(out, depth);
        fprintf(out, "disabled text %08lx bg %08lx pressed text %08lx bg %08lx align %s margin %.1f "
                     "separation %.1f%s%s%s\n",
                (unsigned long)b->disabled_text_color, (unsigned long)b->disabled_color,
                (unsigned long)b->pressed_text_color, (unsigned long)b->pressed_color,
                g_align_names[b->h_align % 3], tidy(b->margin), tidy(b->separation),
                b->toggle ? " toggle" : "", b->disabled ? " disabled" : "", b->pressed ? " pressed" : "");
        if (b->icon != RR_NONE32 || b->icon_pressed != RR_NONE32) {
            indent(out, depth);
            fputs("icon", out);
            if (b->icon != RR_NONE32) {
                fprintf(out, " texture asset %u", (unsigned)b->icon);
            }
            if (b->icon_pressed != RR_NONE32) {
                fprintf(out, " pressed texture asset %u", (unsigned)b->icon_pressed);
            }
            fputc('\n', out);
        }
        for (i = 0; i < 4; i++) {
            textures = textures || b->textures[i] != RR_NONE32;
        }
        if (textures) {
            indent(out, depth);
            fputs("textures", out);
            for (i = 0; i < 4; i++) {
                if (b->textures[i] != RR_NONE32) {
                    fprintf(out, " %s asset %u", states[i], (unsigned)b->textures[i]);
                }
            }
            fputc('\n', out);
        }
        indent(out, depth);
        fprintf(out, "text \"%s\"\n", rr_package_string(pkg, b->text));
        indent(out, depth);
        dump_neighbors(out, pkg, b->neighbors);
        break;
    }
    case RR_NODE_SLIDER: {
        const RRPkgSliderNode *s = (const RRPkgSliderNode *)data;
        dump_rect(out, s->rect);
        fprintf(out, " min %.3f max %.3f step %.3f value %.3f%s%s\n", tidy(s->min), tidy(s->max),
                tidy(s->step), tidy(s->value), s->vertical ? " vertical" : "",
                s->editable ? "" : " not editable");
        indent(out, depth);
        fprintf(out, "track %.1f colors track %08lx fill %08lx focus fill %08lx grabber texture assets "
                     "%u focus %u disabled %u\n",
                tidy(s->track), (unsigned long)s->track_color, (unsigned long)s->fill_color,
                (unsigned long)s->focus_fill_color, (unsigned)s->grabber, (unsigned)s->focus_grabber,
                (unsigned)s->disabled_grabber);
        indent(out, depth);
        dump_neighbors(out, pkg, s->neighbors);
        break;
    }
    case RR_NODE_TEXTURE_RECT: {
        const RRPkgTextureRectNode *t = (const RRPkgTextureRectNode *)data;
        dump_rect(out, t->rect);
        fprintf(out, " texture asset %u color %08lx\n", (unsigned)t->texture, (unsigned long)t->color);
        break;
    }
    case RR_NODE_ANIMATION_PLAYER:
        fprintf(out, "animations asset %u",
                (unsigned)((const RRPkgAnimationPlayerNode *)data)->animations);
        if (((const RRPkgAnimationPlayerNode *)data)->speed_scale != 1.0f) {
            fprintf(out, " speed %.3f", tidy(((const RRPkgAnimationPlayerNode *)data)->speed_scale));
        }
        if (((const RRPkgAnimationPlayerNode *)data)->blend_time != 0.0f) {
            fprintf(out, " blend time %.3f", tidy(((const RRPkgAnimationPlayerNode *)data)->blend_time));
        }
        fputc('\n', out);
        break;
    case RR_NODE_PARTICLES: {
        const RRPkgParticlesNode *p = (const RRPkgParticlesNode *)data;
        fprintf(out, "particles %u lifetime %.3f explosiveness %.3f%s%s%s\n", (unsigned)p->amount,
                tidy(p->lifetime), tidy(p->explosiveness), p->one_shot ? " one-shot" : "",
                p->emitting ? " emitting" : "", p->two_d ? " 2d" : "");
        indent(out, depth);
        fputs("direction ", out);
        dump_vec3(out, p->direction);
        fprintf(out, " spread %.3f velocity %.3f..%.3f gravity ", tidy(p->spread),
                tidy(p->velocity_min), tidy(p->velocity_max));
        dump_vec3(out, p->gravity);
        fputc('\n', out);
        indent(out, depth);
        fprintf(out, "size (%.3f %.3f) scale %.3f..%.3f colors %08lx %08lx radius %.3f", tidy(p->size[0]),
                tidy(p->size[1]), tidy(p->scale_min), tidy(p->scale_max),
                (unsigned long)p->color_start, (unsigned long)p->color_end, tidy(p->emission_radius));
        if (p->texture != RR_NONE32) {
            fprintf(out, " texture asset %u", (unsigned)p->texture);
        }
        fputc('\n', out);
        break;
    }
    default: {
        const RRPkgAudioNode *a = (const RRPkgAudioNode *)data;
        fprintf(out, "sound asset %u volume %.3f pitch %.3f bus %s%s%s", (unsigned)a->sound,
                tidy(a->volume), tidy(a->pitch), rr_package_string(pkg, pkg->buses[a->bus].name),
                a->autoplay ? " autoplay" : "", a->loop ? " loop" : "");
        if (a->mode == RR_AUDIO_3D) {
            static const char *const models[] = { "inverse", "inverse-square", "logarithmic",
                                                  "disabled" };
            fprintf(out, " 3d %s unit %.3f max distance %.3f max gain %.3f pan %.3f",
                    models[a->model], tidy(a->unit_size), tidy(a->max_distance),
                    tidy(a->max_gain), tidy(a->pan_strength));
        } else if (a->mode == RR_AUDIO_2D) {
            fprintf(out, " 2d max distance %.3f attenuation %.3f pan %.3f", tidy(a->max_distance),
                    tidy(a->attenuation), tidy(a->pan_strength));
        }
        fputc('\n', out);
        break;
    }
    }
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

static void dump_fields(const RRPackage *pkg, uint32_t first, uint32_t count, uint32_t depth,
                        FILE *out)
{
    uint32_t k;
    for (k = 0; k < count; k++) {
        const RRPkgField *f = &pkg->fields[first + k];
        indent(out, depth);
        fprintf(out, "%s = ", rr_package_string(pkg, f->name));
        switch (f->type) {
        case RR_FIELD_FLOAT:
            fprintf(out, "float %.3f\n", tidy(f->value.f));
            break;
        case RR_FIELD_INT:
            fprintf(out, "int %ld\n", (long)f->value.i);
            break;
        case RR_FIELD_BOOL:
            fprintf(out, "bool %s\n", f->value.u ? "true" : "false");
            break;
        case RR_FIELD_SCENE:
            fprintf(out, "scene \"%s\"\n", rr_package_string(pkg, f->value.u));
            break;
        case RR_FIELD_RESOURCE:
            if (f->value.u == RR_NONE32) {
                fputs("resource none\n", out);
            } else {
                fprintf(out, "resource %lu\n", (unsigned long)f->value.u);
            }
            break;
        case RR_FIELD_PACKED_SCENE:
            if (f->value.u == RR_NONE32) {
                fputs("packed scene none\n", out);
            } else {
                fprintf(out, "packed scene pool %lu\n", (unsigned long)f->value.u);
            }
            break;
        default:
            if (f->value.u == RR_NONE32) {
                fputs("node none\n", out);
            } else {
                fprintf(out, "node [%u] %s\n", (unsigned)f->value.u, node_name(pkg, f->value.u));
            }
            break;
        }
    }
}

static void dump_node(const RRPackage *pkg, uint32_t index, FILE *out)
{
    const RRPkgNode *n = &pkg->nodes[index];
    const uint32_t   depth = node_depth(pkg, index);
    uint32_t         i;

    indent(out, depth);
    fprintf(out, "[%u] %s (%s)", (unsigned)index, rr_package_string(pkg, n->name),
            g_node_type_names[n->type]);
    if (!(n->flags & RR_NODE_FLAG_VISIBLE)) {
        fputs(" hidden", out);
    }
    if (n->flags & RR_NODE_FLAG_CURRENT_CAMERA) {
        fputs(" current", out);
    }
    if (n->z != 0) {
        fprintf(out, " z %d", (int)n->z);
    }
    if (n->canvas_layer) {
        fputs(" in canvas layer", out);
    }
    if (!n->translate && (n->type == RR_NODE_LABEL || n->type == RR_NODE_BUTTON ||
                          n->type == RR_NODE_LABEL3D || n->type == RR_NODE_RICH_TEXT)) {
        fputs(" not translated", out);
    }
    if (n->flags & RR_NODE_FLAG_AUTOLOAD) {
        fputs(" autoload", out);
    }
    fputc('\n', out);

    if (has_transform(n->type)) {
        indent(out, depth + 1);
        fputs("origin ", out);
        dump_vec3(out, &n->xform.m[9]);
        fputs(" basis ", out);
        dump_vec3(out, &n->xform.m[0]);
        fputc(' ', out);
        dump_vec3(out, &n->xform.m[3]);
        fputc(' ', out);
        dump_vec3(out, &n->xform.m[6]);
        fputc('\n', out);
    }
    if (n->data != RR_NONE32) {
        dump_node_data(pkg, n, depth + 1, out);
    }
    if (n->tag_count > 0) {
        indent(out, depth + 1);
        fputs("tags", out);
        for (i = 0; i < n->tag_count; i++) {
            fprintf(out, " %s", rr_package_string(pkg, pkg->tags[n->tag_first + i]));
        }
        fputc('\n', out);
    }
    for (i = 0; i < n->comp_count; i++) {
        const RRPkgComponent *c = &pkg->comps[n->comp_first + i];
        indent(out, depth + 1);
        fprintf(out, "component %s\n", rr_package_string(pkg, c->name));
        dump_fields(pkg, c->field_first, c->field_count, depth + 2, out);
    }
}

static void dump_asset(const RRPackage *pkg, uint32_t index, FILE *out)
{
    const RRPkgAsset *a = &pkg->assets[index];
    const char       *name = rr_package_string(pkg, a->name);
    uint32_t          s;

    switch (a->type) {
    case RR_ASSET_MESH: {
        const RRPkgMesh *mesh = rr_package_mesh(pkg, index);
        fprintf(out, "  [%u] mesh %s bounds (%.3f, %.3f, %.3f) radius %.3f\n", (unsigned)index, name,
                (double)mesh->center[0], (double)mesh->center[1], (double)mesh->center[2],
                (double)mesh->radius);
        for (s = 0; s < mesh->surface_count; s++) {
            const RRPkgSurface *surf = &mesh->surfaces[s];
            fprintf(out, "    surface %u %s strips %u vertices %u", (unsigned)s,
                    g_blend_names[surf->blend], (unsigned)surf->strip_count,
                    (unsigned)surf->vertex_count);
            if (surf->texture != RR_NONE32) {
                fprintf(out, " texture asset %u", (unsigned)surf->texture);
            }
            fputc('\n', out);
        }
        break;
    }
    case RR_ASSET_TEXTURE: {
        const RRPkgTexture *t = (const RRPkgTexture *)rr_package_asset(pkg, index);
        fprintf(out, "  [%u] texture %s %ux%u (content %ux%u) %s%s\n", (unsigned)index, name,
                (unsigned)t->width, (unsigned)t->height, (unsigned)t->content_width,
                (unsigned)t->content_height, g_texture_formats[t->format],
                t->filter == RR_FILTER_NEAREST ? " nearest" : "");
        break;
    }
    case RR_ASSET_FONT: {
        const RRPkgFont *f = (const RRPkgFont *)rr_package_asset(pkg, index);
        fprintf(out, "  [%u] font %s atlas asset %u glyphs %u line height %.1f ascent %.1f\n",
                (unsigned)index, name, (unsigned)f->texture, (unsigned)f->glyph_count,
                tidy(f->line_height), tidy(f->ascent));
        break;
    }
    case RR_ASSET_MUSIC: {
        const RRPkgMusic *music = (const RRPkgMusic *)rr_package_asset(pkg, index);
        fprintf(out, "  [%u] music %s file %s %.2f s\n", (unsigned)index, name,
                rr_package_string(pkg, music->file), tidy(music->length));
        break;
    }
    case RR_ASSET_SKIN: {
        const RRPkgSkin      *skin = (const RRPkgSkin *)rr_package_asset(pkg, index);
        const uint16_t       *bones = (const uint16_t *)((const uint8_t *)skin + skin->bones_off);
        const RRPkgInfluence *inf = (const RRPkgInfluence *)((const uint8_t *)skin + skin->influences_off);
        uint32_t              b, v, shared = 0;
        /* How many vertices follow more than one bone. */
        for (v = 0; v < skin->vertex_count; v++) {
            shared += inf[v].weight[1] != 0;
        }
        fprintf(out, "  [%u] skin %s %u vertices, %u between bones, bones", (unsigned)index, name,
                (unsigned)skin->vertex_count, (unsigned)shared);
        for (b = 0; b < skin->bone_count; b++) {
            fprintf(out, " [%u] %s", (unsigned)bones[b], node_name(pkg, bones[b]));
        }
        fputc('\n', out);
        break;
    }
    case RR_ASSET_HEIGHT_MAP: {
        const RRPkgHeightMap *map = (const RRPkgHeightMap *)rr_package_asset(pkg, index);
        fprintf(out, "  [%u] height map %s %u x %u points, heights %.3f to %.3f\n", (unsigned)index,
                name, (unsigned)map->width, (unsigned)map->depth, tidy(map->min), tidy(map->max));
        break;
    }
    case RR_ASSET_CURVE: {
        const RRPkgCurve *curve = (const RRPkgCurve *)rr_package_asset(pkg, index);
        uint32_t          i;
        fprintf(out, "  [%u] curve %s length %.3f spacing %.3f points %u\n", (unsigned)index, name,
                tidy(curve->length), tidy(curve->spacing), (unsigned)curve->point_count);
        /* The first, the middle and the last point stand for the rest. */
        for (i = 0; i < 3; i++) {
            const RRPkgCurvePoint *p = &curve->points[i * (curve->point_count - 1) / 2];
            fputs("    at ", out);
            dump_vec3(out, p->pos);
            fputs(" up ", out);
            dump_vec3(out, p->up);
            fputc('\n', out);
        }
        break;
    }
    case RR_ASSET_TILEMAP: {
        const RRPkgTileMap  *map = (const RRPkgTileMap *)rr_package_asset(pkg, index);
        const RRPkgTileCell *cells = (const RRPkgTileCell *)((const uint8_t *)map + map->cells_off);
        const RRPkgTileBox  *boxes = (const RRPkgTileBox *)((const uint8_t *)map + map->boxes_off);
        uint32_t             i;
        fprintf(out, "  [%u] tile map %s texture asset %u tile size (%.0f %.0f)\n", (unsigned)index,
                name, (unsigned)map->texture, tidy(map->tile_size[0]), tidy(map->tile_size[1]));
        fprintf(out, "    cells %u:", (unsigned)map->cell_count);
        for (i = 0; i < map->cell_count; i++) {
            fprintf(out, " (%d %d)@(%u %u)", (int)cells[i].x, (int)cells[i].y, (unsigned)cells[i].u,
                    (unsigned)cells[i].v);
            /* t: transposed, h and v: flipped; then the number of its animation. */
            if (cells[i].flags != 0) {
                fprintf(out, "%s%s%s", (cells[i].flags & RR_TILE_TRANSPOSE) ? "t" : "",
                        (cells[i].flags & RR_TILE_FLIP_H) ? "h" : "",
                        (cells[i].flags & RR_TILE_FLIP_V) ? "v" : "");
            }
            if (cells[i].anim != RR_NONE16) {
                fprintf(out, "a%u", (unsigned)cells[i].anim);
            }
        }
        fprintf(out, "\n    boxes %u:", (unsigned)map->box_count);
        for (i = 0; i < map->box_count; i++) {
            fputc(' ', out);
            dump_rect(out, &boxes[i].x);
            if (boxes[i].one_way) {
                fputs(" one-way", out);
            }
        }
        fputc('\n', out);
        for (i = 0; i < map->anim_count; i++) {
            const RRPkgTileAnim  *anim = (const RRPkgTileAnim *)((const uint8_t *)map + map->anims_off) + i;
            const RRPkgTileFrame *frames = (const RRPkgTileFrame *)((const uint8_t *)map + anim->frames_off);
            uint32_t              f;
            fprintf(out, "    animation %u, %.3f s:", (unsigned)i, tidy(anim->duration));
            for (f = 0; f < anim->frame_count; f++) {
                fprintf(out, " (%u %u) until %.3f", (unsigned)frames[f].u, (unsigned)frames[f].v,
                        tidy(frames[f].end));
            }
            fputc('\n', out);
        }
        break;
    }
    case RR_ASSET_SPRITE_FRAMES: {
        const RRPkgSpriteFrames *set = (const RRPkgSpriteFrames *)rr_package_asset(pkg, index);
        uint32_t                 i, f;
        fprintf(out, "  [%u] sprite frames %s\n", (unsigned)index, name);
        for (i = 0; i < set->anim_count; i++) {
            const RRPkgSpriteAnim  *anim = &set->anims[i];
            const RRPkgSpriteFrame *frames = (const RRPkgSpriteFrame *)((const uint8_t *)set + anim->frames_off);
            fprintf(out, "    animation \"%s\" fps %.3f%s:", rr_package_string(pkg, anim->name),
                    tidy(anim->fps), anim->loop ? " loop" : "");
            for (f = 0; f < anim->frame_count; f++) {
                fprintf(out, " [texture %u (%.0f %.0f %.0f %.0f) x%.3f]", (unsigned)frames[f].texture,
                        tidy(frames[f].region[0]), tidy(frames[f].region[1]), tidy(frames[f].region[2]),
                        tidy(frames[f].region[3]), tidy(frames[f].duration));
            }
            fputc('\n', out);
        }
        break;
    }
    case RR_ASSET_ANIMATION: {
        static const char *const kinds[] = { "", "position", "rotation", "scale", "visible",
                                             "position 2d", "rotation 2d", "scale 2d", "frame",
                                             "value", "method" };
        /* How many of a key's four numbers the track uses. */
        static const int counts[] = { 0, 3, 4, 3, 1, 2, 1, 2, 1, 1, 0 };
        const RRPkgAnimations   *set = (const RRPkgAnimations *)rr_package_asset(pkg, index);
        uint32_t                 c, t, k;
        int                      i;
        fprintf(out, "  [%u] animations %s\n", (unsigned)index, name);
        for (c = 0; c < set->clip_count; c++) {
            const RRPkgClip  *clip = &set->clips[c];
            const RRPkgTrack *tracks = (const RRPkgTrack *)((const uint8_t *)set + clip->tracks_off);
            fprintf(out, "    clip \"%s\" length %.3f%s%s\n", rr_package_string(pkg, clip->name),
                    tidy(clip->length), clip->loop ? " loop" : "",
                    set->autoplay == c ? " autoplay" : "");
            for (t = 0; t < clip->track_count; t++) {
                const RRPkgKey *keys = (const RRPkgKey *)((const uint8_t *)set + tracks[t].keys_off);
                const int       values = counts[tracks[t].kind];
                fprintf(out, "      %s of [%u] %s, %s:", kinds[tracks[t].kind], (unsigned)tracks[t].node,
                        rr_package_string(pkg, pkg->nodes[tracks[t].node].name),
                        tracks[t].interp == RR_INTERP_LINEAR ? "linear" : "nearest");
                for (k = 0; k < tracks[t].key_count; k++) {
                    fprintf(out, " %.3f (", tidy(keys[k].time));
                    if (tracks[t].kind == RR_TRACK_METHOD) {
                        fputs(rr_package_string(pkg, (uint32_t)keys[k].value[0]), out);
                    }
                    for (i = 0; i < values; i++) {
                        fprintf(out, "%s%.3f", i > 0 ? " " : "", tidy(keys[k].value[i]));
                    }
                    fputc(')', out);
                }
                fputc('\n', out);
            }
        }
        break;
    }
    default: {
        const RRPkgSound *snd = (const RRPkgSound *)rr_package_asset(pkg, index);
        fprintf(out, "  [%u] sound %s %s %u Hz %u samples%s\n", (unsigned)index, name,
                g_sound_formats[snd->format], (unsigned)snd->sample_rate,
                (unsigned)snd->sample_count, snd->channels > 1 ? " stereo" : "");
        break;
    }
    }
}

void rr_package_dump(const RRPackage *pkg, FILE *out)
{
    const RRPkgHeader *h = pkg->header;
    uint32_t           i, k;

    fprintf(out, "package version %u platform %s %s-endian\n", (unsigned)h->version,
            rr_platform_name(h->platform), h->big_endian ? "big" : "little");
    fprintf(out, "clear color %08lx\n", (unsigned long)h->clear_color);

    fprintf(out, "nodes %u\n", (unsigned)h->nodes.count);
    for (i = 0; i < h->nodes.count; i++) {
        dump_node(pkg, i, out);
    }

    fprintf(out, "connections %u\n", (unsigned)h->conns.count);
    for (i = 0; i < h->conns.count; i++) {
        const RRPkgConnection *c = &pkg->conns[i];
        fprintf(out, "  [%u] %s.%s -> [%u] %s.%s\n", (unsigned)c->source, node_name(pkg, c->source),
                rr_package_string(pkg, c->signal), (unsigned)c->target, node_name(pkg, c->target),
                rr_package_string(pkg, c->method));
    }

    fprintf(out, "pools %u\n", (unsigned)h->pools.count);
    for (i = 0; i < h->pools.count; i++) {
        const RRPkgPool *p = &pkg->pools[i];
        fprintf(out, "  [%u] %s: %u instances of %u nodes from node %u\n", (unsigned)i,
                rr_package_string(pkg, p->name), (unsigned)p->instance_count,
                (unsigned)p->node_count, (unsigned)p->first_node);
    }

    fprintf(out, "resources %u\n", (unsigned)h->resources.count);
    for (i = 0; i < h->resources.count; i++) {
        const RRPkgResource *r = &pkg->resources[i];
        fprintf(out, "  [%u] %s\n", (unsigned)i, rr_package_string(pkg, r->name));
        dump_fields(pkg, r->field_first, r->field_count, 2, out);
    }

    if (pkg->translations != NULL) {
        const RRPkgTranslations *t = pkg->translations;
        const uint32_t          *keys = t->entries + t->locale_count;
        const uint32_t          *messages = keys + t->key_count;
        fprintf(out, "translations %u keys, locales", (unsigned)t->key_count);
        for (i = 0; i < t->locale_count; i++) {
            fprintf(out, " %s%s", rr_package_string(pkg, t->entries[i]), i == t->fallback ? " (fallback)" : "");
        }
        fputc('\n', out);
        for (i = 0; i < t->key_count; i++) {
            fprintf(out, "  %s:", rr_package_string(pkg, keys[i]));
            for (k = 0; k < t->locale_count; k++) {
                const uint32_t message = messages[k * t->key_count + i];
                if (message != 0) {
                    fprintf(out, " %s \"%s\"", rr_package_string(pkg, t->entries[k]),
                            rr_package_string(pkg, message));
                }
            }
            fputc('\n', out);
        }
    }

    fprintf(out, "audio buses %u\n", (unsigned)h->buses.count);
    for (i = 0; i < h->buses.count; i++) {
        const RRPkgBus *b = &pkg->buses[i];
        fprintf(out, "  [%u] %s volume %.3f%s", (unsigned)i, rr_package_string(pkg, b->name),
                tidy(b->volume), b->mute ? " mute" : "");
        if (b->send != RR_NONE16) {
            fprintf(out, " -> %s", rr_package_string(pkg, pkg->buses[b->send].name));
        }
        fputc('\n', out);
    }

    fprintf(out, "actions %u\n", (unsigned)h->actions.count);
    for (i = 0; i < h->actions.count; i++) {
        const RRPkgAction *a = &pkg->actions[i];
        fprintf(out, "  %s deadzone %.2f:", rr_package_string(pkg, a->name), tidy(a->deadzone));
        for (k = 0; k < a->binding_count; k++) {
            const RRPkgBinding *b = &pkg->bindings[a->binding_first + k];
            fprintf(out, " %s %u", g_bind_names[b->kind], (unsigned)b->code);
            if (b->kind == RR_BIND_PAD_AXIS) {
                fputc(b->sign < 0 ? '-' : '+', out);
            }
        }
        fputc('\n', out);
    }

    fprintf(out, "assets %u\n", (unsigned)h->assets.count);
    for (i = 0; i < h->assets.count; i++) {
        dump_asset(pkg, i, out);
    }
}
