/* Component registry.
 *
 * A component is a C struct plus callbacks, registered under the class name of its
 * placeholder script in the Godot project. One registration block describes the name, the
 * exported fields and the handlers:
 *
 *     typedef struct {
 *         float max_speed;   // exported
 *         Node *camera;      // exported node reference
 *         float speed;       // internal state, starts at zero
 *     } PlayerCar;
 *
 *     static void player_car_update(Node *self, PlayerCar *p, float dt);
 *     static void player_car_on_hit(Node *self, PlayerCar *p);
 *
 *     COMPONENT_BEGIN(PlayerCar, "PlayerCar")
 *         EXPORT_FLOAT(max_speed)
 *         EXPORT_NODE(camera)
 *         ON_UPDATE(player_car_update)
 *         ON_SIGNAL("_on_hit", player_car_on_hit)
 *     COMPONENT_END()
 *
 * The block defines `PlayerCar_component`. Other files see it through
 * COMPONENT_DECLARE(PlayerCar) and register it with rr_component_register().
 *
 * Field types in C: EXPORT_FLOAT float, EXPORT_INT int32_t, EXPORT_BOOL bool,
 * EXPORT_NODE Node *, EXPORT_SCENE const char * (the name of a scene, from an
 * `@export_file("*.tscn")` string in the placeholder), EXPORT_PACKED_SCENE
 * const RRPackedScene * (a scene to make instances of with rr_instantiate, from an
 * `@export var x: PackedScene`), EXPORT_RESOURCE(field, T) const T * (a custom Resource,
 * see below). A wrong member type is a compile
 * error.
 *
 * A custom Resource of the Godot project (a script with `class_name` that extends
 * Resource and exports values) is a plain struct in C, described the same way:
 *
 *     typedef struct { float top_speed; int32_t gears; } CarStats;
 *
 *     RESOURCE_BEGIN(CarStats)
 *         EXPORT_FLOAT(top_speed)
 *         EXPORT_INT(gears)
 *     RESOURCE_END()
 *
 * The struct is named like the class. It is registered like a component, and a
 * component or another resource refers to it with EXPORT_RESOURCE(stats, CarStats).
 * The values are read-only at runtime: nodes that use the same resource share one struct.
 *
 * Signal handlers are found by the method name used in the signal dock of the editor.
 * ON_SIGNAL handlers take no argument. ON_SIGNAL_NODE handlers also get the node the
 * signal passes along, for example the other area of `area_entered`.
 */
#ifndef RR_COMPONENT_H
#define RR_COMPONENT_H

#include <stdio.h>

#include "rr_types.h"

#define RR_MAX_COMPONENT_TYPES   64
#define RR_MAX_COMPONENT_FIELDS  32
#define RR_MAX_COMPONENT_SIGNALS 8

typedef struct Node Node;
typedef struct RRPackedScene RRPackedScene;

typedef void (*RRReadyFn)(Node *self, void *data);
typedef void (*RRUpdateFn)(Node *self, void *data, float dt);
typedef void (*RRSignalFn)(Node *self, void *data);
typedef void (*RRSignalNodeFn)(Node *self, void *data, Node *arg);

typedef struct {
    const char *name;
    uint8_t     type; /* RR_FIELD_* */
    uint16_t    offset;
    const char *resource; /* RR_FIELD_RESOURCE: the class name it must have */
} RRFieldDesc;

typedef struct {
    const char *method;
    void      (*fn)(void); /* RRSignalFn or RRSignalNodeFn */
    bool        takes_node;
} RRSignalDesc;

typedef struct RRComponentType {
    const char  *name;
    uint32_t     size;
    RRFieldDesc  fields[RR_MAX_COMPONENT_FIELDS];
    uint32_t     field_count;
    RRSignalDesc signals[RR_MAX_COMPONENT_SIGNALS];
    uint32_t     signal_count;
    RRReadyFn    on_ready;
    RRUpdateFn   on_update;
    bool         is_resource; /* described with RESOURCE_BEGIN: a custom Resource, not a node's script */
} RRComponentType;

