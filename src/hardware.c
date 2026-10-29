#define _POSIX_C_SOURCE 200809L
#include "hardware.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static const unsigned buttons[]={5,6,12,13,16,19,20,21,26};
static const unsigned lcd[]={17,27,22,23,24,25};
static const unsigned leds[]={7,4,8,9,10,11,18,15};
enum { LED_ROWS=15, LED_COLS=240 };
static void pause_us(long us) {
    struct timespec t={us/1000000,(us%1000000)*1000};
    while (nanosleep(&t,&t) && errno==EINTR) {}
}
static int request(int chip, const unsigned *pins, unsigned count, uint64_t flags, uint64_t initial) {
    struct gpio_v2_line_request r={0};
    r.num_lines=count; r.config.flags=flags;
    snprintf(r.consumer,sizeof(r.consumer),"sampler3x3-c");
    for (unsigned i=0;i<count;i++) r.offsets[i]=pins[i];
    if (flags&GPIO_V2_LINE_FLAG_OUTPUT) {
        r.config.num_attrs=1;
        r.config.attrs[0].attr.id=GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
        r.config.attrs[0].attr.values=initial;
        r.config.attrs[0].mask=(1ULL<<count)-1;
    }
    if (ioctl(chip,GPIO_V2_GET_LINE_IOCTL,&r)<0) return -1;
    return r.fd;
}
static bool set(int fd, uint64_t mask, uint64_t bits) {
    struct gpio_v2_line_values values={.bits=bits,.mask=mask};
    return ioctl(fd,GPIO_V2_LINE_SET_VALUES_IOCTL,&values)==0;
}
static bool leds_off(Hardware *h) {
    /* GPIO updates need not reach all pins simultaneously. Blank anodes first. */
    bool rows=set(h->leds,LED_ROWS,0);
    bool cols=set(h->leds,LED_COLS,LED_COLS);
    return rows && cols;
}
static void nibble(Hardware *h, unsigned n, bool data) {
    uint64_t bits=((uint64_t)n<<2)|(data?1u:0u);
    if (!set(h->lcd,63,bits) || !set(h->lcd,63,bits|2u)) atomic_store(&h->failed,true);
    pause_us(50);
    if (!set(h->lcd,63,bits)) atomic_store(&h->failed,true);
    pause_us(50);
}
static void send(Hardware *h, unsigned value, bool data) {
    nibble(h,(value>>4)&15,data); nibble(h,value&15,data);
    if (!data && (value==1 || value==2)) pause_us(2000);
}
void hardware_lcd_glyphs(Hardware *h) {
    if (h->lcd<0) return;
    /* HD44780 CGRAM: eight 5x8 bars, codes 0..7 => heights 1..8. */
    send(h,0x40,false);
    for (unsigned height=1;height<=8;height++) for (unsigned row=0;row<8;row++)
        send(h,row>=8-height?0x1f:0,true);
    send(h,0x80,false); /* Restore DDRAM addressing before writing any text. */
    h->lcd_valid[0]=h->lcd_valid[1]=false;
}
static void *led_worker(void *arg) {
    Hardware *h=arg; unsigned row=0, phase=0;
    uint32_t lights=0;
    struct timespec deadline; clock_gettime(CLOCK_MONOTONIC,&deadline);
    while (atomic_load(&h->running)) {
        /* Latch a complete frame so an update cannot split the four rows. */
        if (!row) lights=atomic_load(&h->lights);
        unsigned visible=(lights>>16) | ((phase&1)?0:(lights&65535));
        uint64_t bits=LED_COLS;
        for (unsigned col=0;col<4;col++) {
            unsigned step=row*4+col;
            if (visible&(1u<<step)) bits&=~(1ULL<<(4+col));
        }
        /* Break before make: rows OFF -> column data -> exactly one row ON.
           Keep blanking explicit, independent of the driver's GPIO update order. */
        if (!set(h->leds,LED_ROWS,0) || !set(h->leds,LED_COLS,bits)
            || (bits!=LED_COLS && !set(h->leds,LED_ROWS,1ULL<<row))) {
            atomic_store(&h->failed,true); break;
        }
        if (++row==4) { row=0; phase++; }
        deadline.tv_nsec+=1000000; /* 1 ms/row, 250 full scans/s */
        if (deadline.tv_nsec>=1000000000) { deadline.tv_sec++; deadline.tv_nsec-=1000000000; }
        int err;
        do { err=clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&deadline,NULL); }
        while (err==EINTR && atomic_load(&h->running));
        if (err && err!=EINTR) { atomic_store(&h->failed,true); break; }
        /* Reset after a scheduling stall instead of spinning through old deadlines. */
        struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
        int64_t late=(int64_t)(now.tv_sec-deadline.tv_sec)*1000000000+now.tv_nsec-deadline.tv_nsec;
        if (late>0) deadline=now; /* No accelerated catch-up rows after preemption. */
    }
    if (!leds_off(h)) atomic_store(&h->failed,true);
    return NULL;
}
static void hardware_reset(Hardware *h) {
    memset(h,0,sizeof(*h)); h->buttons=h->lcd=h->leds=-1;
    atomic_init(&h->running,false); atomic_init(&h->lights,0); atomic_init(&h->failed,false);
}
static bool leds_start(Hardware *h) {
    atomic_store(&h->running,true);
    int err=pthread_create(&h->thread,NULL,led_worker,h);
    if (err) { hardware_close(h); errno=err; return false; }
    h->started=true;
    return true;
}
bool hardware_init_leds(Hardware *h, const char *chip) {
    hardware_reset(h);
    int fd=open(chip,O_RDONLY|O_CLOEXEC); if (fd<0) return false;
    h->leds=request(fd,leds,8,GPIO_V2_LINE_FLAG_OUTPUT,LED_COLS);
    int saved=errno; close(fd); errno=saved;
    if (h->leds<0) return false;
    return leds_start(h);
}
bool hardware_init(Hardware *h, const char *chip) {
    return hardware_init_config(h,chip,true,true);
}
bool hardware_init_config(Hardware *h, const char *chip, bool lcd_enabled, bool leds_enabled) {
    hardware_reset(h);
    int fd=open(chip,O_RDONLY|O_CLOEXEC); if (fd<0) return false;
    h->buttons=request(fd,buttons,9,GPIO_V2_LINE_FLAG_INPUT|GPIO_V2_LINE_FLAG_BIAS_PULL_UP,0);
    bool ok=h->buttons>=0;
    if (ok && lcd_enabled) { h->lcd=request(fd,lcd,6,GPIO_V2_LINE_FLAG_OUTPUT,0); ok=h->lcd>=0; }
    if (ok && leds_enabled) { h->leds=request(fd,leds,8,GPIO_V2_LINE_FLAG_OUTPUT,LED_COLS); ok=h->leds>=0; }
    int saved=errno; close(fd); errno=saved;
    if (!ok) { hardware_close(h); errno=saved; return false; }
    if (lcd_enabled) {
        pause_us(50000);
        /* Datasheet reset sequence: 3,3,3,2 in 8-bit startup state. */
        nibble(h,3,false); pause_us(4500); nibble(h,3,false); pause_us(200);
        nibble(h,3,false); nibble(h,2,false);
        send(h,0x28,false); send(h,0x0c,false); send(h,0x06,false); send(h,1,false);
        hardware_lcd_glyphs(h);
    }
    if (leds_enabled && !leds_start(h)) return false;
    return true;
}
void hardware_close(Hardware *h) {
    atomic_store(&h->running,false);
    if (h->started) { pthread_join(h->thread,NULL); h->started=false; }
    if (h->leds>=0) {
        if (!leds_off(h)) atomic_store(&h->failed,true);
        close(h->leds); h->leds=-1;
    }
    if (h->lcd>=0) { set(h->lcd,63,0); close(h->lcd); h->lcd=-1; }
    if (h->buttons>=0) { close(h->buttons); h->buttons=-1; }
}
bool hardware_buttons(Hardware *h, unsigned *pressed) {
    struct gpio_v2_line_values values={.mask=511};
    if (ioctl(h->buttons,GPIO_V2_LINE_GET_VALUES_IOCTL,&values)<0) return false;
    *pressed=(unsigned)(~values.bits)&511; return true;
}
static void text_row(const char *text, uint8_t padded[16]) {
    memset(padded,' ',16);
    size_t len=strlen(text); if (len>16) len=16;
    for (size_t i=0;i<len;i++) {
        unsigned char c=(unsigned char)text[i]; padded[i]=c>=32 && c<127?c:'?';
    }
}
static void lcd_rows(Hardware *h, uint8_t cells[2][16]) {
    for (int row=0;row<2;row++) {
        if (h->lcd_valid[row] && !memcmp(cells[row],h->previous[row],16)) continue;
        for (int i=0;i<16;i++) if (!h->lcd_valid[row] || cells[row][i]!=(uint8_t)h->previous[row][i]) {
            send(h,(unsigned)(row?0xc0+i:0x80+i),false);
            send(h,cells[row][i],true);
        }
        memcpy(h->previous[row],cells[row],16); h->previous[row][16]=0;
        h->lcd_valid[row]=true;
    }
}
void hardware_lcd(Hardware *h, const char *top, const char *bottom) {
    if (h->lcd<0) return;
    uint8_t cells[2][16]; text_row(top,cells[0]); text_row(bottom,cells[1]); lcd_rows(h,cells);
}
void hardware_lcd_waveform(Hardware *h, const char *top, const uint8_t heights[WAVE_COLUMNS]) {
    if (h->lcd<0) return;
    uint8_t cells[2][16]; text_row(top,cells[0]);
    /* Fixed-size binary row: custom character 0 is data, never a terminator. */
    for (int i=0;i<WAVE_COLUMNS;i++) cells[1][i]=heights[i]?(heights[i]>8?7:heights[i]-1):' ';
    lcd_rows(h,cells);
}
void hardware_lights(Hardware *h, const State *s, uint16_t voices, double now) {
    uint32_t dim=0, bright=0;
    if (s->mode==LIVE || s->mode==BANK) {
        for (int scene=0;scene<SCENES;scene++) {
            Scene content=state_scene(s,scene); bool occupied=false;
            for (int p=0;p<PADS;p++) for (int step=0;step<STEPS;step++) occupied|=content.pattern[p][step];
            for (int step=0;step<STEPS;step++) occupied|=content.melody[step]>=0;
            unsigned bit=1u<<((scene/3)*4+scene%3);
            if (occupied) dim|=bit;
            if (s->mode==LIVE && s->playing && s->active_scene==scene) bright|=bit;
            if ((s->mode==BANK && s->edit_scene==scene) || (s->mode==LIVE && s->queued_scene==scene)) {
                if ((int)(now*6)%2==0) bright|=bit;
                else { dim&=~bit; bright&=~bit; }
            }
        }
    } else if (s->mode==SEQ || s->mode==BASS || s->mode==SONG) {
        for (int i=0;i<STEPS;i++) {
            if ((s->mode==SEQ && s->pattern[s->drum][i]) || (s->mode==BASS && s->melody[i]>=0)) dim|=1u<<i;
            if (s->mode==SONG && i<s->song_count && i<STEPS) dim|=1u<<i;
            if (s->playing && s->playhead==i) bright|=1u<<i;
        }
        /* Blink the editing position even when it already contains a note. */
        if (s->mode!=SONG && s->cursor>=0 && s->cursor<STEPS) {
            if ((int)(now*4)%2==0) bright|=1u<<s->cursor;
            else { bright&=~(1u<<s->cursor); dim&=~(1u<<s->cursor); }
        }
    } else {
        /* Pads 1..9, then six bass notes; logical row-major LED numbering. */
        bright=voices&32767u;
    }
    atomic_store(&h->lights,dim|(bright<<16));
}
