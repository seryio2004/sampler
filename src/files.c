#define _XOPEN_SOURCE 700
#include "files.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <math.h>
#include <mpg123.h>
#include <samplerate.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>

static bool audio_extension(const char *s) {
    const char *ext=strrchr(s,'.');
    return ext && (!strcasecmp(ext,".wav") || !strcasecmp(ext,".mp3"));
}
static bool inside(const Browser *b, const char *p) {
    size_t n=strlen(b->root);
    return !strncmp(b->root,p,n) && (n==1 || p[n]=='/' || p[n]=='\0');
}
static bool join_path(char out[PATH_MAX], const char *dir, const char *name) {
    int n=snprintf(out,PATH_MAX,"%s/%s",dir,name);
    return n>0 && n<PATH_MAX;
}
static int compare(const void *a, const void *b) {
    const Entry *x=a,*y=b;
    if (x->directory!=y->directory) return x->directory?-1:1;
    return strcasecmp(x->name,y->name);
}
bool browser_scan(Browser *b) {
    DIR *d=opendir(b->path); if (!d) return false;
    b->count=0; b->selected=0; b->truncated=false;
    struct dirent *entry;
    while ((entry=readdir(d))) {
        if (entry->d_name[0]=='.') continue;
        char full[PATH_MAX], real[PATH_MAX]; struct stat st;
        if (!join_path(full,b->path,entry->d_name) || !realpath(full,real)
            || !inside(b,real) || stat(real,&st)) continue;
        bool dir=S_ISDIR(st.st_mode);
        if (!dir && !(S_ISREG(st.st_mode) && audio_extension(entry->d_name))) continue;
        if (b->count==MAX_ENTRIES) { b->truncated=true; continue; }
        Entry *e=&b->entries[b->count++];
        snprintf(e->name,sizeof(e->name),"%s",entry->d_name); e->directory=dir;
    }
    closedir(d); qsort(b->entries,(size_t)b->count,sizeof(Entry),compare);
    if (strcmp(b->path,b->root)) {
        if (b->count==MAX_ENTRIES) { b->count--; b->truncated=true; }
        memmove(b->entries+1,b->entries,(size_t)b->count*sizeof(Entry));
        b->entries[0]=(Entry){.name=".. / VOLVER",.directory=true}; b->count++;
    }
    return true;
}
bool browser_init(Browser *b, const char *root) {
    memset(b,0,sizeof(*b));
    if (!realpath(root,b->root)) return false;
    strcpy(b->path,b->root); return browser_scan(b);
}
bool browser_file(const Browser *b, char path[PATH_MAX]) {
    if (!b->count || b->entries[b->selected].directory) return false;
    char full[PATH_MAX];
    return join_path(full,b->path,b->entries[b->selected].name)
        && realpath(full,path) && inside(b,path);
}
static bool change_dir(Browser *b, const char *path) {
    char real[PATH_MAX], old[PATH_MAX];
    if (!realpath(path,real) || !inside(b,real)) return false;
    strcpy(old,b->path); strcpy(b->path,real);
    if (browser_scan(b)) return true;
    strcpy(b->path,old); return false;
}
bool browser_enter(Browser *b) {
    if (b->count && !strcmp(b->entries[b->selected].name,".. / VOLVER")) return browser_parent(b);
    char full[PATH_MAX];
    return b->count && b->entries[b->selected].directory
        && join_path(full,b->path,b->entries[b->selected].name) && change_dir(b,full);
}
bool browser_parent(Browser *b) {
    char full[PATH_MAX];
    return strcmp(b->path,b->root) && join_path(full,b->path,"..") && change_dir(b,full);
}
/* Takes ownership of mono on both success and failure; only called off audio. */
static bool convert_mono(float *mono, size_t frames, int rate, Sample *out, char error[256]) {
    float peak=0;
    for (size_t i=0;i<frames;i++) {
        if (!isfinite(mono[i])) mono[i]=0;
        if (fabsf(mono[i])>peak) peak=fabsf(mono[i]);
    }
    if (peak>1) for (size_t i=0;i<frames;i++) mono[i]*=.98f/peak;
    if (rate==RATE) { *out=(Sample){mono,frames}; return true; }
    size_t length=(size_t)ceil((double)frames*RATE/rate);
    float *data=calloc(length,sizeof(float));
    if (!data) { free(mono); return false; }
    SRC_DATA conversion={.data_in=mono,.data_out=data,.input_frames=(long)frames,
        .output_frames=(long)length,.end_of_input=1,.src_ratio=(double)RATE/rate};
    int status=src_simple(&conversion,SRC_SINC_BEST_QUALITY,1);
    free(mono);
    if (status || conversion.output_frames_gen<=0 || conversion.input_frames_used!=(long)frames) {
        snprintf(error,256,"Remuestreo: %.235s",status?src_strerror(status):"conversion incompleta");
        free(data); return false;
    }
    *out=(Sample){data,(size_t)conversion.output_frames_gen}; return true;
}
static bool wav_decode(const char *path, Sample *out, char error[256]) {
    SF_INFO info={0}; SNDFILE *f=sf_open(path,SFM_READ,&info);
    if (!f) { snprintf(error,256,"WAV: %.240s",sf_strerror(NULL)); return false; }
    if ((info.format&SF_FORMAT_TYPEMASK)!=SF_FORMAT_WAV
        && (info.format&SF_FORMAT_TYPEMASK)!=SF_FORMAT_WAVEX) {
        snprintf(error,256,"El archivo no es WAV"); sf_close(f); return false;
    }
    if (info.samplerate<1000 || info.samplerate>384000 || info.channels<1 || info.channels>8
        || info.frames<=0 || (double)info.frames/info.samplerate>MAX_SECONDS) {
        snprintf(error,256,"WAV: max 30 s, 1-8 canales, 1-384 kHz"); sf_close(f); return false;
    }
    /* Read/downmix in bounded chunks; keep only mono input and output. */
    float *mono=malloc((size_t)info.frames*sizeof(float));
    if (!mono) { sf_close(f); return false; }
    float chunk[1024*8]; sf_count_t total=0, n;
    while (total<info.frames && (n=sf_readf_float(f,chunk,
            info.frames-total<1024?info.frames-total:1024))>0) {
        for (sf_count_t i=0;i<n;i++) {
            double sum=0; for (int ch=0;ch<info.channels;ch++) sum+=chunk[i*info.channels+ch];
            mono[total+i]=(float)(sum/info.channels);
        }
        total+=n;
    }
    bool ok=total==info.frames && sf_error(f)==SF_ERR_NO_ERROR; sf_close(f);
    if (!ok) { snprintf(error,256,"WAV incompleto o corrupto"); free(mono); return false; }
    return convert_mono(mono,(size_t)info.frames,info.samplerate,out,error);
}
static bool mp3_decode(const char *path, Sample *out, char error[256]) {
    int status=0; mpg123_handle *m=mpg123_new(NULL,&status);
    if (!m) { snprintf(error,256,"MP3: no se pudo crear decoder"); return false; }
    bool ok=false; size_t count=0, limit=0; long rate=0;
    float *data=NULL; int channels,encoding;
    if (mpg123_param(m,MPG123_ADD_FLAGS,MPG123_MONO_MIX|MPG123_FORCE_FLOAT,0)!=MPG123_OK
        || mpg123_format_none(m)!=MPG123_OK) goto done;
    const long *rates; size_t rate_count;
    mpg123_rates(&rates,&rate_count);
    for (size_t i=0;i<rate_count;i++)
        if (mpg123_format(m,rates[i],MPG123_MONO,MPG123_ENC_FLOAT_32)!=MPG123_OK) goto done;
    if (mpg123_open(m,path)!=MPG123_OK || mpg123_getformat(m,&rate,&channels,&encoding)!=MPG123_OK
        || rate<1000 || rate>384000 || channels!=1 || encoding!=MPG123_ENC_FLOAT_32) goto done;
    limit=(size_t)MAX_SECONDS*(size_t)rate;
    data=malloc(limit*sizeof(float)); if (!data) goto done;
    for (;;) {
        float chunk[4096]; size_t bytes=0;
        status=mpg123_read(m,chunk,sizeof(chunk),&bytes);
        if (status==MPG123_NEW_FORMAT) {
            long changed_rate;
            if (mpg123_getformat(m,&changed_rate,&channels,&encoding)!=MPG123_OK
                || changed_rate!=rate || channels!=1 || encoding!=MPG123_ENC_FLOAT_32) goto done;
        }
        size_t n=bytes/sizeof(float);
        if (count+n>limit) { snprintf(error,256,"MP3: maximo 30 segundos"); goto done; }
        memcpy(data+count,chunk,n*sizeof(float)); count+=n;
        if (status==MPG123_DONE) { ok=count>0; break; }
        if (status!=MPG123_OK && status!=MPG123_NEW_FORMAT) goto done;
    }
done:
    if (!ok) {
        if (!error[0]) snprintf(error,256,"MP3: %.240s",mpg123_strerror(m));
        free(data);
    }
    mpg123_delete(m);
    if (ok) {
        float *small=realloc(data,count*sizeof(float));
        return convert_mono(small?small:data,count,(int)rate,out,error);
    }
    return false;
}
bool sample_decode(const char *path, Sample *out, char error[256]) {
    *out=(Sample){0}; error[0]=0;
    const char *ext=strrchr(path,'.');
    bool ok=false;
    if (ext && !strcasecmp(ext,".wav")) ok=wav_decode(path,out,error);
    else if (ext && !strcasecmp(ext,".mp3")) ok=mp3_decode(path,out,error);
    else snprintf(error,256,"Solo WAV o MP3");
    if (!ok && !error[0]) snprintf(error,256,"Sin memoria para cargar muestra");
    if (ok) sample_prepare(out);
    return ok;
}
static void *load_worker(void *arg) {
    /* On Linux this lowers only the calling thread's CPU priority. */
    (void)setpriority(PRIO_PROCESS,0,5);
    Loader *l=arg; Sample result={0}; char error[256];
    uint8_t waveform[WAVE_COLUMNS]={0};
    bool success=sample_decode(l->path,&result,error);
    if (success) sample_waveform(&result,waveform);
    pthread_mutex_lock(&l->mutex);
    l->result=result; l->success=success; memcpy(l->waveform,waveform,sizeof(waveform));
    strcpy(l->error,error); l->done=true;
    pthread_mutex_unlock(&l->mutex); return NULL;
}
static bool start(Loader *l, const char *path, int pad, bool preview) {
    if (l->busy || strlen(path)>=PATH_MAX) return false;
    strcpy(l->path,path); l->pad=pad; l->done=false; l->busy=true; l->error[0]=0;
    l->preview=preview; l->success=false;
    if (pthread_create(&l->thread,NULL,load_worker,l)) { l->busy=false; return false; }
    l->started=true; return true;
}
bool loader_start(Loader *l, const char *path, int pad) { return start(l,path,pad,false); }
bool loader_preview_start(Loader *l, const char *path) { return start(l,path,-1,true); }
void loader_join(Loader *l) {
    if (l->started) { pthread_join(l->thread,NULL); l->started=false; }
}
static struct json_object *get(struct json_object *o, const char *name) {
    struct json_object *v=NULL; json_object_object_get_ex(o,name,&v); return v;
}
static int integer(struct json_object *o,const char *key,int fallback,int lo,int hi) {
    struct json_object *v=get(o,key);
    if (!v || !json_object_is_type(v,json_type_int)) return fallback;
    int64_t x=json_object_get_int64(v); return x<lo?lo:x>hi?hi:(int)x;
}
static bool read_scene(struct json_object *o, Scene *scene) {
    if (!o || !json_object_is_type(o,json_type_object)) return false;
    struct json_object *rows=get(o,"patterns"), *notes=get(o,"bass_pattern"), *name=get(o,"name");
    if (!rows || !json_object_is_type(rows,json_type_array) || json_object_array_length(rows)!=PADS ||
        !notes || !json_object_is_type(notes,json_type_array) || json_object_array_length(notes)!=STEPS ||
        !name || !json_object_is_type(name,json_type_string)) return false;
    size_t n=(size_t)json_object_get_string_len(name); const char *text=json_object_get_string(name);
    if (!n || n>=SCENE_NAME || strlen(text)!=n) return false;
    bool visible=false;
    for (size_t i=0;i<n;i++) { if (text[i]<32 || text[i]>126) return false; if (text[i]!=' ') visible=true; }
    if (!visible) return false;
    for (int p=0;p<PADS;p++) {
        struct json_object *row=json_object_array_get_idx(rows,p);
        if (!row || !json_object_is_type(row,json_type_array) || json_object_array_length(row)!=STEPS) return false;
        for (int i=0;i<STEPS;i++) {
            struct json_object *cell=json_object_array_get_idx(row,i);
            if (!cell || !json_object_is_type(cell,json_type_boolean)) return false;
            scene->pattern[p][i]=json_object_get_boolean(cell);
        }
    }
    for (int i=0;i<STEPS;i++) {
        struct json_object *note=json_object_array_get_idx(notes,i);
        if (!note || !json_object_is_type(note,json_type_int)) return false;
        int64_t value=json_object_get_int64(note); if (value< -1 || value>=NOTES) return false;
        scene->melody[i]=(int)value;
    }
    snprintf(scene->name,SCENE_NAME,"%s",text); return true;
}
static struct json_object *write_scene(const Scene *scene) {
    struct json_object *o=json_object_new_object(), *rows=json_object_new_array(), *notes=json_object_new_array();
    json_object_object_add(o,"name",json_object_new_string(scene->name));
    for (int p=0;p<PADS;p++) {
        struct json_object *row=json_object_new_array();
        for (int i=0;i<STEPS;i++) json_object_array_add(row,json_object_new_boolean(scene->pattern[p][i]));
        json_object_array_add(rows,row);
    }
    for (int i=0;i<STEPS;i++) json_object_array_add(notes,json_object_new_int(scene->melody[i]));
    json_object_object_add(o,"patterns",rows); json_object_object_add(o,"bass_pattern",notes); return o;
}
bool state_load(const char *path, State *s, char paths[PADS][PATH_MAX]) {
    struct json_object *o=json_object_from_file(path);
    if (!o || !json_object_is_type(o,json_type_object)) { if (o) json_object_put(o); return false; }
    for (int i=0;i<FX_SLOTS;i++) s->fx_pending[i]=-1;
    s->bpm=integer(o,"bpm",s->bpm,50,220);
    s->octave=integer(o,"bass_octave",s->octave,1,4);
    s->effect=integer(o,"global_choice",s->effect,0,FX_COUNT-1);
    s->effect2=integer(o,"fx2_choice",0,0,FX_COUNT-1);
    s->enabled2=false;
    struct json_object *extra=get(o,"fx2_enabled");
    if (extra && json_object_is_type(extra,json_type_boolean)) s->enabled2=json_object_get_boolean(extra);
    struct json_object *v=get(o,"master_volume"); double volume=json_object_get_double(v);
    if (v && (json_object_is_type(v,json_type_double) || json_object_is_type(v,json_type_int))
        && isfinite(volume) && volume>=0 && volume<=1) s->volume=(float)volume;
    const char *knobs[]={"fx_cutoff","fx_resonance","fx2_cutoff","fx2_resonance"};
    float *targets[]={&s->cutoff,&s->resonance,&s->cutoff2,&s->resonance2};
    for (int i=0;i<4;i++) {
        v=get(o,knobs[i]); double value=json_object_get_double(v);
        if (v && (json_object_is_type(v,json_type_double) || json_object_is_type(v,json_type_int))
            && isfinite(value) && value>=0 && value<=1) *targets[i]=(float)value;
    }
    v=get(o,"global_enabled"); if (v && json_object_is_type(v,json_type_boolean)) s->enabled=json_object_get_boolean(v);
    v=get(o,"live_backing"); if (v && json_object_is_type(v,json_type_boolean)) s->backing=json_object_get_boolean(v);
    v=get(o,"patterns");
    size_t rows=v && json_object_is_type(v,json_type_array)?json_object_array_length(v):0;
    bool valid=rows==BASE_PADS || rows==PADS;
    for (int p=0;p<(int)rows && valid;p++) {
        struct json_object *row=json_object_array_get_idx(v,(size_t)p);
        valid=row && json_object_is_type(row,json_type_array) && json_object_array_length(row)==STEPS;
    }
    if (valid) for (int p=0;p<(int)rows;p++) for (int i=0;i<STEPS;i++)
        s->pattern[p][i]=json_object_get_boolean(json_object_array_get_idx(json_object_array_get_idx(v,(size_t)p),(size_t)i));
    v=get(o,"bass_pattern");
    valid=v && json_object_is_type(v,json_type_array) && json_object_array_length(v)==STEPS;
    for (int i=0;i<STEPS && valid;i++) {
        struct json_object *n=json_object_array_get_idx(v,(size_t)i);
        valid=n && json_object_is_type(n,json_type_int) && json_object_get_int64(n)>=-1 && json_object_get_int64(n)<NOTES;
    }
    if (valid) for (int i=0;i<STEPS;i++) s->melody[i]=json_object_get_int(json_object_array_get_idx(v,(size_t)i));
    v=get(o,"samples");
    rows=v && json_object_is_type(v,json_type_array)?json_object_array_length(v):0;
    if (rows==BASE_PADS || rows==PADS)
        for (int p=0;p<(int)rows;p++) {
            struct json_object *n=json_object_array_get_idx(v,(size_t)p);
            if (n && json_object_is_type(n,json_type_string) && json_object_get_string_len(n)<PATH_MAX
                && strlen(json_object_get_string(n))==(size_t)json_object_get_string_len(n))
                strcpy(paths[p],json_object_get_string(n));
        }
    /* Legacy presets become scene 1; missing scenes are empty. Logical sample
       indices remain 1..9, now placed physically in row-major order. */
    s->edit_scene=s->active_scene=0; s->queued_scene=-1; s->transport=PATTERN_PLAY;
    s->stop_pending=false; s->song_position=-1; s->song_left=0;
    for (int scene=0;scene<SCENES;scene++) {
        s->scenes[scene]=(Scene){0}; snprintf(s->scenes[scene].name,SCENE_NAME,"ESCENA %d",scene+1);
        for (int i=0;i<STEPS;i++) s->scenes[scene].melody[i]=-1;
    }
    state_scene_store(s);
    v=get(o,"scenes"); Scene parsed[SCENES];
    valid=v && json_object_is_type(v,json_type_array) && json_object_array_length(v)==SCENES;
    for (int i=0;i<SCENES && valid;i++) valid=read_scene(json_object_array_get_idx(v,i),&parsed[i]);
    if (valid) {
        memcpy(s->scenes,parsed,sizeof(parsed));
        s->edit_scene=integer(o,"edit_scene",1,1,SCENES)-1;
        memcpy(s->pattern,s->scenes[s->edit_scene].pattern,sizeof(s->pattern));
        memcpy(s->melody,s->scenes[s->edit_scene].melody,sizeof(s->melody));
        s->active_scene=s->edit_scene;
    }
    s->live_selected=s->edit_scene;
    s->song_count=1; s->song[0]=(SongBlock){0,1}; s->song_loop=true;
    v=get(o,"song_blocks"); SongBlock blocks[SONG_BLOCKS];
    size_t count=v && json_object_is_type(v,json_type_array)?json_object_array_length(v):SONG_BLOCKS+1;
    valid=count<=SONG_BLOCKS;
    for (size_t i=0;i<count && valid;i++) {
        struct json_object *block=json_object_array_get_idx(v,i);
        if (!block || !json_object_is_type(block,json_type_object)) { valid=false; break; }
        struct json_object *a=get(block,"scene"), *b=get(block,"repeats");
        valid=a && b && json_object_is_type(a,json_type_int) && json_object_is_type(b,json_type_int);
        if (valid) {
            int64_t scene=json_object_get_int64(a), repeats=json_object_get_int64(b);
            valid=scene>=1 && scene<=SCENES && repeats>=1 && repeats<=SONG_REPEATS;
            if (valid) blocks[i]=(SongBlock){(uint8_t)(scene-1),(uint8_t)repeats};
        }
    }
    if (valid) { s->song_count=(int)count; memcpy(s->song,blocks,count*sizeof(SongBlock)); }
    v=get(o,"song_loop"); if (v && json_object_is_type(v,json_type_boolean)) s->song_loop=json_object_get_boolean(v);
    state_song_cursor(s,0);
    json_object_put(o); return true;
}
bool state_save(const char *path, const State *s, char paths[PADS][PATH_MAX]) {
    struct json_object *o=json_object_new_object(), *rows=json_object_new_array(),
        *bass=json_object_new_array(), *samples=json_object_new_array();
    json_object_object_add(o,"version",json_object_new_int(5));
    json_object_object_add(o,"pad_order",json_object_new_string("row-major"));
    json_object_object_add(o,"edit_scene",json_object_new_int(s->edit_scene+1));
    struct json_object *scenes=json_object_new_array(), *blocks=json_object_new_array();
    for (int i=0;i<SCENES;i++) { Scene scene=state_scene(s,i); json_object_array_add(scenes,write_scene(&scene)); }
    for (int i=0;i<s->song_count;i++) {
        struct json_object *block=json_object_new_object();
        json_object_object_add(block,"scene",json_object_new_int(s->song[i].scene+1));
        json_object_object_add(block,"repeats",json_object_new_int(s->song[i].repeats));
        json_object_array_add(blocks,block);
    }
    json_object_object_add(o,"scenes",scenes); json_object_object_add(o,"song_blocks",blocks);
    json_object_object_add(o,"song_loop",json_object_new_boolean(s->song_loop));
    Scene first=state_scene(s,0);
    json_object_object_add(o,"bpm",json_object_new_int(s->bpm));
    json_object_object_add(o,"bass_octave",json_object_new_int(s->octave));
    json_object_object_add(o,"global_choice",json_object_new_int(s->effect));
    json_object_object_add(o,"global_enabled",json_object_new_boolean(s->enabled));
    json_object_object_add(o,"live_backing",json_object_new_boolean(s->backing));
    json_object_object_add(o,"master_volume",json_object_new_double(s->volume));
    json_object_object_add(o,"fx2_choice",json_object_new_int(s->effect2));
    json_object_object_add(o,"fx2_enabled",json_object_new_boolean(s->enabled2));
    json_object_object_add(o,"fx2_cutoff",json_object_new_double(s->cutoff2));
    json_object_object_add(o,"fx2_resonance",json_object_new_double(s->resonance2));
    json_object_object_add(o,"fx_cutoff",json_object_new_double(s->cutoff));
    json_object_object_add(o,"fx_resonance",json_object_new_double(s->resonance));
    for (int p=0;p<PADS;p++) {
        struct json_object *row=json_object_new_array();
        for (int i=0;i<STEPS;i++) json_object_array_add(row,json_object_new_boolean(first.pattern[p][i]));
        json_object_array_add(rows,row); json_object_array_add(samples,json_object_new_string(paths[p]));
    }
    for (int i=0;i<STEPS;i++) json_object_array_add(bass,json_object_new_int(first.melody[i]));
    json_object_object_add(o,"patterns",rows); json_object_object_add(o,"bass_pattern",bass);
    json_object_object_add(o,"samples",samples);
    char tmp[PATH_MAX]; int n=snprintf(tmp,sizeof(tmp),"%s.tmp.XXXXXX",path);
    bool ok=false;
    if (n>0 && n<PATH_MAX) {
        int fd=mkstemp(tmp);
        if (fd>=0) {
            const char *text=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PRETTY);
            size_t size=strlen(text), offset=0;
            while (offset<size) {
                ssize_t written=write(fd,text+offset,size-offset);
                if (written<0 && errno==EINTR) continue;
                if (written<=0) break;
                offset+=(size_t)written;
            }
            bool flushed=offset==size && fsync(fd)==0;
            int closed=close(fd);
            ok=flushed && closed==0 && rename(tmp,path)==0;
            if (!ok) unlink(tmp);
        }
    }
    json_object_put(o); return ok;
}
