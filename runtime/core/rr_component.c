#include "rr_component.h"

#include <string.h>

static RRComponentType g_types[RR_MAX_COMPONENT_TYPES];
static uint32_t        g_type_count;

/* Both add functions count past their limit so rr_component_register can report it. */

void rr_component_add_field(RRComponentType *type, const char *name, uint8_t field_type,
                            size_t offset)
{
    if (type->field_count < RR_MAX_COMPONENT_FIELDS) {
        RRFieldDesc *f = &type->fields[type->field_count];
        f->name   = name;
        f->type   = field_type;
        f->offset = (uint16_t)offset;
    }
    type->field_count++;
}

void rr_component_set_field_resource(RRComponentType *type, const char *resource)
{
    if (type->field_count >= 1 && type->field_count <= RR_MAX_COMPONENT_FIELDS) {
        type->fields[type->field_count - 1].resource = resource;
    }
}

void rr_component_add_signal(RRComponentType *type, const char *method, void (*fn)(void),
                             bool takes_node)
{
    if (type->signal_count < RR_MAX_COMPONENT_SIGNALS) {
        RRSignalDesc *s = &type->signals[type->signal_count];
        s->method     = method;
        s->fn         = fn;
        s->takes_node = takes_node;
    }
    type->signal_count++;
}

bool rr_component_register(const RRComponentDef *def)
{
    RRComponentType *type;

    if (g_type_count >= RR_MAX_COMPONENT_TYPES) {
        rr_set_error("component registry is full (%d types)", RR_MAX_COMPONENT_TYPES);
        return false;
    }
    type = &g_types[g_type_count];
    memset(type, 0, sizeof(*type));
    def->describe(type);

    if (type->field_count > RR_MAX_COMPONENT_FIELDS ||
        type->signal_count > RR_MAX_COMPONENT_SIGNALS) {
        rr_set_error("component '%s' has %u exported fields and %u signal handlers, the limits "
                     "are %d and %d", type->name, (unsigned)type->field_count,
                     (unsigned)type->signal_count, RR_MAX_COMPONENT_FIELDS,
                     RR_MAX_COMPONENT_SIGNALS);
        return false;
    }
    if (rr_component_find(type->name) != NULL) {
        rr_set_error("component '%s' is registered twice", type->name);
        return false;
    }
    g_type_count++;
    return true;
}

const RRComponentType *rr_component_find(const char *name)
{
    uint32_t i;
    for (i = 0; i < g_type_count; i++) {
        if (strcmp(g_types[i].name, name) == 0) {
            return &g_types[i];
        }
    }
    return NULL;
}

const RRComponentType *rr_component_at(uint32_t index)
{
    return index < g_type_count ? &g_types[index] : NULL;
}

void rr_component_write_placeholders(FILE *out)
{
    uint32_t i, k;

    for (i = 0; i < g_type_count; i++) {
        const RRComponentType *type = &g_types[i];

        fprintf(out, "#--- %s\n", type->name);
        fprintf(out, "# Declaration only. The logic lives in the C %s \"%s\" of the runtime.\n",
                type->is_resource ? "resource" : "component", type->name);
        fputs("# Generated from its registration (retro --placeholders). Default values, doc\n"
              "# comments and narrower types (Camera3D instead of Node) may be added by hand.\n", out);
        fprintf(out, "class_name %s\nextends %s\n", type->name, type->is_resource ? "Resource" : "Node");
        if (type->field_count > 0) {
            fputc('\n', out);
        }
        for (k = 0; k < type->field_count; k++) {
            const RRFieldDesc *f = &type->fields[k];
            switch (f->type) {
            case 1: /* RR_FIELD_FLOAT */
                fprintf(out, "@export var %s: float\n", f->name);
                break;
            case 2: /* RR_FIELD_INT */
                fprintf(out, "@export var %s: int\n", f->name);
                break;
            case 3: /* RR_FIELD_BOOL */
                fprintf(out, "@export var %s: bool\n", f->name);
                break;
            case 4: /* RR_FIELD_NODE */
                fprintf(out, "@export var %s: Node\n", f->name);
                break;
            case 5: /* RR_FIELD_SCENE: the name of a scene to change to */
                fprintf(out, "@export_file(\"*.tscn\") var %s: String\n", f->name);
                break;
            case 6: /* RR_FIELD_PACKED_SCENE */
                fprintf(out, "@export var %s: PackedScene\n", f->name);
                break;
            default: /* RR_FIELD_RESOURCE */
                fprintf(out, "@export var %s: %s\n", f->name, f->resource != NULL ? f->resource : "Resource");
                break;
            }
        }
        if (type->signal_count > 0) {
            fputc('\n', out);
        }
        for (k = 0; k < type->signal_count; k++) {
            /* Whatever a signal passes along: the runtime hands on a node or nothing. */
            fprintf(out, "func %s(..._arguments: Array) -> void: pass\n", type->signals[k].method);
        }
    }
}
