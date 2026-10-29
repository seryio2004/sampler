#ifndef SAMPLER_TEST_ARDUINO_H
#define SAMPLER_TEST_ARDUINO_H
#include <cstdint>
#include <string>
#include <vector>
const uint8_t A0=14, A1=15, A2=16;
const int INPUT_PULLUP=2, LOW=0, HIGH=1;
int analogRead(uint8_t pin);
int digitalRead(uint8_t pin);
void pinMode(uint8_t pin, int mode);
unsigned long millis();
struct MockSerial {
    unsigned long baud=0;
    std::string pending;
    std::vector<std::string> frames;
    void begin(unsigned long value) { baud=value; }
    void print(const char *text) { pending+=text; }
    void print(long value) { pending+=std::to_string(value); }
    void println(long value) { frames.push_back(pending+std::to_string(value)); pending.clear(); }
};
extern MockSerial Serial;
#endif
