#define _XOPEN_SOURCE 600
#include "arduino.h"
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static void controls(void) {
    PotControl p; pot_reset(&p);
    pot_receive(&p,0,0); assert(pot_target(&p,0,8,16,0)==-1 && p.hint==1);
    pot_receive(&p,1023,.02); assert(pot_target(&p,0,8,16,.02)==15 && p.armed);
    /* A stationary knob never repeats or drifts. */
    for (int i=1;i<20;i++) {
        pot_receive(&p,1023,.02+i*.02);
        assert(pot_target(&p,0,15,16,.02+i*.02)==-1);
    }
    /* Each mode requires a new pickup, without using the previous mode's sweep. */
    assert(pot_target(&p,1,3,7,.41)==-1 && p.hint==-1);
    pot_receive(&p,512,.42); assert(pot_target(&p,1,3,7,.42)==-1 && p.armed);
    pot_receive(&p,0,.44); assert(pot_target(&p,1,3,7,.44)==0);
    /* A button change also releases pickup. */
    assert(pot_target(&p,1,5,7,.45)==-1 && !p.armed);
    pot_receive(&p,805,.46); assert(pot_target(&p,1,5,7,.46)==-1 && p.armed);
    /* Timeout and reconnect require pickup again. */
    assert(pot_target(&p,1,5,7,.8)==-1 && !p.received);
    pot_receive(&p,0,.81); assert(pot_target(&p,1,5,7,.81)==-1);
    pot_receive(&p,1023,1.2); assert(pot_target(&p,1,5,7,1.2)==-1);
    pot_receive(&p,-1,1.21); pot_receive(&p,1024,1.21); assert(p.seen_at==1.2);
    /* Hysteresis around step boundary: 511/512 noise must not toggle. */
    pot_reset(&p); pot_receive(&p,500,0); assert(pot_target(&p,0,7,16,0)==-1);
    pot_receive(&p,512,.02); assert(pot_target(&p,0,7,16,.02)==-1);
    pot_receive(&p,516,.04); assert(pot_target(&p,0,7,16,.04)==8);
    pot_receive(&p,511,.06); assert(pot_target(&p,0,8,16,.06)==-1);
    pot_receive(&p,508,.08); assert(pot_target(&p,0,8,16,.08)==7);
    assert(pot_target(&p,5,0,0,.08)==-1); /* Empty directory. */
    /* Dense file lists and both ends of the volume range remain reachable. */
    pot_reset(&p); pot_receive(&p,0,0); assert(pot_target(&p,5,0,1024,0)==-1);
    pot_receive(&p,1023,.02); assert(pot_target(&p,5,0,1024,.02)==1023);
    pot_reset(&p); pot_receive(&p,765,0); assert(pot_target(&p,4,75,101,0)==-1);
    pot_receive(&p,0,.02); assert(pot_target(&p,4,75,101,.02)==0);
    pot_receive(&p,1023,.04); assert(pot_target(&p,4,0,101,.04)==100);
}
static void parameters(void) {
    PotControl c,r; pot_reset(&c); pot_reset(&r);
    pot_receive(&c,100,0); assert(pot_parameter(&c,665,0)==-1 && c.hint==1);
    pot_receive(&r,900,0); assert(pot_parameter(&r,205,0)==-1 && r.hint==-1);
    pot_receive(&c,659,.02); int current=pot_parameter(&c,665,.02);
    assert(current==659 && c.armed); // Pickup tolerates small ADC/rounding differences.
    pot_receive(&r,205,.02); assert(pot_parameter(&r,205,.02)==-1 && r.armed);
    // One-count noise stays put; slow one-count movement eventually takes effect.
    for (int raw=660;raw<=661;raw++) {
        pot_receive(&c,raw,.04); assert(pot_parameter(&c,current,.04)==-1);
    }
    pot_receive(&c,662,.06); current=pot_parameter(&c,current,.06); assert(current==662);
    pot_receive(&c,659,.08); current=pot_parameter(&c,current,.08); assert(current==659);
    // More than 100 distinct adjustments, monotonic sweeps and reachable endpoints.
    int changes=0;
    for (int raw=660;raw<=1023;raw++) {
        pot_receive(&c,raw,.1); int target=pot_parameter(&c,current,.1);
        if (target>=0) { assert(target>current); current=target; changes++; }
    }
    assert(current==1023 && changes>100);
    for (int raw=1022;raw>=0;raw--) {
        pot_receive(&c,raw,.12); int target=pot_parameter(&c,current,.12);
        if (target>=0) { assert(target<current); current=target; }
    }
    assert(current==0);
    pot_receive(&c,0,.14); assert(pot_parameter(&c,current,.14)==-1 && c.armed);
    assert(r.armed && r.output==205); // The other knob never re-arms or changes.
    // A changed saved value or a timeout still needs intentional pickup.
    assert(pot_parameter(&c,800,.16)==-1 && !c.armed);
    assert(pot_parameter(&c,800,.5)==-1 && !c.received);
}
static void send_to(Arduino *a, int master, const char *text) {
    size_t n=strlen(text); assert(write(master,text,n)==(ssize_t)n);
    struct pollfd ready={.fd=a->fd,.events=POLLIN};
    assert(poll(&ready,1,1000)==1 && (ready.revents&POLLIN));
}
static void serial(void) {
    int master=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK); assert(master>=0);
    assert(!grantpt(master) && !unlockpt(master));
    Arduino a; arduino_init(&a,ptsname(master));
    assert(!arduino_poll(&a,1) && a.fd>=0);
    struct termios config; assert(!tcgetattr(a.fd,&config));
    assert(cfgetispeed(&config)==B115200 && !(config.c_lflag&ICANON));
    send_to(&a,master,"POT 51"); assert(!arduino_poll(&a,1.01));
    send_to(&a,master,"2\r\n"); assert(arduino_poll(&a,1.02) && a.pot.value==512);
    send_to(&a,master,"POT 900\n"); assert(arduino_poll(&a,1.03) && a.pot.value==900);
    send_to(&a,master,"POT 512\nPOT 200\n"); assert(arduino_poll(&a,1.04));
    assert(a.pot.value==200); /* Latest complete reading wins. */
    char noise[200]; memset(noise,'x',sizeof(noise)); noise[sizeof(noise)-1]=0;
    send_to(&a,master,noise); assert(!arduino_poll(&a,1.05));
    send_to(&a,master,"POT 100\nPOT -1\nPOT 1024\nPOT 12x\nBOOT\n");
    assert(!arduino_poll(&a,1.06) && a.pot.seen_at==1.04);
    send_to(&a,master,"POT 512\n"); assert(arduino_poll(&a,1.07));
    assert(!arduino_poll(&a,1.5) && !a.pot.armed);
    send_to(&a,master,"POT 0\n"); assert(arduino_poll(&a,1.51));
    send_to(&a,master,"CTRL 120 400 800 1 0\r\n"); assert(arduino_poll(&a,1.52));
    assert(a.controls && a.ok && !a.fn && a.button_down==1);
    assert(a.pot.value==120 && a.cutoff.value==400 && a.resonance.value==800);
    send_to(&a,master,"CTRL 121 401 801 1 0\n"); assert(arduino_poll(&a,1.54));
    assert(!a.button_down && !a.button_up);
    send_to(&a,master,"CTRL 1024 0 0 0 0\nCTRL 0 0 0 2 0\nCTRL 0 0 0 0 0 extra\n");
    assert(!arduino_poll(&a,1.56) && a.ok && a.cutoff.value==401);
    // A complete press/release in one batch must retain both edges.
    send_to(&a,master,"CTRL 100 300 700 0 1\nCTRL 100 300 700 0 0\n");
    assert(arduino_poll(&a,1.58) && a.button_down==2 && a.button_up==3);
    send_to(&a,master,"CTRL 100 300 700 1 0\n"); assert(arduino_poll(&a,1.6));
    assert(!arduino_poll(&a,2) && a.cancelled && !a.ok && a.button_up==1);
    assert(!a.cutoff.received && !a.resonance.received);
    send_to(&a,master,"CTRL 100 300 700 0 1\n"); assert(arduino_poll(&a,2.02) && a.fn && !a.ok);
    close(master); assert(!arduino_poll(&a,2.04) && a.fd==-1 && a.last_error);
    assert(!a.fn && (a.button_up&2) && a.cancelled);
    assert(!arduino_poll(&a,2.2) && a.fd==-1); /* Retry is delayed, never blocking. */
    arduino_close(&a);
    arduino_init(&a,NULL); assert(!arduino_poll(&a,0)); arduino_close(&a);
}
static void selectors(void) {
    PotControl p; pot_reset(&p);
    pot_receive(&p,0,1); assert(pot_select(&p,1,5,9,1)==-1 && !p.hint);
    pot_receive(&p,3,1.02); assert(pot_select(&p,1,5,9,1.02)==-1);
    pot_receive(&p,6,1.04); assert(pot_select(&p,1,5,9,1.04)==0);
    pot_receive(&p,1023,1.06); assert(pot_select(&p,1,0,9,1.06)==8);
    pot_receive(&p,1021,1.08); assert(pot_select(&p,1,8,9,1.08)==-1);
    assert(pot_select(&p,1,2,9,1.08)==-1); /* A pad changes selection: re-anchor. */
    pot_receive(&p,1000,1.1); assert(pot_select(&p,1,2,9,1.1)==8);
    assert(pot_select(&p,1,8,9,1.5)==-1 && !p.received);
}
int main(void) {
    controls(); selectors(); parameters(); serial(); puts("arduino: posicion absoluta, recogida, histeresis, timeout y protocolo serie OK");
    return 0;
}
