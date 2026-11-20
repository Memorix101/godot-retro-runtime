/* Draws a scene through the platform graphics interface: first the 3D nodes through the
 * active camera, then sprites and controls on top in tree order. Kept apart from
 * rr_scene.c so tools can link the core without a graphics backend. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rr_internal.h"
#include "rr_platform.h"

#define FOCUS_BORDER 2.0f

static const float g_full_uv[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };

/* --- controls inside a ScrollContainer ------------------------------------------------
 * The rectangles of controls are baked, so scrolling is done where their quads are
 * drawn: every quad of the control that is being drawn is moved by how far the
 * containers above it are scrolled, and cut off at their edges. */

#define MAX_SCROLL_DEPTH 4 /* ScrollContainers inside each other */

static struct {
    bool  clip;
    float min[2], max[2]; /* what is outside of this is not drawn */
    float shift[2];
} g_ui;

/* Sets up moving and cutting for one control. */
static void ui_begin(const Node *n)
{
    const Node *above[MAX_SCROLL_DEPTH];
    const Node *at;
    int         count = 0, i, a;

    g_ui.clip     = false;
    g_ui.shift[0] = g_ui.shift[1] = 0.0f;
    for (at = n->parent; at != NULL && count < MAX_SCROLL_DEPTH; at = at->parent) {
        if (at->type == RR_NODE_SCROLL) {
            above[count++] = at;
        }
    }
    /* From the outermost one in: each is itself moved by the ones around it. */
    for (i = count - 1; i >= 0; i--) {
        const RRPkgScrollNode *s = (const RRPkgScrollNode *)above[i]->data;
        for (a = 0; a < 2; a++) {
            const float lo = s->rect[a] + g_ui.shift[a], hi = lo + s->rect[2 + a];
            g_ui.min[a] = g_ui.clip && g_ui.min[a] > lo ? g_ui.min[a] : lo;
            g_ui.max[a] = g_ui.clip && g_ui.max[a] < hi ? g_ui.max[a] : hi;
            g_ui.shift[a] -= above[i]->local.m[9 + a] - s->start[a];
        }
        g_ui.clip = true;
    }
}

/* One quad of a control: upright, corners in the order of rr_gfx_draw_quad. */
static void ui_quad(uint32_t texture, const float *xy, const float *uv, uint32_t color)
{
    float p[4] = { xy[0] + g_ui.shift[0], xy[1] + g_ui.shift[1], xy[4] + g_ui.shift[0], xy[5] + g_ui.shift[1] };
    float t[4] = { uv[0], uv[1], uv[4], uv[5] };
    int   a;

    if (g_ui.clip) {
        for (a = 0; a < 2; a++) {
            const float size = p[2 + a] - p[a], span = t[2 + a] - t[a];
            if (size <= 0.0f || p[a] >= g_ui.max[a] || p[2 + a] <= g_ui.min[a]) {
                return;
            }
            if (p[a] < g_ui.min[a]) {
                t[a] += span * (g_ui.min[a] - p[a]) / size;
                p[a]  = g_ui.min[a];
            }
            if (p[2 + a] > g_ui.max[a]) {
                t[2 + a] -= span * (p[2 + a] - g_ui.max[a]) / size;
                p[2 + a]  = g_ui.max[a];
            }
        }
    }
    {
        const float cut_xy[8] = { p[0], p[1], p[2], p[1], p[2], p[3], p[0], p[3] };
        const float cut_uv[8] = { t[0], t[1], t[2], t[1], t[2], t[3], t[0], t[3] };
        rr_gfx_draw_quad(texture, cut_xy, cut_uv, color);
    }
}

/* Texture coordinates of the image inside its (possibly padded) texture. */
static void content_uv(const RRPkgTexture *t, float *u, float *v)
{
    *u = (float)t->content_width / (float)t->width;
    *v = (float)t->content_height / (float)t->height;
}

static void draw_rect(const float *r, uint32_t color)
{
    const float xy[8] = { r[0], r[1], r[0] + r[2], r[1], r[0] + r[2], r[1] + r[3], r[0], r[1] + r[3] };
    if ((color & 0xFFu) != 0 && r[2] > 0.0f && r[3] > 0.0f) {
        ui_quad(RR_NONE32, xy, g_full_uv, color);
    }
}

static void draw_outline(const float *r, float t, uint32_t color)
{
    const float top[4] = { r[0], r[1], r[2], t };
    const float bottom[4] = { r[0], r[1] + r[3] - t, r[2], t };
    const float left[4] = { r[0], r[1] + t, t, r[3] - 2.0f * t };
    const float right[4] = { r[0] + r[2] - t, r[1] + t, t, r[3] - 2.0f * t };
    draw_rect(top, color);
    draw_rect(bottom, color);
    draw_rect(left, color);
    draw_rect(right, color);
}

/* --- text ---------------------------------------------------------------------------- */

/* Reads one UTF-8 character and advances the pointer. Broken input yields '?'. */
static uint32_t next_codepoint(const char **text)
{
    const uint8_t *s = (const uint8_t *)*text;
    uint32_t       cp = s[0];
    int            extra = 0, i;

    if (cp >= 0xF0u) {
        cp &= 0x07u;
        extra = 3;
    } else if (cp >= 0xE0u) {
        cp &= 0x0Fu;
        extra = 2;
    } else if (cp >= 0xC0u) {
        cp &= 0x1Fu;
        extra = 1;
    } else if (cp >= 0x80u) {
        cp = '?';
    }
    s++;
    for (i = 0; i < extra; i++) {
        if ((*s & 0xC0u) != 0x80u) {
            cp = '?';
            break;
        }
        cp = (cp << 6) | (*s++ & 0x3Fu);
    }
    *text = (const char *)s;
    return cp;
}

static const RRPkgGlyph *find_glyph(const RRPkgFont *font, uint32_t codepoint)
{
    uint32_t lo = 0, hi = font->glyph_count;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2u;
        if (font->glyphs[mid].codepoint < codepoint) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < font->glyph_count && font->glyphs[lo].codepoint == codepoint ? &font->glyphs[lo] : NULL;
}

/* Width of the line that starts at `text` (up to a newline or the end). */
static float line_width(const RRPkgFont *font, const char *text)
{
    float width = 0.0f;
    while (*text != '\0' && *text != '\n') {
        const RRPkgGlyph *g = find_glyph(font, next_codepoint(&text));
        if (g != NULL) {
            width += g->advance;
        }
    }
    return width;
}

/* Finds the end of the line that starts at `text`: a newline, the end of the text or,
 * with `wrap` (RR_WRAP_*), the last place to break it before it gets wider than
 * `max_width`. Spaces at the end of a broken line hang over and are not counted.
 * `end` is where the line stops, `next` where the following one starts, `width` how wide
 * the line is. Returns false if this was the last line. */
