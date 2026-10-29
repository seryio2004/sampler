#ifndef SAMPLER_EFFECTS_H
#define SAMPLER_EFFECTS_H
#include "core.h"
#define HISTORY_FRAMES (RATE*2)
#define PUNCH_DELAY_FRAMES (RATE*2)
/* Allocated once at startup; no file access/allocation in the audio loop. */
typedef struct Effects {
    float history[HISTORY_FRAMES], frozen[HISTORY_FRAMES], echo[RATE], stutter[RATE/5], dry[RATE/5];
    unsigned write, available, frozen_count, loop_age, echo_write, crush_age, stutter_count, stutter_age, dry_write, dry_available;
    float punch_echo[PUNCH_DELAY_FRAMES];
    unsigned punch_echo_write, punch_echo_age, punch_delay;
    int fx, punch, requested;
    float a, b, target_a, target_b, g, target_g, k, target_k;
    double phase;
    float mod_delay[RATE/20];
    unsigned mod_write;
    float ic1, ic2, crush, last, switch_from, fade, punch_wet;
    double position;
} Effects;
void effects_prepare(Effects *f, const State *s);
float effects_frame(Effects *f, float input, const State *s, int punch);
#endif
