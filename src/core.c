#include "core.h"
#include "effects.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846
static const float gains[PADS] = {.76f,.51f,.34f,.31f,.44f,.53f,.30f,.46f,.33f};
static int clamp(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
void sample_waveform(const Sample *s, uint8_t heights[WAVE_COLUMNS]) {
    float peaks[WAVE_COLUMNS]={0}, maximum=0;
    memset(heights,0,WAVE_COLUMNS);
    if (!s || !s->data || !s->length) return;
    for (int col=0;col<WAVE_COLUMNS;col++) {
        /* Map even samples shorter than the LCD to their proportional bins. */
        size_t first=(size_t)col*s->length/WAVE_COLUMNS;
        size_t last=(size_t)(col+1)*s->length/WAVE_COLUMNS;
        for (size_t i=first;i<last;i++) {
            float peak=fabsf(s->data[i]);
            if (isfinite(peak) && peak>peaks[col]) peaks[col]=peak;
        }
        if (peaks[col]>maximum) maximum=peaks[col];
    }
    if (maximum>0) for (int col=0;col<WAVE_COLUMNS;col++)
        heights[col]=(uint8_t)ceilf(peaks[col]/maximum*8);
}
void state_init(State *s) {
    memset(s, 0, sizeof(*s));
    s->bpm=120; s->playing=true; s->enabled=true; s->volume=.75f;
    s->cutoff=s->cutoff2=.65f; s->resonance=s->resonance2=.2f;
    s->octave=2; s->playhead=-1;
    s->queued_scene=-1; s->song_position=-1; s->song_count=1; s->song_times=1;
    s->song_loop=true; s->song[0]=(SongBlock){0,1};
    for (int scene=0;scene<SCENES;scene++) {
        snprintf(s->scenes[scene].name,SCENE_NAME,"ESCENA %d",scene+1);
        for (int i=0;i<STEPS;i++) s->scenes[scene].melody[i]=-1;
    }
    for (int i=0;i<FX_SLOTS;i++) s->fx_pending[i]=-1;
    for (int i=0;i<STEPS;i++) {
        s->pattern[0][i]=i%4==0; s->pattern[1][i]=i==4 || i==12;
        s->pattern[2][i]=i%2==0; s->melody[i]=-1;
    }
    state_scene_store(s);
}
Scene state_scene(const State *s, int scene) {
    Scene result=s->scenes[scene];
    if (scene==s->edit_scene) {
        memcpy(result.pattern,s->pattern,sizeof(result.pattern));
        memcpy(result.melody,s->melody,sizeof(result.melody));
    }
    return result;
}
void state_scene_store(State *s) { s->scenes[s->edit_scene]=state_scene(s,s->edit_scene); }
void state_scene_select(State *s, int scene) {
    if (scene<0 || scene>=SCENES) return;
    state_scene_store(s); s->edit_scene=scene;
    memcpy(s->pattern,s->scenes[scene].pattern,sizeof(s->pattern));
    memcpy(s->melody,s->scenes[scene].melody,sizeof(s->melody));
}
void engine_scene_select(Engine *e, int scene) {
    if (scene<0 || scene>=SCENES) return;
    state_scene_select(&e->s,scene);
    if (e->s.transport==PATTERN_PLAY) {
        if (e->s.playing) e->s.queued_scene=scene;
        else { e->s.active_scene=scene; e->s.queued_scene=-1; }
    }
}
void engine_scene_copy(Engine *e, int source, int target) {
    if (source<0 || source>=SCENES || target<0 || target>=SCENES) return;
    Scene copy=state_scene(&e->s,source);
    e->s.scenes[target]=copy;
    if (target==e->s.edit_scene) {
        memcpy(e->s.pattern,copy.pattern,sizeof(copy.pattern));
        memcpy(e->s.melody,copy.melody,sizeof(copy.melody));
    }
}
void engine_scene_clear(Engine *e, int scene) {
    if (scene<0 || scene>=SCENES) return;
    Scene empty={0}; memcpy(empty.name,e->s.scenes[scene].name,SCENE_NAME);
    for (int i=0;i<STEPS;i++) empty.melody[i]=-1;
    e->s.scenes[scene]=empty;
    if (scene==e->s.edit_scene) {
        memcpy(e->s.pattern,empty.pattern,sizeof(empty.pattern));
        memcpy(e->s.melody,empty.melody,sizeof(empty.melody));
    }
}
bool engine_scene_name(Engine *e, int scene, const char *name) {
    size_t n=strlen(name);
    if (scene<0 || scene>=SCENES || !n || n>=SCENE_NAME) return false;
    bool visible=false;
    for (size_t i=0;i<n;i++) { if (name[i]<32 || name[i]>126) return false; if (name[i]!=' ') visible=true; }
    if (!visible) return false;
    snprintf(e->s.scenes[scene].name,SCENE_NAME,"%s",name); return true;
}
void engine_scene_launch(Engine *e, int scene) {
    if (scene<0 || scene>=SCENES) return;
    State *s=&e->s; s->live_selected=scene;
    if (s->playing && s->transport==LIVE_PLAY && s->queued_scene==scene && !s->stop_pending) {
        s->queued_scene=-1; return;
    }
    s->transport=LIVE_PLAY; s->queued_scene=scene; s->stop_pending=false; s->song_position=-1;
    if (!s->playing) { s->playing=true; s->playhead=-1; e->step_left=0; }
}
void engine_live_toggle(Engine *e) {
    if (!e->s.playing) engine_scene_launch(e,e->s.live_selected);
    else e->s.stop_pending=!e->s.stop_pending;
}
void engine_song_toggle(Engine *e) {
    State *s=&e->s;
    if (s->playing && s->transport==SONG_PLAY) { s->stop_pending=!s->stop_pending; return; }
    if (!s->song_count) return;
    s->transport=SONG_PLAY; s->song_position=-1; s->song_left=0;
    s->queued_scene=-1; s->stop_pending=false;
    if (!s->playing) { s->playing=true; s->playhead=-1; e->step_left=0; }
}
void state_song_cursor(State *s, int cursor) {
    int maximum=s->song_count<SONG_BLOCKS?s->song_count:s->song_count-1;
    s->song_cursor=clamp(cursor,0,maximum);
    bool existing=s->song_cursor<s->song_count;
    s->song_choice=existing?s->song[s->song_cursor].scene:s->edit_scene;
    s->song_times=existing?s->song[s->song_cursor].repeats:1;
    s->song_pending=!existing;
}
bool engine_song_commit(Engine *e, bool insert) {
    State *s=&e->s; int at=s->song_cursor;
    if (at<0 || at> s->song_count || s->song_choice<0 || s->song_choice>=SCENES || s->song_times<1 || s->song_times>SONG_REPEATS) return false;
    if ((insert || at==s->song_count) && s->song_count==SONG_BLOCKS) return false;
    if (insert) {
        memmove(s->song+at+1,s->song+at,(size_t)(s->song_count-at)*sizeof(SongBlock));
        s->song_count++;
        if (s->song_position>=at) s->song_position++;
    } else if (at==s->song_count) s->song_count++;
    s->song[at]=(SongBlock){(uint8_t)s->song_choice,(uint8_t)s->song_times}; s->song_pending=false;
    return true;
}
bool engine_song_delete(Engine *e) {
    State *s=&e->s; int at=s->song_cursor;
    if (at<0 || at>=s->song_count) return false;
    memmove(s->song+at,s->song+at+1,(size_t)(s->song_count-at-1)*sizeof(SongBlock));
    s->song_count--;
    if (s->song_position==at) s->song_left=1;
    if (s->song_position>=at) s->song_position--;
    state_song_cursor(s,at);
    if (!s->song_count && s->transport==SONG_PLAY) s->stop_pending=true;
    return true;
}
int pad_punch(int pad) { return pad<3?pad+BASE_PADS:pad-3; }
static bool allocate(Sample *s, double seconds) {
    s->length=(size_t)(seconds*RATE);
    s->data=calloc(s->length, sizeof(float));
    return s->data!=NULL;
}
void sample_prepare(Sample *s) {
    float peak=0;
    for (size_t i=0;i<s->length;i++) {
        if (!isfinite(s->data[i])) s->data[i]=0;
        if (fabsf(s->data[i])>peak) peak=fabsf(s->data[i]);
    }
    /* One fixed attenuation per source, never a time-varying/block normalizer. */
    if (peak>1) for (size_t i=0;i<s->length;i++) s->data[i]*=.98f/peak;
    size_t fade=s->length/2<(size_t)VOICE_RELEASE?s->length/2:(size_t)VOICE_RELEASE;
    for (size_t i=0;i<fade;i++) {
        float gain=(float)i/(float)fade;
        s->data[i]*=gain; s->data[s->length-1-i]*=gain;
    }
}
static void synth_gain(Sample *s) {
    float peak=0;
    for (size_t i=0;i<s->length;i++) if (fabsf(s->data[i])>peak) peak=fabsf(s->data[i]);
    if (peak>.86f) for (size_t i=0;i<s->length;i++) s->data[i]*=.86f/peak;
    sample_prepare(s);
}
bool synth_drum(Sample *s, int pad) {
    if (pad<0 || pad>=PADS) return false;
    static const double lengths[]={.48,.30,.12,.46,.34,.44,.22,.16,.42};
    if (!allocate(s,lengths[pad])) return false;
    uint32_t rng=2030u; double phase=0; float previous=0;
    for (size_t i=0;i<s->length;i++) {
        double t=(double)i/RATE;
        rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
        float noise=(float)((double)rng/2147483648.-1.);
        float high=noise-previous; previous=noise;
        double value=0;
        switch (pad) {
        case 0: phase+=(49+120*exp(-32*t))/RATE;
            value=sin(2*PI*phase)*exp(-11*t)+.14*high*exp(-120*t); break;
        case 1: value=.72*high*exp(-22*t)+.37*sin(2*PI*175*t)*exp(-28*t); break;
        case 2: value=high*exp(-65*t); break;
        case 3: value=high*exp(-9*t); break;
        case 4: value=high*(exp(-110*t)+.15*exp(-10*t)
            +(t>=.019?.75*exp(-130*(t-.019)):0)
            +(t>=.039?.55*exp(-110*(t-.039)):0)); break;
        case 6: value=high*exp(-24*t)*(.4+.6*fabs(sin(2*PI*47*t))); break;
        case 7: value=(.65*sin(2*PI*820*t)+.3*high)*exp(-60*t); break;
        case 8: value=(sin(2*PI*540*t)+.65*sin(2*PI*800*t))*exp(-12*t); break;
        case 5: phase+=(105+145*exp(-20*t))/RATE;
            value=sin(2*PI*phase)*exp(-12*t)+.05*high*exp(-80*t); break;
        }
        s->data[i]=(float)value;
    }
    synth_gain(s); return true;
}
static bool synth_bass(Sample *s, int pad, int octave) {
    static const int midi[]={36,38,40,43,45,48};
    if (!allocate(s,.68)) return false;
    double hz=440*pow(2.,(midi[pad]+12*(octave-2)-69)/12.), phase=0;
    for (size_t i=0;i<s->length;i++) {
        double t=(double)i/RATE; phase+=hz*(1+.045*exp(-30*t))/RATE;
        double tri=2*fabs(2*(phase-floor(phase))-1)-1;
        s->data[i]=(float)((.49*tri+.66*sin(2*PI*phase)+.095*sin(4*PI*phase))
            *fmin(t/.009,1)*exp(-3.45*t));
    }
    synth_gain(s); return true;
}
bool engine_init(Engine *e) {
    memset(e,0,sizeof(*e)); state_init(&e->s); e->step_left=.2*RATE;
    e->effects=calloc(FX_SLOTS,sizeof(*e->effects));
    if (!e->effects) return false;
    for (int i=0;i<FX_SLOTS;i++) {
        e->effects[i].punch=e->effects[i].requested=-1; e->effects[i].fade=1;
    }
    e->master_gain=e->s.volume; e->limiter_gain=1;
    e->volume_slew=1-expf(-1.f/(.005f*RATE));
    e->attack_coefficient=1-expf(-1.f/(.00025f*RATE));
    e->release_coefficient=1-expf(-1.f/(.05f*RATE));
    for (int p=0;p<PADS;p++) {
        if (!synth_drum(&e->drums[p],p)) { engine_free(e); return false; }
    }
    for (int p=0;p<NOTES;p++) for (int o=0;o<4;o++) if (!synth_bass(&e->bass[o][p],p,o+1)) {
            engine_free(e); return false;
        }
    return true;
}
void engine_free(Engine *e) {
    free(e->effects); e->effects=NULL;
    free(e->audition.data); e->audition=(Sample){0};
    for (int p=0;p<PADS;p++) {
        free(e->drums[p].data); e->drums[p]=(Sample){0};
    }
    for (int p=0;p<NOTES;p++) {
        for (int o=0;o<4;o++) { free(e->bass[o][p].data); e->bass[o][p]=(Sample){0}; }
    }
}
int engine_punch(const Engine *e) {
    return e->s.mode==FX && e->s.held_count ? pad_punch(e->s.held[e->s.held_count-1]) : -1;
}
static float read_at(const Sample *s, double pos) {
    if (pos<0 || pos>=(double)s->length) return 0;
    size_t i=(size_t)pos;
    float a=s->data[i], b=i+1<s->length?s->data[i+1]:0;
    return a+(b-a)*(float)(pos-i);
}
static void retire_voice(Engine *e, Voice *v) {
    if (!v->sample) return;
    /* Keep only the final value, so replacing/freeing the PCM stays safe. */
    if (v->last!=0) {
        Tail *tail=&e->tails[e->tail_index++%VOICES];
        *tail=(Tail){v->last,LOOKAHEAD_FRAMES};
    }
    v->sample=NULL;
}
static void trigger(Engine *e, int slot, int punch) {
    const Sample *s=slot<PADS?&e->drums[slot]:&e->bass[e->s.octave-1][slot-PADS];
    if (!s->data || !s->length) return;
    if (slot>=PADS) for (int i=0;i<VOICES;i++)
        if (e->voices[i].sample && e->voices[i].slot>=PADS) retire_voice(e,&e->voices[i]);
    int index=-1;
    for (int i=0;i<VOICES;i++) if (!e->voices[i].sample) { index=i; break; }
    if (index<0) { index=(int)(e->steal++%VOICES); retire_voice(e,&e->voices[index]); }
    size_t duration=s->length;
    if (punch==4) duration=(size_t)fmin(RATE,fmax(duration,.48*RATE));
    e->voices[index]=(Voice){.sample=s,.slot=slot,.punch=punch,
        .duration=duration,.gain=slot<PADS?gains[slot]:.48f};
}
static void chord(Engine *e, unsigned mask, int punch) {
    for (int i=0;i<PADS+NOTES;i++) if (mask&(1u<<i)) trigger(e,i,punch);
}
static void changed_punch(Engine *e, int old) {
    int p=engine_punch(e);
    if (p==old || e->s.controller) return;
    e->repeat_chord=0;
    if (p<0) return;
    if (p<3) {
        e->repeat_chord=e->last_chord;
        e->repeat_left=RATE*60./e->s.bpm/(2<<p);
        chord(e,e->repeat_chord,-1);
    } else chord(e,e->last_chord,p);
}
void engine_fn(Engine *e, bool down) {
    e->s.fn=down;
    if (!down) { int old=engine_punch(e); e->s.held_count=0; changed_punch(e,old); }
}
void engine_pad(Engine *e, int pad, bool down) {
    if (pad<0 || pad>=PADS) return;
    State *s=&e->s; int old=engine_punch(e);
    if (s->mode==LIVE || s->mode==BANK || s->mode==SONG) {
        if (down) {
            if (s->mode==LIVE) engine_scene_launch(e,pad);
            else if (s->mode==BANK) engine_scene_select(e,pad);
            else { s->song_choice=pad; s->song_pending=true; }
        }
        return;
    }
    for (int i=0;i<s->held_count;i++) if (s->held[i]==pad) {
        memmove(s->held+i,s->held+i+1,(size_t)(s->held_count-i-1)*sizeof(int));
        s->held_count--; break;
    }
    if (down) {
        if (s->mode==FX && ((pad>=3 && !s->controller) || (s->controller && s->fn))) {
            s->held[s->held_count++]=pad;
        }
        else if (s->mode!=FILES) {
            int slot=pad;
            if (s->mode==BASS && pad>=3) { s->bass=pad-3; slot=PADS+s->bass; }
            else s->drum=pad;
            e->last_chord=1u<<slot; trigger(e,slot,-1);
        } else {
            s->drum=pad; e->last_chord=1u<<pad; trigger(e,pad,-1);
        }
    }
    changed_punch(e,old);
}
State state_effect(const State *s, int slot) {
    State result=*s;
    if (slot) { result.effect=s->effect2; result.enabled=s->enabled2;
        result.cutoff=s->cutoff2; result.resonance=s->resonance2; }
    return result;
}
int state_fx_choice(const State *s, int slot) {
    return s->fx_pending[slot]>=0?s->fx_pending[slot]:(slot?s->effect2:s->effect);
}
void engine_fx_select(Engine *e, int effect) {
    if (effect<0 || effect>=FX_COUNT) return;
    int slot=e->s.fx_slot, active=slot?e->s.effect2:e->s.effect;
    e->s.fx_pending[slot]=effect==active?-1:effect;
}
void engine_audition(Engine *e, bool play) {
    if (e->audition_voice.sample) {
        e->audition_tail=(Tail){e->audition_voice.last,LOOKAHEAD_FRAMES};
        e->audition_voice.sample=NULL;
    }
    if (play && e->audition.data && e->audition.length)
        e->audition_voice=(Voice){.sample=&e->audition,.duration=e->audition.length,.gain=.35f,.punch=-1};
}
Sample engine_audition_replace(Engine *e, Sample replacement) {
    engine_audition(e,false);
    Sample old=e->audition; e->audition=replacement; return old;
}
void engine_nav(Engine *e, int key, bool held) {
    State *s=&e->s; int d=key==0?-1:1;
    if (held && key==1) {
        int old=engine_punch(e); s->held_count=0; s->mode=(s->mode+1)%MODES;
        for (int i=0;i<FX_SLOTS;i++) s->fx_pending[i]=-1;
        changed_punch(e,old); return;
    }
    if (held) {
        if (s->mode==SEQ) s->drum=(s->drum+d+PADS)%PADS;
        if (s->mode==FX) s->fx_parameter=(s->fx_parameter+d+4)%4;
        if (s->mode==BASS) s->octave=clamp(s->octave+d,1,4);
        return;
    }
    switch (s->mode) {
    case SEQ: case BASS:
        if (key!=1) s->cursor=(s->cursor+d+STEPS)%STEPS;
        else if (s->mode==SEQ) s->pattern[s->drum][s->cursor]^=1;
        else s->melody[s->cursor]=s->melody[s->cursor]==s->bass?-1:s->bass;
        if (key==1) state_scene_store(s);
        break;
    case FX: {
        int *effect=s->fx_slot?&s->effect2:&s->effect;
        bool *enabled=s->fx_slot?&s->enabled2:&s->enabled;
        float *a=s->fx_slot?&s->cutoff2:&s->cutoff, *b=s->fx_slot?&s->resonance2:&s->resonance;
        int pending=s->fx_pending[s->fx_slot];
        if (key==1) {
            if (pending>=0) { *effect=pending; *enabled=pending!=0; s->fx_pending[s->fx_slot]=-1; }
            else *enabled=*effect!=0 && !*enabled;
        } else if (s->fx_parameter==1) { if (pending<0) *a=fminf(1,fmaxf(0,*a+d*.05f)); }
        else if (s->fx_parameter==2) { if (pending<0) *b=fminf(1,fmaxf(0,*b+d*.05f)); }
        else if (s->fx_parameter==3) s->fx_slot^=1;
        else engine_fx_select(e,(state_fx_choice(s,s->fx_slot)+d+FX_COUNT)%FX_COUNT);
        break;
    }
    case BPM:
        if (key==1) {
            s->playing=!s->playing; s->stop_pending=false;
            if (s->playing) { s->playhead=-1; e->step_left=0; if (s->transport==SONG_PLAY) s->song_position=-1; }
        }
        else { s->bpm=clamp(s->bpm+d*5,50,220); e->step_left=RATE*60./s->bpm/4;
            e->repeat_left=0; }
        break;
    case DRUM:
        if (key==1) s->backing=!s->backing;
        else s->volume=fminf(1,fmaxf(0,s->volume+d*.05f));
        break;
    default: break;
    }
}
static bool bar_boundary(Engine *e) {
    State *s=&e->s;
    if (s->stop_pending) { s->stop_pending=false; s->playing=false; s->queued_scene=-1; s->playhead=-1; return false; }
    if (s->transport==SONG_PLAY) {
        if (!s->song_count) { s->playing=false; s->playhead=-1; return false; }
        if (s->song_position<0) { s->song_position=0; s->song_left=s->song[0].repeats; }
        else if (--s->song_left<=0) {
            if (++s->song_position>=s->song_count) {
                if (!s->song_loop) { s->song_position=s->song_count-1; s->playing=false; s->playhead=-1; return false; }
                s->song_position=0;
            }
            s->song_left=s->song[s->song_position].repeats;
        }
        s->active_scene=s->song[s->song_position].scene;
    } else if (s->queued_scene>=0) {
        s->active_scene=s->queued_scene; s->queued_scene=-1;
    }
    e->play_scene=state_scene(s,s->active_scene);
    return true;
}
static void clock_frame(Engine *e) {
    State *s=&e->s;
    if (!s->playing) return;
    int punch=e->s.controller?-1:engine_punch(e);
    if (e->step_left<=0) {
        int next=(s->playhead+1)%STEPS;
        if (!next && !bar_boundary(e)) return;
        s->playhead=next;
        unsigned mask=0;
        if (s->mode!=DRUM || s->backing) {
            for (int p=0;p<PADS;p++) if (e->play_scene.pattern[p][s->playhead]) mask|=1u<<p;
            int note=e->play_scene.melody[s->playhead]; if (note>=0) mask|=1u<<(note+PADS);
        }
        if (!(punch>=0 && punch<3) && mask) { chord(e,mask,punch); e->last_chord=mask; }
        e->step_left+=RATE*60./s->bpm/4;
    }
    e->step_left--;
    if (punch>=0 && punch<3 && e->repeat_chord) {
        if (e->repeat_left<=0) { chord(e,e->repeat_chord,-1); e->repeat_left+=RATE*60./s->bpm/(2<<punch); }
        e->repeat_left--;
    }
}
static float voice_frame(Voice *v) {
    double pos=(double)v->age; float envelope=1;
    if (v->punch==3) {
        pos=(double)v->duration-1-pos;
        size_t left=v->duration-v->age;
        if (left<(size_t)(.025*RATE)) envelope=(float)left/(.025f*RATE);
    } else if (v->punch==4) {
        double x=(double)v->age/v->duration;
        pos=v->duration*(1.34*x-1.3*x*x+1.3*x*x*x/3);
        envelope=(float)(1-x);
    } else if (v->punch==5) {
        size_t grain=(size_t)(.022*RATE), k=v->age/grain;
        size_t count=(v->duration+grain-1)/grain;
        pos=(double)(((k*5+(k/3)*7)%count)*grain+v->age%grain);
        envelope=k%7==0?0:k%3==0?.42f:.9f;
        size_t edge=v->age%grain, end=grain-1-edge;
        if (edge<VOICE_ATTACK) envelope*=(float)edge/VOICE_ATTACK;
        if (end<VOICE_ATTACK) envelope*=(float)end/VOICE_ATTACK;
    }
    float x=v->punch<3?(v->age<v->sample->length?v->sample->data[v->age]:0):read_at(v->sample,pos);
    if (v->age<VOICE_ATTACK) envelope*=(float)v->age/VOICE_ATTACK;
    size_t left=v->duration-1-v->age;
    if (left<VOICE_RELEASE) envelope*=(float)left/VOICE_RELEASE;
    v->last=x*envelope*v->gain;
    if (++v->age>=v->duration) v->sample=NULL;
    return v->last;
}
static float slew(float current, float target, float coefficient) {
    if (current==target) return current;
    float next=current+coefficient*(target-current);
    /* Complete tiny releases instead of stalling below unity at float precision. */
    return next==current?nextafterf(current,target):next;
}
static float limit_output(Engine *e, float x) {
    float peak=fabsf(x);
    if (peak>=e->detector_peak) { e->detector_peak=peak; e->detector_hold=LOOKAHEAD_FRAMES; }
    else if (e->detector_hold) e->detector_hold--;
    else e->detector_peak*=1-e->release_coefficient;
    if (e->detector_peak<1e-12f) e->detector_peak=0; /* Avoid denormal CPU costs in silence. */
    float target=e->detector_peak>.94f?.94f/e->detector_peak:1;
    float coefficient=target<e->limiter_gain?e->attack_coefficient:e->release_coefficient;
    e->limiter_gain=slew(e->limiter_gain,target,coefficient);
    if (target==1 && 1-e->limiter_gain<1e-6f) e->limiter_gain=1;
    if (e->limiter_gain<.999f) e->limited_frames++;
    float delayed=e->delay[e->delay_index]; e->delay[e->delay_index]=x;
    if (++e->delay_index==LOOKAHEAD_FRAMES) e->delay_index=0;
    return delayed*e->limiter_gain;
}
void engine_render(Engine *e, int16_t *out, size_t frames) {
    State second=state_effect(&e->s,1);
    effects_prepare(&e->effects[0],&e->s);
    effects_prepare(&e->effects[1],&second);
    for (size_t i=0;i<frames;i++) {
        clock_frame(e); float x=0;
        for (int v=0;v<VOICES;v++) if (e->voices[v].sample) {
            int slot=e->voices[v].slot;
            float value=voice_frame(&e->voices[v]); x+=value;
            /* Track the sound itself; a short pulse remains visible for 40 ms. */
            if (fabsf(value*e->master_gain)>1e-4f)
                e->light_until[slot]=e->rendered_frames+RATE/25;
        }
        for (int t=0;t<VOICES;t++) if (e->tails[t].left) {
            Tail *tail=&e->tails[t]; x+=tail->value*(float)tail->left/LOOKAHEAD_FRAMES; tail->left--;
        }
        e->master_gain=slew(e->master_gain,e->s.volume,e->volume_slew);
        if (fabsf(e->s.volume-e->master_gain)<1e-7f) e->master_gain=e->s.volume;
        x=effects_frame(&e->effects[0],x,&e->s,-1);
        x=effects_frame(&e->effects[1],x,&second,e->s.controller?engine_punch(e):-1);
        /* Dry audition is independent of the assigned pad and the FX chain. */
        if (e->audition_voice.sample) x+=voice_frame(&e->audition_voice);
        if (e->audition_tail.left) {
            x+=e->audition_tail.value*(float)e->audition_tail.left/LOOKAHEAD_FRAMES;
            e->audition_tail.left--;
        }
        x*=e->master_gain;
        float peak=fabsf(x);
        if (peak>e->output_peak) e->output_peak=peak;
        if (peak>.98f) e->overloaded_frames++;
        if (e->rendered_frames%VISUAL_INTERVAL==0) {
            AudioVisual *v=&e->visual[(e->rendered_frames/VISUAL_INTERVAL)%VISUAL_HISTORY];
            *v=(AudioVisual){.frame=e->rendered_frames,.playhead=(int8_t)e->s.playhead,
                .scene=(int8_t)e->s.active_scene,.song_position=(int8_t)e->s.song_position,.song_left=(uint8_t)e->s.song_left};
            for (int slot=0;slot<PADS+NOTES;slot++)
                if (e->light_until[slot]>e->rendered_frames) v->voices|=(uint16_t)(1u<<slot);
        }
        e->rendered_frames++;
        x=limit_output(e,x);
        if (fabsf(x)>.98f) e->clipped_frames++;
        x=fminf(.98f,fmaxf(-.98f,x));
        out[2*i]=out[2*i+1]=(int16_t)lrintf(x*32767);
    }
}
AudioVisual engine_visual_at(const Engine *e, uint64_t played_frames) {
    AudioVisual none={.playhead=-1,.scene=-1,.song_position=-1};
    if (played_frames<=LOOKAHEAD_FRAMES) return none;
    uint64_t frame=played_frames-LOOKAHEAD_FRAMES-1;
    if (frame>=e->rendered_frames) return none;
    uint64_t bucket=frame/VISUAL_INTERVAL;
    AudioVisual v=e->visual[bucket%VISUAL_HISTORY];
    return v.frame==bucket*VISUAL_INTERVAL?v:none;
}
Sample engine_replace(Engine *e, int pad, Sample replacement) {
    for (int i=0;i<VOICES;i++) if (e->voices[i].sample && e->voices[i].slot==pad)
        retire_voice(e,&e->voices[i]);
    Sample old=e->drums[pad]; e->drums[pad]=replacement; return old;
}
