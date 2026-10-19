#ifndef CH_PRESETS_H
#define CH_PRESETS_H
#include <stddef.h>
typedef struct { const char *name; float bass, mid, treble, trim; const char *note; } CHPreset;
/* Moderate subjective starting points. No model-specific measured correction. */
static const CHPreset CH_PRESETS[] = {
    {"Neutral", 0, 0, 0, 0, "Flat tone and zero trim; processing can still be on"},
    {"Everyday", -1, 0.5f, -0.5f, 0, "Gentle balance for mixed listening"},
    {"Podcast / Speech", -2.5f, 1.5f, 0.5f, 0, "Less low-end weight, a little more voice presence"},
    {"Gaming", -1, 1, 1, -1, "Modest detail and presence; no positional enhancement"},
    {"Music", 1, -0.5f, 0.5f, -1, "A mild warmth and detail curve"},
    {"Clearer voices", -3, 1, 1, 0, "Less bass weight with a gentle presence lift"},
    {"Less boom", -4, 0, 1, 0, "Reduce bass weight without a large high-frequency boost"},
    {"Softer treble", 0, -1, -3, 0, "Reduce brightness and edge"}
};
#define CH_PRESET_COUNT (sizeof(CH_PRESETS) / sizeof(CH_PRESETS[0]))
#endif