static bool line_end(const RRPkgFont *font, const char *text, float max_width, uint8_t wrap,
                     const char **end, const char **next, float *width)
{
    const char *p = text, *gap = NULL; /* the last run of spaces: the line can end there */
    float       pen = 0.0f, gap_width = 0.0f;
    bool        in_gap = false;

    while (*p != '\0' && *p != '\n') {
        const char       *start = p;
        const uint32_t    cp = next_codepoint(&p);
        const RRPkgGlyph *g = find_glyph(font, cp);
        const float       advance = g != NULL ? g->advance : 0.0f;

        if (cp == ' ') {
            if (!in_gap) {
                gap       = start;
                gap_width = pen;
                in_gap    = true;
            }
        } else {
            in_gap = false;
            if (wrap != RR_WRAP_OFF && start > text && pen + advance > max_width) {
                if (wrap != RR_WRAP_ARBITRARY && gap != NULL && gap > text) {
                    *end = gap;
                    while (*gap == ' ') {
                        gap++;
                    }
                    *next  = gap;
                    *width = gap_width;
                    return true;
                }
                if (wrap != RR_WRAP_WORD) {
                    *end = *next = start;
                    *width       = pen;
                    return true;
                }
            }
        }
        pen += advance;
    }
    *end   = p;
    *next  = *p == '\n' ? p + 1 : p;
    *width = pen;
    return *p == '\n';
}

/* Draws text inside a rectangle. Lines are split at '\n' and, with `wrap` (RR_WRAP_*), at
 * the width of the rectangle. */
static void draw_text(const RRScene *scene, uint32_t font_asset, const char *text, const float *rect,
                      uint8_t h_align, uint8_t v_align, uint8_t wrap, uint32_t color)
{
    const RRPkgFont    *font = (const RRPkgFont *)rr_package_asset(scene->pkg, font_asset);
    const RRPkgTexture *atlas = (const RRPkgTexture *)rr_package_asset(scene->pkg, font->texture);
    const uint32_t      handle = scene->handles[font->texture];
    const float         inv_w = 1.0f / (float)atlas->width, inv_h = 1.0f / (float)atlas->height;
    const char         *p, *end, *next;
    float               lines = 0.0f, baseline, width;
    bool                more;

    if (text == NULL || text[0] == '\0' || (color & 0xFFu) == 0) {
        return;
    }
    for (p = text, more = true; more; p = next) {
        more = line_end(font, p, rect[2], wrap, &end, &next, &width);
        lines += 1.0f;
    }
    baseline = rect[1] + (rect[3] - lines * font->line_height) * 0.5f * (float)v_align + font->ascent;

    for (p = text, more = true; more; p = next, baseline += font->line_height) {
        float pen;
        more = line_end(font, p, rect[2], wrap, &end, &next, &width);
        pen  = rect[0] + (rect[2] - width) * 0.5f * (float)h_align;
        while (p < end) {
            const RRPkgGlyph *g = find_glyph(font, next_codepoint(&p));
            if (g == NULL) {
                continue;
            }
            if (g->w > 0 && g->h > 0) {
                /* Whole pixels keep the glyphs sharp. */
                const float x0 = floorf(pen + (float)g->off_x + 0.5f);
                const float y0 = floorf(baseline + (float)g->off_y + 0.5f);
                const float x1 = x0 + (float)g->w, y1 = y0 + (float)g->h;
                const float u0 = (float)g->x * inv_w, v0 = (float)g->y * inv_h;
                const float u1 = (float)(g->x + g->w) * inv_w, v1 = (float)(g->y + g->h) * inv_h;
                const float xy[8] = { x0, y0, x1, y0, x1, y1, x0, y1 };
                const float uv[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };
                ui_quad(handle, xy, uv, color);
            }
            pen += g->advance;
        }
    }
}

/* --- 2D nodes ------------------------------------------------------------------------ */

/* What the active Camera2D shows: screen = (world - origin) * zoom. Controls are not
 * affected; their rectangles are baked in screen space. */
typedef struct {
    float ox, oy, zx, zy;
} Canvas;

static void canvas_setup(Canvas *c, const RRScene *scene)
{
    /* Where the view is was worked out in the update (rr_canvas.c). */
    c->ox = scene->view[0];
    c->oy = scene->view[1];
    c->zx = c->zy = 1.0f;
    if (scene->camera2d != NULL) {
        const RRPkgCamera2DNode *d = (const RRPkgCamera2DNode *)scene->camera2d->data;
        c->zx = d->zoom[0];
        c->zy = d->zoom[1];
    }
}

/* Draws a textured rectangle of a 2D node. `rect` is x, y, width, height in the node's
 * space, `region` the same in pixels of the image. */
static void draw_node_quad(const RRScene *scene, const Canvas *c, const Node *n, uint32_t texture,
                           const float *rect, const float *region, bool flip_h, bool flip_v,
                           bool transpose, uint32_t color)
{
    const RRPkgTexture *t = (const RRPkgTexture *)rr_package_asset(scene->pkg, texture);
    const float        *m = n->world.m;
    const float         lx[4] = { rect[0], rect[0] + rect[2], rect[0] + rect[2], rect[0] };
    const float         ly[4] = { rect[1], rect[1], rect[1] + rect[3], rect[1] + rect[3] };
    const float         u0 = region[0] / (float)t->width, u1 = (region[0] + region[2]) / (float)t->width;
    const float         v0 = region[1] / (float)t->height, v1 = (region[1] + region[3]) / (float)t->height;
    float               xy[8], uv[8];
    int                 i;

    for (i = 0; i < 4; i++) {
        /* Where in the picture this corner looks: flipped, and with `transpose` X and Y
         * swapped before that (a tile that is turned). */
        const bool across = (i == 1 || i == 2) != flip_h, down = (i >= 2) != flip_v;
        const bool right = transpose ? down : across;
        const bool bottom = transpose ? across : down;
        xy[i * 2]     = (m[0] * lx[i] + m[3] * ly[i] + m[9] - c->ox) * c->zx;
        xy[i * 2 + 1] = (m[1] * lx[i] + m[4] * ly[i] + m[10] - c->oy) * c->zy;
        uv[i * 2]     = right ? u1 : u0;
        uv[i * 2 + 1] = bottom ? v1 : v0;
    }
    rr_gfx_draw_quad(scene->handles[texture], xy, uv, color);
}

static void draw_sprite(const RRScene *scene, const Canvas *c, const Node *n)
{
    const RRPkgSpriteNode *s = (const RRPkgSpriteNode *)n->data;
    /* The cell of the sprite sheet, counted row by row. A frame past the end wraps. */
    const uint32_t         frame = n->frame % ((uint32_t)s->hframes * s->vframes);
    const float            w = s->region[2] / (float)s->hframes, h = s->region[3] / (float)s->vframes;
    const float            region[4] = { s->region[0] + (float)(frame % s->hframes) * w,
                                         s->region[1] + (float)(frame / s->hframes) * h, w, h };
    const float            rect[4] = { s->offset[0] - (s->centered ? w * 0.5f : 0.0f),
                                       s->offset[1] - (s->centered ? h * 0.5f : 0.0f), w, h };

    draw_node_quad(scene, c, n, s->texture, rect, region, s->flip_h != 0, s->flip_v != 0, false, s->color);
}

static void draw_animated_sprite(const RRScene *scene, const Canvas *c, const Node *n)
{
    const RRPkgAnimatedSpriteNode *s = (const RRPkgAnimatedSpriteNode *)n->data;
    const RRSpriteState           *state = rr_sprite_state(n);
    const RRPkgSpriteFrame        *frame = rr_sprite_frame(n, state);
    /* Every frame is centred by its own size, as in Godot. */
    const float w = frame->region[2], h = frame->region[3];
    const float rect[4] = { s->offset[0] - (s->centered ? w * 0.5f : 0.0f),
                            s->offset[1] - (s->centered ? h * 0.5f : 0.0f), w, h };

    draw_node_quad(scene, c, n, frame->texture, rect, frame->region, state->flip_h, state->flip_v, false,
                   s->color);
}

