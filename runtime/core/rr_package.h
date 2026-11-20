/* Package loader. The format is described in docs/package-format.md; keep both in sync. */
#ifndef RR_PACKAGE_H
#define RR_PACKAGE_H

#include <stdio.h>

#include "rr_types.h"

#define RR_PKG_VERSION 27
#define RR_MAX_Z_LEVELS 16 /* different draw levels (z_index, CanvasLayer) in one scene */
#define RR_MAX_BUSES   16 /* audio buses of a project */
#define RR_NONE16      0xFFFFu
#define RR_NONE32      0xFFFFFFFFu
#define RR_MAX_PARTICLES 512 /* per particle system */

enum {
    RR_NODE_NODE = 0,
    RR_NODE_NODE3D,
    RR_NODE_MESH,
    RR_NODE_CAMERA,
    RR_NODE_AREA,
    RR_NODE_SHAPE,
    RR_NODE_NODE2D,
    RR_NODE_SPRITE,
    RR_NODE_CONTROL,
    RR_NODE_LABEL,
    RR_NODE_BUTTON,
    RR_NODE_TEXTURE_RECT,
    RR_NODE_AUDIO,
    RR_NODE_ANIMATION_PLAYER,
    RR_NODE_PARTICLES,
    RR_NODE_STATIC_BODY,
    RR_NODE_CHARACTER_BODY,
    RR_NODE_ANIMATED_SPRITE,
    RR_NODE_CAMERA2D,
    RR_NODE_TIMER,
    RR_NODE_ENVIRONMENT,
    RR_NODE_TILEMAP,
    RR_NODE_RAYCAST,
    RR_NODE_PATH,
    RR_NODE_PATH_FOLLOW,
    RR_NODE_COLOR_RECT,
    RR_NODE_PROGRESS_BAR,
    RR_NODE_NINE_PATCH,
    RR_NODE_AUDIO_SPATIAL, /* AudioStreamPlayer2D and 3D; same data as RR_NODE_AUDIO */
    RR_NODE_SPRITE3D,
    RR_NODE_LABEL3D,
    RR_NODE_RIGID_BODY,
    RR_NODE_SLIDER, /* HSlider and VSlider */
    RR_NODE_PARALLAX, /* Parallax2D */
    RR_NODE_PANEL,    /* Panel, PanelContainer, HSeparator, VSeparator */
    RR_NODE_TEXTURE_PROGRESS, /* TextureProgressBar */
    RR_NODE_SCROLL,   /* ScrollContainer */
    RR_NODE_RICH_TEXT, /* RichTextLabel */
    RR_NODE_TYPE_COUNT
};
enum {
    RR_NODE_FLAG_VISIBLE = 1,
    RR_NODE_FLAG_CURRENT_CAMERA = 2,
    RR_NODE_FLAG_AUTOLOAD = 128, /* part of an autoload: in every scene, keeps its state */
    /* The rest is set by the runtime only and never stored in a package. */
    RR_NODE_FLAG_DISABLED = 4, /* Button: disabled; Slider: not editable (same bit as ON_FLOOR) */
    RR_NODE_FLAG_ON_FLOOR = 4,
    RR_NODE_FLAG_ON_WALL = 8,
    RR_NODE_FLAG_ON_CEILING = 16,
    RR_NODE_FLAG_FREE = 32,   /* not in the tree: freed, or a pool instance not in use */
    RR_NODE_FLAG_QUEUED = 64  /* rr_queue_free was called, freed at the end of the frame */
};
enum {
    RR_FIELD_FLOAT = 1,
    RR_FIELD_INT,
    RR_FIELD_BOOL,
    RR_FIELD_NODE,
    RR_FIELD_SCENE,
    RR_FIELD_PACKED_SCENE,
    RR_FIELD_RESOURCE
};
enum {
    RR_ASSET_MESH = 1,
    RR_ASSET_TEXTURE,
    RR_ASSET_FONT,
    RR_ASSET_SOUND,
    RR_ASSET_ANIMATION,
    RR_ASSET_SPRITE_FRAMES,
    RR_ASSET_TILEMAP,
    RR_ASSET_CURVE,
    RR_ASSET_MUSIC,
    RR_ASSET_HEIGHT_MAP,
    RR_ASSET_SKIN
};
enum {
    RR_TRACK_POSITION = 1, /* 3D: x y z */
    RR_TRACK_ROTATION,     /* 3D: quaternion */
    RR_TRACK_SCALE,        /* 3D: x y z */
    RR_TRACK_VISIBLE,      /* 0 or 1 */
    RR_TRACK_POSITION_2D,  /* x y */
    RR_TRACK_ROTATION_2D,  /* angle in radians */
    RR_TRACK_SCALE_2D,     /* x y */
    RR_TRACK_FRAME,        /* Sprite2D: the frame */
    RR_TRACK_VALUE,        /* ProgressBar value, PathFollow progress */
    RR_TRACK_METHOD        /* calls a handler; the value is the string offset of its name */
};
enum { RR_INTERP_NEAREST = 0, RR_INTERP_LINEAR = 1 };
enum { RR_BLEND_OPAQUE = 0, RR_BLEND_PUNCH_THROUGH = 1, RR_BLEND_TRANSLUCENT = 2 };
enum { RR_SHAPE_BOX = 1, RR_SHAPE_SPHERE = 2, RR_SHAPE_CAPSULE = 3, RR_SHAPE_HEIGHT_MAP = 4 };
enum { RR_FOG_EXPONENTIAL = 0, RR_FOG_DEPTH = 1 };
enum { RR_BILLBOARD_OFF = 0, RR_BILLBOARD_ON = 1, RR_BILLBOARD_Y = 2 };
enum { RR_AUDIO_PLAIN = 0, RR_AUDIO_2D = 1, RR_AUDIO_3D = 2 };
enum { RR_ATTENUATION_INVERSE = 0, RR_ATTENUATION_INVERSE_SQUARE, RR_ATTENUATION_LOGARITHMIC, RR_ATTENUATION_DISABLED };
enum { RR_PATH_ROTATION_NONE = 0, RR_PATH_ROTATION_Y, RR_PATH_ROTATION_FORWARD, RR_PATH_ROTATION_ORIENTED };
enum { RR_FILL_BEGIN_TO_END = 0, RR_FILL_END_TO_BEGIN, RR_FILL_TOP_TO_BOTTOM, RR_FILL_BOTTOM_TO_TOP };
enum { RR_BIND_KEY = 1, RR_BIND_PAD_BUTTON = 2, RR_BIND_PAD_AXIS = 3 };
enum { RR_TEX_RGBA8888 = 1, RR_TEX_RGB565 = 2, RR_TEX_ARGB1555 = 3, RR_TEX_ARGB4444 = 4 };
enum { RR_FILTER_LINEAR = 0, RR_FILTER_NEAREST = 1 };
enum { RR_SOUND_PCM16 = 1, RR_SOUND_ADPCM = 2 };
enum { RR_SIDE_LEFT = 0, RR_SIDE_TOP, RR_SIDE_RIGHT, RR_SIDE_BOTTOM };
enum { RR_ALIGN_BEGIN = 0, RR_ALIGN_CENTER = 1, RR_ALIGN_END = 2 };
/* Godot's TextServer.AUTOWRAP_*. A word that is wider than the line stays whole with
 * WORD and is broken with WORD_SMART. */
