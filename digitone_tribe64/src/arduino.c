#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "arduino.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define POT_TIMEOUT .30
void pot_reset(PotControl *p) { memset(p,0,sizeof(*p)); }
void pot_release(PotControl *p) {
    p->armed=false; p->configured=false; p->have_previous=false; p->hint=0;
}
void pot_receive(PotControl *p, int value, double now) {
    if (value<0 || value>1023) return;
    if (p->received && now-p->seen_at>POT_TIMEOUT) pot_reset(p);
    p->value=value; p->seen_at=now; p->received=true;
}
int pot_target(PotControl *p, int context, int current, int count, double now) {
    if (!p->received || now-p->seen_at>POT_TIMEOUT) { pot_reset(p); return -1; }
    if (count<1 || current<0 || current>=count) { pot_release(p); return -1; }
    if (!p->configured || p->context!=context || p->count!=count || p->output!=current) {
        pot_release(p); p->configured=true; p->context=context;
        p->count=count; p->output=current;
    }
    int mapped=p->value*count/1024;
    int center=(int)(((long)current*2+1)*1024/(2*count));
    if (!p->armed) {
        bool crossed=p->have_previous &&
            ((p->previous<center && p->value>=center) ||
             (p->previous>center && p->value<=center));
        p->armed=mapped==current || crossed;
        p->hint=p->armed?0:(p->value<center?1:-1);
    }
    p->previous=p->value; p->have_previous=true;
    if (!p->armed || mapped==current) return -1;
    /* Schmitt margin around bin boundaries; retain access to both endpoints. */
    int margin=1024/count/4; if (margin>3) margin=3;
    if (mapped>current && p->value<(current+1)*1024/count+margin) return -1;
    if (mapped<current && p->value>current*1024/count-margin) return -1;
    p->output=mapped; return mapped;
}
int pot_select(PotControl *p, int context, int current, int count, double now) {
    if (!p->received || now-p->seen_at>POT_TIMEOUT) { pot_reset(p); return -1; }
    if (count<1 || current<0 || current>=count) { pot_release(p); return -1; }
    if (!p->configured || p->context!=context || p->count!=count || p->output!=current) {
        pot_release(p); p->configured=true; p->context=context; p->count=count;
        p->output=current; p->previous=p->value; p->have_previous=true; return -1;
    }
    if (!p->armed && abs(p->value-p->previous)<6) return -1;
    p->armed=true; p->hint=0;
    int mapped=p->value*count/1024;
    int margin=1024/count/4; if (margin>3) margin=3;
    if (mapped==current) return -1;
    if (mapped>current && p->value<(current+1)*1024/count+margin) return -1;
    if (mapped<current && p->value>current*1024/count-margin) return -1;
    p->output=mapped; return mapped;
}
int pot_parameter(PotControl *p, int current, double now) {
    if (!p->received || now-p->seen_at>POT_TIMEOUT) { pot_reset(p); return -1; }
    if (current<0 || current>1023) { pot_release(p); return -1; }
    if (!p->configured || p->output!=current) {
        pot_release(p); p->configured=true; p->output=current;
    }
    if (!p->armed) {
        bool crossed=p->have_previous &&
            ((p->previous<current && p->value>=current) ||
             (p->previous>current && p->value<=current));
        p->armed=abs(p->value-current)<=8 || crossed;
        p->hint=p->armed?0:(p->value<current?1:-1);
    }
    p->previous=p->value; p->have_previous=true;
    if (!p->armed || p->value==current) return -1;
    /* Compare with the last applied value, not the last reading: slow turns work. */
    if (abs(p->value-current)<3 && p->value!=0 && p->value!=1023) return -1;
    p->output=p->value; return p->value;
}
void arduino_init(Arduino *a, const char *path) {
    memset(a,0,sizeof(*a)); a->fd=-1; a->path=path;
}
void arduino_close(Arduino *a) {
    if (a->fd>=0) close(a->fd);
    a->fd=-1; a->used=0; a->overflow=false;
    pot_reset(&a->pot); pot_reset(&a->cutoff); pot_reset(&a->resonance);
    a->controls=false; a->ok=a->fn=false; a->button_down=a->button_up=0;
}
static void controls_release(Arduino *a) {
    a->cancelled=true;
    if (a->ok) a->button_up|=1;
    if (a->fn) a->button_up|=2;
    a->ok=a->fn=false;
    pot_reset(&a->pot); pot_reset(&a->cutoff); pot_reset(&a->resonance);
}
static void disconnect(Arduino *a, double now, int error) {
    controls_release(a);
    unsigned released=a->button_up;
    arduino_close(a); a->button_up=released;
    a->last_error=error; a->retry_at=now+2;
}
static bool connect_serial(Arduino *a, double now) {
    int fd=open(a->path,O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
    if (fd<0) { a->last_error=errno; a->retry_at=now+2; return false; }
    struct termios config;
    if (tcgetattr(fd,&config)<0) goto failed;
    cfmakeraw(&config);
    config.c_cflag|=CLOCAL|CREAD;
    config.c_cflag&=~(CSTOPB|PARENB|CSIZE|CRTSCTS); config.c_cflag|=CS8;
    config.c_cc[VMIN]=0; config.c_cc[VTIME]=0;
    if (cfsetispeed(&config,B115200)<0 || cfsetospeed(&config,B115200)<0
        || tcsetattr(fd,TCSANOW,&config)<0 || tcflush(fd,TCIFLUSH)<0) goto failed;
    a->fd=fd; a->last_error=0; a->used=0; a->overflow=false; pot_reset(&a->pot);
    return true;
failed: {
        int error=errno; close(fd); a->last_error=error; a->retry_at=now+2; return false;
    }
}
static bool parse_values(const char *line, size_t length, const char *prefix, int *values, int count) {
    size_t pos=strlen(prefix);
    if (length<=pos || memcmp(line,prefix,pos)) return false;
    for (int i=0;i<count;i++) {
        if (pos>=length || line[pos]<'0' || line[pos]>'9') return false;
        int value=0;
        while (pos<length && line[pos]>='0' && line[pos]<='9') {
            value=value*10+line[pos++]-'0'; if (value>1023) return false;
        }
        values[i]=value;
        if (i<count-1 && (pos>=length || line[pos++]!=' ')) return false;
    }
    return pos==length;
}
bool arduino_poll(Arduino *a, double now) {
    a->cancelled=false; a->button_down=a->button_up=0;
    if (a->controls && now-a->controls_at>POT_TIMEOUT) controls_release(a);
    if (!a->path) return 0;
    if (a->fd<0 && (now<a->retry_at || !connect_serial(a,now))) return 0;
    bool fresh=false;
    /* Bound work per UI iteration, and act on the latest complete reading only. */
    for (int batch=0;batch<4;batch++) {
        struct pollfd pollfd={.fd=a->fd,.events=POLLIN};
        int status=poll(&pollfd,1,0);
        if (status<0) {
            if (errno==EINTR) break;
            disconnect(a,now,errno); return 0;
        }
        if (!status) break;
        if (pollfd.revents&(POLLERR|POLLHUP|POLLNVAL)) { disconnect(a,now,EIO); return 0; }
        if (!(pollfd.revents&POLLIN)) break;
        char bytes[256]; ssize_t n=read(a->fd,bytes,sizeof(bytes));
        if (n<0) {
            if (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR) break;
            disconnect(a,now,errno); return 0;
        }
        if (!n) break;
        for (ssize_t i=0;i<n;i++) {
            if (bytes[i]=='\n') {
                size_t length=a->used;
                if (length && a->line[length-1]=='\r') length--;
                int values[5];
                if (!a->overflow && parse_values(a->line,length,"CTRL ",values,5)
                    && values[3]<=1 && values[4]<=1) {
                    if (a->controls && now-a->controls_at>POT_TIMEOUT) controls_release(a);
                    unsigned before=(a->ok?1u:0u)|(a->fn?2u:0u);
                    unsigned after=(unsigned)values[3]|((unsigned)values[4]<<1);
                    a->button_down|=after&~before; a->button_up|=before&~after;
                    a->ok=values[3]; a->fn=values[4]; a->controls=true; a->controls_at=now;
                    pot_receive(&a->pot,values[0],now);
                    pot_receive(&a->cutoff,values[1],now);
                    pot_receive(&a->resonance,values[2],now);
                    fresh=true;
                } else if (!a->overflow && !a->controls && parse_values(a->line,length,"POT ",values,1)) {
                    pot_receive(&a->pot,values[0],now); fresh=true;
                }
                a->used=0; a->overflow=false;
            } else if (a->used<sizeof(a->line)) a->line[a->used++]=bytes[i];
            else a->overflow=true;
        }
    }
    if (a->pot.received && now-a->pot.seen_at>POT_TIMEOUT) pot_reset(&a->pot);
    return fresh;
}
