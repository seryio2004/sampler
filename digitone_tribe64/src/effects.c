#include "effects.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846
static float bound(float x) { return isfinite(x)?fminf(1,fmaxf(0,x)):0; }
void effects_prepare(Effects *f, const State *s) {
    f->target_a=bound(s->cutoff); f->target_b=bound(s->resonance);
    float hz=20*powf(900,f->target_a);
    f->target_g=tanf((float)PI*hz/RATE);
    f->target_k=1/(.5f+7.5f*f->target_b);
}
static float frozen_at(const Effects *f, double position) {
    if (!f->frozen_count) return 0;
    position=fmod(position,f->frozen_count);
    if (position<0) position+=f->frozen_count;
    unsigned i=(unsigned)position, next=(i+1)%f->frozen_count;
    return f->frozen[i]+(f->frozen[next]-f->frozen[i])*(float)(position-i);
}
static void capture(Effects *f, const State *s, int punch) {
    unsigned beat=(unsigned)lround(RATE*60./s->bpm);
    if (punch==8) {
        f->punch_delay=beat<PUNCH_DELAY_FRAMES?beat:PUNCH_DELAY_FRAMES-1;
        if (!f->punch_delay) f->punch_delay=1;
        f->punch_echo_write=f->punch_echo_age=0;
        f->frozen_count=0; f->position=0; f->loop_age=0; return;
    }
    unsigned count=punch<3?(unsigned)(RATE*60./s->bpm/(2<<punch)):
        (punch==6 || punch==7)?beat:punch==4?RATE:RATE/2;
    if (count>HISTORY_FRAMES) count=HISTORY_FRAMES;
    if (count>f->available) count=f->available;
    if (punch==7) count-=count%4;
    f->frozen_count=count;
    unsigned start=(f->write+HISTORY_FRAMES-count)%HISTORY_FRAMES;
    for (unsigned i=0;i<count;i++) f->frozen[i]=f->history[(start+i)%HISTORY_FRAMES];
    f->position=0; f->loop_age=0;
}
float effects_frame(Effects *f, float input, const State *s, int punch) {
    /* Smooth controls over ~5 ms, including coefficients of the resonant filter. */
    const float smooth=.0045f;
    f->a+=smooth*(f->target_a-f->a); f->b+=smooth*(f->target_b-f->b);
    f->g+=smooth*(f->target_g-f->g); f->k+=smooth*(f->target_k-f->k);
    int fx=s->enabled?s->effect:0;
    if (fx!=f->fx) {
        f->switch_from=f->last; f->fade=0; f->fx=fx;
        f->ic1=f->ic2=0; f->stutter_count=f->stutter_age=0;
    }
    float x=input;
    switch (fx) {
    case 1: case 2: {
        /* Topology-preserving state variable filter, stable at high resonance. */
        float a1=1/(1+f->g*(f->g+f->k));
        float v1=a1*(f->ic1+f->g*(input-f->ic2));
        float v2=f->ic2+f->g*v1;
        f->ic1=2*v1-f->ic1; f->ic2=2*v2-f->ic2;
        x=fx==1?v2:input-f->k*v1-v2;
        break;
    }
    case 3: {
        unsigned hold=1+(unsigned)(63*f->a);
        float levels=powf(2,3+13*(1-f->b));
        if (++f->crush_age>=hold) { f->crush_age=0; f->crush=roundf(input*levels)/levels; }
        x=f->crush; break;
    }
    case 4: {
        float driven=input*(1+20*f->a);
        float wet=driven/(1+fabsf(driven));
        x=input+(wet-input)*f->b; break;
    }
    case 5: {
        unsigned delay=(unsigned)(RATE*(.04f+.56f*f->a));
        float delayed=f->echo[(f->echo_write+RATE-delay)%RATE];
        f->echo[f->echo_write]=input+delayed*(.75f*f->b);
        x=input+.5f*delayed; break;
    }
    case 6: {
        unsigned grain=(unsigned)(RATE*(.015f+.185f*f->a));
        if (grain>RATE/5) grain=RATE/5;
        /* Refresh the captured grain after four repetitions. */
        if (!f->stutter_count || f->stutter_age>=4*f->stutter_count) {
            if (grain>f->dry_available) grain=f->dry_available;
            f->stutter_count=grain; f->stutter_age=0;
            for (unsigned i=0;i<grain;i++) f->stutter[i]=f->dry[(f->dry_write+RATE/5-grain+i)%(RATE/5)];
        }
        if (f->stutter_count) {
            unsigned offset=f->stutter_age++%f->stutter_count;
            float edge=fminf(offset,f->stutter_count-offset)/(RATE*.002f);
            float wet=f->stutter[offset]*fminf(1,edge);
            x=input+(wet-input)*f->b;
        }
        break;
    }
    case 7: /* Tremolo: rate / depth. */
        f->phase+=(.5+15.5*f->a)/RATE;
        x=input*(1-f->b*(.5f-.5f*sinf((float)(2*PI*f->phase))));
        break;
    case 8: /* Ring modulation: carrier frequency / wet mix. */
        f->phase+=(20+1980*f->a)/RATE;
        x=input*(1-f->b+f->b*sinf((float)(2*PI*f->phase)));
        break;
    case 9: case 10: {
        /* Interpolated modulated delay; independent of echo and grain buffers. */
        f->phase+=(.1+4.9*f->a)/RATE;
        float wave=.5f+.5f*sinf((float)(2*PI*f->phase));
        float delay=fx==9?RATE*(.012f+.012f*wave):RATE*(.001f+.004f*wave);
        float at=f->mod_write+RATE/20-delay;
        unsigned i=(unsigned)at%(RATE/20), next=(i+1)%(RATE/20);
        float delayed=f->mod_delay[i]+(f->mod_delay[next]-f->mod_delay[i])*(at-floorf(at));
        f->mod_delay[f->mod_write]=input+(fx==10?.5f*delayed:0);
        x=input+f->b*(delayed-input*.5f);
        break;
    }
    }
    f->phase-=floor(f->phase);
    if (fx!=9 && fx!=10) f->mod_delay[f->mod_write]=0;
    f->mod_write=(f->mod_write+1)%(RATE/20);
    if (fx!=5) f->echo[f->echo_write]=0;
    f->echo_write=(f->echo_write+1)%RATE;
    if (f->fade<1) { f->fade=fminf(1,f->fade+1.f/(RATE*.005f)); x=f->switch_from+(x-f->switch_from)*f->fade; }
    f->last=x;
    /* Snapshot the complete mix, so FN affects drums, bass and ongoing voices. */
    if (punch>=0 && punch!=f->requested) { capture(f,s,punch); f->punch=punch; }
    f->requested=punch;
    float wet=x;
    if (f->punch==8) {
        /* Independent beat-synced dub echo; old delay data is ignored on entry. */
        unsigned read=(f->punch_echo_write+PUNCH_DELAY_FRAMES-f->punch_delay)%PUNCH_DELAY_FRAMES;
        float delayed=f->punch_echo_age>=f->punch_delay?f->punch_echo[read]:0;
        f->punch_echo[f->punch_echo_write]=x+.65f*delayed;
        f->punch_echo_write=(f->punch_echo_write+1)%PUNCH_DELAY_FRAMES;
        if (f->punch_echo_age<PUNCH_DELAY_FRAMES) f->punch_echo_age++;
        wet=x+.65f*delayed;
    } else if (f->punch>=0 && f->frozen_count) {
        double pos=f->position;
        if (f->punch==3) pos=f->frozen_count-1-pos;
        if (f->punch==5) {
            unsigned segment=RATE/25;
            pos=(f->loop_age/segment*7919u)%f->frozen_count+f->loop_age%segment;
        }
        float seam=1;
        if (f->punch==7) {
            static const unsigned order[]={0,2,1,3};
            unsigned grain=f->frozen_count/4;
            unsigned offset=f->loop_age%grain, slice=(f->loop_age/grain)%4;
            pos=order[slice]*grain+offset;
            seam=fminf(1,fminf(offset,grain-offset)/(RATE*.002f));
        }
        wet=frozen_at(f,pos)*seam;
        if (f->punch==4) {
            float speed=fmaxf(0,1-f->loop_age/(.7f*RATE));
            f->position+=speed; wet*=speed;
        } else {
            f->position+=1;
            /* Short fades suppress seams in repeated/reversed captured audio. */
            unsigned at=(unsigned)fmod(f->position,f->frozen_count);
            float edge=fminf(at,f->frozen_count-at)/(RATE*.002f);
            wet*=fminf(1,edge);
        }
    }
    f->punch_wet+=smooth*((punch>=0?1.f:0.f)-f->punch_wet);
    if (punch<0 && f->punch_wet<.0001f) { f->punch=-1; f->punch_wet=0; }
    f->loop_age++;
    f->dry[f->dry_write]=input; f->dry_write=(f->dry_write+1)%(RATE/5);
    if (f->dry_available<RATE/5) f->dry_available++;
    f->history[f->write]=x; f->write=(f->write+1)%HISTORY_FRAMES;
    if (f->available<HISTORY_FRAMES) f->available++;
    return x+(wet-x)*f->punch_wet;
}