enum { RR_WRAP_OFF = 0, RR_WRAP_ARBITRARY = 1, RR_WRAP_WORD = 2, RR_WRAP_WORD_SMART = 3 };
enum { RR_BUTTON_TEX_NORMAL = 0, RR_BUTTON_TEX_PRESSED, RR_BUTTON_TEX_FOCUSED, RR_BUTTON_TEX_DISABLED };

/* On-disk records. All of them are made of naturally aligned members, so the structs
 * have no hidden padding; the static asserts in rr_package.c guard the sizes. */

typedef struct {
    uint32_t off, count;
} RRPkgTable;

typedef struct {
    char       magic[4];
    uint8_t    platform;
    uint8_t    big_endian;
    uint16_t   version;
    uint32_t   file_size;
    RRPkgTable strings;   /* count = size in bytes */
    RRPkgTable nodes;
    RRPkgTable node_data; /* count = size in bytes */
    RRPkgTable tags;
    RRPkgTable comps;
    RRPkgTable fields;
    RRPkgTable conns;
    RRPkgTable actions;
    RRPkgTable bindings;
    RRPkgTable assets;
    RRPkgTable data;      /* count = size in bytes */
    uint32_t   clear_color; /* 0xRRGGBBAA: what the screen is cleared to */
    RRPkgTable pools;
    RRPkgTable resources;
    RRPkgTable buses;
    RRPkgTable translations; /* count = size in bytes; 0: the project has none */
} RRPkgHeader;

typedef struct {
    uint32_t name;
    uint16_t parent;
    uint8_t  type;
    uint8_t  flags;
    RRMat34  xform;
    uint16_t tag_first, tag_count;
    uint16_t comp_first, comp_count;
    uint32_t data; /* offset into the node data section, RR_NONE32 if the type has none */
    int16_t  z;    /* 2D nodes and controls: z_index, with that of the parents it is relative to */
    uint8_t  canvas_layer; /* 1: inside a CanvasLayer, drawn over everything that is not */
    uint8_t  translate;    /* 1: its text is a key that is looked up in the translations */
} RRPkgNode;

