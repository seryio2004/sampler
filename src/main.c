#define _POSIX_C_SOURCE 200809L
#include "core.h"
#include "files.h"
#include "hardware.h"
#include "arduino.h"
#include <alsa/asoundlib.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <mpg123.h>
#include <poll.h>
#include <signal.h>
#include <sndfile.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct { bool raw, stable, long_sent, sound, suppressed, modifier; double changed, down; } Button;
typedef struct {
    Engine engine;
    pthread_mutex_t mutex;
    pthread_t audio_thread;
    atomic_bool running;
    atomic_int audio_error;
    atomic_uint xruns;
    uint64_t played_frames; /* Protected by mutex, including on 32-bit Raspberry Pi OS. */
    uint64_t written_frames;
    double playback_at;
    snd_pcm_t *pcm;
    size_t audio_block;
    double max_render_ms, max_lock_ms;
    bool audio_started, gpio, no_save, dirty;
    Hardware hw;
    Arduino arduino;
    Button arduino_ok, arduino_fn;
    bool fn_used, menu_open, audition_pending;
    int menu_selected;
    int song_action, song_field, bank_action, bank_target, name_cursor, name_choice;
    bool delete_confirm, clear_confirm, name_edit;
    char name_draft[SCENE_NAME];
    double connection_until;
    int knob_context; /* 0: FX parameters, 1: pattern sound selection. */
    Browser browser;
    Loader loader, preview;
    char preview_path[PATH_MAX];
    uint8_t preview_waveform[WAVE_COLUMNS];
    bool preview_requested, preview_ready, preview_valid;
    unsigned preview_generation, preview_job_generation;
    double preview_at;
    Button buttons[9];
    char assignments[PADS][PATH_MAX], store[PATH_MAX];
    char message[128];
    double message_until, save_at;
} App;
static const char *mode_names[]={"SEQ","FX","BPM","BASS","DRUM","FILES","SONG","LIVE","BANK"};
static const char *menu_labels[]={"PATRONES","EFECTOS","TEMPO","BAJO","PADS","SAMPLES","CANCION","LANZAR","ESCENAS"};
static const char *song_actions[]={"EDITAR","INSERTAR","BORRAR","BUCLE"};
static const char *bank_actions[]={"EDITAR","COPIAR","VACIAR","NOMBRE","ESCUCHAR"};
static const char name_chars[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
static volatile sig_atomic_t quit;
static void signal_exit(int n) { (void)n; quit=1; }
static double monotonic(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9;
}
static int audio_mutex_init(pthread_mutex_t *mutex) {
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr)==0) {
        int result=pthread_mutexattr_setprotocol(&attr,PTHREAD_PRIO_INHERIT);
        if (!result) result=pthread_mutex_init(mutex,&attr);
        pthread_mutexattr_destroy(&attr);
        if (!result) return 0;
    }
    return pthread_mutex_init(mutex,NULL);
}
static void recovery_fade(int16_t *data, size_t frames, unsigned *left) {
    const unsigned fade=RATE/200; /* 5 ms after an actual device interruption. */
    for (size_t i=0;i<frames && *left;i++,(*left)--) {
        float gain=1-(float)*left/fade;
        data[2*i]=(int16_t)(data[2*i]*gain); data[2*i+1]=(int16_t)(data[2*i+1]*gain);
    }
}
static void *audio_worker(void *arg) {
    App *a=arg; int16_t buffer[2048*2]; unsigned fade_left=0;
    uint64_t written=0;
    while (atomic_load(&a->running)) {
        double before=monotonic();
        pthread_mutex_lock(&a->mutex);
        double locked=monotonic();
        engine_render(&a->engine,buffer,a->audio_block);
        pthread_mutex_unlock(&a->mutex);
        double rendered=monotonic();
        if ((locked-before)*1000>a->max_lock_ms) a->max_lock_ms=(locked-before)*1000;
        if ((rendered-locked)*1000>a->max_render_ms) a->max_render_ms=(rendered-locked)*1000;
        recovery_fade(buffer,a->audio_block,&fade_left);
        size_t sent=0;
        while (sent<a->audio_block && atomic_load(&a->running)) {
            snd_pcm_sframes_t n=snd_pcm_writei(a->pcm,buffer+sent*2,a->audio_block-sent);
            if (n==-EINTR) continue;
            if (n==-EAGAIN) { snd_pcm_wait(a->pcm,10); continue; }
            if (n<0) {
                if (n==-EPIPE || n==-ESTRPIPE) {
                    atomic_fetch_add(&a->xruns,1); fade_left=RATE/200;
                }
                /* Nonblocking recovery; suspend retries remain interruptible. */
                if (n==-ESTRPIPE) n=snd_pcm_prepare(a->pcm);
                else n=snd_pcm_recover(a->pcm,(int)n,1);
                if (n<0) { atomic_store(&a->audio_error,(int)n); atomic_store(&a->running,false); break; }
                recovery_fade(buffer+sent*2,a->audio_block-sent,&fade_left);
            } else if (n==0) {
                struct timespec delay={0,1000000}; nanosleep(&delay,NULL);
            } else {
                sent+=(size_t)n; written+=(uint64_t)n;
                snd_pcm_sframes_t delay=0;
                /* Visuals follow frames delivered to the device, not render-ahead state. */
                if (snd_pcm_delay(a->pcm,&delay)>=0) {
                    uint64_t pending=delay>0?(uint64_t)delay:0;
                    double observed=monotonic();
                    bool advancing=snd_pcm_state(a->pcm)==SND_PCM_STATE_RUNNING;
                    pthread_mutex_lock(&a->mutex);
                    a->played_frames=written>pending?written-pending:0;
                    a->written_frames=written;
                    a->playback_at=advancing?observed+(pending>written?(pending-written)/(double)RATE:0):0;
                    pthread_mutex_unlock(&a->mutex);
                }
            }
        }
    }
    return NULL;
}
static void notify(App *a, const char *text) {
    snprintf(a->message,sizeof(a->message),"%s",text);
    a->message_until=monotonic()+2.5; printf("%s\n",text);
}
static void mark_dirty(App *a) {
    if (!a->no_save) { a->dirty=true; a->save_at=monotonic()+1.5; }
}
static State snapshot(App *a) {
    pthread_mutex_lock(&a->mutex); State s=a->engine.s; pthread_mutex_unlock(&a->mutex); return s;
}
static State visual_snapshot(App *a, AudioVisual *visual) {
    pthread_mutex_lock(&a->mutex);
    State s=a->engine.s;
    uint64_t played=a->played_frames;
    double elapsed=a->playback_at>0?monotonic()-a->playback_at:0;
    if (elapsed>0) {
        /* Interpolate between ALSA reports, even with long device periods.
           Never show frames which have not been accepted by the device. */
        double advance=elapsed*RATE;
        uint64_t left=a->written_frames-played;
        played+=advance>=left?left:(uint64_t)advance;
    }
    *visual=engine_visual_at(&a->engine,played);
    pthread_mutex_unlock(&a->mutex);
    s.playhead=visual->playhead;
    if (visual->scene>=0) { s.active_scene=visual->scene; s.song_position=visual->song_position; s.song_left=visual->song_left; }
    return s;
}
static bool save(App *a) {
    if (a->no_save || !a->dirty) return true;
    State s=snapshot(a);
    if (!state_save(a->store,&s,a->assignments)) {
        fprintf(stderr,"No se pudo guardar %s: %s\n",a->store,strerror(errno));
        a->save_at=monotonic()+5; return false;
    }
    a->dirty=false; return true;
}
static void list_files(App *a) {
    Browser *b=&a->browser;
    printf("Carpeta: %s\n",b->path);
    for (int i=0;i<b->count;i++) printf("%c %3d %s%s\n",i==b->selected?'>':' ',i+1,
        b->entries[i].name,b->entries[i].directory?"/":"");
    if (!b->count) puts("(sin archivos WAV/MP3 ni carpetas)");
    if (b->truncated) puts("Limite: 1024 entradas. Divide los archivos en subcarpetas.");
}
static void reset_preview(App *a) {
    pthread_mutex_lock(&a->mutex); engine_audition(&a->engine,false); pthread_mutex_unlock(&a->mutex);
    a->audition_pending=false;
    a->preview_generation++;
    a->preview_requested=a->preview_ready=a->preview_valid=false;
    a->preview_path[0]=0; a->preview_at=monotonic()+.25;
}
static void update_preview(App *a, double now) {
    Loader *l=&a->preview;
    if (l->busy) {
        pthread_mutex_lock(&l->mutex); bool done=l->done; pthread_mutex_unlock(&l->mutex);
        if (done) {
            loader_join(l); l->busy=false;
            /* Ignore work for a file that the user has already moved away from. */
            if (a->preview_job_generation==a->preview_generation) {
                a->preview_ready=true; a->preview_valid=l->success;
                memcpy(a->preview_waveform,l->waveform,WAVE_COLUMNS);
                pthread_mutex_lock(&a->mutex);
                Sample old=engine_audition_replace(&a->engine,l->result);
                l->result=(Sample){0};
                if (a->audition_pending && l->success) engine_audition(&a->engine,true);
                pthread_mutex_unlock(&a->mutex);
                a->audition_pending=false; free(old.data);
            }
            free(l->result.data); l->result=(Sample){0};
        }
    }
    if (a->menu_open || snapshot(a).mode!=FILES || a->loader.busy || l->busy || a->preview_requested || now<a->preview_at) return;
    a->preview_requested=true;
    if (!browser_file(&a->browser,a->preview_path)) return;
    a->preview_job_generation=a->preview_generation;
    if (!loader_preview_start(l,a->preview_path)) a->preview_ready=true;
}
static void file_nav(App *a, int key, bool held) {
    Browser *b=&a->browser;
    if (held) {
        if (key==0) { if (!browser_parent(b)) notify(a,"Carpeta raiz"); }
        if (key==2 && !browser_scan(b)) notify(a,"Error al leer");
        reset_preview(a);
        return;
    }
    if (key!=1) {
        if (b->count) b->selected=(b->selected+(key==0?-1:1)+b->count)%b->count;
        reset_preview(a);
        return;
    }
    if (!b->count) { notify(a,"Carpeta vacia"); return; }
    if (b->entries[b->selected].directory) {
        if (!browser_enter(b)) notify(a,"No se puede abrir");
        reset_preview(a);
        return;
    }
    char path[PATH_MAX];
    if (!browser_file(b,path)) { notify(a,"Archivo no valido"); return; }
    State s=snapshot(a);
    if (!loader_start(&a->loader,path,s.drum)) { notify(a,"Carga ocupada/error"); return; }
    notify(a,"Cargando...");
}
static void toggle_preview(App *a) {
    char path[PATH_MAX];
    if (!browser_file(&a->browser,path)) { notify(a,"ELIGE UN WAV/MP3"); return; }
    pthread_mutex_lock(&a->mutex);
    bool playing=a->engine.audition_voice.sample!=NULL;
    if (playing) engine_audition(&a->engine,false);
    else if (a->preview_ready && a->preview_valid) engine_audition(&a->engine,true);
    pthread_mutex_unlock(&a->mutex);
    if (playing || a->audition_pending) { a->audition_pending=false; notify(a,"ESCUCHA OFF"); }
    else if (a->preview_ready && !a->preview_valid) notify(a,"ERROR DE MUESTRA");
    else { a->audition_pending=!a->preview_ready; a->preview_at=0; notify(a,"ESCUCHA ON"); }
}
static void set_mode(App *a, int mode) {
    if (mode<0 || mode>=MODES) return;
    pthread_mutex_lock(&a->mutex);
    engine_fn(&a->engine,false);
    if (a->engine.s.mode==FX && mode!=FX)
        for (int i=0;i<FX_SLOTS;i++) a->engine.s.fx_pending[i]=-1;
    a->engine.s.mode=mode;
    if (mode==LIVE) a->engine.s.live_selected=a->engine.s.queued_scene>=0?a->engine.s.queued_scene:a->engine.s.active_scene;
    pthread_mutex_unlock(&a->mutex);
    a->name_edit=a->clear_confirm=a->delete_confirm=false; a->song_action=a->song_field=a->bank_action=0;
    a->menu_open=false; reset_preview(a); pot_release(&a->arduino.pot);
    pot_release(&a->arduino.cutoff); pot_release(&a->arduino.resonance); a->knob_context=-1;
    printf("Modo: %s\n",mode_names[mode]);
    if (mode==FILES) { browser_scan(&a->browser); list_files(a); }
}
static int name_index(char c) { const char *at=strchr(name_chars,c); return at?(int)(at-name_chars):0; }
static void bank_nav(App *a, int key, bool held) {
    State s=snapshot(a); int d=key==0?-1:1;
    if (a->name_edit) {
        if (key==1) {
            char text[SCENE_NAME]; memcpy(text,a->name_draft,SCENE_NAME);
            int last=SCENE_NAME-2; while (last>=0 && text[last]==' ') text[last--]=0;
            pthread_mutex_lock(&a->mutex); bool ok=engine_scene_name(&a->engine,s.edit_scene,text); pthread_mutex_unlock(&a->mutex);
            if (ok) { a->name_edit=false; mark_dirty(a); notify(a,"NOMBRE GUARDADO"); }
            else notify(a,"NOMBRE VACIO");
        } else if (held) { a->name_cursor=(a->name_cursor+d+SCENE_NAME-1)%(SCENE_NAME-1); a->name_choice=name_index(a->name_draft[a->name_cursor]); }
        else { a->name_choice=(a->name_choice+d+(int)sizeof(name_chars)-1)%((int)sizeof(name_chars)-1); a->name_draft[a->name_cursor]=name_chars[a->name_choice]; }
        return;
    }
    if (key!=1) {
        a->clear_confirm=false;
        if (held) a->bank_action=(a->bank_action+d+5)%5;
        else if (a->bank_action==1) a->bank_target=(a->bank_target+d+SCENES)%SCENES;
        else { pthread_mutex_lock(&a->mutex); engine_scene_select(&a->engine,(s.edit_scene+d+SCENES)%SCENES); pthread_mutex_unlock(&a->mutex); }
        return;
    }
    if (a->bank_action==0) { set_mode(a,SEQ); return; }
    if (a->bank_action==3) {
        memset(a->name_draft,' ',SCENE_NAME-1); a->name_draft[SCENE_NAME-1]=0;
        size_t n=strlen(s.scenes[s.edit_scene].name); memcpy(a->name_draft,s.scenes[s.edit_scene].name,n);
        a->name_cursor=0; a->name_choice=name_index(a->name_draft[0]); a->name_edit=true;
        pot_release(&a->arduino.pot); pot_release(&a->arduino.cutoff); return;
    }
    if (a->bank_action==2 && !a->clear_confirm) { a->clear_confirm=true; notify(a,"OK: VACIAR ESC"); return; }
    pthread_mutex_lock(&a->mutex);
    if (a->bank_action==1) engine_scene_copy(&a->engine,s.edit_scene,a->bank_target);
    if (a->bank_action==2) engine_scene_clear(&a->engine,s.edit_scene);
    if (a->bank_action==4) engine_scene_launch(&a->engine,s.edit_scene);
    pthread_mutex_unlock(&a->mutex);
    a->clear_confirm=false; mark_dirty(a); notify(a,a->bank_action==1?"ESCENA COPIADA":a->bank_action==2?"ESCENA VACIA":"ESCENA PREPARADA");
}
static void song_nav(App *a, int key, bool held) {
    State s=snapshot(a); int d=key==0?-1:1;
    if (key!=1) {
        a->delete_confirm=false;
        if (held) a->song_field=(a->song_field+d+4)%4;
        else if (a->song_field==3) a->song_action=(a->song_action+d+4)%4;
        else {
            pthread_mutex_lock(&a->mutex); State *live=&a->engine.s;
            if (a->song_field==0) {
                int count=live->song_count+(live->song_count<SONG_BLOCKS?1:0);
                state_song_cursor(live,(live->song_cursor+d+count)%count);
            } else if (a->song_field==1) { live->song_choice=(live->song_choice+d+SCENES)%SCENES; live->song_pending=true; }
            else { live->song_times=live->song_times+d; if (live->song_times<1) live->song_times=1; if (live->song_times>SONG_REPEATS) live->song_times=SONG_REPEATS; live->song_pending=true; }
            pthread_mutex_unlock(&a->mutex);
        }
        return;
    }
    if (a->song_action==2 && !a->delete_confirm) { a->delete_confirm=true; notify(a,"OK: BORRAR BLOQ"); return; }
    pthread_mutex_lock(&a->mutex); bool changed=false, success=true;
    if (a->song_action==3) { a->engine.s.song_loop=!s.song_loop; changed=true; }
    else if (a->song_action==2) { success=engine_song_delete(&a->engine); changed=success; }
    else if (a->song_action==1 || s.song_pending || s.song_cursor==s.song_count) { success=engine_song_commit(&a->engine,a->song_action==1); changed=success; }
    else engine_song_toggle(&a->engine);
    pthread_mutex_unlock(&a->mutex);
    a->delete_confirm=false; a->song_action=0;
    if (changed) mark_dirty(a);
    if (!success) notify(a,"BLOQUE NO VALIDO");
}
static void nav(App *a, int key, bool held) {
    pot_release(&a->arduino.pot);
    State before=snapshot(a);
    if (held && key==1) {
        a->fn_used=true; a->name_edit=a->clear_confirm=a->delete_confirm=false;
        if (a->menu_open) a->menu_open=false;
        else {
            a->menu_open=true; a->menu_selected=before.mode; reset_preview(a);
            pthread_mutex_lock(&a->mutex); engine_fn(&a->engine,false); pthread_mutex_unlock(&a->mutex);
        }
        a->message_until=0; return;
    }
    if (a->menu_open) {
        if (key==1) set_mode(a,a->menu_selected);
        else a->menu_selected=(a->menu_selected+(key==0?-1:1)+MODES)%MODES;
        return;
    }
    if (before.controller && before.fn) a->fn_used=true;
    if (before.mode==BANK) { bank_nav(a,key,held); return; }
    if (before.mode==SONG) { song_nav(a,key,held); return; }
    if (before.mode==LIVE) {
        pthread_mutex_lock(&a->mutex);
        if (key!=1) a->engine.s.live_selected=(before.live_selected+(key==0?-1:1)+SCENES)%SCENES;
        else if (before.live_selected!=before.active_scene && before.queued_scene!=before.live_selected) engine_scene_launch(&a->engine,before.live_selected);
        else engine_live_toggle(&a->engine);
        pthread_mutex_unlock(&a->mutex); return;
    }
    if (before.mode==FILES) {
        if (held && key==2) toggle_preview(a);
        else file_nav(a,key,held);
        return;
    }
    if (before.mode==FX && before.controller && before.fn && key==1 && !held) {
        pthread_mutex_lock(&a->mutex); a->engine.s.fx_slot^=1; pthread_mutex_unlock(&a->mutex);
        pot_release(&a->arduino.cutoff); pot_release(&a->arduino.resonance);
        return;
    }
    pthread_mutex_lock(&a->mutex); engine_nav(&a->engine,key,held); pthread_mutex_unlock(&a->mutex);
    if (before.mode==FX && key==1 && !held && before.fx_pending[before.fx_slot]>=0) {
        pot_release(&a->arduino.cutoff); pot_release(&a->arduino.resonance);
    }
    mark_dirty(a);
}
static void pad(App *a, int p, bool down) {
    pthread_mutex_lock(&a->mutex);
    if (down && a->engine.s.fn) a->fn_used=true;
    if (down && a->engine.s.mode==BANK && a->bank_action==1) { a->bank_target=p; a->clear_confirm=false; }
    else if (!(a->engine.s.mode==BANK && a->name_edit)) engine_pad(&a->engine,p,down);
    if (down) a->clear_confirm=a->delete_confirm=false;
    pthread_mutex_unlock(&a->mutex);
}
static void poll_arduino(App *a, double now) {
    int old_fd=a->arduino.fd, old_error=a->arduino.last_error;
    bool fresh=arduino_poll(&a->arduino,now);
    if (old_fd<0 && a->arduino.fd>=0)
        fprintf(stderr,"Arduino conectado: %s.\n",a->arduino.path);
    if (a->arduino.last_error && (old_fd>=0 || old_error!=a->arduino.last_error))
        fprintf(stderr,"Arduino %s: %s; reintento en 2 s.\n",a->arduino.path,strerror(a->arduino.last_error));
    Arduino *control=&a->arduino;
    bool online=control->controls && now-control->controls_at<=.30 && control->fd>=0;
    pthread_mutex_lock(&a->mutex);
    bool changed=online!=a->engine.s.controller;
    if (changed) {
        engine_fn(&a->engine,false); a->engine.s.controller=online;
        for (int i=0;i<3;i++) {
            a->buttons[i].suppressed=a->buttons[i].raw || a->buttons[i].stable;
            a->buttons[i].long_sent=true;
        }
    }
    if (online) engine_fn(&a->engine,control->fn);
    pthread_mutex_unlock(&a->mutex);
    if (changed) {
        a->connection_until=now+2;
        notify(a,online?"ARDUINO ON: 9PAD":"OFF: CONTROLES");
    }
    Button *fn=&a->arduino_fn;
    if (!online || control->cancelled) {
        fn->suppressed|=fn->stable || control->fn; fn->stable=false; a->fn_used=true;
    } else if (fresh) {
        if (fn->suppressed) { if (!control->fn) fn->suppressed=false; }
        else if (control->fn && !fn->stable) {
            fn->stable=true; fn->down=now; fn->long_sent=false; a->fn_used=false;
        } else if (!control->fn && fn->stable) {
            if (!fn->long_sent && !a->fn_used && !a->menu_open) {
                int mode=snapshot(a).mode;
                if (mode==FILES) toggle_preview(a);
                else if (mode==SONG) { pthread_mutex_lock(&a->mutex); engine_song_toggle(&a->engine); pthread_mutex_unlock(&a->mutex); }
                else if (mode==BANK) a->name_edit=a->clear_confirm=false;
            }
            fn->stable=false;
        }
    }
    if (fn->stable && !fn->long_sent && !a->fn_used && now-fn->down>=.65 && snapshot(a).mode==FILES && !a->menu_open) {
        fn->long_sent=true; file_nav(a,0,true);
    }
    Button *ok=&a->arduino_ok;
    if (!online || control->cancelled) {
        ok->suppressed|=ok->stable || control->ok; ok->stable=false;
    } else if (fresh) {
        if (ok->suppressed) { if (!control->ok) ok->suppressed=false; }
        else {
            if (!ok->stable && (control->ok || (control->button_down&1))) {
                ok->stable=true; ok->down=now; ok->long_sent=false; ok->modifier=control->fn;
                if (control->fn) a->fn_used=true;
            }
            if (ok->stable && !control->ok) {
                if (!ok->long_sent) {
                    bool held=now-ok->down>=.65;
                    if (!held && ok->modifier && snapshot(a).mode==FX && !a->menu_open) {
                        pthread_mutex_lock(&a->mutex); a->engine.s.fx_slot^=1; pthread_mutex_unlock(&a->mutex);
                        pot_release(&control->pot); pot_release(&control->cutoff); pot_release(&control->resonance);
                    } else nav(a,1,held);
                }
                ok->stable=false;
            }
        }
    }
    if (ok->stable && !ok->long_sent && now-ok->down>=.65) {
        ok->long_sent=true; nav(a,1,true);
    }
    if (!fresh) return;
    State s=snapshot(a);
    if (a->menu_open) {
        int target=pot_select(&control->pot,200,a->menu_selected,MODES,now);
        if (target>=0) a->menu_selected=target;
        return;
    }
    if (s.mode==SONG) {
        if (s.fn) {
            int action=pot_select(&control->pot,410,a->song_action,4,now);
            if (action>=0) { a->song_action=action; a->delete_confirm=false; a->fn_used=true; }
            return;
        }
        int count=s.song_count+(s.song_count<SONG_BLOCKS?1:0);
        int at=pot_select(&control->pot,411,s.song_cursor,count,now);
        if (at>=0) {
            pthread_mutex_lock(&a->mutex); state_song_cursor(&a->engine.s,at); pthread_mutex_unlock(&a->mutex);
            pot_release(&control->cutoff); pot_release(&control->resonance); a->delete_confirm=false; s=snapshot(a);
        }
        if (online) {
            int scene=pot_select(&control->cutoff,412,s.song_choice,SCENES,now);
            int times=pot_select(&control->resonance,413,s.song_times-1,SONG_REPEATS,now);
            if (scene>=0 || times>=0) {
                pthread_mutex_lock(&a->mutex);
                if (scene>=0) a->engine.s.song_choice=scene;
                if (times>=0) a->engine.s.song_times=times+1;
                a->engine.s.song_pending=true; pthread_mutex_unlock(&a->mutex); a->delete_confirm=false;
            }
        }
        return;
    }
    if (s.mode==LIVE) {
        int scene=pot_select(&control->pot,420,s.live_selected,SCENES,now);
        if (scene>=0) { pthread_mutex_lock(&a->mutex); a->engine.s.live_selected=scene; pthread_mutex_unlock(&a->mutex); }
        return;
    }
    if (s.mode==BANK) {
        if (a->name_edit) {
            int at=pot_select(&control->pot,430,a->name_cursor,SCENE_NAME-1,now);
            if (at>=0) { a->name_cursor=at; a->name_choice=name_index(a->name_draft[at]); pot_release(&control->cutoff); }
            if (online) {
                int ch=pot_select(&control->cutoff,431,a->name_choice,(int)sizeof(name_chars)-1,now);
                if (ch>=0) { a->name_choice=ch; a->name_draft[a->name_cursor]=name_chars[ch]; }
            }
        } else {
            int scene=pot_select(&control->pot,432,s.edit_scene,SCENES,now);
            if (scene>=0) { pthread_mutex_lock(&a->mutex); engine_scene_select(&a->engine,scene); pthread_mutex_unlock(&a->mutex); a->clear_confirm=false; }
            if (online) {
                int action=pot_select(&control->cutoff,433,a->bank_action,5,now);
                if (action>=0) { a->bank_action=action; a->clear_confirm=false; }
                if (a->bank_action==1) {
                    int target=pot_select(&control->resonance,434,a->bank_target,SCENES,now);
                    if (target>=0) a->bank_target=target;
                }
            }
        }
        return;
    }
    if ((s.mode==SEQ || s.mode==BASS) && s.fn) {
        int scene=pot_select(&control->pot,440,s.edit_scene,SCENES,now);
        if (scene>=0) { pthread_mutex_lock(&a->mutex); engine_scene_select(&a->engine,scene); pthread_mutex_unlock(&a->mutex); a->fn_used=true; mark_dirty(a); }
    }
    if (online) {
        int context=(s.mode==SEQ || s.mode==BASS)?(s.mode==BASS && s.fn?2:1):s.mode==FX?10+s.fx_slot:s.mode==FILES?20:30;
        if (context!=a->knob_context) {
            pot_release(&control->cutoff); pot_release(&control->resonance); a->knob_context=context;
        }
        if (context==1 || context==2 || context==20) {
            int drum=pot_select(&control->cutoff,100,s.drum,PADS,now);
            int note=context==20?-1:pot_select(&control->resonance,context,context==2?s.octave-1:s.bass,context==2?4:NOTES,now);
            pthread_mutex_lock(&a->mutex);
            if (drum>=0) a->engine.s.drum=drum;
            if (note>=0) {
                if (context==2) { a->engine.s.octave=note+1; a->fn_used=true; }
                else a->engine.s.bass=note;
            }
            pthread_mutex_unlock(&a->mutex);
            if (s.fn && (drum>=0 || note>=0)) a->fn_used=true;
            if (context==2 && note>=0) mark_dirty(a);
        } else if (s.mode==FX && s.fx_pending[s.fx_slot]<0) {
            State selected=state_effect(&s,s.fx_slot);
            int c=pot_parameter(&control->cutoff,(int)lroundf(selected.cutoff*1023),now);
            int r=pot_parameter(&control->resonance,(int)lroundf(selected.resonance*1023),now);
            if (c>=0 || r>=0) {
                pthread_mutex_lock(&a->mutex);
                float *ca=s.fx_slot?&a->engine.s.cutoff2:&a->engine.s.cutoff;
                float *ra=s.fx_slot?&a->engine.s.resonance2:&a->engine.s.resonance;
                if (c>=0) *ca=c/1023.f;
                if (r>=0) *ra=r/1023.f;
                pthread_mutex_unlock(&a->mutex); mark_dirty(a);
            }
        }
    }
    int current=0, count=1;
    switch (s.mode) {
    case SEQ: case BASS: if (s.fn) return; current=s.cursor; count=STEPS; break;
    case FX: current=state_fx_choice(&s,s.fx_slot); count=FX_COUNT; break;
    case BPM: current=s.bpm-60; count=121; break;
    case DRUM: {
        int value=pot_parameter(&control->pot,(int)lroundf(s.volume*1023),now);
        if (value>=0) {
            pthread_mutex_lock(&a->mutex); a->engine.s.volume=value/1023.f; pthread_mutex_unlock(&a->mutex); mark_dirty(a);
        }
        return;
    }
    case FILES: current=a->browser.selected; count=a->browser.count; break;
    }
    if (current<0) current=0;
    if (current>=count && count>0) current=count-1;
    int target=s.mode==BPM?pot_target(&control->pot,s.mode,current,count,now):
        pot_select(&control->pot,s.mode==FX?50+s.fx_slot:s.mode,current,count,now);
    if (target<0) return;
    if (s.fn) a->fn_used=true;
    if (s.mode==FILES) { a->browser.selected=target; reset_preview(a); return; }
    pthread_mutex_lock(&a->mutex);
    State *live=&a->engine.s;
    switch (s.mode) {
    case SEQ: case BASS: live->cursor=target; break;
    case FX: engine_fx_select(&a->engine,target); break;
    case BPM: live->bpm=target+60; break;
    }
    pthread_mutex_unlock(&a->mutex); mark_dirty(a);
}
static bool poll_buttons(App *a, double now) {
    unsigned pressed;
    if (!hardware_buttons(&a->hw,&pressed)) return false;
    bool online=snapshot(a).controller;
    for (int i=0;i<9;i++) {
        Button *b=&a->buttons[i]; bool raw=(pressed&(1u<<i))!=0;
        if (raw!=b->raw) { b->raw=raw; b->changed=now; }
        if (raw!=b->stable && now-b->changed>=.025) {
            b->stable=raw;
            if (raw) {
                b->down=now; b->long_sent=false;
                b->sound=i>=3 || online;
                if (b->sound && !b->suppressed) pad(a,i,true);
            } else {
                if (b->sound) pad(a,i,false);
                else if (!b->long_sent && !b->suppressed) nav(a,i,false);
                b->suppressed=false; b->sound=false;
            }
        }
        if (b->stable && !b->sound && !b->suppressed && !b->long_sent && now-b->down>=.65) {
            b->long_sent=true; nav(a,i,true);
        }
    }
    return true;
}
static void finish_load(App *a) {
    Loader *l=&a->loader;
    if (!l->busy) return;
    pthread_mutex_lock(&l->mutex); bool done=l->done; pthread_mutex_unlock(&l->mutex);
    if (!done) return;
    loader_join(l);
    if (l->result.data) {
        pthread_mutex_lock(&a->mutex);
        Sample old=engine_replace(&a->engine,l->pad,l->result);
        pthread_mutex_unlock(&a->mutex);
        free(old.data); l->result=(Sample){0};
        strcpy(a->assignments[l->pad],l->path); mark_dirty(a);
        char selected[PATH_MAX];
        if (browser_file(&a->browser,selected) && !strcmp(selected,l->path)) {
            reset_preview(a); a->preview_at=0;
        }
        char text[64]; snprintf(text,sizeof(text),"Cargado en PAD %d",l->pad+1); notify(a,text);
    } else {
        fprintf(stderr,"Error cargando %s: %s\n",l->path,l->error); notify(a,"Error de carga");
    }
    l->busy=false;
}
static char pickup_arrow(const PotControl *p, double now) {
    if (!p->received || now-p->seen_at>.30 || !p->hint) return ' ';
    return p->hint<0?'<':'>';
}
static void sample_name(const App *a, int pad, char name[64]) {
    static const char *defaults[]={"BOMBO","CAJA","HAT CER","HAT ABI","PALMAS","TOM","SHAKER","RIM","COWBELL"};
    const char *path=a->assignments[pad];
    const char *base=strrchr(path,'/'); base=base?base+1:path;
    const char *prefix=base; while (*prefix>='0' && *prefix<='9') prefix++;
    if (prefix>base && (*prefix=='_' || *prefix=='-')) base=prefix+1;
    snprintf(name,64,"%s",*base?base:defaults[pad]);
    char *ext=strrchr(name,'.'); if (ext) *ext=0;
    for (char *c=name;*c;c++) { if (*c=='_') *c=' '; if ((unsigned char)*c>=128) *c='?'; }
}
static void screen(App *a, double now) {
    AudioVisual visual; State s=visual_snapshot(a,&visual);
    char top[128]={0}, bottom[128]={0};
    bool waveform=false;
    static const char *fx[]={"OFF","LOWPASS","HIGHPASS","BITCRUSH","DIST","ECHO","STUTTER","TREMOLO","RING MOD","CHORUS","FLANGER"};
    static const char *punch[]={"REP 1/8","REP 1/16","REP 1/32","REVERSE","TAPE STOP","GLITCH","ROLL 1/4","BEAT SLICER","DUB ECHO"};
    static const char *notes[]={"C","D","E","G","A","C+"};
    char label[64]; sample_name(a,s.drum,label);
    if (a->menu_open) {
        snprintf(top,sizeof(top),"MENU: %s",menu_labels[a->menu_selected]);
        snprintf(bottom,sizeof(bottom),s.controller?"NAV ELIGE OK:ENT":"IZQ/DER OK:ENT");
    } else if (s.mode==SEQ || s.mode==BASS) {
        if (s.mode==SEQ) snprintf(top,sizeof(top),"E%d P%d %.4s %02d%c",s.edit_scene+1,s.drum+1,label,s.cursor+1,s.playing?'>':'|');
        else snprintf(top,sizeof(top),"E%d B %-2s%d %02d%c",s.edit_scene+1,notes[s.bass],s.octave,s.cursor+1,s.playing?'>':'|');
        if (s.active_scene!=s.edit_scene && s.playing) snprintf(top,sizeof(top),"EDIT E%d SON E%d",s.edit_scene+1,s.active_scene+1);
        if (s.fn) snprintf(top,sizeof(top),"E%d %.12s",s.edit_scene+1,s.scenes[s.edit_scene].name);
        for (int i=0;i<16;i++) {
            bool active=s.mode==SEQ?s.pattern[s.drum][i]:s.melody[i]>=0;
            char ch=s.mode==SEQ?(active?'X':'.'):(active?(char)('1'+s.melody[i]):'.');
            if (s.playing && s.playhead==i) ch=active?'O':':';
            if (s.cursor==i) ch=active?'*':'^';
            bottom[i]=ch;
        }
        bottom[16]=0;
    } else if (s.mode==SONG) {
        snprintf(top,sizeof(top),"B%02d E%d x%02d %s",s.song_cursor+1,s.song_choice+1,s.song_times,s.song_pending?"*":s.song_loop?"LOOP":"FIN");
        if (a->delete_confirm) snprintf(bottom,sizeof(bottom),"OK: BORRAR BLOQ");
        else if (s.fn || a->song_action) snprintf(bottom,sizeof(bottom),"ACC: %s",song_actions[a->song_action]);
        else snprintf(bottom,sizeof(bottom),"%s B%02d R%02d",s.playing && s.transport==SONG_PLAY?(s.stop_pending?"PARA":"PLAY"):"FN:PLAY",s.song_position+1,s.song_left);
        if (!s.controller) {
            static const char *fields[]={"BLQ","ESC","REP","ACC"};
            snprintf(top,sizeof(top),"%s B%02d E%d x%02d",fields[a->song_field],s.song_cursor+1,s.song_choice+1,s.song_times);
        }
    } else if (s.mode==LIVE) {
        snprintf(top,sizeof(top),"SUENA E%d %s",s.active_scene+1,s.playing?"PLAY":"STOP");
        if (s.stop_pending) snprintf(bottom,sizeof(bottom),"PARA AL COMPAS");
        else if (s.queued_scene>=0) snprintf(bottom,sizeof(bottom),"SIGUE E%d %.7s",s.queued_scene+1,s.scenes[s.queued_scene].name);
        else snprintf(bottom,sizeof(bottom),"SEL E%d %.9s",s.live_selected+1,s.scenes[s.live_selected].name);
    } else if (s.mode==BANK) {
        snprintf(top,sizeof(top),"E%d %.12s",s.edit_scene+1,s.scenes[s.edit_scene].name);
        if (a->name_edit) {
            snprintf(top,sizeof(top),"N%02d %.12s",a->name_cursor+1,a->name_draft);
            snprintf(bottom,sizeof(bottom),"OK:GUARDAR FN:X");
        } else if (a->clear_confirm) snprintf(bottom,sizeof(bottom),"OK: VACIAR ESC");
        else if (a->bank_action==1) snprintf(bottom,sizeof(bottom),"COPIAR E%d > E%d",s.edit_scene+1,a->bank_target+1);
        else snprintf(bottom,sizeof(bottom),"OK: %s",bank_actions[a->bank_action]);
    } else if (s.mode==FX) {
        int slot=s.fx_slot, pending=s.fx_pending[slot];
        bool other=slot?(s.enabled && s.effect):(s.enabled2 && s.effect2);
        s=state_effect(&s,slot);
        const char *name=fx[pending>=0?pending:(s.enabled?s.effect:0)];
        char status=pending>=0?'*':other?'+':' ';
        snprintf(top,sizeof(top),"FX%d%c %s",slot+1,status,name);
        if (s.held_count) snprintf(bottom,sizeof(bottom),"%s",punch[pad_punch(s.held[s.held_count-1])]);
        else if (s.controller && s.fn) snprintf(bottom,sizeof(bottom),"FN + PAD");
        else {
            char ca=pickup_arrow(&a->arduino.cutoff,now), ra=pickup_arrow(&a->arduino.resonance,now);
            char na=pickup_arrow(&a->arduino.pot,now);
            if (!s.controller) snprintf(top,sizeof(top),"FX%d%c %.7s %s",slot+1,status,name,s.fx_parameter==0?"FX":s.fx_parameter==1?"A":s.fx_parameter==2?"B":"SLOT");
            if (na!=' ') snprintf(top,sizeof(top),"FX%d%c %-7s N%c",slot+1,status,name,na);
            switch (s.effect) {
            case 1: case 2: snprintf(bottom,sizeof(bottom),"C%5d%c Q%3.1f%c",(int)lroundf(20*powf(900,s.cutoff)),ca,.5+7.5*s.resonance,ra); break;
            case 3: snprintf(bottom,sizeof(bottom),"RATE%02d%c BITS%02d%c",1+(int)(63*s.cutoff),ca,(int)lroundf(16-13*s.resonance),ra); break;
            case 4: snprintf(bottom,sizeof(bottom),"DRV%02d%c MIX%3d%c",1+(int)(20*s.cutoff),ca,(int)lroundf(100*s.resonance),ra); break;
            case 5: snprintf(bottom,sizeof(bottom),"D%3dms%c F%2d%%%c",(int)lroundf(40+560*s.cutoff),ca,(int)lroundf(75*s.resonance),ra); break;
            case 6: snprintf(bottom,sizeof(bottom),"T%3dms%c M%3d%c",(int)lroundf(15+185*s.cutoff),ca,(int)lroundf(100*s.resonance),ra); break;
            case 7: snprintf(bottom,sizeof(bottom),"R%4.1fHz%c D%3d%c",.5+15.5*s.cutoff,ca,(int)lroundf(100*s.resonance),ra); break;
            case 8: snprintf(bottom,sizeof(bottom),"R%4dHz%c M%3d%c",(int)lroundf(20+1980*s.cutoff),ca,(int)lroundf(100*s.resonance),ra); break;
            case 9: case 10: snprintf(bottom,sizeof(bottom),"R%4.1fHz%c M%3d%c",.1+4.9*s.cutoff,ca,(int)lroundf(100*s.resonance),ra); break;
            default: snprintf(bottom,sizeof(bottom),"NAV: ELIGE FX"); break;
            }
        }
        if (pending>=0 && !s.held_count && !s.fn) snprintf(bottom,sizeof(bottom),"OK PARA APLICAR");
    } else if (s.mode==BPM) {
        snprintf(top,sizeof(top),s.controller?"BPM: NAV / OK":"BPM IZQ/DER");
        snprintf(bottom,sizeof(bottom),"%d BPM %s",s.bpm,s.playing?"PLAY":"STOP");
    } else if (s.mode==DRUM) {
        snprintf(top,sizeof(top),"P%d %.12s",s.drum+1,label);
        snprintf(bottom,sizeof(bottom),"VOL %d%% BG %s",(int)lroundf(s.volume*100),s.backing?"ON":"OFF");
    } else {
        Browser *b=&a->browser;
        if (b->count) {
            const Entry *e=&b->entries[b->selected]; size_t n=strlen(e->name), offset=0;
            /* Reserve three columns for the destination, thirteen for the name. */
            if (n>13) offset=(size_t)(now*3)%(n-12+5);
            if (n>13 && offset>n-13) offset=n-13;
            snprintf(top,sizeof(top),"P%d %.13s",s.drum+1,e->name+offset);
            if (e->directory) snprintf(bottom,sizeof(bottom),!strcmp(e->name,".. / VOLVER")?"OK: VOLVER":"CARPETA: OK");
            else if (!a->preview_ready) snprintf(bottom,sizeof(bottom),"LEYENDO ONDA...");
            else if (!a->preview_valid) snprintf(bottom,sizeof(bottom),"SIN VISTA PREVIA");
            else {
                waveform=true;
                /* Printable terminal equivalent of the eight CGRAM bar levels. */
                for (int i=0;i<WAVE_COLUMNS;i++) bottom[i]=a->preview_waveform[i]?(char)('0'+a->preview_waveform[i]):'.';
            }
        } else {
            snprintf(top,sizeof(top),"FILES P%d",s.drum+1); snprintf(bottom,sizeof(bottom),"SIN WAV/MP3");
        }
    }
    if (!a->menu_open && s.mode!=SEQ && s.mode!=BASS && !(s.controller && s.mode==FX) && a->arduino.pot.received && now-a->arduino.pot.seen_at<=.30 && a->arduino.pot.hint) {
        snprintf(bottom,sizeof(bottom),"POT GIRA %s",a->arduino.pot.hint<0?"IZQ":"DER");
        waveform=false;
    }
    if (!a->menu_open && s.mode!=SEQ && s.mode!=BASS && now<a->message_until) { snprintf(bottom,sizeof(bottom),"%.16s",a->message); waveform=false; }
    if (!a->menu_open && now<a->connection_until)
        snprintf(top,sizeof(top),s.controller?"ARDUINO: 9 PADS":"FILA: CONTROLES");
    if (a->gpio) {
        if (waveform) hardware_lcd_waveform(&a->hw,top,a->preview_waveform);
        else hardware_lcd(&a->hw,top,bottom);
    }
    else {
        static char prev[2][17]; char t[17],b[17];
        snprintf(t,sizeof(t),"%.16s",top); snprintf(b,sizeof(b),"%.16s",bottom);
        if (strcmp(t,prev[0]) || strcmp(b,prev[1])) {
            printf("[%-16s] [%-16s]\n",t,b); strcpy(prev[0],t); strcpy(prev[1],b);
        }
    }
}
static void command(App *a, char *line) {
    if (!strcmp(line,"q")) { quit=1; return; }
    if (!strcmp(line,"ls")) { list_files(a); return; }
    if (!strncmp(line,"mode ",5)) {
        for (int i=0;i<MODES;i++) if (!strcasecmp(line+5,mode_names[i])) { set_mode(a,i); return; }
    }
    int first, second;
    if (sscanf(line,"scene %d",&first)==1 && first>=1 && first<=SCENES) {
        pthread_mutex_lock(&a->mutex); engine_scene_select(&a->engine,first-1); pthread_mutex_unlock(&a->mutex); mark_dirty(a); return;
    }
    if (sscanf(line,"launch %d",&first)==1 && first>=1 && first<=SCENES) {
        pthread_mutex_lock(&a->mutex); engine_scene_launch(&a->engine,first-1); pthread_mutex_unlock(&a->mutex); return;
    }
    if (sscanf(line,"copy %d %d",&first,&second)==2 && first>=1 && first<=SCENES && second>=1 && second<=SCENES) {
        pthread_mutex_lock(&a->mutex); engine_scene_copy(&a->engine,first-1,second-1); pthread_mutex_unlock(&a->mutex); mark_dirty(a); return;
    }
    if (sscanf(line,"clear %d",&first)==1 && first>=1 && first<=SCENES) {
        pthread_mutex_lock(&a->mutex); engine_scene_clear(&a->engine,first-1); pthread_mutex_unlock(&a->mutex); mark_dirty(a); return;
    }
    if (!strcmp(line,"song play")) { pthread_mutex_lock(&a->mutex); engine_song_toggle(&a->engine); pthread_mutex_unlock(&a->mutex); return; }
    if (!strcmp(line,"song empty")) {
        pthread_mutex_lock(&a->mutex); a->engine.s.song_count=0; state_song_cursor(&a->engine.s,0); pthread_mutex_unlock(&a->mutex); mark_dirty(a); return;
    }
    if (sscanf(line,"song add %d %d",&first,&second)==2 && first>=1 && first<=SCENES && second>=1 && second<=SONG_REPEATS) {
        pthread_mutex_lock(&a->mutex);
        if (a->engine.s.song_count==SONG_BLOCKS) { pthread_mutex_unlock(&a->mutex); notify(a,"64 BLOQUES MAX"); return; }
        state_song_cursor(&a->engine.s,a->engine.s.song_count); a->engine.s.song_choice=first-1; a->engine.s.song_times=second;
        bool ok=engine_song_commit(&a->engine,false); pthread_mutex_unlock(&a->mutex);
        if (ok) mark_dirty(a); else notify(a,"64 BLOQUES MAX"); return;
    }
    if (!strcmp(line,"preview")) { if (snapshot(a).mode==FILES) toggle_preview(a); return; }
    if (!strncmp(line,"fxslot ",7) && (line[7]=='1' || line[7]=='2') && !line[8]) {
        pthread_mutex_lock(&a->mutex); a->engine.s.fx_slot=line[7]-'1'; pthread_mutex_unlock(&a->mutex);
        pot_release(&a->arduino.pot); pot_release(&a->arduino.cutoff); pot_release(&a->arduino.resonance); return;
    }
    if (!strcmp(line,"slots")) {
        for (int p=0;p<PADS;p++) printf("PAD %d: %s\n",p+1,a->assignments[p][0]?a->assignments[p]:"sintetizado");
        return;
    }
    if (!strcmp(line,"l") || !strcmp(line,"o") || !strcmp(line,"r")) nav(a,line[0]=='l'?0:line[0]=='o'?1:2,false);
    else if (!strcmp(line,"L") || !strcmp(line,"O") || !strcmp(line,"R")) nav(a,line[0]=='L'?0:line[0]=='O'?1:2,true);
    else if (line[0]>='1' && line[0]<='9' && !line[1]) { pad(a,line[0]-'1',true); pad(a,line[0]-'1',false); }
    else if ((line[0]=='+' || line[0]=='-') && line[1]>='1' && line[1]<='9' && !line[2]) pad(a,line[1]-'1',line[0]=='+');
    else puts("Comandos: l o r / L O R (largos), 1..9, +1..+9/-1..-9 (mantener/soltar), mode seq/fx/bpm/bass/drum/files/song/live/bank, scene N, launch N, copy A B, clear N, song empty/add N R/play, fxslot 1/2, preview, ls, slots, q");
}
static void terminal(App *a) {
    static char line[512]; static size_t used; static bool eof, overflow;
    struct pollfd p={.fd=STDIN_FILENO,.events=POLLIN};
    if (eof || poll(&p,1,0)<=0) return;
    if (p.revents&(POLLERR|POLLNVAL)) { eof=true; return; }
    char bytes[512]; ssize_t n=read(STDIN_FILENO,bytes,sizeof(bytes));
    if (n<=0) { eof=true; return; }
    for (ssize_t i=0;i<n;i++) {
        if (bytes[i]=='\n') {
            if (!overflow) { line[used]=0; command(a,line); }
            used=0; overflow=false;
        } else if (bytes[i]!='\r') {
            if (used+1<sizeof(line)) line[used++]=bytes[i]; else overflow=true;
        }
    }
}
static void audio_statistics(const Engine *e) {
    fprintf(stderr,"Audio digital: pico antes del recorte %.6f; frames recortados %" PRIu64 "/%" PRIu64 "\n",
        e->output_peak,e->clipped_frames,e->rendered_frames);
    fprintf(stderr,"Limitador: frames con reduccion %" PRIu64 "; exceso de nivel en mezcla %" PRIu64
        "; anticipacion %d frames\n",e->limited_frames,e->overloaded_frames,LOOKAHEAD_FRAMES);
}
static int render_file(App *a, const char *path, double seconds) {
    SF_INFO info={.samplerate=RATE,.channels=2,.format=SF_FORMAT_WAV|SF_FORMAT_PCM_16};
    SNDFILE *out=sf_open(path,SFM_WRITE,&info);
    if (!out) { fprintf(stderr,"Render WAV: %s\n",sf_strerror(NULL)); return 1; }
    size_t total=(size_t)(seconds*RATE), done=0;
    int16_t block[512*2]; int result=0;
    while (done<total) {
        size_t n=total-done<512?total-done:512;
        engine_render(&a->engine,block,n);
        if (sf_writef_short(out,block,(sf_count_t)n)!=(sf_count_t)n) {
            fprintf(stderr,"Error escribiendo WAV: %s\n",sf_strerror(out)); result=1; break;
        }
        done+=n;
    }
    if (sf_close(out)) result=1;
    printf("Render digital: %s (%zu frames a %d Hz); sin ALSA ni GPIO\n",path,done,RATE);
    audio_statistics(&a->engine); return result;
}
static int test_leds(const char *chip, double seconds) {
    Hardware h;
    if (!hardware_init_leds(&h,chip)) {
        fprintf(stderr,"Prueba LED, GPIO %s: %s\n",chip,strerror(errno)); return 1;
    }
    struct sigaction action={0}; action.sa_handler=signal_exit; sigemptyset(&action.sa_mask);
    sigaction(SIGINT,&action,NULL); sigaction(SIGTERM,&action,NULL);
    puts("Prueba LED: solo matriz, sin audio, LCD, botones ni Arduino. Ctrl+C para salir.");
    puts("Orden: de izquierda a derecha, empezando por la fila superior.");
    double start=monotonic(); int previous=-1, result=0;
    while (!quit) {
        double elapsed=monotonic()-start;
        int stage=(int)(elapsed/.75);
        if (stage>=26 || (seconds && elapsed>=seconds)) break;
        if (atomic_load(&h.failed)) { fputs("Error de escritura en matriz LED\n",stderr); result=1; break; }
        if (stage!=previous) {
            uint32_t mask;
            if (stage<16) {
                mask=1u<<stage;
                printf("LED %02d: fila %d, columna %d\n",stage+1,stage/4+1,stage%4+1);
            } else if (stage<20) {
                int row=stage-16; mask=0;
                for (int col=0;col<4;col++) mask|=1u<<(row*4+col);
                printf("Fila %d completa\n",row+1);
            } else if (stage<24) {
                int col=stage-20; mask=0;
                for (int row=0;row<4;row++) mask|=1u<<(row*4+col);
                printf("Columna %d completa\n",col+1);
            } else {
                mask=65535; puts(stage==24?"Todos: intensidad tenue":"Todos: intensidad brillante");
            }
            atomic_store(&h.lights,stage==24?mask:mask<<16); previous=stage;
        }
        struct timespec delay={0,10000000}; nanosleep(&delay,NULL);
    }
    hardware_close(&h);
    if (atomic_load(&h.failed)) result=1;
    puts("Prueba LED terminada; matriz apagada."); return result;
}
static void usage(const char *exe) {
    printf("Uso: %s [--samples carpeta] [--device ALSA] [--gpiochip dispositivo]\n"
        "       [--headless] [--state archivo.json] [--no-save] [--seconds N]\n"
        "       [--arduino /dev/ttyACM0] (NAV A0, CUTOFF A1, RES A2, OK D2, FN D3 por USB)\n"
        "       [--no-leds] [--no-lcd] [--latency-ms 10..500] [--audio-info]\n"
        "       [--render salida.wav --seconds N] (exporta PCM sin ALSA/GPIO)\n"
        "       --test-leds [--gpiochip dispositivo] [--seconds N] (solo matriz LED)\n"
        "OK largo: abrir menu; NAV elige; OK corto confirma. Ctrl+C para salir.\n",exe);
}
int main(int argc, char **argv) {
    App a={0}; a.gpio=true;
    bool lcd_enabled=true, leds_enabled=true, audio_info=false, led_test=false;
    unsigned latency_ms=60;
    const char *render_path=NULL;
    const char *arduino_path=NULL;
    atomic_init(&a.running,false); atomic_init(&a.audio_error,0); atomic_init(&a.xruns,0);
    atomic_init(&a.hw.lights,0); atomic_init(&a.hw.failed,false);
    const char *root="samples", *device="default", *chip="/dev/gpiochip0", *state=NULL;
    double seconds=0;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--help")) { usage(argv[0]); return 0; }
        else if (!strcmp(argv[i],"--headless")) a.gpio=false;
        else if (!strcmp(argv[i],"--no-save")) a.no_save=true;
        else if (!strcmp(argv[i],"--no-leds")) leds_enabled=false;
        else if (!strcmp(argv[i],"--no-lcd")) lcd_enabled=false;
        else if (!strcmp(argv[i],"--audio-info")) audio_info=true;
        else if (!strcmp(argv[i],"--test-leds")) led_test=true;
        else if (i+1<argc && !strcmp(argv[i],"--render")) render_path=argv[++i];
        else if (i+1<argc && !strcmp(argv[i],"--latency-ms")) {
            char *end; long value=strtol(argv[++i],&end,10);
            if (*end || value<10 || value>500) { usage(argv[0]); return 1; }
            latency_ms=(unsigned)value;
        }
        else if (i+1<argc && !strcmp(argv[i],"--samples")) root=argv[++i];
        else if (i+1<argc && !strcmp(argv[i],"--device")) device=argv[++i];
        else if (i+1<argc && !strcmp(argv[i],"--gpiochip")) chip=argv[++i];
        else if (i+1<argc && !strcmp(argv[i],"--state")) state=argv[++i];
        else if (i+1<argc && !strcmp(argv[i],"--arduino")) arduino_path=argv[++i];
        else if (i+1<argc && !strcmp(argv[i],"--seconds")) {
            char *end; seconds=strtod(argv[++i],&end);
            if (*end || !isfinite(seconds) || seconds<=0) { usage(argv[0]); return 1; }
        } else { usage(argv[0]); return 1; }
    }
    if (led_test) {
        if (!a.gpio || !leds_enabled || render_path) {
            fputs("--test-leds requiere GPIO y LED activos y no admite --render\n",stderr); return 1;
        }
        setvbuf(stdout,NULL,_IOLBF,0);
        return test_leds(chip,seconds);
    }
    if (render_path) {
        if (!seconds) seconds=8;
        if (seconds>3600 || seconds*RATE<1) { fputs("Render: duracion entre un frame y 3600 segundos\n",stderr); return 1; }
        a.no_save=true; a.gpio=false;
    }
    setvbuf(stdout,NULL,_IOLBF,0);
    arduino_init(&a.arduino,arduino_path);
    if (state) {
        if (strlen(state)>=sizeof(a.store)) { fputs("Ruta de estado muy larga\n",stderr); return 1; }
        strcpy(a.store,state);
    } else {
        const char *home=getenv("HOME"); if (!home) { fputs("HOME no definido\n",stderr); return 1; }
        char dir[PATH_MAX];
        if (snprintf(dir,sizeof(dir),"%s/.sampler3x3",home)>=(int)sizeof(dir)
            || snprintf(a.store,sizeof(a.store),"%s/patrones_v4.json",dir)>=(int)sizeof(a.store)) return 1;
        if (!a.no_save && mkdir(dir,0700) && errno!=EEXIST) { perror(dir); return 1; }
    }
    if (!browser_init(&a.browser,root)) { perror(root); return 1; }
    if (!engine_init(&a.engine)) { fputs("Sin memoria para iniciar\n",stderr); return 1; }
    int mutex_error=audio_mutex_init(&a.mutex);
    if (mutex_error) { fprintf(stderr,"Mutex audio: %s\n",strerror(mutex_error)); engine_free(&a.engine); return 1; }
    pthread_mutex_init(&a.loader.mutex,NULL);
    pthread_mutex_init(&a.preview.mutex,NULL);
    int rc=1;
    if (mpg123_init()!=MPG123_OK) { fputs("No se pudo iniciar mpg123\n",stderr); goto cleanup; }
    bool loaded=state_load(a.store,&a.engine.s,a.assignments);
    if (!loaded && !state) {
        char legacy[PATH_MAX]; const char *home=getenv("HOME");
        snprintf(legacy,sizeof(legacy),"%s/.sampler3x3/patrones_v3.json",home);
        loaded=state_load(legacy,&a.engine.s,a.assignments);
        if (!loaded) {
            snprintf(legacy,sizeof(legacy),"%s/.sampler3x3/patrones.json",home);
            loaded=state_load(legacy,&a.engine.s,a.assignments);
        }
        if (loaded) mark_dirty(&a);
    }
    for (int p=0;p<PADS;p++) if (a.assignments[p][0]) {
        Sample sample; char error[256];
        if (sample_decode(a.assignments[p],&sample,error)) {
            Sample old=engine_replace(&a.engine,p,sample); free(old.data);
        } else fprintf(stderr,"PAD %d conserva sonido sintetizado: %s (%s)\n",p+1,error,a.assignments[p]);
    }
    if (render_path) { rc=render_file(&a,render_path,seconds); goto mpg_cleanup; }
    if (a.gpio && !hardware_init_config(&a.hw,chip,lcd_enabled,leds_enabled)) {
        fprintf(stderr,"GPIO %s: %s. Requiere GPIO v2 (Linux >= 5.10) y acceso al chip.\n",chip,strerror(errno));
        goto mpg_cleanup;
    }
    int err=snd_pcm_open(&a.pcm,device,SND_PCM_STREAM_PLAYBACK,0);
    if (err<0) { fprintf(stderr,"Audio %s: %s\n",device,snd_strerror(err)); goto hw_cleanup; }
    err=snd_pcm_set_params(a.pcm,SND_PCM_FORMAT_S16_LE,SND_PCM_ACCESS_RW_INTERLEAVED,2,RATE,1,latency_ms*1000);
    if (err<0) { fprintf(stderr,"Configuracion ALSA: %s\n",snd_strerror(err)); goto pcm_cleanup; }
    snd_pcm_uframes_t buffer_size, period_size;
    snd_pcm_get_params(a.pcm,&buffer_size,&period_size);
    a.audio_block=period_size>=64 && period_size<=2048?(size_t)period_size:512;
    printf("Sampler C | %d Hz mono -> estereo | buffer ALSA %lu, periodo %lu frames | dispositivo %s | solicitud %u ms\n",
        RATE,(unsigned long)buffer_size,(unsigned long)period_size,device,latency_ms);
    printf("Mezcla por bloque: %zu frames (%.2f ms); limitador con %.2f ms de anticipacion\n",
        a.audio_block,a.audio_block*1000./RATE,LOOKAHEAD_FRAMES*1000./RATE);
    if (audio_info) {
        snd_output_t *output=NULL;
        if (snd_output_stdio_attach(&output,stderr,0)>=0) { snd_pcm_dump(a.pcm,output); snd_output_close(output); }
    }
    puts("Terminal: l/o/r, L/O/R (largos), 1..9, +1/-1, mode seq/fx/bpm/bass/drum/files/song/live/bank, scene N, launch N, copy A B, clear N, song empty/add N R/play, fxslot 1/2, preview, ls, slots, q (Enter tras cada comando)");
    struct sigaction action={0}; action.sa_handler=signal_exit; sigemptyset(&action.sa_mask);
    sigaction(SIGINT,&action,NULL); sigaction(SIGTERM,&action,NULL);
    atomic_store(&a.running,true);
    err=pthread_create(&a.audio_thread,NULL,audio_worker,&a);
    if (err) { fprintf(stderr,"Hilo audio: %s\n",strerror(err)); atomic_store(&a.running,false); goto pcm_cleanup; }
    a.audio_started=true;
    double start=monotonic(), next_screen=0, next_lights=0; rc=0;
    while (!quit && atomic_load(&a.running)) {
        double now=monotonic();
        if (seconds && now-start>=seconds) break;
        terminal(&a); poll_arduino(&a,now);
        if (a.gpio && (!poll_buttons(&a,now) || atomic_load(&a.hw.failed))) {
            fputs("Error de lectura/escritura GPIO\n",stderr); rc=1; break;
        }
        finish_load(&a);
        if (a.gpio && leds_enabled && now>=next_lights) {
            AudioVisual visual; State s=visual_snapshot(&a,&visual);
            hardware_lights(&a.hw,&s,visual.voices,now); next_lights=now+.01;
        }
        if (now>=next_screen) { update_preview(&a,now); screen(&a,now); next_screen=now+.05; }
        if (a.dirty && now>=a.save_at) save(&a);
        struct timespec delay={0,1000000}; nanosleep(&delay,NULL);
    }
    atomic_store(&a.running,false);
    pthread_join(a.audio_thread,NULL); a.audio_started=false;
    loader_join(&a.loader);
    finish_load(&a);
    if (!save(&a)) rc=1;
    err=atomic_load(&a.audio_error);
    if (err) { fprintf(stderr,"Audio detenido: %s\n",snd_strerror(err)); rc=1; }
    fprintf(stderr,"Recuperaciones ALSA (xruns): %u\n",atomic_load(&a.xruns));
    audio_statistics(&a.engine);
    fprintf(stderr,"Tiempo maximo por bloque: render %.3f ms; espera de controles %.3f ms\n",
        a.max_render_ms,a.max_lock_ms);
pcm_cleanup:
    snd_pcm_drop(a.pcm); snd_pcm_close(a.pcm);
hw_cleanup:
    if (a.gpio) { hardware_lcd(&a.hw,"SAMPLER DETENIDO","HASTA LUEGO"); hardware_close(&a.hw); }
mpg_cleanup:
    arduino_close(&a.arduino);
    loader_join(&a.loader); free(a.loader.result.data);
    loader_join(&a.preview); free(a.preview.result.data); mpg123_exit();
cleanup:
    engine_free(&a.engine); pthread_mutex_destroy(&a.mutex); pthread_mutex_destroy(&a.loader.mutex);
    pthread_mutex_destroy(&a.preview.mutex);
    return rc;
}
