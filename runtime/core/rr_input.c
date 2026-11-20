/* Input actions. The action table comes from the current package (the project's Input
 * Map, baked to this platform's keys and buttons); the platform layer reads one binding
 * at a time. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rr_app.h"
#include "rr_internal.h"
#include "rr_platform.h"

#define MAX_ACTIONS 64

static const RRPackage *g_pkg;
static float            g_strength[MAX_ACTIONS];
static bool             g_down[MAX_ACTIONS];
static bool             g_was_down[MAX_ACTIONS];
static const char      *g_script; /* test input, see rr_input_set_script */
static long             g_frame;

static uint32_t action_count(void)
{
    const uint32_t count = g_pkg != NULL ? g_pkg->header->actions.count : 0;
    return count < MAX_ACTIONS ? count : MAX_ACTIONS;
}

/* The test script, read once into a table (rr_input_set_script). Reading the text again
 * for every action in every frame costs milliseconds on a console for a long script. */
#define MAX_SCRIPT_ENTRIES 1024

typedef struct {
    long        start, held;
    const char *name;   /* points into the script */
    size_t      length;
    int         action; /* index in the current package's action table, -1 for none */
} ScriptEntry;

static ScriptEntry g_entries[MAX_SCRIPT_ENTRIES];
static uint32_t    g_entry_count;

static void parse_script(const char *p)
{
    g_entry_count = 0;
    while (p != NULL && *p != '\0') {
        char       *end;
        const long  start = strtol(p, &end, 10);
        ScriptEntry entry;

        if (*end != ':') {
            fprintf(stderr, "retro: input script: expected 'frame:action' at '%s'\n", p);
            break;
        }
        p            = end + 1;
        entry.start  = start;
        entry.held   = 1;
        entry.name   = p;
        entry.length = strcspn(p, ":,");
        entry.action = -1;
        p += entry.length;
        if (*p == ':') {
            entry.held = strtol(p + 1, &end, 10);
            p          = end;
        }
        if (g_entry_count == MAX_SCRIPT_ENTRIES) {
            fprintf(stderr, "retro: input script has more than %d entries; the rest is ignored\n",
                    MAX_SCRIPT_ENTRIES);
            break;
        }
        g_entries[g_entry_count++] = entry;
        if (*p == ',') {
            p++;
        }
    }
}

/* Finds the actions of the script's entries in the current package. */
static void resolve_script(void)
{
    uint32_t e, i;

    for (e = 0; e < g_entry_count; e++) {
        g_entries[e].action = -1;
        for (i = 0; g_pkg != NULL && i < action_count(); i++) {
            const char *name = rr_package_string(g_pkg, g_pkg->actions[i].name);
            if (strlen(name) == g_entries[e].length &&
                strncmp(name, g_entries[e].name, g_entries[e].length) == 0) {
                g_entries[e].action = (int)i;
                break;
            }
        }
    }
}

static void sample(void)
{
    const uint32_t count = action_count();
    uint32_t       i, k;
    bool           held[MAX_ACTIONS];

    if (g_script != NULL) {
        memset(held, 0, sizeof(held));
        for (k = 0; k < g_entry_count; k++) {
            const ScriptEntry *e = &g_entries[k];
            if (e->action >= 0 && g_frame >= e->start && g_frame < e->start + e->held) {
                held[e->action] = true;
            }
        }
    }

    for (i = 0; i < count; i++) {
        const RRPkgAction *a = &g_pkg->actions[i];
        float              strength = 0.0f;
        if (g_script != NULL) {
            /* A scripted run must not depend on keys someone happens to press. */
            strength = held[i] ? 1.0f : 0.0f;
        } else {
            for (k = 0; k < a->binding_count; k++) {
                const float v = rr_plat_binding_value(&g_pkg->bindings[a->binding_first + k]);
                if (v > strength) {
                    strength = v;
                }
            }
        }
        g_was_down[i] = g_down[i];
        g_down[i]     = strength >= a->deadzone && strength > 0.0f;
        g_strength[i] = g_down[i] ? strength : 0.0f;
    }
}

void rr_input_set_script(const char *script)
{
    g_script = script;
    parse_script(script);
    resolve_script();
}

void rr_input_attach(const RRPackage *pkg)
{
    g_pkg = pkg;
    resolve_script();
    memset(g_strength, 0, sizeof(g_strength));
    memset(g_down, 0, sizeof(g_down));
    memset(g_was_down, 0, sizeof(g_was_down));
    /* Sample once now, so buttons held across a scene change do not count as pressed. */
    sample();
}

static void update_rumble(void);

void rr_input_update(void)
{
    g_frame++;
    sample();
    update_rumble();
}

int rr_action(const RRScene *scene, const char *name)
{
    const RRPackage *pkg = scene->pkg;
    uint32_t         i;
    for (i = 0; i < pkg->header->actions.count && i < MAX_ACTIONS; i++) {
        if (strcmp(rr_package_string(pkg, pkg->actions[i].name), name) == 0) {
            return (int)i;
        }
    }
    fprintf(stderr, "retro: input action '%s' is not in the package\n", name);
    return -1;
}

bool rr_action_down(int action)
{
    return action >= 0 && (uint32_t)action < action_count() && g_down[action];
}

bool rr_action_pressed(int action)
{
    return rr_action_down(action) && !g_was_down[action];
}

float rr_action_strength(int action)
{
    return action >= 0 && (uint32_t)action < action_count() ? g_strength[action] : 0.0f;
}

/* --- rumble --------------------------------------------------------------------------- */

static bool     g_rumble;     /* the motors run */
static bool     g_rumble_timed;
static uint32_t g_rumble_end; /* on the platform's clock, if timed */

void rr_input_vibrate(float weak, float strong, float seconds)
{
    weak   = weak < 0.0f ? 0.0f : (weak > 1.0f ? 1.0f : weak);
    strong = strong < 0.0f ? 0.0f : (strong > 1.0f ? 1.0f : strong);
    rr_plat_vibrate(weak, strong);
    g_rumble       = weak > 0.0f || strong > 0.0f;
    g_rumble_timed = seconds > 0.0f;
    g_rumble_end   = rr_plat_time_us() + (uint32_t)(seconds * 1000000.0f);
}

void rr_input_stop_vibration(void)
{
    if (g_rumble) {
        rr_plat_vibrate(0.0f, 0.0f);
        g_rumble = false;
    }
}

/* Stops a rumble whose time is up. The clock wraps, so the difference is what counts. */
static void update_rumble(void)
{
    if (g_rumble && g_rumble_timed && (int32_t)(rr_plat_time_us() - g_rumble_end) >= 0) {
        rr_input_stop_vibration();
    }
}