/* Per-type node data. Colours are 0xRRGGBBAA values. */
typedef struct {
    uint32_t mesh;
    /* Visibility range: drawn only while the camera is this far away. 0 = no limit. */
    float    range_begin, range_end;
    /* A mesh that bones deform: asset index of its skin (RRPkgSkin), RR_NONE32 for a mesh
     * that keeps its shape. */
    uint32_t skin;
} RRPkgMeshNode;

typedef struct {
    float fov, z_near, z_far;
    float size; /* an orthogonal camera: the height of its view in world units; 0: perspective */
} RRPkgCameraNode;

typedef struct {
    uint32_t layer, mask;
    uint8_t  monitoring, monitorable, pad[2];
} RRPkgAreaNode;

typedef struct {
    uint32_t kind;    /* RR_SHAPE_* */
    float    size[3]; /* box: half extents; sphere: radius in size[0]; capsule: radius in
                       * size[0], half the length of its line in size[1]. 2D shapes are a
                       * box that is 0.5 thick along Z, or a sphere. */
    uint8_t  disabled;
    uint8_t  one_way; /* 2D, on a static body: solid only for what comes down onto it */
    uint8_t  pad[2];
    uint32_t map;     /* height map: asset id of its heights; RR_NONE32 for the others */
} RRPkgShapeNode;

/* StaticBody, CharacterBody and RigidBody, 2D and 3D. Their shapes are their direct
 * children. */
typedef struct {
    uint32_t layer, mask;
    uint8_t  two_d, pad[3]; /* two_d: a 2D body, where down is +Y */
    float    bounce, friction; /* of the physics material; 0 and 1 without one */
    /* RigidBody only: */
    float    gravity[3];  /* acceleration, with the body's gravity scale */
    float    mass;
    float    linear_damp; /* part of the velocity that is lost per second */
    float    velocity[3]; /* at the start */
} RRPkgBodyNode;

typedef struct {
    uint32_t texture;
    uint32_t color;
    float    offset[2];
    float    region[4]; /* x, y, width, height in pixels of the image: the sprite sheet */
    uint16_t hframes, vframes; /* the sheet is a grid of this many cells, at least 1 x 1 */
    uint16_t frame;            /* the cell shown at the start, counted row by row */
    uint8_t  centered, flip_h, flip_v, pad[3];
} RRPkgSpriteNode;

/* Sprite3D: a picture on a flat quad in the 3D world. `frame` at runtime is the node's. */
typedef struct {
    uint32_t texture;
    uint32_t color;
    float    region[4]; /* the sprite sheet, in pixels of the image */
    float    offset[2]; /* in pixels */
    float    pixel_size; /* size of one pixel in the 3D world */
    uint16_t hframes, vframes, frame;
    uint8_t  centered, flip_h, flip_v;
    uint8_t  billboard; /* RR_BILLBOARD_* */
    uint8_t  axis;      /* the axis the quad faces: 0 X, 1 Y, 2 Z */
    uint8_t  pad;
} RRPkgSprite3DNode;

/* Label3D: text in the 3D world. The text at runtime is the node's. */
typedef struct {
    uint32_t text;
    uint32_t font;
    uint32_t color;
    float    pixel_size;
    float    offset[2]; /* in pixels */
    uint8_t  h_align, v_align; /* RR_ALIGN_*, around the node's origin */
    uint8_t  billboard;        /* RR_BILLBOARD_* */
    uint8_t  pad;
} RRPkgLabel3DNode;

/* AnimatedSprite2D: shows the frames of one animation of a SpriteFrames asset. */
typedef struct {
    uint32_t frames;      /* asset index (RRPkgSpriteFrames) */
    uint32_t color;
    float    offset[2];
    float    speed_scale;
    uint16_t animation;   /* index of the animation shown at the start */
    uint8_t  centered, flip_h, flip_v;
    uint8_t  playing;     /* 1: the start animation plays right away (autoplay) */
    uint8_t  pad[2];
} RRPkgAnimatedSpriteNode;

/* Camera2D: decides which part of the 2D world is on the screen. */
typedef struct {
    float   limit[4];  /* RR_SIDE_*: the view never leaves these world coordinates */
    float   offset[2];
    float   zoom[2];
    uint8_t enabled;
    uint8_t top_left;  /* 1: the camera is the view's top left corner, 0: its centre */
    uint8_t smoothing; /* 1: the view follows the camera with a delay */
    uint8_t limit_smoothed; /* 1: with smoothing, the view slows down before a limit */
    /* Drag margins, RR_SIDE_*: how far from the middle of the view, as a part of half its
     * size, the camera may be before the view moves along. Only if the axis is on. */
    float   drag_margin[4];
    float   drag_offset[2]; /* where in the margins the camera starts, -1..1; used if the axis is off */
    float   smoothing_speed;
    uint8_t drag_h, drag_v;
    uint8_t pad[2];
} RRPkgCamera2DNode;

