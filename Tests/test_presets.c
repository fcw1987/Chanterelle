#include "CHPresets.h"
#include "CHEQ.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    float in[256] = {0}, out[256];
    for (size_t i = 0; i < CH_PRESET_COUNT; i++) {
        const CHPreset *p = &CH_PRESETS[i];
        assert(p->name && p->note && strlen(p->note));
        assert(isfinite(p->bass) && fabsf(p->bass) <= 6);
        assert(isfinite(p->mid) && fabsf(p->mid) <= 6);
        assert(isfinite(p->treble) && fabsf(p->treble) <= 6);
        assert(isfinite(p->trim) && p->trim >= -12 && p->trim <= 0);
        for (size_t j = 0; j < i; j++) assert(strcmp(p->name, CH_PRESETS[j].name));
        CHEQ *eq = cheq_create(48000); assert(eq);
        cheq_set_controls(eq, p->bass, p->mid, p->treble, p->trim, false);
        for (int n = 0; n < 200; n++) cheq_process_stereo(eq, in, 2, in + 1, 2, out, 2, out + 1, 2, 128);
        CHEQMeters normal; cheq_get_meters(eq, &normal);
        assert(isfinite(normal.headroom_db) && normal.headroom_db >= 0 && normal.headroom_db <= 20);
        if (p->bass || p->mid || p->treble) assert(normal.headroom_db >= 0.99f);
        else assert(normal.headroom_db == 0);
        cheq_set_controls(eq, p->bass, p->mid, p->treble, p->trim, true);
        for (int n = 0; n < 200; n++) cheq_process_stereo(eq, in, 2, in + 1, 2, out, 2, out + 1, 2, 128);
        CHEQMeters bypass; cheq_get_meters(eq, &bypass);
        assert(fabsf(normal.headroom_db - bypass.headroom_db) < 0.01f);
        cheq_destroy(eq);
    }
    printf("PASS: %zu presets; finite bounds, unique names, flat/boost headroom and comparison retention\n", CH_PRESET_COUNT);
}
