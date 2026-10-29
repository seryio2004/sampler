#ifndef SAMPLER_HARDWARE_H
#define SAMPLER_HARDWARE_H
#include "core.h"
#include <pthread.h>
#include <stdatomic.h>
typedef struct {
    int buttons, lcd, leds;
    pthread_t thread;
    bool started;
    atomic_bool running;
    atomic_uint_least32_t lights; /* low 16 dim, high 16 bright */
    atomic_bool failed;
    char previous[2][17];
    bool lcd_valid[2];
} Hardware;
bool hardware_init(Hardware *h, const char *chip);
bool hardware_init_config(Hardware *h, const char *chip, bool lcd_enabled, bool leds_enabled);
bool hardware_init_leds(Hardware *h, const char *chip); /* Only the eight matrix pins. */
void hardware_close(Hardware *h);
bool hardware_buttons(Hardware *h, unsigned *pressed);
void hardware_lcd(Hardware *h, const char *top, const char *bottom);
void hardware_lcd_waveform(Hardware *h, const char *top, const uint8_t heights[WAVE_COLUMNS]);
void hardware_lcd_glyphs(Hardware *h);
void hardware_lights(Hardware *h, const State *s, uint16_t voices, double now);
#endif