static void draw_tilemap(const RRScene *scene, const Canvas *c, const Node *n)
{
    const RRPkgTileMapNode *d = (const RRPkgTileMapNode *)n->data;
    const RRPkgTileMap     *map = (const RRPkgTileMap *)rr_package_asset(scene->pkg, d->tiles);
    const RRPkgTileCell    *cells = (const RRPkgTileCell *)((const uint8_t *)map + map->cells_off);
    const RRPkgTileAnim    *anims = (const RRPkgTileAnim *)((const uint8_t *)map + map->anims_off);
    const float            *m = n->world.m;
    const float             tw = map->tile_size[0], th = map->tile_size[1];
    /* How far a tile can reach from its corner on the screen, for the visibility test. */
    const float reach_x = (fabsf(m[0]) * tw + fabsf(m[3]) * th) * c->zx;
    const float reach_y = (fabsf(m[1]) * tw + fabsf(m[4]) * th) * c->zy;
    uint32_t    i;

    for (i = 0; i < map->cell_count; i++) {
        const float rect[4] = { (float)cells[i].x * tw, (float)cells[i].y * th, tw, th };
        float       region[4] = { (float)cells[i].u, (float)cells[i].v, tw, th };
        const float sx = (m[0] * rect[0] + m[3] * rect[1] + m[9] - c->ox) * c->zx;
        const float sy = (m[1] * rect[0] + m[4] * rect[1] + m[10] - c->oy) * c->zy;

        if (sx < -reach_x || sx > (float)RR_SCREEN_WIDTH + reach_x || sy < -reach_y ||
            sy > (float)RR_SCREEN_HEIGHT + reach_y) {
            continue;
        }
        if (cells[i].anim != RR_NONE16) {
            /* An animated tile: the picture whose time it is. All tiles run on the clock
             * of the scene, so equal ones stay in step. */
            const RRPkgTileAnim  *anim = &anims[cells[i].anim];
            const RRPkgTileFrame *frames = (const RRPkgTileFrame *)((const uint8_t *)map + anim->frames_off);
            const float           at = fmodf(scene->time, anim->duration);
            uint32_t              f = 0;
            while (f + 1 < anim->frame_count && frames[f].end <= at) {
                f++;
            }
            region[0] = (float)frames[f].u;
            region[1] = (float)frames[f].v;
        }
        draw_node_quad(scene, c, n, map->texture, rect, region, (cells[i].flags & RR_TILE_FLIP_H) != 0,
                       (cells[i].flags & RR_TILE_FLIP_V) != 0, (cells[i].flags & RR_TILE_TRANSPOSE) != 0,
                       d->color);
    }
}

