#ifndef SAMPLER_ARDUINO_H
#define SAMPLER_ARDUINO_H
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int value, previous, context, count, output, hint;
    bool armed, received, configured, have_previous;
    double seen_at;
} PotControl;
void pot_reset(PotControl *p);
void pot_release(PotControl *p);
void pot_receive(PotControl *p, int value, double now);
/* Absolute index 0..count-1, or -1 while waiting/unchanged. hint: -1 left, +1 right. */
int pot_target(PotControl *p, int context, int current, int count, double now);
/* Dedicated parameter knob: raw 10-bit value, pickup once and 3-count deadband. */
/* Discrete selectors take over on the first deliberate movement, without pickup. */
int pot_select(PotControl *p, int context, int current, int count, double now);
int pot_parameter(PotControl *p, int current, double now);

typedef struct {
    int fd, last_error;
    const char *path;
    double retry_at;
    char line[64];
    size_t used;
    bool overflow;
    PotControl pot, cutoff, resonance;
    bool controls, ok, fn, cancelled;
    unsigned button_down, button_up; /* Bit 0 D2, bit 1 D3; accumulated edges. */
    double controls_at;
} Arduino;
void arduino_init(Arduino *a, const char *path);
bool arduino_poll(Arduino *a, double now); /* Nonblocking; true for a new valid reading. */
void arduino_close(Arduino *a);
#endif
