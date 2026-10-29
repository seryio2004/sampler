#include "core.h"
#include "effects.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PI 3.14159265358979323846
static Effects *fresh(void) {
    Effects *f=calloc(1,sizeof(*f)); assert(f);
    f->punch=f->requested=-1; f->fade=1; return f;
}
static double tone(int fx, float cutoff, float resonance, double hz) {
    State s; state_init(&s); s.bpm=120; s.effect=fx; s.cutoff=cutoff; s.resonance=resonance;
    Effects *f=fresh(); effects_prepare(f,&s); double power=0;
    for (int i=0;i<RATE/2;i++) {
        float x=.1f*sinf((float)(2*PI*hz*i/RATE));
        float y=effects_frame(f,x,&s,-1); assert(isfinite(y));
        if (i>RATE/4) power+=y*y;
    }
    free(f); return power;
}
static void master_controls(void) {
    Engine e; assert(engine_init(&e)); e.s.controller=true; e.s.mode=FX; e.s.effect=3;
    engine_pad(&e,2,true); assert(e.s.effect==3 && engine_punch(&e)==-1);
    assert(e.voices[0].sample==&e.drums[2]);
    engine_pad(&e,6,true); assert(e.voices[1].sample==&e.drums[6]);
    engine_pad(&e,2,false); engine_fn(&e,true);
    engine_pad(&e,3,true); assert(engine_punch(&e)==0 && e.s.effect==3);
    engine_pad(&e,7,true); assert(engine_punch(&e)==4);
    engine_pad(&e,7,false); assert(engine_punch(&e)==0);
    engine_fn(&e,false); assert(engine_punch(&e)==-1 && e.s.held_count==0);
    engine_pad(&e,3,false); assert(e.s.effect==3);
    engine_fn(&e,true); engine_pad(&e,1,true); engine_nav(&e,1,true);
    assert(engine_punch(&e)==-1);
    e.s.controller=false; e.s.mode=FX; e.s.effect=FX_COUNT-1;
    engine_nav(&e,2,false); assert(e.s.effect==FX_COUNT-1 && e.s.fx_pending[0]==0);
    engine_nav(&e,1,false); assert(e.s.effect==0 && !e.s.enabled && e.s.fx_pending[0]==-1);
    engine_nav(&e,2,true); assert(e.s.fx_parameter==1);
    float before=e.s.cutoff; engine_nav(&e,0,false); assert(e.s.cutoff<before);
    engine_nav(&e,2,true); assert(e.s.fx_parameter==2);
    before=e.s.resonance; engine_nav(&e,2,false); assert(e.s.resonance>before);
    engine_nav(&e,2,true); assert(e.s.fx_parameter==3);
    engine_nav(&e,2,false); assert(e.s.fx_slot==1);
    engine_nav(&e,2,true); assert(e.s.fx_parameter==0);
    engine_free(&e);
}
static void upper_punches(void) {
    Engine e; assert(engine_init(&e)); e.s.controller=true; e.s.mode=FX;
    engine_fn(&e,true);
    for (int p=0;p<3;p++) { engine_pad(&e,p,true); assert(engine_punch(&e)==p+6); }
    assert(!e.voices[0].sample && e.s.held_count==3);
    engine_pad(&e,2,false); assert(engine_punch(&e)==7);
    engine_pad(&e,1,false); assert(engine_punch(&e)==6);
    engine_fn(&e,false); assert(engine_punch(&e)==-1 && !e.s.held_count);
    engine_free(&e);
    State s; state_init(&s); s.bpm=120; Effects *f=fresh(); effects_prepare(f,&s);
    /* Dub echo starts dry, repeats at a quarter note, and decays by feedback. */
    float first=0, second=0;
    unsigned beat=RATE/2;
    for (unsigned i=0;i<=2*beat;i++) {
        float out=effects_frame(f,i==0?.2f:0,&s,8); assert(isfinite(out));
        if (i==beat) first=out;
        if (i==2*beat) second=out;
        if (i>0 && i<beat) assert(fabsf(out)<1e-7f);
    }
    assert(fabsf(first-.13f)<1e-5f && fabsf(second-.0845f)<1e-5f);
    float out=0; for (int i=0;i<RATE/5;i++) out=effects_frame(f,.2f,&s,-1);
    assert(fabsf(out-.2f)<1e-6f && f->punch==-1);
    out=effects_frame(f,0,&s,8); assert(fabsf(out)<1e-6f); free(f);
    for (int bpm=50;bpm<=220;bpm+=170) for (int p=6;p<8;p++) {
        s.bpm=bpm; f=fresh(); effects_prepare(f,&s);
        beat=(unsigned)lround(RATE*60./bpm);
        unsigned count=p==7?beat-beat%4:beat, grain=count/4;
        /* Four labelled segments make the slicer's order observable. */
        for (unsigned i=0;i<HISTORY_FRAMES;i++) {
            unsigned at=i>=HISTORY_FRAMES-count?i-(HISTORY_FRAMES-count):0;
            float x=.1f*(1+at/grain);
            effects_frame(f,x,&s,-1);
        }
        float observed[4]={0};
        for (unsigned i=0;i<count;i++) {
            out=effects_frame(f,0,&s,p); assert(isfinite(out));
            for (unsigned j=0;j<4;j++) if (i==j*grain+grain/2) observed[j]=out;
        }
        assert(f->frozen_count==count);
        const int order[]={0,2,1,3};
        for (unsigned j=0;j<4;j++) assert(fabsf(observed[j]-.1f*(1+(p==7?order[j]:(int)j)))<.002f);
        free(f);
    }

}
static void repeat_mix(void) {
    State s; state_init(&s); s.bpm=120; s.effect=0;
    for (int punch=0;punch<8;punch++) {
        Effects *f=fresh(); effects_prepare(f,&s);
        // Prime history with a mix of two tones; then remove every input voice.
        for (int i=0;i<RATE;i++) {
            float x=.1f*sinf((float)(2*PI*100*i/RATE))+.1f*sinf((float)(2*PI*173*i/RATE));
            effects_frame(f,x,&s,-1);
        }
        double energy=0;
        for (int i=0;i<RATE/4;i++) {
            float y=effects_frame(f,0,&s,punch); assert(isfinite(y)); energy+=y*y;
        }
        assert(energy>1); // Playback of the captured full mix, without retriggering pads.
        float tail=0;
        for (int i=0;i<RATE/5;i++) tail=effects_frame(f,0,&s,-1);
        assert(fabsf(tail)<1e-6f && f->punch==-1);
        free(f);
    }
}
static void stability(void) {
    State s; state_init(&s); s.bpm=120;
    for (int fx=0;fx<FX_COUNT;fx++) for (int a=0;a<=1;a++) for (int b=0;b<=1;b++) {
        Effects *f=fresh(); s.effect=fx; s.cutoff=a; s.resonance=b; effects_prepare(f,&s);
        for (int i=0;i<RATE;i++) {
            float x=.1f*sinf((float)(2*PI*700*i/RATE));
            float y=effects_frame(f,x,&s,-1); assert(isfinite(y) && fabsf(y)<10);
        }
        free(f);
    }
    assert(tone(7,.5f,1,300)<tone(7,.5f,0,300)*.5);
    assert(tone(8,.5f,1,300)<tone(8,.5f,0,300)*.6);
    assert(tone(6,.5f,1,300)>1); // Full-wet stutter must keep capturing the dry mix.
    assert(tone(1,.1f,.2f,4000)<tone(1,.9f,.2f,4000)*.01);
    assert(tone(2,.9f,.2f,100)<tone(2,.1f,.2f,100)*.01);
    assert(tone(1,.5f,1,600)>tone(1,.5f,0,600)*2);
    // Knobs change audio already running: no new voice or trigger required.
    Effects *f=fresh(); s.effect=1; s.cutoff=.1f; s.resonance=.2f; effects_prepare(f,&s);
    double quiet=0,loud=0;
    for (int i=0;i<RATE;i++) {
        if (i==RATE/2) { s.cutoff=.9f; effects_prepare(f,&s); }
        float y=effects_frame(f,.1f*sinf((float)(2*PI*4000*i/RATE)),&s,-1);
        if (i>RATE/4 && i<RATE/2) quiet+=y*y;
        if (i>3*RATE/4) loud+=y*y;
    }
    assert(loud>100*quiet); free(f);
}
static void block_independence(void) {
    Engine a,b; assert(engine_init(&a) && engine_init(&b));
    a.s.controller=b.s.controller=true; a.s.effect=b.s.effect=5;
    a.s.enabled2=b.s.enabled2=true; a.s.effect2=b.s.effect2=9;
    size_t frames=RATE/2;
    int16_t *x=malloc(frames*2*sizeof(*x)), *y=malloc(frames*2*sizeof(*y)); assert(x && y);
    engine_render(&a,x,frames);
    for (size_t pos=0;pos<frames;) {
        size_t n=frames-pos<127?frames-pos:127;
        engine_render(&b,y+2*pos,n); pos+=n;
    }
    assert(!memcmp(x,y,frames*2*sizeof(*x)));
    a.s.mode=b.s.mode=FX; engine_fn(&a,true); engine_fn(&b,true);
    engine_pad(&a,0,true); engine_pad(&b,0,true);
    engine_render(&a,x,frames);
    for (size_t pos=0;pos<frames;) {
        size_t n=frames-pos<127?frames-pos:127;
        engine_render(&b,y+2*pos,n); pos+=n;
    }
    assert(!memcmp(x,y,frames*2*sizeof(*x)));
    engine_free(&a); engine_free(&b); free(x); free(y);
}
static void dual_effects(void) {
    Engine a,b; assert(engine_init(&a) && engine_init(&b));
    a.s.effect=b.s.effect=4; a.s.cutoff=b.s.cutoff=.7f;
    b.s.enabled2=true; b.s.effect2=7; b.s.resonance2=1;
    int16_t *x=malloc(RATE*sizeof(int16_t)), *y=malloc(RATE*sizeof(int16_t)); assert(x && y);
    engine_render(&a,x,RATE/2); engine_render(&b,y,RATE/2);
    assert(memcmp(x,y,RATE*sizeof(int16_t)) && !a.clipped_frames && !b.clipped_frames);
    free(x); free(y); engine_free(&a); engine_free(&b);
    for (int i=0;i<FX_COUNT;i++) for (int j=0;j<FX_COUNT;j++) {
        State s; state_init(&s); s.bpm=120; s.effect=i; s.effect2=j; s.enabled2=true;
        s.cutoff=s.cutoff2=.6f; s.resonance=s.resonance2=1;
        State second=state_effect(&s,1);
        Effects *f=fresh(), *g=fresh(); effects_prepare(f,&s); effects_prepare(g,&second);
        for (int frame=0;frame<RATE/5;frame++) {
            float sample=.1f*sinf(frame*.1f);
            float out=effects_frame(g,effects_frame(f,sample,&s,-1),&second,-1);
            assert(isfinite(out) && fabsf(out)<256);
        }
        free(f); free(g);
    }
}
static void dry_audition(void) {
    Engine a,b; assert(engine_init(&a) && engine_init(&b));
    a.s.playing=b.s.playing=false;
    b.s.effect=1; b.s.cutoff=.01f; b.s.resonance=1;
    for (int which=0;which<2;which++) {
        Engine *e=which?&b:&a;
        Sample pcm={calloc(RATE,sizeof(float)),RATE}; assert(pcm.data);
        for (int i=0;i<RATE;i++) pcm.data[i]=.2f*sinf(i*.13f);
        Sample old=engine_audition_replace(e,pcm); assert(!old.data);
        engine_audition(e,true);
    }
    int16_t x[1024], y[1024];
    engine_render(&a,x,512); engine_render(&b,y,512); assert(!memcmp(x,y,sizeof(x)));
    engine_audition(&a,false); engine_audition(&b,false);
    engine_render(&a,x,512); engine_render(&b,y,512); assert(!memcmp(x,y,sizeof(x)));
    engine_render(&a,x,512); engine_render(&b,y,512);
    for (int i=0;i<1024;i++) assert(x[i]==0 && y[i]==0);
    engine_free(&a); engine_free(&b);
}
static void effect_confirmation(void) {
    for (int slot=0;slot<FX_SLOTS;slot++) {
        Engine a,b; assert(engine_init(&a) && engine_init(&b));
        a.s.playing=b.s.playing=false; a.s.mode=b.s.mode=DRUM;
        engine_pad(&a,0,true); engine_pad(&b,0,true);
        a.s.mode=b.s.mode=FX; a.s.controller=b.s.controller=true;
        a.s.effect=b.s.effect=5; a.s.effect2=b.s.effect2=9;
        a.s.enabled2=b.s.enabled2=true; a.s.fx_slot=b.s.fx_slot=slot;
        int16_t x[8192],y[8192];
        engine_render(&a,x,4096); engine_render(&b,y,4096); assert(!memcmp(x,y,sizeof(x)));
        engine_fx_select(&a,slot?8:7);
        assert(a.s.effect==b.s.effect && a.s.effect2==b.s.effect2);
        engine_render(&a,x,4096); engine_render(&b,y,4096); assert(!memcmp(x,y,sizeof(x)));
        engine_nav(&a,1,false);
        assert(a.s.fx_pending[slot]==-1);
        engine_render(&a,x,4096); engine_render(&b,y,4096); assert(memcmp(x,y,sizeof(x)));
        engine_fx_select(&a,0);
        assert(slot?a.s.enabled2:a.s.enabled);
        engine_nav(&a,1,false); assert(!(slot?a.s.enabled2:a.s.enabled));
        engine_fx_select(&a,3); assert(!(slot?a.s.enabled2:a.s.enabled));
        engine_fx_select(&a,0); assert(a.s.fx_pending[slot]==-1);
        engine_free(&a); engine_free(&b);
    }
}
int main(void) {
    master_controls(); upper_punches(); effect_confirmation(); dry_audition(); dual_effects(); repeat_mix(); stability(); block_independence();
    puts("effects: FN/pads, mezcla global sin voces, nueve momentaneos, cutoff/resonancia en vivo y estabilidad OK");
}