/* Parallax2D: a 2D node that places itself from where the view is, so that what is below
 * it scrolls slower or faster than the rest, and that can show it several times side by
 * side. The fields are the node's properties of the same names. */
typedef struct {
    float    scroll_scale[2];  /* 1: moves like everything else, 0: stays on the screen */
    float    scroll_offset[2];
    float    repeat_size[2];   /* 0: not repeated along that axis */
    float    autoscroll[2];    /* pixels per second, only with a repeat size */
    float    limit_begin[2], limit_end[2];
    float    screen_offset[2]; /* used in place of the view's position if the camera is ignored */
    uint16_t repeat_times;     /* the children are drawn this many times more */
    uint8_t  follow_viewport, ignore_camera_scroll;
} RRPkgParallaxNode;

/* Timer: counts down and emits `timeout`. */
typedef struct {
    float   wait_time; /* seconds */
    uint8_t one_shot, autostart, pad[2];
} RRPkgTimerNode;

/* WorldEnvironment: the fog of the 3D scene. The background colour of the scene is in
 * the header, and the ambient light went into the vertex colours at export time. */
typedef struct {
    uint32_t fog_color;
    float    fog_density;        /* RR_FOG_EXPONENTIAL: fog = 1 - exp(-density * depth) */
    float    fog_begin, fog_end; /* RR_FOG_DEPTH: none before begin, all from end on */
    uint8_t  fog_enabled, fog_mode, pad[2];
    /* The sky: asset indices of six textures, the faces of a cube around the camera in
     * the order +X, -X, +Y, -Y, +Z, -Z (see rr_render.c), or RR_NONE32 without a sky. */
    uint32_t sky[6];
} RRPkgEnvironmentNode;

/* TileMapLayer: a grid of tiles from one texture, with the boxes that are solid. */
typedef struct {
    uint32_t tiles; /* asset index (RRPkgTileMap) */
    uint32_t color;
    uint32_t layer, mask; /* collision layer and mask of the boxes */
} RRPkgTileMapNode;

/* RayCast3D and RayCast2D: a line from the node to `target` that reports what it hits.
 * ShapeCast3D and ShapeCast2D: a shape that is moved along that line. */
typedef struct {
    float    target[3]; /* in the node's space; Z is 0 for a 2D ray */
    uint32_t mask;
    uint8_t  enabled, collide_areas, collide_bodies, exclude_parent;
    /* ShapeCast3D and ShapeCast2D: the shape that is moved along the line, as in
     * RRPkgShapeNode. 0 for a ray. */
    uint32_t shape;
    float    shape_size[3];
} RRPkgRayCastNode;

/* Path3D and Path2D: a curve its PathFollow children move along. */
typedef struct {
    uint32_t curve; /* asset index (RRPkgCurve) */
} RRPkgPathNode;

/* PathFollow3D and PathFollow2D. The progress at runtime is the node's `value`. */
typedef struct {
    float   progress; /* distance along the curve at the start */
    float   h_offset, v_offset;
    uint8_t rotation; /* RR_PATH_ROTATION_* */
    uint8_t loop, two_d, pad;
} RRPkgPathFollowNode;

/* All control types start with their baked screen rectangle. */
typedef struct {
    float rect[4]; /* x, y, width, height in screen pixels */
} RRPkgControlNode;

typedef struct {
    float    rect[4];
    uint32_t color;
} RRPkgColorRectNode;

/* ProgressBar. The value at runtime is the node's `value`. */
typedef struct {
    float    rect[4];
    float    min, max, value;
    uint32_t bg_color, fill_color;
    uint32_t font, text_color; /* for the percentage */
    uint8_t  fill_mode;        /* RR_FILL_* */
    uint8_t  show_percentage, pad[2];
} RRPkgProgressBarNode;

/* Panel and PanelContainer: the theme's panel as a filled rectangle with a border.
 * HSeparator and VSeparator: their line, as a rectangle without a border. */
typedef struct {
    float    rect[4];
    uint32_t color;        /* the fill; alpha 0 for none */
    uint32_t border_color;
    float    border[4];    /* RR_SIDE_*: width of the border, inside the rectangle */
} RRPkgPanelNode;

/* TextureProgressBar. The textures are drawn at their own size from the top left corner
 * of the rectangle; of the progress texture only the part the value has reached. The
 * value at runtime is the node's `value`. */
typedef struct {
    float    rect[4];
    float    min, max, value;
    uint32_t under, progress, over; /* texture assets, RR_NONE32 for none */
    uint32_t tint_under, tint_progress, tint_over;
    uint8_t  fill_mode; /* RR_FILL_* */
    uint8_t  pad[3];
} RRPkgTextureProgressNode;

/* ScrollContainer: the controls below it are only drawn inside its rectangle, and moved
 * by how far it is scrolled. The baked rectangles of those controls are where they were
 * with the container scrolled to `start`. */
