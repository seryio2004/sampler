/* Exercise the actual application dispatch with GPIO and serial input simulated. */
#define main sampler_application_main
#define hardware_buttons simulated_buttons
#include "../src/main.c"
#undef main
#undef hardware_buttons
#include <assert.h>
#include <fcntl.h>
static unsigned pressed;
bool simulated_buttons(Hardware *h, unsigned *value) {
    (void)h; *value=pressed; return true;
}
static void buttons(App *a, unsigned value, double at) {
    pressed=value; assert(poll_buttons(a,at)); assert(poll_buttons(a,at+.03));
}
static void frame(App *a, int writer, double at) {
    const char line[]="CTRL 0 665 205 0 0\n";
    assert(write(writer,line,sizeof(line)-1)==sizeof(line)-1);
    poll_arduino(a,at);
}
static void control_frame(App *a, int writer, double at, int nav, int c, int r, int ok) {
    char line[80]; int n=snprintf(line,sizeof(line),"CTRL %d %d %d %d 0\n",nav,c,r,ok);
    assert(write(writer,line,(size_t)n)==n); poll_arduino(a,at);
}
static void pattern_and_menu_controls(void) {
    App a={0}; a.no_save=true;
    assert(engine_init(&a.engine)); assert(!pthread_mutex_init(&a.mutex,NULL));
    arduino_init(&a.arduino,"simulated");
    int pipefd[2]; assert(!pipe(pipefd));
    assert(fcntl(pipefd[0],F_SETFL,O_NONBLOCK)==0); a.arduino.fd=pipefd[0];
    control_frame(&a,pipefd[1],10,0,0,0,0);
    float cutoff=a.engine.s.cutoff, resonance=a.engine.s.resonance;
    control_frame(&a,pipefd[1],10.1,1023,1023,1023,0);
    assert(a.engine.s.cursor==15 && a.engine.s.drum==8 && a.engine.s.bass==5);
    assert(a.engine.s.cutoff==cutoff && a.engine.s.resonance==resonance);
    control_frame(&a,pipefd[1],10.2,1023,1023,1023,1);
    control_frame(&a,pipefd[1],10.3,1023,1023,1023,0);
    assert(a.engine.s.pattern[8][15]); /* Short OK edits the selected sound. */
    /* Holding D2 traverses every menu once, even with a bounce in a serial batch. */
    for (int mode=0;mode<MODES;mode++) {
        set_mode(&a,mode);
        double start=11+mode*2;
        for (int i=3;i>0;i--) control_frame(&a,pipefd[1],start-i*.2,0,0,0,0);
        control_frame(&a,pipefd[1],start,0,0,0,1);
        control_frame(&a,pipefd[1],start+.2,0,0,0,1);
        const char bounce[]="CTRL 0 0 0 0 0\nCTRL 0 0 0 1 0\n";
        assert(write(pipefd[1],bounce,sizeof(bounce)-1)==sizeof(bounce)-1);
        poll_arduino(&a,start+.4);
        assert(a.arduino_ok.stable && a.arduino_ok.down==start);
        control_frame(&a,pipefd[1],start+.6,0,0,0,1);
        control_frame(&a,pipefd[1],start+.7,0,0,0,1);
        assert(a.menu_open && a.engine.s.mode==mode && a.menu_selected==mode);
        for (int i=8;i<=12;i++) control_frame(&a,pipefd[1],start+i*.1,0,0,0,1);
        assert(a.menu_open && a.engine.s.mode==mode);
        State held=a.engine.s;
        control_frame(&a,pipefd[1],start+1.3,0,0,0,0);
        assert(a.engine.s.mode==held.mode && a.engine.s.enabled==held.enabled);
        assert(a.engine.s.playing==held.playing && a.engine.s.backing==held.backing);
        assert(!memcmp(a.engine.s.pattern,held.pattern,sizeof(held.pattern)));
        assert(!memcmp(a.engine.s.melody,held.melody,sizeof(held.melody)));
        int next=menu_item(&a,(menu_index(&a)+1)%menu_count(&a));
        nav(&a,2,false); nav(&a,1,false);
        if (next==MENU_MORE || next==MENU_BACK) {
            assert(a.menu_open && a.engine.s.mode==mode);
            next=a.menu_selected; nav(&a,1,false);
        }
        assert(!a.menu_open && a.engine.s.mode==next);
    }
    /* Changing roles requires FX pickup again; a pattern selection never sets FX. */
    a.engine.s.mode=BASS;
    control_frame(&a,pipefd[1],24,0,0,0,0);
    control_frame(&a,pipefd[1],24.1,0,1023,1023,0);
    assert(a.engine.s.bass==5 && a.engine.s.drum==8);
    control_frame(&a,pipefd[1],24.2,0,1023,1023,1);
    control_frame(&a,pipefd[1],24.3,0,1023,1023,0);
    assert(a.engine.s.melody[a.engine.s.cursor]==5);
    a.engine.s.mode=FX; a.engine.s.cutoff=.5f; a.engine.s.resonance=.5f;
    control_frame(&a,pipefd[1],24.4,0,1023,1023,0);
    assert(a.engine.s.cutoff==.5f && a.engine.s.resonance==.5f);
    control_frame(&a,pipefd[1],24.5,0,512,512,0);
    control_frame(&a,pipefd[1],24.6,0,0,0,0);
    assert(a.engine.s.cutoff==0 && a.engine.s.resonance==0);
    /* A lost OK gesture stays cancelled if the connection returns while held. */
    control_frame(&a,pipefd[1],24.7,0,0,0,1);
    poll_arduino(&a,25.1);
    for (int i=0;i<10;i++) control_frame(&a,pipefd[1],25.2+i*.1,0,0,0,1);
    assert(a.engine.s.mode==FX);
    bool enabled=a.engine.s.enabled;
    control_frame(&a,pipefd[1],26.2,0,0,0,0);
    assert(a.engine.s.enabled==enabled);
    arduino_close(&a.arduino); close(pipefd[1]);
    engine_free(&a.engine); pthread_mutex_destroy(&a.mutex);
}
static void gesture_frame(App *a, int writer, double at, int c, int r, int ok, int fn) {
    char line[80]; int n=snprintf(line,sizeof(line),"CTRL 0 %d %d %d %d\n",c,r,ok,fn);
    assert(write(writer,line,(size_t)n)==n); poll_arduino(a,at);
}
static void pages_and_mutes(void) {
    App a={0}; a.no_save=true;
    assert(engine_init(&a.engine)); assert(!pthread_mutex_init(&a.mutex,NULL));
    arduino_init(&a.arduino,"simulated"); int fds[2]; assert(!pipe(fds));
    assert(fcntl(fds[0],F_SETFL,O_NONBLOCK)==0); a.arduino.fd=fds[0];
    gesture_frame(&a,fds[1],50,0,0,0,0);
    for (int page=1;page<=4;page++) {
        double at=50+page*.3;
        gesture_frame(&a,fds[1],at,0,0,0,1);
        gesture_frame(&a,fds[1],at+.1,0,0,0,0);
        assert(a.engine.s.cursor/PAGE_STEPS==page%4);
    }
    gesture_frame(&a,fds[1],52,0,0,0,1);
    gesture_frame(&a,fds[1],52.1,0,0,1,1);
    gesture_frame(&a,fds[1],52.2,0,0,1,0);
    gesture_frame(&a,fds[1],52.3,0,0,0,0);
    assert(a.engine.s.mode==MUTE && a.engine.s.cursor==0);
    gesture_frame(&a,fds[1],52.4,0,0,1,0);
    gesture_frame(&a,fds[1],52.5,0,0,0,0);
    assert(state_scene(&a.engine.s,0).muted[0]);
    pad(&a,0,true); pad(&a,0,false);
    assert(!state_scene(&a.engine.s,0).muted[0]);
    State before=a.engine.s;
    gesture_frame(&a,fds[1],52.6,0,0,1,1);
    gesture_frame(&a,fds[1],52.7,0,0,0,1);
    gesture_frame(&a,fds[1],52.8,0,0,0,0);
    assert(a.engine.s.mode==SEQ && a.engine.s.cursor==0);
    assert(!memcmp(before.pattern,a.engine.s.pattern,sizeof(before.pattern)));
    command(&a,"page 4"); control_frame(&a,fds[1],53,1023,0,0,0);
    assert(a.engine.s.cursor==63); nav(&a,1,false);
    assert(a.engine.s.pattern[0][63]);
    set_mode(&a,MUTE); a.engine.s.drum=PADS; nav(&a,1,false);
    assert(state_scene(&a.engine.s,0).muted[PADS]);
    set_mode(&a,SEQ); assert(a.engine.s.drum<PADS);
    arduino_close(&a.arduino); close(fds[1]); engine_free(&a.engine); pthread_mutex_destroy(&a.mutex);
}
static void compact_menu_and_mutes(void) {
    App a={0}; a.no_save=true;
    assert(engine_init(&a.engine)); assert(!pthread_mutex_init(&a.mutex,NULL));
    nav(&a,1,true); assert(a.menu_open && menu_count(&a)==7);
    nav(&a,0,false); assert(a.menu_selected==MENU_MORE);
    nav(&a,1,false); assert(a.menu_advanced && a.menu_open && a.menu_selected==BASS);
    nav(&a,0,false); assert(a.menu_selected==MENU_BACK);
    nav(&a,1,false); assert(!a.menu_advanced && a.menu_selected==SEQ && a.menu_open);
    nav(&a,2,false); assert(a.menu_selected==MUTE);
    nav(&a,1,false); assert(a.engine.s.mode==MUTE && !a.menu_open);
    set_mode(&a,SOUND); engine_fn(&a.engine,true); nav(&a,1,false);
    assert(a.engine.s.mode==MUTE && a.mute_return==SOUND);
    engine_fn(&a.engine,true); pad(&a,1,true); pad(&a,1,false);
    assert(a.engine.s.solo_track==1);
    nav(&a,1,false); assert(a.engine.s.mode==SOUND && a.engine.s.solo_track== -1);
    engine_fn(&a.engine,true); pad(&a,2,true); pad(&a,2,false);
    assert(a.engine.s.scenes[0].muted[2] && a.engine.s.mode==SOUND);
    engine_fn(&a.engine,false);
    engine_free(&a.engine); pthread_mutex_destroy(&a.mutex);
}
static void sound_frame(App *a, int writer, double at, int pitch, int cutoff, int length, int fn) {
    char line[80]; int n=snprintf(line,sizeof(line),"CTRL %d %d %d 0 %d\n",pitch,cutoff,length,fn);
    assert(write(writer,line,(size_t)n)==n); poll_arduino(a,at);
}
static void sound_knobs(void) {
    App a={0}; a.no_save=true;
    assert(engine_init(&a.engine)); assert(!pthread_mutex_init(&a.mutex,NULL));
    a.engine.s.playing=false;
    arduino_init(&a.arduino,"simulated"); int fds[2]; assert(!pipe(fds));
    assert(fcntl(fds[0],F_SETFL,O_NONBLOCK)==0); a.arduino.fd=fds[0];
    set_mode(&a,SOUND);
    sound_frame(&a,fds[1],60,0,0,0,0);
    assert(a.engine.s.sound[0].pitch==0 && a.engine.s.sound[0].cutoff==1 && a.engine.s.sound[0].length==1);
    sound_frame(&a,fds[1],60.1,512,1023,1023,0);
    sound_frame(&a,fds[1],60.2,1023,500,300,0);
    PadSound first=a.engine.s.sound[0];
    assert(first.pitch==24 && fabsf(first.cutoff-500/1023.f)<1e-6f && first.length<.31f);
    assert(a.engine.s.cutoff==.65f && a.engine.s.resonance==.2f && a.engine.s.cursor==0);
    pad(&a,1,true); pad(&a,1,false);
    sound_frame(&a,fds[1],60.3,1023,500,300,0);
    assert(a.engine.s.sound[1].pitch==0 && a.engine.s.sound[1].cutoff==1 && a.engine.s.sound[1].length==1);
    sound_frame(&a,fds[1],60.4,512,1023,1023,0);
    sound_frame(&a,fds[1],60.5,0,256,100,0);
    assert(a.engine.s.sound[1].pitch== -24 && a.engine.s.sound[1].cutoff<.26f);
    assert(!memcmp(&a.engine.s.sound[0],&first,sizeof(first)));
    sound_frame(&a,fds[1],60.6,0,256,100,1);
    sound_frame(&a,fds[1],60.7,1023,256,100,1);
    assert(a.engine.s.drum==8);
    sound_frame(&a,fds[1],60.8,1023,256,100,0);
    assert(a.engine.s.sound[8].pitch==0 && a.engine.s.sound[8].cutoff==1 && a.engine.s.sound[8].length==1);
    poll_arduino(&a,61.2); assert(!a.engine.s.controller);
    assert(!memcmp(&a.engine.s.sound[0],&first,sizeof(first)));
    arduino_close(&a.arduino); close(fds[1]); engine_free(&a.engine); pthread_mutex_destroy(&a.mutex);
}
static void files_and_gestures(void) {
    App a={0}; a.no_save=true;
    assert(engine_init(&a.engine)); assert(!pthread_mutex_init(&a.mutex,NULL));
    assert(!pthread_mutex_init(&a.preview.mutex,NULL)); assert(!pthread_mutex_init(&a.loader.mutex,NULL));
    char root[]="/tmp/sampler-ui-XXXXXX"; assert(mkdtemp(root));
    char folder[PATH_MAX], path[PATH_MAX]; snprintf(folder,sizeof(folder),"%s/sounds",root); assert(!mkdir(folder,0700));
    snprintf(path,sizeof(path),"%s/sounds/01_radio_alien.wav",root);
    SF_INFO info={.samplerate=RATE,.channels=1,.format=SF_FORMAT_WAV|SF_FORMAT_PCM_16};
    SNDFILE *wav=sf_open(path,SFM_WRITE,&info); assert(wav);
    float pcm[4096]; for (int i=0;i<4096;i++) pcm[i]=.25f*sinf(i*.1f);
    assert(sf_writef_float(wav,pcm,4096)==4096); assert(!sf_close(wav));
    assert(browser_init(&a.browser,root)); set_mode(&a,FILES);
    file_nav(&a,1,false); assert(a.browser.count==2 && !strcmp(a.browser.entries[0].name,".. / VOLVER"));
    file_nav(&a,2,false); toggle_preview(&a);
    update_preview(&a,monotonic()+1); loader_join(&a.preview); update_preview(&a,monotonic()+1);
    assert(a.preview_ready && a.preview_valid && a.engine.audition_voice.sample);
    assert(!a.assignments[0][0]);
    int16_t out[1024]; engine_render(&a.engine,out,512); assert(a.engine.output_peak>0);
    toggle_preview(&a); assert(!a.engine.audition_voice.sample);
    for (int i=0;i<PADS;i++) {
        memset(a.engine.voices,0,sizeof(a.engine.voices)); pad(&a,i,true);
        assert(a.engine.s.drum==i && a.engine.voices[0].sample==&a.engine.drums[i]); pad(&a,i,false);
    }
    strcpy(a.assignments[0],path); char label[64]; sample_name(&a,0,label); assert(!strcmp(label,"radio alien"));
    arduino_init(&a.arduino,"simulated"); int fds[2]; assert(!pipe(fds));
    assert(fcntl(fds[0],F_SETFL,O_NONBLOCK)==0); a.arduino.fd=fds[0];
    gesture_frame(&a,fds[1],30,0,0,0,0);
    gesture_frame(&a,fds[1],30.1,0,0,0,1);
    gesture_frame(&a,fds[1],30.2,0,0,0,0); assert(a.engine.audition_voice.sample);
    gesture_frame(&a,fds[1],30.3,0,0,0,1);
    gesture_frame(&a,fds[1],30.4,0,0,0,0); assert(!a.engine.audition_voice.sample);
    gesture_frame(&a,fds[1],30.5,0,0,0,1);
    for (int i=1;i<=8;i++) gesture_frame(&a,fds[1],30.5+i*.1,0,0,0,1);
    assert(!strcmp(a.browser.path,root));
    gesture_frame(&a,fds[1],31.4,0,0,0,0); assert(!a.engine.audition_voice.sample);
    /* Both release orders of FN+OK select an FX slot without toggling bypass. */
    set_mode(&a,FX); bool enabled=a.engine.s.enabled;
    gesture_frame(&a,fds[1],31.5,0,0,0,1);
    gesture_frame(&a,fds[1],31.6,0,0,1,1);
    gesture_frame(&a,fds[1],31.7,0,0,1,0);
    gesture_frame(&a,fds[1],31.8,0,0,0,0);
    assert(a.engine.s.fx_slot==1 && a.engine.s.enabled==enabled && !a.engine.s.enabled2);
    gesture_frame(&a,fds[1],31.9,0,0,1,1);
    gesture_frame(&a,fds[1],32,0,0,0,1);
    gesture_frame(&a,fds[1],32.1,0,0,0,0); assert(a.engine.s.fx_slot==0);
    set_mode(&a,BASS);
    gesture_frame(&a,fds[1],32.2,0,0,0,1);
    gesture_frame(&a,fds[1],32.3,0,1023,0,1); assert(a.engine.s.octave==4);
    gesture_frame(&a,fds[1],32.4,0,1023,0,0); assert(a.engine.s.octave==4);
    float before=a.engine.s.cutoff;
    set_mode(&a,FILES); gesture_frame(&a,fds[1],32.5,0,0,0,0);
    gesture_frame(&a,fds[1],32.6,1023,1023,0,0);
    assert(a.engine.s.drum==8 && a.engine.s.cutoff==before);
    /* NAV selects a menu without changing sound/FX; physical short OK confirms. */
    set_mode(&a,SEQ); nav(&a,1,true);
    int bass_before=a.engine.s.bass;
    control_frame(&a,fds[1],32.7,0,0,0,0);
    control_frame(&a,fds[1],32.8,512,1023,1023,0);
    assert(a.menu_open && a.menu_selected==FILES && a.engine.s.mode==SEQ);
    assert(a.engine.s.bass==bass_before && a.engine.s.cutoff==before);
    control_frame(&a,fds[1],32.9,512,1023,1023,1);
    control_frame(&a,fds[1],33,512,1023,1023,0);
    assert(!a.menu_open && a.engine.s.mode==FILES);
    /* Navigating away discards an unfinished audition request. */
    file_nav(&a,1,false); file_nav(&a,2,false); toggle_preview(&a);
    update_preview(&a,monotonic()+1); file_nav(&a,0,false);
    loader_join(&a.preview); update_preview(&a,monotonic()+1);
    assert(!a.engine.audition_voice.sample && !a.preview_valid);
    loader_join(&a.preview); free(a.preview.result.data);
    arduino_close(&a.arduino); close(fds[1]); engine_free(&a.engine);
    pthread_mutex_destroy(&a.mutex); pthread_mutex_destroy(&a.preview.mutex); pthread_mutex_destroy(&a.loader.mutex);
    unlink(path); rmdir(folder); rmdir(root);
}
static void serial_effect_confirmation(void) {
    App a={0}; a.no_save=true;
    assert(engine_init(&a.engine)); assert(!pthread_mutex_init(&a.mutex,NULL));
    arduino_init(&a.arduino,"simulated"); int fds[2]; assert(!pipe(fds));
    assert(fcntl(fds[0],F_SETFL,O_NONBLOCK)==0); a.arduino.fd=fds[0];
    set_mode(&a,FX); a.engine.s.enabled=false;
    control_frame(&a,fds[1],10,0,665,205,0);
    control_frame(&a,fds[1],10.1,1023,665,205,0);
    assert(a.engine.s.fx_pending[0]==10 && a.engine.s.effect==0 && !a.engine.s.enabled);
    float cutoff=a.engine.s.cutoff, resonance=a.engine.s.resonance;
    control_frame(&a,fds[1],10.2,1023,0,1023,0);
    assert(a.engine.s.cutoff==cutoff && a.engine.s.resonance==resonance);
    control_frame(&a,fds[1],10.3,1023,0,1023,1);
    control_frame(&a,fds[1],10.4,1023,0,1023,0);
    assert(a.engine.s.effect==10 && a.engine.s.enabled && a.engine.s.fx_pending[0]==-1);
    assert(a.engine.s.cutoff==cutoff && a.engine.s.resonance==resonance);
    control_frame(&a,fds[1],10.5,0,0,1023,0);
    assert(a.engine.s.fx_pending[0]==0 && a.engine.s.effect==10 && a.engine.s.enabled);
    control_frame(&a,fds[1],10.6,0,0,1023,1);
    control_frame(&a,fds[1],10.7,0,0,1023,0);
    assert(a.engine.s.effect==0 && !a.engine.s.enabled);
    a.engine.s.fx_slot=1;
    control_frame(&a,fds[1],10.8,0,0,1023,0);
    control_frame(&a,fds[1],10.9,512,0,1023,0);
    assert(a.engine.s.fx_pending[1]==5 && a.engine.s.effect2==0 && !a.engine.s.enabled2);
    control_frame(&a,fds[1],11,512,0,1023,1);
    control_frame(&a,fds[1],11.1,512,0,1023,0);
    assert(a.engine.s.effect2==5 && a.engine.s.enabled2 && !a.engine.s.enabled);
    engine_fx_select(&a.engine,3); set_mode(&a,SEQ);
    assert(a.engine.s.fx_pending[1]==-1 && a.engine.s.effect2==5 && a.engine.s.enabled2);
    arduino_close(&a.arduino); close(fds[1]); engine_free(&a.engine); pthread_mutex_destroy(&a.mutex);
}
int main(void) {
    App a={0}; a.no_save=true;
    assert(engine_init(&a.engine)); assert(!pthread_mutex_init(&a.mutex,NULL));
    arduino_init(&a.arduino,"simulated");
    int pipefd[2]; assert(!pipe(pipefd));
    assert(fcntl(pipefd[0],F_SETFL,O_NONBLOCK)==0); a.arduino.fd=pipefd[0];
    a.engine.s.playing=false;
    frame(&a,pipefd[1],100); assert(a.engine.s.controller);
    /* Upper pads sound normally; in FX+FN they select the three new punches. */
    for (int mode=0;mode<SONG;mode++) for (int i=0;i<3;i++) {
        memset(a.engine.voices,0,sizeof(a.engine.voices));
        a.engine.s.mode=mode; a.engine.s.fn=true;
        bool previous_mute=a.engine.s.scenes[a.engine.s.edit_scene].muted[i];
        buttons(&a,1u<<i,101+mode*10+i*2);
        if (mode==FX) {
            assert(!a.engine.voices[0].sample && engine_punch(&a.engine)==BASE_PADS+i);
        } else if (mode==SEQ || mode==BASS || mode==DRUM) {
            assert(!a.engine.voices[0].sample && a.engine.s.scenes[a.engine.s.edit_scene].muted[i]!=previous_mute);
        } else assert(a.engine.voices[0].sample==&a.engine.drums[i]);
        assert(a.engine.s.mode==mode);
        assert(poll_buttons(&a,102+mode*10+i*2)); /* Holding never navigates. */
        assert(a.engine.s.mode==mode);
        buttons(&a,0,102.1+mode*10+i*2);
        assert(a.engine.s.mode==mode && engine_punch(&a.engine)==-1);
    }
    a.engine.s.mode=FX; a.engine.s.fn=false;
    for (int i=0;i<3;i++) {
        memset(a.engine.voices,0,sizeof(a.engine.voices));
        buttons(&a,1u<<i,180+i*2);
        assert(a.engine.voices[0].sample==&a.engine.drums[i] && engine_punch(&a.engine)==-1);
        buttons(&a,0,181+i*2);
    }
    /* A serial timeout restores controls and cancels an upper sound still held. */
    a.engine.s.mode=DRUM; a.engine.s.volume=.75f;
    frame(&a,pipefd[1],200);
    buttons(&a,1,200.05);
    poll_arduino(&a,200.5); assert(!a.engine.s.controller && !a.engine.s.fn);
    buttons(&a,0,200.6); assert(a.engine.s.volume==.75f);
    buttons(&a,1,201); buttons(&a,0,201.1);
    assert(fabsf(a.engine.s.volume-.70f)<1e-6f);
    /* Connecting while OK is held cannot confirm or change mode. */
    buttons(&a,2,202); frame(&a,pipefd[1],202.1);
    assert(poll_buttons(&a,203)); buttons(&a,0,203.1);
    assert(a.engine.s.mode==DRUM && !a.engine.s.backing);
    poll_arduino(&a,204); assert(!a.engine.s.controller);
    buttons(&a,2,205); assert(poll_buttons(&a,205.8)); buttons(&a,0,206);
    assert(a.menu_open && a.engine.s.mode==DRUM); /* Offline long OK opens the menu. */
    for (int i=0;i<4;i++) nav(&a,2,false);
    assert(a.menu_selected==MENU_BACK); nav(&a,1,false);
    for (int i=0;i<3;i++) nav(&a,2,false);
    nav(&a,1,false); assert(a.engine.s.mode==FILES);
    arduino_close(&a.arduino); close(pipefd[1]);
    engine_free(&a.engine); pthread_mutex_destroy(&a.mutex);
    pattern_and_menu_controls(); pages_and_mutes(); compact_menu_and_mutes(); sound_knobs(); files_and_gestures(); serial_effect_confirmation();
    puts("controls: pattern knobs, long OK through every menu, bounce, pickup, nine sounds, upper pads in all modes, timeout and held-button transitions OK");
    return 0;
}
