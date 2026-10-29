#ifndef SAMPLER_FILES_H
#define SAMPLER_FILES_H
#include "core.h"
#include <limits.h>
#include <pthread.h>
#define MAX_ENTRIES 1024
typedef struct { char name[256]; bool directory; } Entry;
typedef struct {
    char root[PATH_MAX], path[PATH_MAX];
    Entry entries[MAX_ENTRIES];
    int count, selected;
    bool truncated;
} Browser;
typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    bool busy, done, started, preview, success;
    int pad;
    char path[PATH_MAX], error[256];
    Sample result;
    uint8_t waveform[WAVE_COLUMNS];
} Loader;
bool browser_init(Browser *b, const char *root);
bool browser_scan(Browser *b);
bool browser_enter(Browser *b); /* directory selected */
bool browser_parent(Browser *b);
bool browser_file(const Browser *b, char path[PATH_MAX]);
bool sample_decode(const char *path, Sample *out, char error[256]);
bool loader_start(Loader *l, const char *path, int pad);
/* Decode for waveform and explicit audition; caller owns the returned PCM. */
bool loader_preview_start(Loader *l, const char *path);
void loader_join(Loader *l);
bool state_load(const char *path, State *s, char paths[PADS][PATH_MAX]);
bool state_save(const char *path, const State *s, char paths[PADS][PATH_MAX]);
#endif
