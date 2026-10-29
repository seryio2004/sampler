#include "arduino_mock/Arduino.h"
#include <cassert>
#include <iostream>
#include <sstream>
MockSerial Serial;
static int input[3]={512,256,768}, reads[3]={0}, buttons[2]={HIGH,HIGH}, modes[2]={0};
static unsigned long clock_ms;
static int lastPin=-1;
static bool stale_conversion=false;
int analogRead(uint8_t pin) {
    assert(pin>=A0 && pin<=A2); reads[pin-A0]++;
    // Simulate a stale first conversion when the ADC multiplexer changes.
    int old=lastPin; lastPin=pin;
    return stale_conversion && old>=A0 && old!=pin?input[old-A0]:input[pin-A0];
}
int digitalRead(uint8_t pin) { assert(pin==2 || pin==3); return buttons[pin-2]; }
void pinMode(uint8_t pin, int mode) { assert(pin==2 || pin==3); modes[pin-2]=mode; }
unsigned long millis() { return clock_ms; }
#include "../arduino/sampler_pot/sampler_pot.ino"
static void until(unsigned long end) { for (;clock_ms<=end;clock_ms+=5) loop(); }
int main() {
    stale_conversion=true;
    setup(); assert(Serial.baud==115200);
    for (int i=0;i<3;i++) assert(reads[i]==16);
    assert(modes[0]==INPUT_PULLUP && modes[1]==INPUT_PULLUP);
    until(100); assert(Serial.frames.size()==5);
    for (const auto &frame:Serial.frames) assert(frame=="CTRL 512 256 768 0 0");
    input[0]=1016; input[1]=8; input[2]=1016;
    until(300); assert(Serial.frames.back()=="CTRL 1023 0 1023 0 0");
    // Brief contact bounce does not produce a button event.
    buttons[0]=LOW; until(315); buttons[0]=HIGH; until(340);
    assert(Serial.frames.back()=="CTRL 1023 0 1023 0 0");
    buttons[0]=buttons[1]=LOW; until(400);
    assert(Serial.frames.back()=="CTRL 1023 0 1023 1 1");
    buttons[0]=buttons[1]=HIGH; until(460);
    assert(Serial.frames.back()=="CTRL 1023 0 1023 0 0");
    auto count=Serial.frames.size(); until(560); assert(Serial.frames.size()==count+5);
    input[0]=0; input[1]=512; // NAV and CUTOFF keep the ADC direction independently.
    until(800); assert(Serial.frames.back()=="CTRL 0 512 1023 0 0");
    input[2]=0;
    until(1040); assert(Serial.frames.back()=="CTRL 0 512 0 0 0");
    for (const auto &frame:Serial.frames) {
        std::istringstream in(frame); std::string name,extra; int nav,c,r,ok,fn;
        assert(in>>name>>nav>>c>>r>>ok>>fn); assert(!(in>>extra));
        assert(name=="CTRL" && nav>=0 && nav<=1023 && c>=0 && c<=1023 && r>=0 && r<=1023);
        assert((ok==0 || ok==1) && (fn==0 || fn==1));
    }
    std::cout<<"sketch: A0/A1/A2 independientes, filtro, D2/D3 pull-up, antirrebote y CTRL OK (simulado)\n";
}