/* The whole image of a texture asset, stretched over a rectangle. */
static void draw_image(const RRScene *scene, uint32_t asset, const float *r, uint32_t color)
{
    const RRPkgTexture *t = (const RRPkgTexture *)rr_package_asset(scene->pkg, asset);
    const float xy[8] = { r[0], r[1], r[0] + r[2], r[1], r[0] + r[2], r[1] + r[3], r[0], r[1] + r[3] };
    float       uv[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    content_uv(t, &uv[2], &uv[5]);
    uv[4] = uv[2];
    uv[7] = uv[5];
    ui_quad(scene->handles[asset], xy, uv, color);
}

static void draw_texture_rect(const RRScene *scene, const Node *n)
{
    const RRPkgTextureRectNode *d = (const RRPkgTextureRectNode *)n->data;
    draw_image(scene, d->texture, d->rect, d->color);
}

/* Button, CheckBox, CheckButton and TextureButton: the background of its state, the
 * textures of a TextureButton, the mark of the focus, the icon and the text. */
static void draw_button(const RRScene *scene, const Node *n)
{
    const RRPkgButtonNode *b = (const RRPkgButtonNode *)n->data;
    const float           *r = b->rect;
    const bool             focused = n == scene->focus;
    const bool             disabled = (n->flags & RR_NODE_FLAG_DISABLED) != 0, pressed = n->frame != 0;
    const uint32_t         icon = pressed && b->icon_pressed != RR_NONE32 ? b->icon_pressed : b->icon;
    uint32_t               texture = b->textures[RR_BUTTON_TEX_NORMAL];
    float                  inner[4] = { r[0] + b->margin, r[1], r[2] - 2.0f * b->margin, r[3] };

    draw_rect(r, disabled ? b->disabled_color : (pressed ? b->pressed_color : b->bg_color));
    if (disabled && b->textures[RR_BUTTON_TEX_DISABLED] != RR_NONE32) {
        texture = b->textures[RR_BUTTON_TEX_DISABLED];
    } else if (pressed && b->textures[RR_BUTTON_TEX_PRESSED] != RR_NONE32) {
        texture = b->textures[RR_BUTTON_TEX_PRESSED];
    }
    if (texture != RR_NONE32) {
        draw_image(scene, texture, r, 0xFFFFFFFFu);
    }
    if (focused) {
        if (b->textures[RR_BUTTON_TEX_FOCUSED] != RR_NONE32) {
            draw_image(scene, b->textures[RR_BUTTON_TEX_FOCUSED], r, 0xFFFFFFFFu);
        }
        draw_outline(r, FOCUS_BORDER, b->focus_color);
    }
    if (icon != RR_NONE32) {
        /* At its own size, left of the text and half as bright on a disabled button. */
        const RRPkgTexture *t = (const RRPkgTexture *)rr_package_asset(scene->pkg, icon);
        const float         w = (float)t->content_width, h = (float)t->content_height;
        const float         at[4] = { inner[0], floorf(r[1] + (r[3] - h) * 0.5f + 0.5f), w, h };
        draw_image(scene, icon, at, disabled ? 0xFFFFFF80u : 0xFFFFFFFFu);
        inner[0] += w + b->separation;
        inner[2] -= w + b->separation;
    }
    if (b->font != RR_NONE32) {
        draw_text(scene, b->font, n->text, inner, b->h_align, RR_ALIGN_CENTER, RR_WRAP_OFF,
                  disabled ? b->disabled_text_color
                           : (focused ? b->focus_text_color
                                      : (pressed ? b->pressed_text_color : b->text_color)));
    }
}

/* HSlider and VSlider: the track, the part of it below the value, and the grabber. */
static void draw_slider(const RRScene *scene, const Node *n)
{
    const RRPkgSliderNode *s = (const RRPkgSliderNode *)n->data;
    const float           *r = s->rect;
    const bool             focused = n == scene->focus;
    const uint32_t         grabber = (n->flags & RR_NODE_FLAG_DISABLED) ? s->disabled_grabber
                                                                        : (focused ? s->focus_grabber : s->grabber);
    const RRPkgTexture    *t = (const RRPkgTexture *)rr_package_asset(scene->pkg, grabber);
    const float            gw = (float)t->content_width, gh = (float)t->content_height;
    const uint32_t         fill_color = focused ? s->focus_fill_color : s->fill_color;
    float                  ratio = s->max > s->min ? (n->value - s->min) / (s->max - s->min) : 0.0f;
    float                  track[4], fill[4], at[4];

    ratio = ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
    if (s->vertical) {
        at[0]    = floorf(r[0] + (r[2] - gw) * 0.5f + 0.5f);
        at[1]    = floorf(r[1] + (1.0f - ratio) * (r[3] - gh) + 0.5f);
        track[0] = fill[0] = r[0] + (r[2] - s->track) * 0.5f;
        track[1] = r[1];
        track[2] = fill[2] = s->track;
        track[3] = r[3];
        fill[1]  = at[1] + gh * 0.5f;
        fill[3]  = r[1] + r[3] - fill[1];
    } else {
        at[0]    = floorf(r[0] + ratio * (r[2] - gw) + 0.5f);
        at[1]    = floorf(r[1] + (r[3] - gh) * 0.5f + 0.5f);
        track[0] = fill[0] = r[0];
        track[1] = fill[1] = r[1] + (r[3] - s->track) * 0.5f;
        track[2] = r[2];
        track[3] = fill[3] = s->track;
        fill[2]  = at[0] + gw * 0.5f - r[0];
    }
    at[2] = gw;
    at[3] = gh;
    draw_rect(track, s->track_color);
    draw_rect(fill, fill_color);
    draw_image(scene, grabber, at, 0xFFFFFFFFu);
}

static void draw_progress_bar(const RRScene *scene, const Node *n)
{
    const RRPkgProgressBarNode *d = (const RRPkgProgressBarNode *)n->data;
    const float                *r = d->rect;
    float                       ratio = d->max > d->min ? (n->value - d->min) / (d->max - d->min) : 0.0f;
    float                       fill[4] = { r[0], r[1], r[2], r[3] };

    ratio = ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
    switch (d->fill_mode) {
    case RR_FILL_END_TO_BEGIN:
        fill[0] = r[0] + r[2] * (1.0f - ratio);
        fill[2] = r[2] * ratio;
        break;
    case RR_FILL_TOP_TO_BOTTOM:
        fill[3] = r[3] * ratio;
        break;
    case RR_FILL_BOTTOM_TO_TOP:
        fill[1] = r[1] + r[3] * (1.0f - ratio);
        fill[3] = r[3] * ratio;
        break;
    default:
        fill[2] = r[2] * ratio;
        break;
    }
    draw_rect(r, d->bg_color);
    draw_rect(fill, d->fill_color);
    if (d->show_percentage) {
        char text[8];
        snprintf(text, sizeof(text), "%d%%", (int)(ratio * 100.0f + 0.5f));
        draw_text(scene, d->font, text, r, RR_ALIGN_CENTER, RR_ALIGN_CENTER, RR_WRAP_OFF, d->text_color);
    }
}

/* Nine quads: the corners keep the size they have in the image, the rest stretches. */
static void draw_nine_patch(const RRScene *scene, const Node *n)
{
    const RRPkgNinePatchNode *d = (const RRPkgNinePatchNode *)n->data;
    const RRPkgTexture       *t = (const RRPkgTexture *)rr_package_asset(scene->pkg, d->texture);
    const float              *r = d->rect, *g = d->region, *m = d->margin;
    const float x[4] = { r[0], r[0] + m[RR_SIDE_LEFT], r[0] + r[2] - m[RR_SIDE_RIGHT], r[0] + r[2] };
    const float y[4] = { r[1], r[1] + m[RR_SIDE_TOP], r[1] + r[3] - m[RR_SIDE_BOTTOM], r[1] + r[3] };
    const float u[4] = { g[0] / (float)t->width, (g[0] + m[RR_SIDE_LEFT]) / (float)t->width,
                         (g[0] + g[2] - m[RR_SIDE_RIGHT]) / (float)t->width,
                         (g[0] + g[2]) / (float)t->width };
    const float v[4] = { g[1] / (float)t->height, (g[1] + m[RR_SIDE_TOP]) / (float)t->height,
                         (g[1] + g[3] - m[RR_SIDE_BOTTOM]) / (float)t->height,
                         (g[1] + g[3]) / (float)t->height };
    int         row, col;

    for (row = 0; row < 3; row++) {
        for (col = 0; col < 3; col++) {
            const float xy[8] = { x[col], y[row], x[col + 1], y[row], x[col + 1], y[row + 1], x[col], y[row + 1] };
            const float uv[8] = { u[col], v[row], u[col + 1], v[row], u[col + 1], v[row + 1], u[col], v[row + 1] };
            if ((row == 1 && col == 1 && !d->draw_center) || x[col + 1] <= x[col] ||
                y[row + 1] <= y[row]) {
                continue;
            }
            ui_quad(scene->handles[d->texture], xy, uv, d->color);
        }
    }
}

/* --- culling ------------------------------------------------------------------------- */

/* The view volume of the camera, set up once per frame. */
typedef struct {
    float origin[3];
    float x[3], y[3], z[3]; /* unit axes of the camera; it looks along -z */
    float z_near, z_far;
    float tan_x, tan_y;     /* half extent of the view at depth 1 */
    float half_x, half_y;   /* orthogonal camera: half extent of the view; 0: perspective */
    float inv_x, inv_y;     /* 1 / length of the side plane normals */
} Frustum;

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void unit_axis(const float *in, float *out)
{
    const float length = sqrtf(dot3(in, in));
    const float inv = length > 0.0f ? 1.0f / length : 0.0f;
    out[0] = in[0] * inv;
    out[1] = in[1] * inv;
    out[2] = in[2] * inv;
}

static void frustum_setup(Frustum *f, const RRMat34 *camera, const RRPkgCameraNode *c)
{
    const float *m = camera->m;

    unit_axis(&m[0], f->x);
    unit_axis(&m[3], f->y);
    unit_axis(&m[6], f->z);
    f->origin[0] = m[9];
    f->origin[1] = m[10];
    f->origin[2] = m[11];
    f->z_near = c->z_near;
    f->z_far = c->z_far;
    f->half_y = c->size * 0.5f;
    f->half_x = f->half_y * ((float)RR_SCREEN_WIDTH / (float)RR_SCREEN_HEIGHT);
    f->tan_y = tanf(c->fov * (3.14159265f / 360.0f));
    f->tan_x = f->tan_y * ((float)RR_SCREEN_WIDTH / (float)RR_SCREEN_HEIGHT);
    f->inv_x = 1.0f / sqrtf(1.0f + f->tan_x * f->tan_x);
    f->inv_y = 1.0f / sqrtf(1.0f + f->tan_y * f->tan_y);
}

/* False when the mesh's bounding sphere lies completely outside the view volume. */
static bool mesh_in_view(const Frustum *f, const RRPkgMesh *mesh, const RRMat34 *world)
{
    const float *m = world->m;
    const float *c = mesh->center;
    const float  d[3] = { m[0] * c[0] + m[3] * c[1] + m[6] * c[2] + m[9] - f->origin[0],
                          m[1] * c[0] + m[4] * c[1] + m[7] * c[2] + m[10] - f->origin[1],
                          m[2] * c[0] + m[5] * c[1] + m[8] * c[2] + m[11] - f->origin[2] };
    /* The largest axis scale bounds how far the node's transform can stretch the sphere. */
    const float  sx = dot3(&m[0], &m[0]), sy = dot3(&m[3], &m[3]), sz = dot3(&m[6], &m[6]);
    const float  s2 = sx > sy ? (sx > sz ? sx : sz) : (sy > sz ? sy : sz);
    const float  r = mesh->radius * sqrtf(s2);
    const float  depth = -dot3(d, f->z);
    float        side;

    if (depth + r < f->z_near || depth - r > f->z_far) {
        return false;
    }
    side = dot3(d, f->x);
    if (side < 0.0f) {
        side = -side;
    }
    if (f->half_y > 0.0f ? side - f->half_x > r : (side - depth * f->tan_x) * f->inv_x > r) {
        return false;
    }
    side = dot3(d, f->y);
    if (side < 0.0f) {
        side = -side;
    }
    return f->half_y > 0.0f ? side - f->half_y <= r : (side - depth * f->tan_y) * f->inv_y <= r;
}

/* The two directions a flat node (Sprite3D, Label3D) is laid out along in the world: its
 * own axes, or the camera's for a billboard, with the node's scale kept. */
static void flat_axes(const RRScene *scene, const Node *n, uint8_t billboard, uint8_t axis,
                      float *right, float *up)
{
    const float *m = n->world.m, *cam = scene->camera->world.m;
    /* The quad faces `axis`: for Z it lies along X and Y, for X along -Z and Y, for Y along
     * X and -Z. */
    const int   r = axis == 0 ? 2 : 0, u = axis == 1 ? 2 : 1;
    const float r_sign = axis == 0 ? -1.0f : 1.0f, u_sign = axis == 1 ? -1.0f : 1.0f;
    float       r_length = 0.0f, u_length = 0.0f, length;
    int         k;

    for (k = 0; k < 3; k++) {
        right[k] = m[r * 3 + k] * r_sign;
        up[k]    = m[u * 3 + k] * u_sign;
        r_length += right[k] * right[k];
        u_length += up[k] * up[k];
    }
    if (billboard == RR_BILLBOARD_OFF) {
        return;
    }
    r_length = sqrtf(r_length);
    u_length = sqrtf(u_length);
    for (k = 0; k < 3; k++) {
        right[k] = cam[k];
        up[k]    = billboard == RR_BILLBOARD_Y ? (k == 1 ? 1.0f : 0.0f) : cam[3 + k];
    }
    if (billboard == RR_BILLBOARD_Y) {
        right[1] = 0.0f; /* turns around the Y axis only */
    }
    length = sqrtf(dot3(right, right));
    for (k = 0; k < 3; k++) {
        right[k] *= length > 0.0f ? r_length / length : 0.0f;
        up[k]    *= u_length;
    }
}

/* One quad of a flat node: `x0..x1`, `y0..y1` are in pixels, Y up, from the node's origin. */
static void draw_flat_quad(const float *origin, const float *right, const float *up, float pixel,
                           float x0, float y0, float x1, float y1, uint32_t texture, const float *uv,
                           uint32_t color)
{
    float xyz[12];
    int   k;

    for (k = 0; k < 3; k++) {
        xyz[k]     = origin[k] + (right[k] * x0 + up[k] * y1) * pixel; /* top left */
        xyz[3 + k] = origin[k] + (right[k] * x1 + up[k] * y1) * pixel;
        xyz[6 + k] = origin[k] + (right[k] * x1 + up[k] * y0) * pixel;
        xyz[9 + k] = origin[k] + (right[k] * x0 + up[k] * y0) * pixel;
    }
    rr_gfx_draw_quad_3d(texture, xyz, uv, color);
}

static void draw_sprite3d(const RRScene *scene, const Node *n)
{
    const RRPkgSprite3DNode *s = (const RRPkgSprite3DNode *)n->data;
    const RRPkgTexture      *t = (const RRPkgTexture *)rr_package_asset(scene->pkg, s->texture);
    const uint32_t           frame = n->frame % ((uint32_t)s->hframes * s->vframes);
    const float              w = s->region[2] / (float)s->hframes, h = s->region[3] / (float)s->vframes;
    const float              px = s->region[0] + (float)(frame % s->hframes) * w;
    const float              py = s->region[1] + (float)(frame / s->hframes) * h;
    const float              x0 = s->offset[0] - (s->centered ? w * 0.5f : 0.0f);
    const float              y0 = s->offset[1] - (s->centered ? h * 0.5f : 0.0f);
    float                    u0 = px / (float)t->width, u1 = (px + w) / (float)t->width;
    float                    v0 = py / (float)t->height, v1 = (py + h) / (float)t->height;
    float                    right[3], up[3], uv[8];

    if (s->flip_h) {
        const float swap = u0;
        u0 = u1;
        u1 = swap;
    }
    if (s->flip_v) {
        const float swap = v0;
        v0 = v1;
        v1 = swap;
    }
    uv[0] = u0; uv[1] = v0; uv[2] = u1; uv[3] = v0;
    uv[4] = u1; uv[5] = v1; uv[6] = u0; uv[7] = v1;
    flat_axes(scene, n, s->billboard, s->axis, right, up);
    draw_flat_quad(&n->world.m[9], right, up, s->pixel_size, x0, y0, x0 + w, y0 + h,
                   scene->handles[s->texture], uv, s->color);
}

static void draw_label3d(const RRScene *scene, const Node *n)
{
    const RRPkgLabel3DNode *l = (const RRPkgLabel3DNode *)n->data;
    const RRPkgFont        *font = (const RRPkgFont *)rr_package_asset(scene->pkg, l->font);
    const RRPkgTexture     *atlas = (const RRPkgTexture *)rr_package_asset(scene->pkg, font->texture);
    const float             inv_w = 1.0f / (float)atlas->width, inv_h = 1.0f / (float)atlas->height;
    const char             *p;
    float                   right[3], up[3], lines = 1.0f, baseline;

    if (n->text == NULL || n->text[0] == '\0') {
        return;
    }
    for (p = n->text; *p != '\0'; p++) {
        if (*p == '\n') {
            lines += 1.0f;
        }
    }
    flat_axes(scene, n, l->billboard, 2, right, up);
    /* Laid out with Y down like on the screen, around the origin, and turned over when
     * the quads are made. The vertical alignment says which edge of the text block is at
     * the origin: begin = top. */
    baseline = -lines * font->line_height * 0.5f * (float)l->v_align + font->ascent - l->offset[1];
    for (p = n->text; *p != '\0';) {
        float pen = -line_width(font, p) * 0.5f * (float)l->h_align + l->offset[0];
        while (*p != '\0' && *p != '\n') {
            const RRPkgGlyph *g = find_glyph(font, next_codepoint(&p));
            if (g == NULL) {
                continue;
            }
            if (g->w > 0 && g->h > 0) {
                const float x0 = pen + (float)g->off_x, y0 = baseline + (float)g->off_y;
                const float u0 = (float)g->x * inv_w, v0 = (float)g->y * inv_h;
                const float u1 = (float)(g->x + g->w) * inv_w, v1 = (float)(g->y + g->h) * inv_h;
                const float uv[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };
                draw_flat_quad(&n->world.m[9], right, up, l->pixel_size, x0, -(y0 + (float)g->h),
                               x0 + (float)g->w, -y0, scene->handles[font->texture], uv, l->color);
            }
            pen += g->advance;
        }
        if (*p == '\n') {
            p++;
            baseline += font->line_height;
        }
    }
}

/* The sky is a cube that moves with the camera. Face i has its middle at g_sky[i][0];
 * [1] and [2] point to the right and up on its picture. The exporter fills the six
 * pictures from the same table (SKY_FACES in exporter.gd). */
static const float g_sky[6][3][3] = {
    { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
    { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
    { { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
    { { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
    { { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
    { { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
};

static void draw_sky(const RRScene *scene, const RRPkgEnvironmentNode *env, float z_far)
{
    /* Top left, top right, bottom right, bottom left. */
    static const float sign[4][2] = { { -1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, -1.0f }, { -1.0f, -1.0f } };
    const float       *cam = scene->camera->world.m;
    /* Well inside the far plane, corners included. */
    const float        size = z_far * 0.5f;
    int                face, corner, k;

    for (face = 0; face < 6; face++) {
        const RRPkgTexture *t = (const RRPkgTexture *)rr_package_asset(scene->pkg, env->sky[face]);
        /* Half a pixel in from the edge, so that no face shows the border of the next. */
        const float u0 = 0.5f / (float)t->width, u1 = ((float)t->content_width - 0.5f) / (float)t->width;
        const float v0 = 0.5f / (float)t->height, v1 = ((float)t->content_height - 0.5f) / (float)t->height;
        const float uv[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };
        float       xyz[12];

        for (corner = 0; corner < 4; corner++) {
            for (k = 0; k < 3; k++) {
                xyz[corner * 3 + k] = cam[9 + k] + size * (g_sky[face][0][k] +
                                                         g_sky[face][1][k] * sign[corner][0] +
                                                         g_sky[face][2][k] * sign[corner][1]);
            }
        }
        rr_gfx_draw_sky(scene->handles[env->sky[face]], xyz, uv);
    }
}

/* --- RichTextLabel ------------------------------------------------------------------- */

#define RICH_COLOR_DEPTH 4 /* [color] tags inside each other */

/* What the tags read so far have set. */
typedef struct {
    const RRPkgRichTextNode *d;
    uint8_t                  bold, italic; /* how many [b] and [i] are open */
    uint8_t                  align;
    uint8_t                  depth;        /* open [color] tags */
    uint32_t                 colors[RICH_COLOR_DEPTH];
} Rich;

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0'
         : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* Reads "#rrggbb" or "#rrggbbaa". Returns false for anything else. */
static bool parse_color(const char *text, size_t length, uint32_t *color)
{
    uint32_t value = 0;
    size_t   i;

    if ((length != 7 && length != 9) || text[0] != '#') {
        return false;
    }
    for (i = 1; i < length; i++) {
        const int digit = hex_digit(text[i]);
        if (digit < 0) {
            return false;
        }
        value = (value << 4) | (uint32_t)digit;
    }
    *color = length == 7 ? (value << 8) | 0xFFu : value;
    return true;
}

/* Applies the tag between `name` and `name + length` (without the brackets). Returns
 * false if it is none the runtime knows, and 1 in `bracket` for [lb], 2 for [rb]. */
static bool rich_tag(Rich *st, const char *name, size_t length, int *bracket)
{
    uint32_t color;

    *bracket = 0;
    if (length == 1 && name[0] == 'b') {
        st->bold++;
    } else if (length == 2 && memcmp(name, "/b", 2) == 0) {
        st->bold -= st->bold > 0;
    } else if (length == 1 && name[0] == 'i') {
        st->italic++;
    } else if (length == 2 && memcmp(name, "/i", 2) == 0) {
        st->italic -= st->italic > 0;
    } else if (length == 6 && memcmp(name, "center", 6) == 0) {
        st->align = RR_ALIGN_CENTER;
    } else if (length == 5 && memcmp(name, "right", 5) == 0) {
        st->align = RR_ALIGN_END;
    } else if (length == 4 && memcmp(name, "left", 4) == 0) {
        st->align = RR_ALIGN_BEGIN;
    } else if ((length == 7 && memcmp(name, "/center", 7) == 0) ||
               (length == 6 && memcmp(name, "/right", 6) == 0) ||
               (length == 5 && memcmp(name, "/left", 5) == 0)) {
        st->align = RR_ALIGN_BEGIN;
    } else if (length > 6 && memcmp(name, "color=", 6) == 0 && parse_color(name + 6, length - 6, &color)) {
        if (st->depth < RICH_COLOR_DEPTH) {
            st->colors[st->depth] = color;
        }
        st->depth++;
    } else if (length == 6 && memcmp(name, "/color", 6) == 0) {
        st->depth -= st->depth > 0;
    } else if (length == 2 && memcmp(name, "lb", 2) == 0) {
        *bracket = 1;
    } else if (length == 2 && memcmp(name, "rb", 2) == 0) {
        *bracket = 2;
    } else {
        return false;
    }
    return true;
}

/* The next character of the text, after applying the tags in front of it. A tag the
 * runtime does not know is text, as in Godot. Returns 0 at the end. */
static uint32_t rich_next(Rich *st, const char **text)
{
    while (st->d->bbcode && **text == '[') {
        const char *close = *text + 1;
        int         bracket;
        while (*close != '\0' && *close != ']' && *close != '[' && *close != '\n' && close - *text < 24) {
            close++;
        }
        if (*close != ']' || !rich_tag(st, *text + 1, (size_t)(close - *text - 1), &bracket)) {
            break;
        }
        *text = close + 1;
        if (bracket != 0) {
            return bracket == 1 ? '[' : ']';
        }
    }
    return **text == '\0' ? 0 : next_codepoint(text);
}

static const RRPkgFont *rich_font(const RRScene *scene, const Rich *st, uint32_t *asset)
{
    const uint32_t wanted = st->d->fonts[(st->bold ? 1 : 0) + (st->italic ? 2 : 0)];
    *asset = wanted != RR_NONE32 ? wanted : st->d->fonts[0];
    return (const RRPkgFont *)rr_package_asset(scene->pkg, *asset);
}

/* The same as line_end(), for text with tags. `st` is the state at the start of the line
 * and is not changed; `align` is what the line's first character says. */
static bool rich_line(const RRScene *scene, const Rich *st, const char *text, float max_width,
                      const char **end, const char **next, float *width, uint8_t *align)
{
    Rich        at = *st;
    const char *p = text, *gap = NULL;
    float       pen = 0.0f, gap_width = 0.0f;
    bool        in_gap = false, first = true;
    uint8_t     wrap = st->d->wrap;

    *align = st->align;
    for (;;) {
        const char       *start = p;
        const uint32_t    cp = rich_next(&at, &p);
        uint32_t          asset;
        const RRPkgGlyph *g;
        float             advance;

        if (first) {
            *align = at.align;
            first  = false;
        }
        if (cp == 0 || cp == '\n') {
            *end   = start;
            *next  = p;
            *width = pen;
            return cp == '\n';
        }
        g       = find_glyph(rich_font(scene, &at, &asset), cp);
        advance = g != NULL ? g->advance : 0.0f;
        if (cp == ' ') {
            if (!in_gap) {
                gap       = start;
                gap_width = pen;
                in_gap    = true;
            }
        } else {
            in_gap = false;
            if (wrap != RR_WRAP_OFF && start > text && pen + advance > max_width) {
                if (wrap != RR_WRAP_ARBITRARY && gap != NULL && gap > text) {
                    *end = gap;
                    while (*gap == ' ') {
                        gap++;
                    }
                    *next  = gap;
                    *width = gap_width;
                    return true;
                }
                if (wrap != RR_WRAP_WORD) {
                    *end = *next = start;
                    *width       = pen;
                    return true;
                }
            }
        }
        pen += advance;
    }
}

/* Lays the text of a RichTextLabel out and, with `draw`, draws it with its first line at
 * `top`. Returns the number of lines. */
static float rich_layout(const RRScene *scene, const Node *n, bool draw, float top)
{
    const RRPkgRichTextNode *d = (const RRPkgRichTextNode *)n->data;
    const RRPkgFont         *normal = (const RRPkgFont *)rr_package_asset(scene->pkg, d->fonts[0]);
    Rich                     st = { d, 0, 0, RR_ALIGN_BEGIN, 0, { 0, 0, 0, 0 } };
    const char              *p = n->text, *end, *next;
    float                    lines = 0.0f, width, baseline = top + normal->ascent;
    uint8_t                  align;
    bool                     more = true;

    for (; more; p = next, baseline += normal->line_height, lines += 1.0f) {
        float pen;
        more = rich_line(scene, &st, p, d->rect[2], &end, &next, &width, &align);
        pen  = d->rect[0] + (d->rect[2] - width) * 0.5f * (float)align;
        while (p < end) {
            const uint32_t      cp = rich_next(&st, &p);
            uint32_t            asset;
            const RRPkgFont    *font = rich_font(scene, &st, &asset);
            const RRPkgGlyph   *g = find_glyph(font, cp);
            const RRPkgTexture *atlas;

            if (g == NULL) {
                continue;
            }
            atlas = (const RRPkgTexture *)rr_package_asset(scene->pkg, font->texture);
            if (draw && g->w > 0 && g->h > 0) {
                const float    x0 = floorf(pen + (float)g->off_x + 0.5f);
                const float    y0 = floorf(baseline + (float)g->off_y + 0.5f);
                const float    x1 = x0 + (float)g->w, y1 = y0 + (float)g->h;
                const float    u0 = (float)g->x / (float)atlas->width, v0 = (float)g->y / (float)atlas->height;
                const float    u1 = (float)(g->x + g->w) / (float)atlas->width;
                const float    v1 = (float)(g->y + g->h) / (float)atlas->height;
                const float    xy[8] = { x0, y0, x1, y0, x1, y1, x0, y1 };
                const float    uv[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };
                const uint32_t color = st.depth == 0 ? d->color
                                     : st.colors[(st.depth < RICH_COLOR_DEPTH ? st.depth : RICH_COLOR_DEPTH) - 1];
                ui_quad(scene->handles[font->texture], xy, uv, color);
            }
            pen += g->advance;
        }
        /* The tags between this line and the next (after spaces that were dropped). */
        while (p < next) {
            rich_next(&st, &p);
        }
    }
    return lines;
}

static void draw_rich_text(const RRScene *scene, const Node *n)
{
    const RRPkgRichTextNode *d = (const RRPkgRichTextNode *)n->data;
    const RRPkgFont         *normal = (const RRPkgFont *)rr_package_asset(scene->pkg, d->fonts[0]);
    float                    top = d->rect[1];

    if (n->text == NULL || n->text[0] == '\0') {
        return;
    }
    if (d->v_align != RR_ALIGN_BEGIN) {
        top += (d->rect[3] - rich_layout(scene, n, false, 0.0f) * normal->line_height) * 0.5f * (float)d->v_align;
    }
    rich_layout(scene, n, true, top);
}

/* Panel, PanelContainer and the separators: a filled rectangle with a border inside it. */
static void draw_panel(const Node *n)
{
    const RRPkgPanelNode *p = (const RRPkgPanelNode *)n->data;
    const float          *r = p->rect, *b = p->border;
    const float           inner_h = r[3] - b[RR_SIDE_TOP] - b[RR_SIDE_BOTTOM];
    const float top[4] = { r[0], r[1], r[2], b[RR_SIDE_TOP] };
    const float bottom[4] = { r[0], r[1] + r[3] - b[RR_SIDE_BOTTOM], r[2], b[RR_SIDE_BOTTOM] };
    const float left[4] = { r[0], r[1] + b[RR_SIDE_TOP], b[RR_SIDE_LEFT], inner_h };
    const float right[4] = { r[0] + r[2] - b[RR_SIDE_RIGHT], r[1] + b[RR_SIDE_TOP], b[RR_SIDE_RIGHT], inner_h };

    draw_rect(r, p->color);
    draw_rect(top, p->border_color);
    draw_rect(bottom, p->border_color);
    draw_rect(left, p->border_color);
    draw_rect(right, p->border_color);
}

/* TextureProgressBar: the texture below, the part of the progress texture the value has
 * reached, the texture above; each at its own size. */
static void draw_texture_progress(const RRScene *scene, const Node *n)
{
    const RRPkgTextureProgressNode *d = (const RRPkgTextureProgressNode *)n->data;
    float ratio = d->max > d->min ? (n->value - d->min) / (d->max - d->min) : 0.0f;

    ratio = ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
    if (d->under != RR_NONE32) {
        const RRPkgTexture *t = (const RRPkgTexture *)rr_package_asset(scene->pkg, d->under);
        const float         at[4] = { d->rect[0], d->rect[1], (float)t->content_width, (float)t->content_height };
        draw_image(scene, d->under, at, d->tint_under);
    }
    if (d->progress != RR_NONE32 && ratio > 0.0f) {
        const RRPkgTexture *t = (const RRPkgTexture *)rr_package_asset(scene->pkg, d->progress);
        const float         w = (float)t->content_width, h = (float)t->content_height;
        /* The part that is shown, as fractions of the picture: from, and to. */
        float               from[2] = { 0.0f, 0.0f }, to[2] = { 1.0f, 1.0f }, u, v;

        switch (d->fill_mode) {
        case RR_FILL_END_TO_BEGIN:
            from[0] = 1.0f - ratio;
            break;
        case RR_FILL_TOP_TO_BOTTOM:
            to[1] = ratio;
            break;
        case RR_FILL_BOTTOM_TO_TOP:
            from[1] = 1.0f - ratio;
            break;
        default:
            to[0] = ratio;
            break;
        }
        content_uv(t, &u, &v);
        {
            const float x0 = d->rect[0] + w * from[0], x1 = d->rect[0] + w * to[0];
            const float y0 = d->rect[1] + h * from[1], y1 = d->rect[1] + h * to[1];
            const float xy[8] = { x0, y0, x1, y0, x1, y1, x0, y1 };
            const float uv[8] = { u * from[0], v * from[1], u * to[0], v * from[1],
                                  u * to[0],   v * to[1],   u * from[0], v * to[1] };
            ui_quad(scene->handles[d->progress], xy, uv, d->tint_progress);
        }
    }
    if (d->over != RR_NONE32) {
        const RRPkgTexture *t = (const RRPkgTexture *)rr_package_asset(scene->pkg, d->over);
        const float         at[4] = { d->rect[0], d->rect[1], (float)t->content_width, (float)t->content_height };
        draw_image(scene, d->over, at, d->tint_over);
    }
}

/* ScrollContainer: its scroll bars. What is inside is cut and moved as it is drawn. */
static void draw_scroll(const Node *n)
{
    const RRPkgScrollNode *s = (const RRPkgScrollNode *)n->data;
    const float           *bars[2] = { s->bar_h, s->bar_v };
    int                    a;

    for (a = 0; a < 2; a++) {
        const float *bar = bars[a];
        const float  view = s->rect[2 + a], content = s->content[a] > view ? s->content[a] : view;
        float        grabber[4] = { bar[0], bar[1], bar[2], bar[3] };

        if (bar[2] <= 0.0f || bar[3] <= 0.0f) {
            continue;
        }
        /* The grabber shows which part of the content is in view. */
        grabber[2 + a] = bar[2 + a] * view / content;
        grabber[a]     = bar[a] + bar[2 + a] * n->local.m[9 + a] / content;
        draw_rect(bar, s->bar_color);
        draw_rect(grabber, s->grabber_color);
    }
}

/* One 2D node or control. `c` is the view it is seen through. */
static void draw_2d_node(const RRScene *scene, const Canvas *c, const Node *n)
{
    ui_begin(n);
    switch (n->type) {
    case RR_NODE_SPRITE:
        draw_sprite(scene, c, n);
        break;
    case RR_NODE_TILEMAP:
        draw_tilemap(scene, c, n);
        break;
    case RR_NODE_ANIMATED_SPRITE:
        draw_animated_sprite(scene, c, n);
        break;
    case RR_NODE_LABEL: {
        const RRPkgLabelNode *l = (const RRPkgLabelNode *)n->data;
        draw_text(scene, l->font, n->text, l->rect, l->h_align, l->v_align, l->wrap, l->color);
        break;
    }
    case RR_NODE_BUTTON:
        draw_button(scene, n);
        break;
    case RR_NODE_TEXTURE_RECT:
        draw_texture_rect(scene, n);
        break;
    case RR_NODE_PARTICLES:
        if (((const RRPkgParticlesNode *)n->data)->two_d) {
            rr_particles_draw_2d(scene, n, c->ox, c->oy, c->zx, c->zy);
        }
        break;
    case RR_NODE_COLOR_RECT:
        draw_rect(((const RRPkgColorRectNode *)n->data)->rect,
                  ((const RRPkgColorRectNode *)n->data)->color);
        break;
    case RR_NODE_RICH_TEXT:
        draw_rich_text(scene, n);
        break;
    case RR_NODE_PANEL:
        draw_panel(n);
        break;
    case RR_NODE_TEXTURE_PROGRESS:
        draw_texture_progress(scene, n);
        break;
    case RR_NODE_SCROLL:
        draw_scroll(n);
        break;
    case RR_NODE_SLIDER:
        draw_slider(scene, n);
        break;
    case RR_NODE_PROGRESS_BAR:
        draw_progress_bar(scene, n);
        break;
    case RR_NODE_NINE_PATCH:
        draw_nine_patch(scene, n);
        break;
    default:
        break;
    }
}

/* A node below a Parallax2D that repeats is drawn several times, one repeat size apart:
 * `times` more copies along every axis that has a size, laid out around the node as in
 * Godot. Returns false for a node that is drawn once. */
static bool draw_repeated(const RRScene *scene, const Canvas *c, const Node *n)
{
    float       step[2];
    int         times, rx, ry, nx, ny;
    const Node *source;

    if (n->type != RR_NODE_SPRITE && n->type != RR_NODE_TILEMAP && n->type != RR_NODE_ANIMATED_SPRITE &&
        n->type != RR_NODE_PARTICLES) {
        return false;
    }
    source = rr_canvas_repeat(n, step, &times);
    if (source == NULL) {
        return false;
    }
    nx = step[0] > 0.0f ? times : 0;
    ny = step[1] > 0.0f ? times : 0;
    for (ry = 0; ry <= ny; ry++) {
        for (rx = 0; rx <= nx; rx++) {
            /* The copy is this far away in the space of the Parallax2D node. */
            const float  dx = step[0] * (float)(rx - times / 2), dy = step[1] * (float)(ry - times / 2);
            const float *m = source->world.m;
            Canvas       shifted = *c;
            shifted.ox -= m[0] * dx + m[3] * dy;
            shifted.oy -= m[1] * dx + m[4] * dy;
            draw_2d_node(scene, &shifted, n);
        }
    }
    return true;
}

void rr_scene_draw(const RRScene *scene)
{
    uint32_t i, level;
    Canvas   canvas;

    if (scene->camera != NULL) {
        const RRPkgCameraNode *c = (const RRPkgCameraNode *)scene->camera->data;
        Frustum                frustum;

        rr_gfx_set_camera(&scene->camera->world, c->fov, c->z_near, c->z_far, c->size);
        frustum_setup(&frustum, &scene->camera->world, c);
        if (scene->environment != NULL &&
            ((const RRPkgEnvironmentNode *)scene->environment->data)->sky[0] != RR_NONE32) {
            draw_sky(scene, (const RRPkgEnvironmentNode *)scene->environment->data, c->z_far);
        }
        rr_gfx_set_fog(scene->environment != NULL
                           ? (const RRPkgEnvironmentNode *)scene->environment->data
                           : NULL);
        for (i = 0; i < scene->node_count; i++) {
            const Node *n = &scene->nodes[i];
            if (n->type == RR_NODE_MESH && rr_node_visible_in_tree(n)) {
                const RRPkgMeshNode *d = (const RRPkgMeshNode *)n->data;
                /* A mesh with a skin is drawn as its bones have posed it. */
                const RRPkgMesh     *mesh = d->skin != RR_NONE32 ? rr_skin_mesh(scene, n)
                                                                 : rr_package_mesh(scene->pkg, d->mesh);
                if (d->range_begin > 0.0f || d->range_end > 0.0f) {
                    /* Visibility range: by the distance from the camera to the node. */
                    const float *c = scene->camera->world.m, *w = n->world.m;
                    const float  dx = w[9] - c[9], dy = w[10] - c[10], dz = w[11] - c[11];
                    const float  dist2 = dx * dx + dy * dy + dz * dz;
                    if (dist2 < d->range_begin * d->range_begin ||
                        (d->range_end > 0.0f && dist2 > d->range_end * d->range_end)) {
                        continue;
                    }
                }
                if (mesh_in_view(&frustum, mesh, &n->world)) {
                    rr_gfx_draw_mesh(mesh, &n->world, scene->handles);
                }
            }
        }
        /* Flat nodes are translucent quads like particles, so they come after the meshes. */
        for (i = 0; i < scene->node_count; i++) {
            const Node *n = &scene->nodes[i];
            if (n->type == RR_NODE_SPRITE3D && rr_node_visible_in_tree(n)) {
                draw_sprite3d(scene, n);
            } else if (n->type == RR_NODE_LABEL3D && rr_node_visible_in_tree(n)) {
                draw_label3d(scene, n);
            }
        }
        rr_particles_draw(scene);
    }

    rr_gfx_begin_2d();
    canvas_setup(&canvas, scene);
    /* Level by level (z_index; what is in a CanvasLayer last), in tree order inside one. */
    for (level = 0; level < scene->level_count; level++) {
        for (i = 0; i < scene->node_count; i++) {
            const Node *n = &scene->nodes[i];
            if (n->z != scene->levels[level] || !rr_node_visible_in_tree(n)) {
                continue;
            }
            if (!draw_repeated(scene, &canvas, n)) {
                draw_2d_node(scene, &canvas, n);
            }
        }
    }
}
