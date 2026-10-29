/* Test-only ALSA fault injection: partial writes, EAGAIN, EINTR and one xrun. */
#define _POSIX_C_SOURCE 200809L
#include "core.h"
#include <alsa/asoundlib.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *data, snd_pcm_uframes_t frames) {
    static snd_pcm_sframes_t (*real_write)(snd_pcm_t *,const void *,snd_pcm_uframes_t);
    static snd_pcm_t *primary;
    static unsigned call; static snd_pcm_uframes_t written; static int injected;
    if (!real_write) {
        void *symbol=dlsym(RTLD_NEXT,"snd_pcm_writei");
        _Static_assert(sizeof(symbol)==sizeof(real_write),"POSIX function pointer size");
        memcpy(&real_write,&symbol,sizeof(real_write));
        if (!real_write) return -ENOSYS;
    }
    if (!primary) primary=pcm;
    if (pcm!=primary) return real_write(pcm,data,frames);
    if (++call==1) return -EAGAIN;
    if (call==2) return -EINTR;
    if (!injected && written>=RATE/3) {
        injected=1; fprintf(stderr,"TEST_XRUN_AT=%lu\n",(unsigned long)written); return -EPIPE;
    }
    if (frames>37) frames=37;
    snd_pcm_sframes_t result=real_write(pcm,data,frames);
    if (result>0) written+=(snd_pcm_uframes_t)result;
    return result;
}
