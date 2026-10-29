#ifndef SAMPLER_CORE_H
#define SAMPLER_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RATE 44100
#define PADS 9
#define BASE_PADS 6
#define NOTES 6
#define SCENES 9
#define SCENE_NAME 13
#define SONG_BLOCKS 64
#define SONG_REPEATS 64
#define FX_COUNT 11
#define FX_SLOTS 2
#define STEPS 16
#define VOICES 32
#define MAX_SECONDS 30
#define WAVE_COLUMNS 16
#define LOOKAHEAD_FRAMES (RATE * 3 / 1000)
#define VOICE_ATTACK (RATE / 2000)
#define VOICE_RELEASE (RATE / 500)
#define VISUAL_INTERVAL 64
#define VISUAL_HISTORY 1024 /* ~1.49 s, beyond the maximum requested ALSA buffer. */
enum { SEQ, FX, BPM, BASS, DRUM, FILES, SONG, LIVE, BANK, MODES };
enum { PATTERN_PLAY, SONG_PLAY, LIVE_PLAY };
typedef struct { bool pattern[PADS][STEPS]; int melody[STEPS]; char name[SCENE_NAME]; } Scene;
typedef struct { uint8_t scene, repeats; } SongBlock;
typedef struct { float *data; size_t length; } Sample;
typedef struct {
    Scene scenes[SCENES];
    SongBlock song[SONG_BLOCKS];
    int edit_scene, active_scene, queued_scene, live_selected, transport;
    int song_count, song_cursor, song_choice, song_times, song_position, song_left;
    bool song_loop, song_pending, stop_pending;
    int effect2, fx_slot;
    int fx_pending[FX_SLOTS]; /* -1: no unconfirmed selection; never serialized. */
    bool enabled2;
    float cutoff2, resonance2;
    int mode, bpm, cursor, playhead, drum, bass, octave, effect, fx_parameter;
    bool playing, enabled, backing, pattern[PADS][STEPS];
    int melody[STEPS], held[PADS], held_count;
    float volume, cutoff, resonance;
    bool controller, fn; /* Transient controller capabilities and modifier. */
} State;
typedef struct {
    const Sample *sample;
    size_t age, duration;
    int slot, punch;
    float gain, last;
} Voice;
typedef struct { float value; unsigned left; } Tail;
typedef struct { uint64_t frame; uint16_t voices; int8_t playhead, scene, song_position; uint8_t song_left; } AudioVisual;
typedef struct {
    State s;
    Scene play_scene; /* Audio snapshot, replaced only at a bar boundary. */
    struct Effects *effects;
    Sample drums[PADS], bass[4][NOTES], audition;
    Voice audition_voice;
    Tail audition_tail;
    Voice voices[VOICES];
    Tail tails[VOICES];
    unsigned tail_index;
    unsigned steal, last_chord, repeat_chord;
    double step_left, repeat_left;
    uint64_t rendered_frames, clipped_frames;
    float output_peak; /* Before the existing output clamp, including master gain. */
    uint64_t overloaded_frames, limited_frames;
    float master_gain, volume_slew, limiter_gain, detector_peak;
    float attack_coefficient, release_coefficient;
    unsigned detector_hold, delay_index;
    float delay[LOOKAHEAD_FRAMES];
    AudioVisual visual[VISUAL_HISTORY];
    uint64_t light_until[PADS+NOTES];
} Engine;
void state_init(State *s);
Scene state_scene(const State *s, int scene);
void state_scene_store(State *s);
void state_scene_select(State *s, int scene);
void engine_scene_select(Engine *e, int scene);
void engine_scene_copy(Engine *e, int source, int target);
void engine_scene_clear(Engine *e, int scene);
bool engine_scene_name(Engine *e, int scene, const char *name);
void engine_scene_launch(Engine *e, int scene);
void engine_live_toggle(Engine *e);
void engine_song_toggle(Engine *e);
void state_song_cursor(State *s, int cursor);
bool engine_song_commit(Engine *e, bool insert);
bool engine_song_delete(Engine *e);
int pad_punch(int pad);
bool engine_init(Engine *e);
void engine_free(Engine *e);
int engine_punch(const Engine *e);
void engine_fn(Engine *e, bool down);
void engine_pad(Engine *e, int pad, bool down);
void engine_audition(Engine *e, bool play);
Sample engine_audition_replace(Engine *e, Sample replacement);
State state_effect(const State *s, int slot);
int state_fx_choice(const State *s, int slot);
void engine_fx_select(Engine *e, int effect);
void engine_nav(Engine *e, int key, bool held); /* 0 left, 1 OK, 2 right */
void engine_render(Engine *e, int16_t *out, size_t frames);
/* Already-played output frames, including the limiter delay; caller holds engine lock. */
AudioVisual engine_visual_at(const Engine *e, uint64_t played_frames);
Sample engine_replace(Engine *e, int pad, Sample replacement);
bool synth_drum(Sample *s, int pad);
void sample_prepare(Sample *s); /* Sanitize, attenuate only if over-range, fade edges. */
/* Relative peak envelope: 16 chronological bins, heights 0 (silence)..8. */
void sample_waveform(const Sample *s, uint8_t heights[WAVE_COLUMNS]);
#endif
