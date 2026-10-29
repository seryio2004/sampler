#include "core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void render(Engine *e, size_t frames) {
    int16_t buffer[512*2];
    while (frames) { size_t n=frames<512?frames:512; engine_render(e,buffer,n); frames-=n; }
}
static void waveform(void) {
    uint8_t heights[WAVE_COLUMNS];
    sample_waveform(NULL,heights);
    for (int i=0;i<WAVE_COLUMNS;i++) assert(heights[i]==0);
    float pcm[32]={0}; Sample sample={pcm,32};
    for (int i=0;i<16;i++) pcm[2*i]=(float)(i%9)/16;
    sample_waveform(&sample,heights);
    for (int i=0;i<16;i++) assert(heights[i]==i%9);
    /* Preserve negative peaks too; invalid PCM never contaminates the view. */
    pcm[0]=-.5f; pcm[1]=NAN; pcm[2]=INFINITY; pcm[3]=-.125f;
    sample_waveform(&sample,heights); assert(heights[0]==8 && heights[1]==2);
    memset(pcm,0,sizeof(pcm)); sample_waveform(&sample,heights);
    for (int i=0;i<WAVE_COLUMNS;i++) assert(heights[i]==0);
    sample=(Sample){pcm,1}; pcm[0]=1; sample_waveform(&sample,heights);
    for (int i=0;i<15;i++) assert(heights[i]==0);
    assert(heights[15]==8);
}
static void visual_timing(void) {
    Engine e; assert(engine_init(&e));
    render(&e,RATE);
    assert(engine_visual_at(&e,0).playhead==-1);
    assert(engine_visual_at(&e,LOOKAHEAD_FRAMES).voices==0);
    /* Render-ahead must not expose the future head at the loudspeaker. */
    assert(e.s.playhead>0);
    assert(engine_visual_at(&e,RATE/5+LOOKAHEAD_FRAMES).playhead==-1);
    for (int step=0;step<6;step++) {
        uint64_t source=RATE/5+(uint64_t)step*RATE/8+VISUAL_INTERVAL;
        assert(engine_visual_at(&e,source+LOOKAHEAD_FRAMES+1).playhead==step);
    }
    render(&e,2*RATE);
    assert(engine_visual_at(&e,RATE/4).playhead==-1); /* Expired history is never reused. */
    assert(!engine_visual_at(&e,e.rendered_frames+LOOKAHEAD_FRAMES+100).voices);
    engine_free(&e);

    for (int slot=0;slot<12;slot++) {
        assert(engine_init(&e)); e.s.playing=false; e.s.mode=slot<6?DRUM:BASS;
        Sample *sample=slot<6?&e.drums[slot]:&e.bass[e.s.octave-1][slot-6];
        free(sample->data); *sample=(Sample){calloc(256,sizeof(float)),256}; assert(sample->data);
        for (int i=0;i<256;i++) sample->data[i]=.25f;
        render(&e,64); engine_pad(&e,slot<6?slot:slot%6+3,true); render(&e,256);
        assert(engine_visual_at(&e,64+LOOKAHEAD_FRAMES).voices==0);
        assert(engine_visual_at(&e,192+LOOKAHEAD_FRAMES).voices==(1u<<(slot<6?slot:slot-6+PADS)));
        /* A 6 ms sound still produces a visible pulse, then turns off. */
        render(&e,RATE/50);
        assert(engine_visual_at(&e,e.rendered_frames+LOOKAHEAD_FRAMES).voices==(1u<<(slot<6?slot:slot-6+PADS)));
        render(&e,RATE/10);
        assert(!engine_visual_at(&e,e.rendered_frames+LOOKAHEAD_FRAMES).voices);
        engine_free(&e);
    }
    assert(engine_init(&e)); e.s.playing=false;
    Sample silence={calloc(256,sizeof(float)),256}; assert(silence.data);
    Sample old=engine_replace(&e,0,silence); free(old.data);
    engine_pad(&e,0,true); render(&e,512);
    assert(!engine_visual_at(&e,512+LOOKAHEAD_FRAMES).voices); /* No flash for silent WAV. */
    engine_free(&e);
}
static void controls(void) {
    Engine e; assert(engine_init(&e));
    assert(e.s.bpm==120 && e.s.playhead==-1);
    engine_nav(&e,1,false); assert(!e.s.pattern[0][0]);
    engine_nav(&e,0,false); assert(e.s.cursor==15);
    engine_nav(&e,0,true); assert(e.s.drum==8);
    engine_nav(&e,1,true); assert(e.s.mode==FX);
    engine_pad(&e,3,true); engine_pad(&e,7,true);
    assert(engine_punch(&e)==4);
    engine_pad(&e,7,false); assert(engine_punch(&e)==0);
    engine_pad(&e,3,false); assert(engine_punch(&e)==-1);
    engine_nav(&e,1,true); assert(e.s.mode==BPM);
    for (int i=0;i<100;i++) engine_nav(&e,0,false);
    assert(e.s.bpm==50);
    for (int i=0;i<100;i++) engine_nav(&e,2,false);
    assert(e.s.bpm==220);
    engine_nav(&e,1,false); assert(!e.s.playing);
    int before=e.s.playhead; render(&e,RATE); assert(e.s.playhead==before);
    engine_nav(&e,1,true); assert(e.s.mode==BASS);
    for (int i=0;i<10;i++) engine_nav(&e,2,true);
    assert(e.s.octave==4);
    engine_pad(&e,6,true); engine_nav(&e,1,false); assert(e.s.melody[15]==3);
    engine_nav(&e,1,false); assert(e.s.melody[15]==-1);
    engine_nav(&e,1,true); engine_nav(&e,1,true); assert(e.s.mode==FILES);
    engine_pad(&e,2,true); assert(e.s.drum==2);
    for (int i=0;i<4;i++) engine_nav(&e,1,true);
    assert(e.s.mode==SEQ);
    engine_free(&e);
}
static void clock_and_blocks(void) {
    Engine a,b; assert(engine_init(&a)); assert(engine_init(&b));
    a.s.bpm=b.s.bpm=137;
    size_t n=RATE*10; int16_t *x=malloc(n*2*sizeof(*x)), *y=malloc(n*2*sizeof(*y));
    assert(x && y); engine_render(&a,x,n);
    size_t pos=0;
    while (pos<n) { size_t chunk=n-pos<127?n-pos:127; engine_render(&b,y+2*pos,chunk); pos+=chunk; }
    assert(!memcmp(x,y,n*2*sizeof(*x)));
    double first=.2*RATE, period=RATE*60./137/4;
    int steps=(int)ceil((n-first)/period);
    assert(a.s.playhead==(steps-1)%16);
    for (int i=0;i<VISUAL_HISTORY;i++) {
        assert(a.visual[i].frame==b.visual[i].frame);
        assert(a.visual[i].playhead==b.visual[i].playhead && a.visual[i].voices==b.visual[i].voices);
    }
    assert(fabs(a.step_left-(first+steps*period-n))<1e-5);
    for (size_t i=0;i<n;i++) assert(x[2*i]==x[2*i+1]);
    free(x); free(y); engine_free(&a); engine_free(&b);
}
static void fx_and_replace(void) {
    Engine e; assert(engine_init(&e)); e.s.playing=false;
    for (int fx=0;fx<7;fx++) for (int punch=0;punch<6;punch++) {
        e.s.mode=SEQ; e.s.effect=fx; e.s.enabled=true;
        engine_pad(&e,1,true);
        e.s.mode=FX; engine_pad(&e,punch+3,true);
        render(&e,RATE/2); engine_pad(&e,punch+3,false);
    }
    for (int i=0;i<100;i++) { e.s.mode=SEQ; engine_pad(&e,0,true); }
    Sample replacement={calloc(128,sizeof(float)),128}; assert(replacement.data);
    Sample old=engine_replace(&e,0,replacement); free(old.data);
    for (int i=0;i<VOICES;i++) assert(!e.voices[i].sample || e.voices[i].slot!=0);
    render(&e,RATE); engine_free(&e);
}
static void digital_noise(void) {
    Engine e; assert(engine_init(&e)); e.s.playing=false;
    int16_t out[256*2];
    engine_render(&e,out,256);
    for (size_t i=0;i<512;i++) assert(out[i]==0);
    assert(e.output_peak==0 && !e.clipped_frames);
    Sample sine={calloc(RATE,sizeof(float)),RATE}; assert(sine.data);
    for (size_t i=0;i<sine.length;i++) sine.data[i]=.25f*sinf((float)(2*3.141592653589793*1000*i/RATE));
    Sample old=engine_replace(&e,0,sine); free(old.data);
    engine_pad(&e,0,true);
    size_t total=RATE+LOOKAHEAD_FRAMES;
    for (size_t first=0;first<total;) {
        size_t n=total-first<256?total-first:256;
        engine_render(&e,out,n);
        for (size_t i=0;i<n;i++) {
            long k=(long)(first+i)-LOOKAHEAD_FRAMES; float value=0;
            if (k>=0 && k<RATE) {
                float envelope=1;
                if (k<VOICE_ATTACK) envelope*=(float)k/VOICE_ATTACK;
                if (RATE-1-k<VOICE_RELEASE) envelope*=(float)(RATE-1-k)/VOICE_RELEASE;
                value=sine.data[k]*envelope*.76f*.75f;
            }
            int expected=(int)(value*32767);
            assert(abs(out[2*i]-expected)<=1 && out[2*i]==out[2*i+1]);
        }
        first+=n;
    }
    engine_render(&e,out,256);
    for (size_t i=0;i<512;i++) assert(out[i]==0); /* No background after a voice ends. */
    assert(e.output_peak<.143f && !e.clipped_frames);
    memset(e.voices,0,sizeof(e.voices));
    for (size_t i=0;i<sine.length;i++) sine.data[i]=.9f;
    for (int i=0;i<VOICES;i++) engine_pad(&e,0,true);
    render(&e,RATE/4);
    assert(e.output_peak>1 && e.overloaded_frames>0 && e.limited_frames>0);
    assert(e.clipped_frames==0); /* Smooth look-ahead handles real overload. */
    render(&e,RATE*3); engine_render(&e,out,256);
    for (size_t i=0;i<512;i++) assert(out[i]==0);
    assert(e.detector_peak==0 && e.limiter_gain==1);
    engine_free(&e);
}
static void replacement_click(void) {
    Engine e; assert(engine_init(&e)); e.s.playing=false;
    Sample constant={calloc(RATE,sizeof(float)),RATE}; assert(constant.data);
    for (int i=0;i<RATE;i++) constant.data[i]=.5f;
    Sample old=engine_replace(&e,0,constant); free(old.data);
    engine_pad(&e,0,true); render(&e,1000);
    int16_t out[2]; engine_render(&e,out,1); int last=out[0], largest=0;
    Sample silence={calloc(RATE,sizeof(float)),RATE}; assert(silence.data);
    old=engine_replace(&e,0,silence); free(old.data);
    for (int i=0;i<LOOKAHEAD_FRAMES*3;i++) {
        engine_render(&e,out,1);
        int delta=abs(out[0]-last); if (delta>largest) largest=delta; last=out[0];
    }
    assert(largest<100 && last==0 && e.clipped_frames==0);
    engine_free(&e);
}
static void volume_click(void) {
    Engine e; assert(engine_init(&e)); e.s.playing=false;
    Sample constant={calloc(RATE,sizeof(float)),RATE}; assert(constant.data);
    for (int i=0;i<RATE;i++) constant.data[i]=.5f;
    Sample old=engine_replace(&e,0,constant); free(old.data);
    engine_pad(&e,0,true); render(&e,1000);
    int16_t out[2]; engine_render(&e,out,1); int last=out[0], largest=0;
    e.s.volume=.1f;
    for (int i=0;i<2000;i++) {
        engine_render(&e,out,1); int delta=abs(out[0]-last);
        if (delta>largest) largest=delta;
        last=out[0];
    }
    assert(largest<100 && last<1300 && last>1200);
    engine_free(&e);
}
int main(void) {
    waveform(); controls(); clock_and_blocks(); fx_and_replace(); digital_noise(); replacement_click(); volume_click(); visual_timing();
    puts("core: reloj, efectos, audio limpio y visualizacion de 12 voces con retardo de salida OK"); return 0;
}