typedef struct {
    float    rect[4];
    float    content[2];  /* size of what is scrolled */
    float    start[2];    /* scroll position in the scene, horizontal and vertical */
    float    bar_h[4], bar_v[4]; /* rectangles of the scroll bars; no width: not shown */
    uint32_t bar_color, grabber_color;
    uint8_t  scroll_h, scroll_v; /* 1: scrolls along that axis */
    uint8_t  follow_focus;       /* 1: scrolls to the control that gets the focus */
    uint8_t  pad;
} RRPkgScrollNode;

/* RichTextLabel: text with BBCode tags that the runtime reads as it draws: [b], [i],
 * [color=#rrggbb], [center], [right], [left], and [lb] and [rb] for the brackets. */
typedef struct {
    float    rect[4];
    uint32_t text;
    uint32_t fonts[4]; /* normal, bold, italic, bold and italic; RR_NONE32: the normal one */
    uint32_t color;    /* of text outside of a [color] tag */
    uint8_t  bbcode;   /* 0: the text is drawn as it is, brackets included */
    uint8_t  wrap;     /* RR_WRAP_* */
    uint8_t  v_align;  /* RR_ALIGN_* */
    uint8_t  pad;
} RRPkgRichTextNode;

/* NinePatchRect: the corners keep their size, the edges and the centre stretch. */
typedef struct {
    float    rect[4];
    uint32_t texture;
    uint32_t color;
    float    region[4]; /* x, y, width, height in pixels of the image */
    float    margin[4]; /* RR_SIDE_*: width of the border patches, in pixels */
    uint8_t  draw_center, pad[3];
} RRPkgNinePatchNode;

typedef struct {
    float    rect[4];
    uint32_t text;
    uint32_t font;
    uint32_t color;
    uint8_t  h_align, v_align;
    uint8_t  wrap; /* RR_WRAP_*: lines are broken at the width of the rectangle */
    uint8_t  pad;
} RRPkgLabelNode;

/* Button, CheckBox, CheckButton and TextureButton. At runtime the node's flag
 * RR_NODE_FLAG_DISABLED and its `frame` (1 while pressed) hold the state. */
typedef struct {
    float    rect[4];
    uint32_t text;
    uint32_t font; /* RR_NONE32 for a button without text (TextureButton) */
    uint32_t text_color, focus_text_color;
    uint32_t bg_color, focus_color;
    uint16_t neighbors[4]; /* RR_SIDE_* -> node index of a button or slider, or RR_NONE16 */
    uint32_t disabled_text_color, pressed_text_color;
    uint32_t disabled_color, pressed_color; /* the background in these states */
    /* A picture left of the text: the icon of a Button, the box of a CheckBox, the switch
     * of a CheckButton. `icon_pressed` is shown while the button is pressed. Texture
     * assets, RR_NONE32 for none. */
    uint32_t icon, icon_pressed;
    /* TextureButton: RR_BUTTON_TEX_* -> texture asset, RR_NONE32 for a state without one.
     * They are drawn over the whole rectangle. */
    uint32_t textures[4];
    float    margin;     /* between the edges of the button and what is inside */
    float    separation; /* between the icon and the text */
    uint8_t  h_align;    /* RR_ALIGN_*: of the text inside the button */
    uint8_t  toggle;     /* accept switches it on and off (`toggled`) */
    uint8_t  disabled, pressed; /* the state at the start */
} RRPkgButtonNode;

/* HSlider and VSlider. The value at runtime is the node's `value`. */
typedef struct {
    float    rect[4];
    float    min, max;
    float    step;  /* what one press of a direction changes the value by */
    float    value;
    float    track; /* thickness of the track in pixels */
    uint32_t track_color, fill_color, focus_fill_color;
    uint32_t grabber, focus_grabber, disabled_grabber; /* texture assets */
    uint16_t neighbors[4]; /* as for buttons */
    uint8_t  vertical;     /* VSlider: the value grows upwards */
    uint8_t  editable, pad[2];
} RRPkgSliderNode;

typedef struct {
    float    rect[4];
    uint32_t texture;
    uint32_t color;
} RRPkgTextureRectNode;

typedef struct {
    uint32_t sound;  /* asset index of a sound, or of music that is streamed from a file */
    float    volume; /* linear, 1.0 = unchanged */
    uint8_t  autoplay, loop;
    uint8_t  mode;   /* RR_AUDIO_*: plain, or placed in the 2D or 3D world */
    uint8_t  model;  /* RR_AUDIO_3D: RR_ATTENUATION_* */
    float    unit_size;    /* RR_AUDIO_3D: the distance at which the volume is unchanged */
    float    max_distance; /* silent from here on; 0 = no limit (3D only) */
    float    max_gain;     /* RR_AUDIO_3D: upper limit of the volume, linear */
    float    attenuation;  /* RR_AUDIO_2D: exponent of the fall-off */
    float    pan_strength; /* 0 = always centred, 1 = full left and right */
    float    pitch;        /* 1.0 = as recorded, 2.0 = twice as fast and an octave up */
    uint16_t bus;          /* index of the audio bus it plays on */
    uint16_t pad;
} RRPkgAudioNode;

