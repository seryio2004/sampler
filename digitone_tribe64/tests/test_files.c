#define _XOPEN_SOURCE 700
#include "files.h"
#include <assert.h>
#include <math.h>
#include <mpg123.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_audio(const char *path, int format, int rate, int channels) {
    SF_INFO info={.samplerate=rate,.channels=channels,.format=format};
    SNDFILE *f=sf_open(path,SFM_WRITE,&info); assert(f);
    float frame[8];
    for (int i=0;i<rate/10;i++) {
        for (int c=0;c<channels;c++) frame[c]=.5f*sinf((float)i*.04f);
        assert(sf_writef_float(f,frame,1)==1);
    }
    assert(sf_close(f)==0);
}
static void check_decoded(const char *path) {
    Sample s; char error[256];
    if (!sample_decode(path,&s,error)) { fprintf(stderr,"decode %s: %s\n",path,error); abort(); }
    assert(s.length>=RATE/12 && s.length<=RATE/5);
    float peak=0;
    for (size_t i=0;i<s.length;i++) {
        assert(isfinite(s.data[i]) && fabsf(s.data[i])<=1);
        if (fabsf(s.data[i])>peak) peak=fabsf(s.data[i]);
    }
    assert(peak>.1f); assert(s.data[0]==0 && s.data[s.length-1]==0); free(s.data);
}
static void conversion_quality(const char *path) {
    SF_INFO info={.samplerate=48000,.channels=1,.format=SF_FORMAT_WAV|SF_FORMAT_FLOAT};
    float pcm[9600];
    for (int i=0;i<9600;i++) pcm[i]=(float)(.25*sin(2*3.141592653589793*1000*i/48000));
    SNDFILE *file=sf_open(path,SFM_WRITE,&info); assert(file);
    assert(sf_writef_float(file,pcm,9600)==9600); sf_close(file);
    Sample s; char error[256]; assert(sample_decode(path,&s,error));
    assert(s.length>=8819 && s.length<=8821);
    double power=0, noise=0;
    for (size_t i=882;i<s.length-882;i++) {
        double expected=.25*sin(2*3.141592653589793*1000*i/RATE), diff=s.data[i]-expected;
        power+=expected*expected; noise+=diff*diff;
    }
    double snr=10*log10(power/noise);
    printf("Remuestreo 48 -> 44,1 kHz: SNR %.1f dB\n",snr); assert(snr>85); free(s.data);
    for (int i=0;i<9600;i++) pcm[i]=(float)(.25*sin(2*3.141592653589793*23000*i/48000));
    file=sf_open(path,SFM_WRITE,&info); assert(file);
    assert(sf_writef_float(file,pcm,9600)==9600); sf_close(file);
    assert(sample_decode(path,&s,error)); double alias=0;
    for (size_t i=882;i<s.length-882;i++) alias+=s.data[i]*s.data[i];
    alias=sqrt(alias/(s.length-1764));
    printf("Antialias 23 kHz: RMS residual %.8f\n",alias); assert(alias<.0002); free(s.data);
    /* Native rate is preserved: a quiet float WAV is never amplified. */
    info.samplerate=RATE;
    for (int i=0;i<9600;i++) pcm[i]=(float)(.05*sin(2*3.141592653589793*1000*i/RATE));
    file=sf_open(path,SFM_WRITE,&info); assert(file);
    assert(sf_writef_float(file,pcm,9600)==9600); sf_close(file);
    assert(sample_decode(path,&s,error)); assert(s.length==9600);
    for (int i=200;i<9400;i++) assert(fabsf(s.data[i]-pcm[i])<1e-7f);
    free(s.data);
}
int main(void) {
    char dir[]="/tmp/sampler-files-XXXXXX"; assert(mkdtemp(dir));
    char wav[PATH_MAX],mp3[PATH_MAX],sub[PATH_MAX],link[PATH_MAX],bad[PATH_MAX],state[PATH_MAX];
    snprintf(wav,sizeof(wav),"%s/test space.WAV",dir);
    snprintf(mp3,sizeof(mp3),"%s/test.MP3",dir);
    snprintf(sub,sizeof(sub),"%s/kit",dir); assert(!mkdir(sub,0700));
    snprintf(link,sizeof(link),"%s/outside",dir); assert(!symlink("/tmp",link));
    snprintf(bad,sizeof(bad),"%s/bad.wav",dir);
    FILE *f=fopen(bad,"w"); assert(f); fputs("bad data",f); fclose(f);
    write_audio(wav,SF_FORMAT_WAV|SF_FORMAT_PCM_16,48000,2);
    assert(mpg123_init()==MPG123_OK);
    check_decoded(wav);
    /* Committed sine fixture: MP3 playback does not require an MP3 encoder. */
    FILE *source=fopen("tests/fixtures/tone.mp3","rb"), *dest=fopen(mp3,"wb");
    assert(source && dest);
    int byte;
    while ((byte=fgetc(source))!=EOF) assert(fputc(byte,dest)!=EOF);
    assert(!ferror(source)); fclose(source); assert(!fclose(dest));
    check_decoded(mp3);
    conversion_quality(bad);
    f=fopen(bad,"w"); assert(f); fputs("bad data",f); fclose(f);
    Sample s; char error[256]; assert(!sample_decode(bad,&s,error) && error[0]);
    /* A valid WAV whose header declares overlong duration must be refused. */
    SF_INFO info={.samplerate=8000,.channels=1,.format=SF_FORMAT_WAV|SF_FORMAT_PCM_16};
    SNDFILE *large=sf_open(bad,SFM_WRITE,&info); assert(large);
    assert(sf_seek(large,31*8000-1,SEEK_SET)>=0); float zero=0;
    assert(sf_writef_float(large,&zero,1)==1); sf_close(large);
    assert(!sample_decode(bad,&s,error));
    Browser b; assert(browser_init(&b,dir)); assert(b.count==4);
    assert(b.entries[0].directory && !strcmp(b.entries[0].name,"kit"));
    assert(browser_enter(&b)); assert(b.count==1 && !strcmp(b.entries[0].name,".. / VOLVER")); assert(browser_enter(&b));
    assert(!browser_parent(&b));
    for (int i=0;i<b.count;i++) assert(strcmp(b.entries[i].name,"outside"));
    snprintf(state,sizeof(state),"%s/state.json",dir);
    State original,restored; state_init(&original); state_init(&restored);
    original.volume=0; original.cutoff=.35f; original.resonance=.8f;
    original.fx_pending[0]=4; original.fx_pending[1]=7;
    original.effect2=5; original.enabled2=true; original.cutoff2=.4f; original.resonance2=.7f;
    original.effect=FX_COUNT-1; original.pattern[8][7]=true;
    original.pattern[7][63]=true; original.melody[48]=4;
    original.scenes[0].muted[7]=true; original.scenes[0].muted[PADS]=true;
    original.sound[4]=(PadSound){-7.5f,.35f,.28f};
    original.sound[8]=(PadSound){24,0,.01f};
    original.solo_scene=0; original.solo_track=8;
    original.bpm=155; original.octave=4; original.melody[3]=5; original.enabled=false;
    char paths[PADS][PATH_MAX]={{0}}, loaded[PADS][PATH_MAX]={{0}}; strcpy(paths[2],wav); strcpy(paths[8],wav);
    assert(state_save(state,&original,paths)); assert(state_load(state,&restored,loaded));
    assert(restored.pattern[7][63] && restored.melody[48]==4);
    assert(restored.scenes[0].muted[7] && restored.scenes[0].muted[PADS]);
    assert(restored.sound[8].length==.01f && restored.sound[8].pitch==24 && restored.sound[8].cutoff==0);
    assert(fabsf(restored.sound[4].pitch+7.5f)<1e-6f && fabsf(restored.sound[4].cutoff-.35f)<1e-6f && fabsf(restored.sound[4].length-.28f)<1e-6f);
    assert(restored.solo_scene== -1 && restored.solo_track== -1);
    assert(restored.fx_pending[0]==-1 && restored.fx_pending[1]==-1);
    assert(restored.effect2==5 && restored.enabled2 && fabsf(restored.cutoff2-.4f)<1e-6f && fabsf(restored.resonance2-.7f)<1e-6f);
    assert(restored.effect==FX_COUNT-1 && !strcmp(loaded[8],wav));
    assert(restored.volume==0 && fabsf(restored.cutoff-.35f)<1e-6f && fabsf(restored.resonance-.8f)<1e-6f);
    assert(restored.bpm==155 && restored.octave==4 && restored.melody[3]==5 && !restored.enabled);
    assert(!strcmp(loaded[2],wav)); assert(!memcmp(original.pattern,restored.pattern,sizeof(original.pattern)));
    /* Old six-pad kits retain their patterns and assignments. */
    f=fopen(state,"w"); assert(f); fputs("{\"patterns\":[",f);
    for (int pad=0;pad<BASE_PADS;pad++) {
        if (pad) fputc(',',f);
        fputc('[',f);
        for (int step=0;step<PAGE_STEPS;step++) fprintf(f,"%s%s",step?",":"",step==pad?"true":"false");
        fputc(']',f);
    }
    fputs("],\"samples\":[\"legacy.wav\",\"\",\"\",\"\",\"\",\"\"]}",f); fclose(f);
    State legacy; state_init(&legacy); char legacy_paths[PADS][PATH_MAX]={{0}};
    assert(state_load(state,&legacy,legacy_paths));
    assert(!legacy.enabled2 && legacy.effect2==0);
    for (int pad=0;pad<PADS;pad++) assert(legacy.sound[pad].pitch==0 && legacy.sound[pad].cutoff==1 && legacy.sound[pad].length==1);
    assert(!strcmp(legacy_paths[0],"legacy.wav") && !legacy_paths[8][0]);
    for (int pad=0;pad<PADS;pad++) for (int step=0;step<STEPS;step++)
        assert(legacy.pattern[pad][step]==(pad<BASE_PADS && step==pad));
    f=fopen(state,"w"); assert(f);
    fputs("{\"bpm\":999999999999,\"bass_octave\":-9,\"bass_pattern\":[999],\"patterns\":{},\"samples\":5}",f); fclose(f);
    assert(state_load(state,&restored,loaded)); assert(restored.bpm==220 && restored.octave==1);
    assert(restored.melody[3]==5);
    f=fopen(state,"w"); assert(f); fputs("broken JSON",f); fclose(f); assert(!state_load(state,&restored,loaded));
    Loader l={0}; pthread_mutex_init(&l.mutex,NULL);
    assert(loader_start(&l,wav,3)); assert(!loader_start(&l,mp3,1)); loader_join(&l);
    assert(l.done && l.success && l.result.data && l.pad==3);
    uint8_t expected[WAVE_COLUMNS]; sample_waveform(&l.result,expected);
    assert(!memcmp(expected,l.waveform,sizeof(expected)));
    free(l.result.data); l.result=(Sample){0}; l.busy=false;
    assert(loader_preview_start(&l,wav)); loader_join(&l);
    assert(l.done && l.success && l.result.data);
    assert(!memcmp(expected,l.waveform,sizeof(expected)));
    free(l.result.data); l.result=(Sample){0}; l.busy=false;
    assert(loader_preview_start(&l,bad)); loader_join(&l);
    assert(l.done && !l.success && !l.result.data && l.error[0]);
    pthread_mutex_destroy(&l.mutex);
    unlink(wav); unlink(mp3); unlink(link); unlink(bad); unlink(state); rmdir(sub); rmdir(dir);
    mpg123_exit(); puts("files: WAV/MP3, limites, carpetas, JSON y carga asincrona OK"); return 0;
}
