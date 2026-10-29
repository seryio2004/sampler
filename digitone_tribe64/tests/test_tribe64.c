#include "core.h"
#include "hardware.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void tick(Engine *e, int step) {
    int16_t out[2]; e->s.playhead=step-1; e->step_left=0;
    memset(e->voices,0,sizeof(e->voices));
    engine_render(e,out,1);
}
static bool sounding(Engine *e, int track) {
    for (int i=0;i<VOICES;i++) if (e->voices[i].sample && e->voices[i].slot==track) return true;
    return false;
}
static void live_mutes_and_solo(void) {
    Engine e; assert(engine_init(&e));
    for (int i=0;i<SCENES;i++) engine_scene_clear(&e,i);
    e.s.active_scene=0; state_scene_select(&e.s,3);
    e.s.scenes[0].pattern[0][0]=true;
    e.s.scenes[0].pattern[1][0]=true;
    e.s.scenes[0].muted[0]=true;
    e.s.mode=MUTE; engine_fn(&e,true); engine_pad(&e,0,true);
    assert(e.s.solo_scene==0 && e.s.solo_track==0 && e.s.scenes[0].muted[0]);
    tick(&e,0); assert(sounding(&e,0) && !sounding(&e,1));
    engine_pad(&e,0,false); engine_pad(&e,0,true); /* Same solo gesture restores exact stored mutes. */
    assert(e.s.solo_track== -1 && e.s.scenes[0].muted[0]);
    tick(&e,0); assert(!sounding(&e,0) && sounding(&e,1));
    engine_fn(&e,false); engine_pad(&e,1,true);
    assert(e.s.scenes[0].muted[1] && !e.s.scenes[3].muted[1]);
    e.s.mode=SEQ; int selected=e.s.drum;
    engine_fn(&e,true); engine_pad(&e,0,true);
    assert(!e.s.scenes[0].muted[0] && e.s.drum==selected && e.s.held_count==0);
    engine_fn(&e,false);
    engine_solo(&e,PADS); assert(!state_track_muted(&e.s,0,PADS));
    engine_scene_launch(&e,2); tick(&e,0);
    assert(e.s.active_scene==2 && e.s.solo_track== -1);
    e.s.playing=false; engine_mute(&e,2);
    assert(e.s.scenes[3].muted[2] && !e.s.scenes[2].muted[2]);
    Hardware h={0}; atomic_init(&h.lights,0); e.s.mode=MUTE;
    hardware_lights(&h,&e.s,0,0);
    unsigned lights=atomic_load(&h.lights)&65535;
    assert((lights&(1u<<4)) && !(lights&(1u<<2)) && (lights&(1u<<12)));
    assert(!(lights&0xe888u)); /* Unused fourth column and final three LEDs stay off. */
    engine_free(&e);
}
int main(void) {
    live_mutes_and_solo();
    Engine e; assert(engine_init(&e));
    for (int i=0;i<SCENES;i++) engine_scene_clear(&e,i);
    for (int page=0;page<STEP_PAGES;page++) {
        engine_page(&e,page); assert(e.s.cursor==page*16);
        engine_nav(&e,0,false); assert(e.s.cursor==page*16+15);
        engine_nav(&e,1,false); assert(e.s.pattern[0][page*16+15]);
        engine_nav(&e,2,false); assert(e.s.cursor==page*16);
    }
    e.s.cursor=63; engine_page(&e,4); assert(e.s.cursor==63);
    e.s.melody[63]=2;
    tick(&e,0); tick(&e,63);
    assert(e.s.playhead==63 && sounding(&e,0) && sounding(&e,PADS+2));
    engine_mute(&e,0); engine_mute(&e,PADS); tick(&e,63);
    assert(!sounding(&e,0) && !sounding(&e,PADS+2) && e.s.pattern[0][63]);
    engine_mute(&e,0); engine_mute(&e,PADS); tick(&e,63);
    assert(sounding(&e,0) && sounding(&e,PADS+2));
    /* Pattern switches wait through all four pages, and mutes belong to a scene. */
    engine_mute(&e,0); engine_scene_copy(&e,0,2);
    assert(state_scene(&e.s,2).muted[0]);
    engine_scene_launch(&e,1); tick(&e,16); assert(e.s.active_scene==0);
    tick(&e,32); tick(&e,48); tick(&e,63); assert(e.s.active_scene==0);
    tick(&e,0); assert(e.s.active_scene==1);
    e.s.playing=false; engine_scene_select(&e,1);
    assert(!state_scene(&e.s,1).muted[0]);
    engine_scene_select(&e,0); assert(state_scene(&e.s,0).muted[0]);
    e.s.mode=MUTE; engine_pad(&e,0,true); assert(!state_scene(&e.s,0).muted[0]);
    engine_pad(&e,0,false); assert(!state_scene(&e.s,0).muted[0]);
    e.s.drum=PADS; engine_nav(&e,1,false); assert(state_scene(&e.s,0).muted[PADS]);
    engine_scene_clear(&e,2); assert(!state_scene(&e.s,2).muted[0]);
    Hardware h={0}; atomic_init(&h.lights,0);
    e.s.mode=SEQ; e.s.drum=0; e.s.cursor=63;
    hardware_lights(&h,&e.s,0,0); assert((atomic_load(&h.lights)>>16)==(1u<<15));
    assert((atomic_load(&h.lights)&65535)==(1u<<15));
    e.s.mode=MUTE; hardware_lights(&h,&e.s,0,.3);
    assert(!(atomic_load(&h.lights)&(1u<<12)));
    engine_free(&e); puts("tribe64: four pages, step 64, immediate reversible mutes, bass, scenes and LEDs OK");
}