typedef struct {
    uint32_t animations; /* asset index of the player's clips (RRPkgAnimations) */
    float    speed_scale; /* 1.0 = as made; the clips play this much faster */
    float    blend_time;  /* seconds a clip takes to take over from the one before it */
} RRPkgAnimationPlayerNode;

/* A particle system. Particles are camera-facing quads that live in world space. */
typedef struct {
    uint32_t texture;       /* asset index, RR_NONE32 for plain coloured quads */
    uint32_t amount;        /* number of particles per lifetime */
    float    lifetime;      /* seconds */
    float    explosiveness; /* 0: spread evenly over the lifetime, 1: all at once */
    float    direction[3];  /* in the node's space, unit length */
    float    spread;        /* half angle of the cone around the direction, radians */
    float    gravity[3];    /* world space */
    float    velocity_min, velocity_max;
    float    size[2];       /* width and height of the quad */
    float    scale_min, scale_max;
    uint32_t color_start, color_end; /* at birth and at death, blended in between */
    float    emission_radius;        /* 0: all start at the node, else inside a sphere */
    uint8_t  one_shot, emitting;
    uint8_t  two_d; /* CPUParticles2D: drawn with the 2D nodes, in screen units */
    uint8_t  pad;
} RRPkgParticlesNode;

typedef struct {
    uint32_t name;
    uint16_t node;
    uint16_t field_count;
    uint32_t field_first;
} RRPkgComponent;

typedef struct {
    uint32_t name;
    uint8_t  type;
    uint8_t  pad[3];
    union {
        uint32_t u;
        int32_t  i;
        float    f;
    } value;
} RRPkgField;

typedef struct {
    uint16_t source;
    uint16_t target;
    uint32_t signal;
    uint32_t method;
} RRPkgConnection;

/* The instances of one exported PackedScene field. They are ordinary nodes at the end of
 * the node table, all children of the root: `instance_count` subtrees of `node_count`
 * nodes each, one after the other. */
typedef struct {
    uint32_t name; /* name of the scene, for messages */
    uint16_t first_node;
    uint16_t node_count;
    uint16_t instance_count;
    uint16_t pad;
} RRPkgPool;

/* An audio bus of the project's bus layout (Audio tab of the editor). Bus 0 is Master.
 * What a player is heard at is its own volume times the volume of its bus and of every
 * bus on the way to Master. */
typedef struct {
    uint32_t name;
    float    volume; /* linear, 1.0 = unchanged */
    uint16_t send;   /* index of the bus it plays into (a lower one), RR_NONE16 for Master */
    uint8_t  mute;
    uint8_t  pad;
} RRPkgBus;

/* The translations of the project (Project Settings > Localization): for every locale
 * the message of every key. All of them are string offsets. */
typedef struct {
    uint32_t locale_count, key_count;
    uint32_t fallback; /* index of the locale that is used where the wanted one has nothing */
    /* locale_count locale names ("en", "de", "pt_BR"), then key_count keys in the order of
     * strcmp, then for every locale key_count messages (0: this locale has none). */
    uint32_t entries[1];
} RRPkgTranslations;

/* A custom Resource a component or another resource refers to: a class name and its
 * exported values, which become a plain C struct. */
typedef struct {
    uint32_t name; /* class name of the resource script */
    uint16_t field_count;
    uint16_t pad;
    uint32_t field_first;
} RRPkgResource;

typedef struct {
    uint32_t name;
    uint16_t binding_first, binding_count;
    float    deadzone;
} RRPkgAction;

typedef struct {
    uint8_t  kind; /* RR_BIND_* */
    int8_t   sign; /* axes only: -1 or 1 */
    uint16_t code; /* platform-specific, chosen by the baker */
} RRPkgBinding;

typedef struct {
    uint32_t name;
    uint8_t  type;
    uint8_t  pad[3];
    uint32_t offset;
    uint32_t size;
} RRPkgAsset;

typedef struct {
    uint32_t texture; /* asset index, RR_NONE32 for vertex colours only */
    uint8_t  blend;
    uint8_t  flags;
    uint16_t pad;
    uint32_t strip_count;
    uint32_t vertex_count;
    uint32_t strips_off;
    uint32_t verts_off;
} RRPkgSurface;

typedef struct {
    uint32_t     surface_count;
    float        center[3]; /* bounding sphere in the mesh's own space, for culling */
    float        radius;
    RRPkgSurface surfaces[1]; /* surface_count entries */
} RRPkgMesh;

