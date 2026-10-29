#define _POSIX_C_SOURCE 200809L
#include "hardware.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* Decode the actual four-bit LCD bus, without touching a GPIO device. */
static struct { unsigned value; bool data; } bytes[512];
static size_t count;
static uint64_t previous_bits;
static unsigned high;
static bool half;
static unsigned requested_lines;
static Hardware *scan_h;
static int led_fd=-1;
static unsigned pins_state, scan_slots, led_writes, inject_failure;
static bool interrupted;
static struct timespec interrupted_deadline;
static uint32_t scan_pattern;
static uint32_t expected_frame(unsigned frame) {
    if (frame<2) return frame==0?0xffffu:0x8421u;
    return 1u<<((frame-2)/2); /* Two full frames per position, all 16 positions. */
}
/* Run the real worker without wall-clock delays; EINTR must repeat the deadline. */
int clock_nanosleep(clockid_t clock, int flags, const struct timespec *time, struct timespec *remaining) {
    (void)remaining;
    assert(scan_h && clock==CLOCK_MONOTONIC && flags==TIMER_ABSTIME);
    if (!interrupted) { interrupted=true; interrupted_deadline=*time; return EINTR; }
    if (!scan_slots) assert(time->tv_sec==interrupted_deadline.tv_sec && time->tv_nsec==interrupted_deadline.tv_nsec);
    unsigned row=scan_slots%4, frame=scan_slots/4, columns=0;
    for (unsigned col=0;col<4;col++)
        if (expected_frame(frame)&(1u<<(row*4+col))) columns|=1u<<(4+col);
    assert(pins_state==((240u&~columns)|(columns?1u<<row:0u)));
    scan_slots++;
    /* Change midway through frame zero; it must take effect in frame one only. */
    if (scan_slots==2) atomic_store(&scan_h->lights,(0x8421u<<16)|0x1248u);
    if (scan_slots>=4 && scan_slots%8==0)
        atomic_store(&scan_h->lights,(1u<<((scan_slots/4-2)/2))<<16);
    if (scan_slots==34*4) atomic_store(&scan_h->running,false);
    return 0;
}
int ioctl(int fd, unsigned long request, ...) {
    va_list args; va_start(args,request);
    if (request==GPIO_V2_GET_LINE_IOCTL) {
        struct gpio_v2_line_request *r=va_arg(args,struct gpio_v2_line_request *); va_end(args);
        requested_lines+=r->num_lines;
        r->fd=open("/dev/null",O_RDONLY); assert(r->fd>=0);
        if (r->num_lines==8) {
            const unsigned expected[]={7,4,8,9,10,11,18,15};
            for (unsigned i=0;i<8;i++) assert(r->offsets[i]==expected[i]);
            assert(r->config.flags==GPIO_V2_LINE_FLAG_OUTPUT && r->config.num_attrs==1);
            assert(r->config.attrs[0].mask==255 && r->config.attrs[0].attr.values==240);
            led_fd=r->fd; pins_state=240; atomic_store(&scan_h->lights,scan_pattern);
        } else assert(r->num_lines==9);
        return 0;
    }
    assert(request==GPIO_V2_LINE_SET_VALUES_IOCTL);
    struct gpio_v2_line_values *v=va_arg(args,struct gpio_v2_line_values *); va_end(args);
    if (fd==led_fd) {
        /* Enforce electrical safety, even if a kernel applies each pin separately. */
        assert(v->mask==15 || v->mask==240);
        if (++led_writes==inject_failure) { errno=EIO; return -1; }
        if (v->mask==240) assert(!(pins_state&15));
        else {
            unsigned rows=(unsigned)v->bits&15;
            assert(!rows || !(rows&(rows-1)));
            if (rows) assert(!(pins_state&15));
        }
        pins_state=(pins_state&~v->mask)|(v->bits&v->mask); return 0;
    }
    assert(fd==99);
    if ((previous_bits&2) && !(v->bits&2)) {
        unsigned nibble=(unsigned)(v->bits>>2)&15;
        if (!half) high=nibble<<4;
        else {
            assert(count<sizeof(bytes)/sizeof(bytes[0]));
            bytes[count].value=high|nibble; bytes[count++].data=(v->bits&1)!=0;
        }
        half=!half;
    }
    previous_bits=v->bits; return 0;
}
static void led_scan(void) {
    Hardware h; scan_h=&h;
    /* All notes in first frame; subsequent frames distinguish dim from bright. */
    scan_pattern=(0x9669u<<16)|0x6996u;
    assert(hardware_init_leds(&h,"/dev/null"));
    assert(h.buttons==-1 && h.lcd==-1 && h.started);
    pthread_join(h.thread,NULL); h.started=false;
    assert(scan_slots==34*4 && pins_state==240 && !atomic_load(&h.failed));
    hardware_close(&h); assert(pins_state==240); led_fd=-1;

    scan_slots=led_writes=0; inject_failure=2; interrupted=false;
    assert(hardware_init_leds(&h,"/dev/null"));
    pthread_join(h.thread,NULL); h.started=false;
    assert(atomic_load(&h.failed) && scan_slots==0 && pins_state==240);
    hardware_close(&h); assert(pins_state==240); led_fd=-1; scan_h=NULL;
}
static void lcd_waveform(void) {
    Hardware h={.lcd=99}; atomic_init(&h.failed,false);
    hardware_lcd_glyphs(&h);
    assert(count==66 && bytes[0].value==0x40 && !bytes[0].data);
    for (unsigned glyph=0;glyph<8;glyph++) for (unsigned row=0;row<8;row++) {
        size_t i=1+glyph*8+row;
        assert(bytes[i].data && bytes[i].value==(row>=7-glyph?31u:0u));
    }
    assert(!bytes[65].data && bytes[65].value==0x80);
    count=0;
    uint8_t heights[16]={1,2,3,4,5,6,7,8,0,1,0,8,2,0,3,1};
    hardware_lcd_waveform(&h,"P1 test.wav",heights);
    assert(count==64); /* Explicit cell addressing + byte, for all 32 cells. */
    for (unsigned i=0;i<16;i++) {
        assert(!bytes[32+2*i].data && bytes[32+2*i].value==0xc0+i);
        unsigned expected=heights[i]?heights[i]-1u:32;
        assert(bytes[33+2*i].data && bytes[33+2*i].value==expected);
    }
    count=0; hardware_lcd_waveform(&h,"P1 test.wav",heights); assert(count==0);
    hardware_lcd(&h,"FX OFF","SIN EFECTO"); assert(count>0);
    count=0; hardware_lcd_waveform(&h,"P1 test.wav",heights);
    assert(count>0 && !atomic_load(&h.failed));
}

