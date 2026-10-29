#include "core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void render(Engine *e, size_t frames) {
    int16_t output[512*2];
    while (frames) { size_t count=frames<512?frames:512; engine_render(e,output,count); frames-=count; }
}
static void empty(Engine *e) {
    assert(engine_init(e));
    for (int i=0;i<SCENES;i++) engine_scene_clear(e,i);
    e->step_left=0; e->s.bpm=120;
}
static void bank(void) {
    Engine e; empty(&e);
    e.s.cursor=3; e.s.drum=2; engine_nav(&e,1,false);
    assert(e.s.pattern[2][3]); engine_scene_select(&e,4); assert(!e.s.pattern[2][3]);
    e.s.mode=BASS; e.s.bass=5; engine_nav(&e,1,false);
    assert(e.s.melody[3]==5);
    engine_scene_copy(&e,4,7); engine_scene_select(&e,7); assert(e.s.melody[3]==5);
    assert(engine_scene_name(&e,7,"BREAK")); assert(!engine_scene_name(&e,7,"             "));
    engine_scene_clear(&e,7); assert(e.s.melody[3]==-1 && !strcmp(e.s.scenes[7].name,"BREAK"));
    engine_scene_select(&e,0); assert(e.s.pattern[2][3]);
    engine_scene_select(&e,4); assert(e.s.melody[3]==5);
    engine_free(&e);
}
static void song_order(void) {
    Engine e; empty(&e); e.s.playing=false;
    const SongBlock song[]={{0,3},{3,1},{4,1},{5,1}};
    memcpy(e.s.song,song,sizeof(song)); e.s.song_count=4; e.s.song_loop=false;
    engine_song_toggle(&e);
    const int sequence[]={0,0,0,3,4,5};
    const size_t bar=RATE*8;
    for (int i=0;i<6;i++) {
        render(&e,1); assert(e.s.active_scene==sequence[i] && e.s.playhead==0);
        render(&e,bar-1); assert(e.s.active_scene==sequence[i] && e.s.playing);
    }
    render(&e,1); assert(!e.s.playing && e.s.playhead==-1 && e.s.song_position==3);
    e.s.song_loop=true; engine_song_toggle(&e);
    render(&e,bar*6+1); assert(e.s.playing && e.s.active_scene==0 && e.s.song_left==3);
    engine_song_toggle(&e); assert(e.s.stop_pending); render(&e,bar);
    assert(!e.s.playing); engine_free(&e);
}
static void quantization(void) {
    Engine e; empty(&e); const size_t bar=RATE*8;
    render(&e,1); assert(e.s.active_scene==0);
    engine_scene_launch(&e,3); engine_scene_launch(&e,5); assert(e.s.queued_scene==5);
    engine_scene_launch(&e,5); assert(e.s.queued_scene==-1);
    engine_scene_launch(&e,3); render(&e,bar-1); assert(e.s.active_scene==0);
    render(&e,1); assert(e.s.active_scene==3 && e.s.queued_scene==-1);
    engine_live_toggle(&e); assert(e.s.stop_pending);
    engine_live_toggle(&e); assert(!e.s.stop_pending);
    engine_scene_launch(&e,8); render(&e,bar); assert(e.s.active_scene==8);
    engine_live_toggle(&e); render(&e,bar-1); assert(e.s.playing);
    render(&e,1); assert(!e.s.playing);
    e.s.live_selected=2; engine_live_toggle(&e); render(&e,1); assert(e.s.active_scene==2 && e.s.playhead==0);
    engine_free(&e);
}
static void deferred_edits_and_tails(void) {
    Engine e; empty(&e); const size_t bar=RATE*8;
    render(&e,1);
    e.s.drum=0; e.s.cursor=8; engine_nav(&e,1,false);
    render(&e,bar-1);
    for (int i=0;i<VOICES;i++) assert(!e.voices[i].sample); /* Current bar is unchanged. */
    render(&e,bar/8+1); assert(e.voices[0].sample==&e.drums[0]);
    engine_free(&e);
    empty(&e);
    Sample sample={calloc(RATE*10,sizeof(float)),RATE*10}; assert(sample.data);
    for (size_t i=0;i<sample.length;i++) sample.data[i]=.1f;
    Sample old=engine_replace(&e,0,sample); free(old.data);
    e.s.pattern[0][0]=true;
    render(&e,100); engine_scene_launch(&e,1); render(&e,bar);
    assert(e.s.active_scene==1 && e.voices[0].sample==&e.drums[0]);
    engine_free(&e);
}
static void limits_and_editing(void) {
    Engine e; empty(&e); e.s.song_count=0; state_song_cursor(&e.s,0);
    for (int i=0;i<SONG_BLOCKS;i++) {
        state_song_cursor(&e.s,i); e.s.song_choice=i%SCENES; e.s.song_times=1+i%SONG_REPEATS;
        assert(engine_song_commit(&e,false));
    }
    assert(e.s.song_count==SONG_BLOCKS && !engine_song_commit(&e,true));
    e.s.song_cursor=10; assert(engine_song_delete(&e)); assert(e.s.song[10].scene==11%SCENES);
    assert(engine_song_commit(&e,true) && e.s.song_count==SONG_BLOCKS);
    e.s.song_times=0; assert(!engine_song_commit(&e,false));
    e.s.song_times=65; assert(!engine_song_commit(&e,false));
    engine_free(&e);
}
static void block_independence(void) {
    Engine a,b; assert(engine_init(&a) && engine_init(&b));
    a.s.bpm=b.s.bpm=137;
    for (int i=1;i<SCENES;i++) { engine_scene_copy(&a,0,i); engine_scene_copy(&b,0,i); }
    const SongBlock song[]={{0,3},{3,1},{4,1},{5,1}};
    memcpy(a.s.song,song,sizeof(song)); memcpy(b.s.song,song,sizeof(song)); a.s.song_count=b.s.song_count=4;
    engine_song_toggle(&a); engine_song_toggle(&b);
    size_t total=RATE*12; int16_t *x=malloc(total*2*sizeof(*x)), *y=malloc(total*2*sizeof(*y)); assert(x && y);
    engine_render(&a,x,total);
    for (size_t at=0;at<total;) { size_t count=total-at<127?total-at:127; engine_render(&b,y+2*at,count); at+=count; }
    assert(!memcmp(x,y,total*2*sizeof(*x)));
    assert(a.s.active_scene==b.s.active_scene && a.s.song_position==b.s.song_position && a.s.song_left==b.s.song_left);
    free(x); free(y); engine_free(&a); engine_free(&b);
}
int main(void) {
    bank(); song_order(); quantization(); deferred_edits_and_tails(); limits_and_editing(); block_independence();
    puts("scenes: bank/copy/clear/name, 1x3-4-5-6, loop/end, quantized launch/stop, edits next bar, tails and block-independent audio OK");
}