typedef struct {
    uint16_t width, height;                 /* stored size */
    uint16_t content_width, content_height; /* the part that holds the image */
    uint8_t  format;                        /* RR_TEX_* */
    uint8_t  filter;                        /* RR_FILTER_* */
    uint8_t  pad[2];
    /* pixel data follows, rows top to bottom */
} RRPkgTexture;

typedef struct {
    uint32_t codepoint;
    uint16_t x, y, w, h; /* place in the atlas texture, in pixels */
    int16_t  off_x, off_y; /* from the pen position on the baseline to the top left corner */
    float    advance;
} RRPkgGlyph;

typedef struct {
    uint32_t   texture; /* asset index of the atlas */
    float      line_height, ascent;
    uint32_t   glyph_count;
    RRPkgGlyph glyphs[1]; /* glyph_count entries, sorted by codepoint */
} RRPkgFont;

typedef struct {
    uint32_t sample_rate;
    uint32_t sample_count;
    uint8_t  format; /* RR_SOUND_* */
    uint8_t  channels;
    uint16_t pad;
    uint32_t data_size; /* bytes of sample data that follow */
} RRPkgSound;

/* Animation asset: all clips of one AnimationPlayer. Offsets are relative to the start
 * of the asset. */
typedef struct {
    float time;
    float value[4]; /* what it means depends on the track, see RR_TRACK_* */
} RRPkgKey;

typedef struct {
    uint16_t node;   /* the node the track moves */
    uint8_t  kind;   /* RR_TRACK_* */
    uint8_t  interp; /* RR_INTERP_* */
    uint32_t key_count; /* at least 1, sorted by time */
    uint32_t keys_off;
} RRPkgTrack;

typedef struct {
    uint32_t name;
    float    length; /* seconds */
    uint8_t  loop;
    uint8_t  pad[3];
    uint32_t track_count;
    uint32_t tracks_off;
} RRPkgClip;

typedef struct {
    uint32_t  clip_count;
    uint32_t  autoplay; /* clip index, RR_NONE32 for none */
    RRPkgClip clips[1]; /* clip_count entries */
} RRPkgAnimations;

/* Sprite frames asset (Godot's SpriteFrames): named animations made of pictures. */
typedef struct {
    uint32_t texture;   /* asset index of a texture stored before this asset */
    float    region[4]; /* x, y, width, height in pixels of the image */
    float    duration;  /* length of the frame in units of 1 / fps */
} RRPkgSpriteFrame;

typedef struct {
    uint32_t name;
    float    fps;
    uint8_t  loop, pad[3];
    uint32_t frame_count; /* at least 1 */
    uint32_t frames_off;  /* relative to the start of the asset */
} RRPkgSpriteAnim;

typedef struct {
    uint32_t        anim_count;
    RRPkgSpriteAnim anims[1]; /* anim_count entries, sorted by name */
} RRPkgSpriteFrames;

/* Tile map asset: the cells of one TileMapLayer, its solid boxes and the animations of
 * its animated tiles. */
enum { RR_TILE_FLIP_H = 1, RR_TILE_FLIP_V = 2, RR_TILE_TRANSPOSE = 4 };

typedef struct {
    int16_t  x, y;  /* cell coordinates */
    uint16_t u, v;  /* top left corner of the tile's picture, in pixels of the image */
    uint8_t  flags; /* RR_TILE_*: the picture is transposed (X and Y swapped) first, then
                     * flipped, as in Godot; a turn to the right is TRANSPOSE | FLIP_H */
    uint8_t  pad;
    uint16_t anim;  /* index of its animation, RR_NONE16 for a tile that stands still */
} RRPkgTileCell;

typedef struct {
    float    x, y, w, h; /* rectangle in the node's space */
    uint32_t one_way;    /* 1: solid only for what comes down onto it */
} RRPkgTileBox;

/* One picture of an animated tile and the time at which it ends, counted from the start
 * of the animation. */
typedef struct {
    uint16_t u, v;
    float    end;
} RRPkgTileFrame;

typedef struct {
    uint32_t frame_count;
    uint32_t frames_off; /* RRPkgTileFrame entries, relative to the start of the asset */
    float    duration;   /* of all frames together, in seconds; then it starts again */
} RRPkgTileAnim;

typedef struct {
    uint32_t texture;      /* asset index of a texture stored before this asset */
    float    tile_size[2]; /* size of a cell and of a tile's picture */
    uint32_t cell_count, box_count;
    uint32_t cells_off, boxes_off; /* relative to the start of the asset */
    uint32_t anim_count, anims_off;
} RRPkgTileMap;

/* Skin asset: how the bones of a skeleton deform one mesh. A bone is a node of the scene.
 * Every vertex of the mesh (the vertices of all its surfaces, in their order) follows up
 * to four bones, by weights that add up to 255. The asset belongs to one mesh node. */