typedef struct {
    void (*describe)(RRComponentType *type);
} RRComponentDef;

/* Registers a component type. Returns false (with an error message) if the registry is
 * full, the name is already taken or the description is invalid. */
bool rr_component_register(const RRComponentDef *def);

const RRComponentType *rr_component_find(const char *name);
/* The registered types in the order they were registered; NULL past the last one. */
const RRComponentType *rr_component_at(uint32_t index);

/* Writes a placeholder script for every registered type: what the Godot project needs so
 * that the component can be attached to a node and its fields show in the inspector. The
 * scripts follow each other, each after a line "#--- <class name>". */
void rr_component_write_placeholders(FILE *out);

/* Used by the registration macros. */
void rr_component_add_field(RRComponentType *type, const char *name, uint8_t field_type,
                            size_t offset);
/* Names the resource class of the field that was added last. */
void rr_component_set_field_resource(RRComponentType *type, const char *resource);
void rr_component_add_signal(RRComponentType *type, const char *method, void (*fn)(void),
                             bool takes_node);

#define COMPONENT_DECLARE(T) extern const RRComponentDef T##_component;

#define COMPONENT_BEGIN(T, NAME)                                 \
    static void T##_describe_(RRComponentType *ct_);             \
    const RRComponentDef T##_component = { T##_describe_ };      \
    static void T##_describe_(RRComponentType *ct_)              \
    {                                                            \
        typedef T Self_;                                         \
        ct_->name = (NAME);                                      \
        ct_->size = (uint32_t)sizeof(Self_);

#define RR_EXPORT_(field, ctype, ftype)                                              \
    _Static_assert(_Generic(((Self_ *)0)->field, ctype: 1, default: 0),              \
                   "exported field '" #field "' must have type " #ctype);            \
    rr_component_add_field(ct_, #field, (ftype), offsetof(Self_, field));

#define EXPORT_FLOAT(field) RR_EXPORT_(field, float, 1)
#define EXPORT_INT(field)   RR_EXPORT_(field, int32_t, 2)
#define EXPORT_BOOL(field)  RR_EXPORT_(field, bool, 3)
#define EXPORT_NODE(field)  RR_EXPORT_(field, Node *, 4)
#define EXPORT_SCENE(field) RR_EXPORT_(field, const char *, 5)
#define EXPORT_PACKED_SCENE(field) RR_EXPORT_(field, const RRPackedScene *, 6)
#define EXPORT_RESOURCE(field, T)                                                    \
    _Static_assert(_Generic(((Self_ *)0)->field, const T *: 1, default: 0),          \
                   "exported field '" #field "' must have type const " #T " *");     \
    rr_component_add_field(ct_, #field, 7, offsetof(Self_, field));                  \
    rr_component_set_field_resource(ct_, #T);

/* Handlers take the component struct as their second parameter. The local typed pointer
 * makes the compiler check the signature before it is stored in its generic form. */
#define ON_READY(fn)                                              \
    {                                                             \
        void (*chk_)(Node *, Self_ *) = (fn);                     \
        ct_->on_ready = (RRReadyFn)(void (*)(void))chk_;          \
    }

#define ON_UPDATE(fn)                                             \
    {                                                             \
        void (*chk_)(Node *, Self_ *, float) = (fn);              \
        ct_->on_update = (RRUpdateFn)(void (*)(void))chk_;        \
    }

#define ON_SIGNAL(method, fn)                                                 \
    {                                                                         \
        void (*chk_)(Node *, Self_ *) = (fn);                                 \
        rr_component_add_signal(ct_, (method), (void (*)(void))chk_, false);  \
    }

#define ON_SIGNAL_NODE(method, fn)                                            \
    {                                                                         \
        void (*chk_)(Node *, Self_ *, Node *) = (fn);                         \
        rr_component_add_signal(ct_, (method), (void (*)(void))chk_, true);   \
    }

#define COMPONENT_END() }

#define RESOURCE_BEGIN(T) COMPONENT_BEGIN(T, #T) ct_->is_resource = true;
#define RESOURCE_END()    COMPONENT_END()

#endif