int main(void) {
    lcd_waveform();
    Hardware disabled;
    assert(hardware_init_config(&disabled,"/dev/null",false,false));
    assert(requested_lines==9 && disabled.lcd==-1 && disabled.leds==-1 && !disabled.started);
    size_t sent=count;
    uint8_t no_wave[16]={0};
    hardware_lcd(&disabled,"OFF","OFF"); hardware_lcd_waveform(&disabled,"OFF",no_wave);
    hardware_lcd_glyphs(&disabled); assert(count==sent);
    hardware_close(&disabled);
    led_scan();
    Hardware h={0}; atomic_init(&h.lights,0);
    State s; state_init(&s);
    s.mode=SEQ; s.drum=5; s.cursor=-1;
    for (int i=0;i<16;i++) {
        s.playhead=i; hardware_lights(&h,&s,4095,.3);
        assert((atomic_load(&h.lights)>>16)==(1u<<i));
    }
    s.playing=false; s.pattern[5][2]=true; s.cursor=7;
    hardware_lights(&h,&s,0,0);
    assert((atomic_load(&h.lights)&65535)==(1u<<2));
    assert((atomic_load(&h.lights)>>16)==(1u<<7));
    hardware_lights(&h,&s,0,.3);
    assert((atomic_load(&h.lights)>>16)==0 && (atomic_load(&h.lights)&65535)==(1u<<2));
    s.cursor=2; hardware_lights(&h,&s,0,.3);
    assert(!atomic_load(&h.lights)); /* Cursor blinks even over a programmed note. */
    hardware_lights(&h,&s,0,.5); assert((atomic_load(&h.lights)>>16)==(1u<<2));
    s.mode=BASS; s.cursor=7; s.melody[10]=4; hardware_lights(&h,&s,4095,.3);
    assert((atomic_load(&h.lights)&65535)==(1u<<10) && !(atomic_load(&h.lights)>>16));
    for (int mode=0;mode<MODES;mode++) if (mode!=SEQ && mode!=BASS && mode<SONG) {
        s.mode=mode; s.playing=true; s.playhead=15;
        for (int slot=0;slot<15;slot++) {
            int source=slot;
            hardware_lights(&h,&s,(uint16_t)(1u<<source),0);
            assert((atomic_load(&h.lights)>>16)==(1u<<slot));
        }
        hardware_lights(&h,&s,0,0); assert(!atomic_load(&h.lights));
        hardware_lights(&h,&s,65535,0); assert((atomic_load(&h.lights)>>16)==32767);
    }
    puts("hardware: LCD/onda, 16 LED, barrido sin cruces, brillo, EINTR y apagado tras fallo OK (GPIO simulado)"); return 0;
}