typedef struct {
    uint8_t bone[4];   /* indices into the skin's bones */
    uint8_t weight[4]; /* 0 for a slot that is not used */
} RRPkgInfluence;

typedef struct {
    uint32_t bone_count;
    uint32_t vertex_count;
    uint32_t bones_off;      /* uint16 node index per bone; offsets are relative to the asset */
    uint32_t binds_off;      /* RRMat34 per bone: from the mesh's space at rest into the bone's */
    uint32_t influences_off; /* RRPkgInfluence per vertex */
} RRPkgSkin;

/* Music asset: sound that is not in the package but streamed from a WAV file next to
 * it (16-bit PCM), so that it does not take up memory. */
typedef struct {
    uint32_t file;   /* string offset of the file name, like "music.wav" */
    float    length; /* seconds */
} RRPkgMusic;

/* Height map asset: the ground of a HeightMapShape3D. The points are one unit apart in
 * the shape's own space and centred on its origin, as in Godot: point (x, z) is at
 * (x - (width - 1) / 2, heights[z * width + x], z - (depth - 1) / 2). Every cell is two
 * triangles, split along the diagonal from point (x + 1, z) to point (x, z + 1). */
typedef struct {
    uint16_t width, depth; /* points along X and Z, at least 2 each */
    float    min, max;     /* the lowest and the highest height */
    float    heights[1];   /* width * depth entries, row by row along X */
} RRPkgHeightMap;

/* Curve asset: the curve of a Path, sampled at equal distances. */
typedef struct {
    float pos[3];
    float up[3]; /* 3D only: the up direction at this point, unit length */
} RRPkgCurvePoint;

typedef struct {
    uint32_t        point_count; /* at least 2 */
    float           spacing;     /* distance between two points along the curve */
    float           length;      /* (point_count - 1) * spacing */
    RRPkgCurvePoint points[1];   /* point_count entries */
} RRPkgCurve;

/* Vertex layouts. Both are 24 bytes; they differ in how the colour is packed. */
typedef struct {
    float   pos[3];
    float   uv[2];
    uint8_t rgba[4];
} RRVertexPC;

typedef struct {
    float    pos[3];
    float    uv[2];
    uint32_t argb;
} RRVertexDC;

/* A validated view into a package that was read into memory. It allocates nothing and
 * points into the caller's block, which must stay alive and must be 4-byte aligned. */
typedef struct {
    const uint8_t         *base;
    const RRPkgHeader     *header;
    const char            *strings;
    const RRPkgNode       *nodes;
    const uint8_t         *node_data;
    const uint32_t        *tags;
    const RRPkgComponent  *comps;
    const RRPkgField      *fields;
    const RRPkgConnection *conns;
    const RRPkgAction     *actions;
    const RRPkgBinding    *bindings;
    const RRPkgAsset      *assets;
    const RRPkgPool       *pools;
    const RRPkgResource   *resources;
    const RRPkgBus        *buses;
    const RRPkgTranslations *translations; /* NULL if the project has none */
} RRPackage;

/* Checks the whole package. `platform` is the platform the caller can use, or
 * RR_PLATFORM_ANY. Returns false and sets the error message if the package is not usable. */
bool rr_package_open(RRPackage *pkg, const void *data, uint32_t size, uint8_t platform);

static inline const char *rr_package_string(const RRPackage *pkg, uint32_t offset)
{
    return pkg->strings + offset;
}

/* Type-specific data of a node; only valid for node types that have data. */
static inline const void *rr_package_node_data(const RRPackage *pkg, const RRPkgNode *node)
{
    return pkg->node_data + node->data;
}

static inline const void *rr_package_asset(const RRPackage *pkg, uint32_t asset)
{
    return pkg->base + pkg->assets[asset].offset;
}

static inline const RRPkgMesh *rr_package_mesh(const RRPackage *pkg, uint32_t asset)
{
    return (const RRPkgMesh *)rr_package_asset(pkg, asset);
}

static inline const uint16_t *rr_surface_strips(const RRPkgMesh *mesh, const RRPkgSurface *s)
{
    return (const uint16_t *)((const uint8_t *)mesh + s->strips_off);
}

static inline const void *rr_surface_verts(const RRPkgMesh *mesh, const RRPkgSurface *s)
{
    return (const uint8_t *)mesh + s->verts_off;
}

/* The level a 2D node or control is drawn at: lower levels first, and inside a level in
 * tree order. It is the node's z_index, and everything inside a CanvasLayer comes after
 * everything outside of one. */
static inline int rr_package_node_level(const RRPkgNode *node)
{
    return (int)node->z + (node->canvas_layer ? 16384 : 0);
}

const char *rr_platform_name(uint8_t platform);

/* Writes the package content as text: node tree, components, connections, assets. */
void rr_package_dump(const RRPackage *pkg, FILE *out);

#endif
