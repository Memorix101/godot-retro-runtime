/* Translations, after Godot's tr(): the messages of the project's translations, by key
 * and locale. The locale that is set stays across scene changes; before the game sets
 * one it is the language of the machine (rr_plat_locale). */
#include <string.h>

#include "rr_internal.h"
#include "rr_platform.h"

static char g_locale[16]; /* "" until the first use */

const char *rr_get_locale(void)
{
    if (g_locale[0] == '\0') {
        const char *wanted = rr_plat_locale();
        strncpy(g_locale, wanted != NULL && wanted[0] != '\0' ? wanted : "en", sizeof(g_locale) - 1);
    }
    return g_locale;
}

/* The locale of the package that fits the one that is set: the same name, else one of
 * the same language ("de" for "de_AT", and the other way round), else the fallback. */
static uint32_t pick_locale(const RRPackage *pkg)
{
    const RRPkgTranslations *t = pkg->translations;
    const char              *wanted = rr_get_locale();
    size_t                   language = strcspn(wanted, "_");
    uint32_t                 i;

    for (i = 0; i < t->locale_count; i++) {
        if (strcmp(rr_package_string(pkg, t->entries[i]), wanted) == 0) {
            return i;
        }
    }
    for (i = 0; i < t->locale_count; i++) {
        const char *name = rr_package_string(pkg, t->entries[i]);
        if (strcspn(name, "_") == language && strncmp(name, wanted, language) == 0) {
            return i;
        }
    }
    return t->fallback;
}

const char *rr_tr(const RRScene *scene, const char *key)
{
    const RRPackage         *pkg = scene->pkg;
    const RRPkgTranslations *t = pkg->translations;
    const uint32_t          *keys, *messages;
    uint32_t                 lo = 0, hi, locale;

    if (t == NULL || key == NULL) {
        return key;
    }
    keys     = t->entries + t->locale_count;
    messages = keys + t->key_count;
    hi       = t->key_count;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2u;
        if (strcmp(rr_package_string(pkg, keys[mid]), key) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == t->key_count || strcmp(rr_package_string(pkg, keys[lo]), key) != 0) {
        return key; /* not a key: the text itself, as in Godot */
    }
    locale = pick_locale(pkg);
    if (messages[locale * t->key_count + lo] == 0) {
        locale = t->fallback;
    }
    return messages[locale * t->key_count + lo] != 0
               ? rr_package_string(pkg, messages[locale * t->key_count + lo]) : key;
}

void rr_set_locale(RRScene *scene, const char *locale)
{
    memset(g_locale, 0, sizeof(g_locale));
    strncpy(g_locale, locale, sizeof(g_locale) - 1);
    rr_scene_retranslate(scene);
}
