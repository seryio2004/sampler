#include "core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PI 3.14159265358979323846
static void tone(Engine *e, int pad, size_t frames, double hz) {
    Sample sample={calloc(frames,sizeof(float)),frames}; assert(sample.data);
    for (size_t i=0;i<frames;i++) sample.data[i]=(float)(.2*sin(2*PI*hz*i/RATE));
    Sample old=engine_replace(e,pad,sample); free(old.data);
}
static void render(Engine *e, size_t frames) {
    int16_t out[1024];
    while (frames) { size_t n=frames<512?frames:512; engine_render(e,out,n); frames-=n; }
}
static void init(Engine *e) {
    assert(engine_init(e)); e->s.playing=false; e->s.enabled=false; e->s.enabled2=false;
    tone(e,0,16384,1000);
}
static void duration_and_pitch(void) {
    Engine e; init(&e);
    assert(engine_sound_set(&e,0,12,1,1)); engine_pad(&e,0,true);
    render(&e,8191); assert(e.voices[0].sample);
    render(&e,1); assert(!e.voices[0].sample);
    assert(engine_sound_set(&e,0,-12,1,1)); engine_pad(&e,0,true);
    render(&e,32767); assert(e.voices[0].sample);
    render(&e,1); assert(!e.voices[0].sample);
    assert(engine_sound_set(&e,0,0,1,.25f)); engine_pad(&e,0,true);
    render(&e,4095); assert(e.voices[0].sample);
    render(&e,1); assert(!e.voices[0].sample);
    assert(!engine_sound_set(&e,-1,0,1,1) && !engine_sound_set(&e,PADS,0,1,1));
    assert(!engine_sound_set(&e,0,NAN,1,1) && !engine_sound_set(&e,0,25,1,1));
    assert(!engine_sound_set(&e,0,0,INFINITY,1) && !engine_sound_set(&e,0,0,1,0));
    engine_free(&e);
}
static double energy(const int16_t *out, int frames) {
    double value=0;
    for (int i=1024;i<frames;i++) value+=(double)out[2*i]*out[2*i];
    return value;
}
static int crossings(const int16_t *out, int frames) {
    int count=0;
    for (int i=1025;i<frames;i++) if (out[2*(i-1)]<=0 && out[2*i]>0) count++;
    return count;
}
static void audible_controls(void) {
    Engine a,b; init(&a); init(&b);
    int16_t x[8192*2], y[8192*2];
    engine_sound_set(&b,0,12,1,1);
    engine_pad(&a,0,true); engine_pad(&b,0,true);
    engine_render(&a,x,4096); engine_render(&b,y,4096);
    int ca=crossings(x,4096), cb=crossings(y,4096);
    assert(abs(cb-2*ca)<=2); /* +12 st doubles the measured audio frequency. */
    /* Changes to a running voice alter pitch and filter without restarting it. */
    size_t age=b.voices[0].age;
    engine_sound_set(&b,0,-12,.2f,1); render(&b,512);
    assert(b.voices[0].age==age+512 && b.voices[0].rate<1);
    engine_free(&a); engine_free(&b);
    init(&a); init(&b); tone(&a,0,16384,8000); tone(&b,0,16384,8000);
    engine_sound_set(&b,0,0,.3f,1); engine_pad(&a,0,true); engine_pad(&b,0,true);
    engine_render(&a,x,8192); engine_render(&b,y,8192);
    assert(energy(y,8192)<energy(x,8192)*.001); /* LPF audibly attenuates high frequencies. */
    engine_free(&a); engine_free(&b);
}
static void isolation_and_blocks(void) {
    Engine a,b; init(&a); init(&b);
    engine_sound_set(&a,0,24,0,.1f);
    engine_pad(&a,1,true); engine_pad(&b,1,true);
    int16_t x[4096*2], y[4096*2];
    engine_render(&a,x,4096); engine_render(&b,y,4096);
    assert(!memcmp(x,y,sizeof(x))); /* Other sample slots are unaffected. */
    engine_free(&a); engine_free(&b);
    init(&a); init(&b);
    engine_sound_set(&a,0,7.3f,.63f,.82f); engine_sound_set(&b,0,7.3f,.63f,.82f);
    engine_pad(&a,0,true); engine_pad(&b,0,true);
    engine_render(&a,x,4096);
    for (int i=0;i<4096;i+=64) engine_render(&b,y+2*i,64);
    assert(!memcmp(x,y,sizeof(x)) && !a.clipped_frames && !b.clipped_frames);
    engine_free(&a); engine_free(&b);
}
static void real_scope_signals(void) {
    Engine e; init(&e); engine_pad(&e,0,true); render(&e,512);
    bool master=false, voice=false;
    for (int i=0;i<256;i++) master|=e.scope[i]!=0;
    for (int i=0;i<128;i++) voice|=e.track_scope[0][i]!=0;
    assert(master && voice);
    for (int t=1;t<TRACKS;t++) for (int i=0;i<128;i++) assert(e.track_scope[t][i]==0);
    e.s.mode=BASS; engine_pad(&e,3,true); render(&e,512); voice=false;
    for (int i=0;i<128;i++) voice|=e.track_scope[PADS][i]!=0;
    assert(voice);
    memset(e.voices,0,sizeof(e.voices)); render(&e,LOOKAHEAD_FRAMES+8*256+8);
    for (int i=0;i<256;i++) assert(e.scope[i]==0);
    for (int t=0;t<TRACKS;t++) for (int i=0;i<128;i++) assert(e.track_scope[t][i]==0);
    engine_free(&e);
}
int main(void) {
    duration_and_pitch(); real_scope_signals(); audible_controls(); isolation_and_blocks();
    puts("sound: measured tuning, LPF attenuation, length, live changes, isolation and block-independent PCM OK");
}
