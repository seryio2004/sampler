// UNO R3: NAV=A0, CUTOFF=A1, RESONANCE=A2. Pot ends to GND/5V.
// D2=OK and D3=FN, each switch to GND (internal pull-ups).
// USB serial: CTRL nav cutoff resonance ok fn, 115200 baud, 8N1.
#include <Arduino.h>
const uint8_t POT_PINS[3] = {A0, A1, A2};
// Logical direction is configured here; never invert again in the sampler.
const bool POT_INVERT[3] = {false, false, false};
const int ADC_EDGE=12; // Reach the endpoints even if the ADC misses a few counts.
const uint8_t BUTTON_PINS[2] = {2, 3};
const unsigned long SAMPLE_MS=5, SEND_MS=20, DEBOUNCE_MS=25;
long filtered[3]={0};
bool rawButton[2]={false}, stableButton[2]={false};
unsigned long changedAt[2]={0}, sampledAt=0, sentAt=0;

int readPot(uint8_t pin) {
  // Discard the first conversion after selecting a different ADC input.
  analogRead(pin);
  return analogRead(pin);
}
long potValue(uint8_t index) {
  long value=(filtered[index]+8)/16;
  if (value<=ADC_EDGE) value=0;
  else if (value>=1023-ADC_EDGE) value=1023;
  return POT_INVERT[index]?1023-value:value;
}
void setup() {
  Serial.begin(115200);
  for (uint8_t i=0;i<3;i++) {
    long sum=0;
    for (uint8_t n=0;n<8;n++) sum+=readPot(POT_PINS[i]);
    filtered[i]=(sum/8)*16;
  }
  for (uint8_t i=0;i<2;i++) pinMode(BUTTON_PINS[i],INPUT_PULLUP);
}
void loop() {
  unsigned long now=millis();
  if (now-sampledAt>=SAMPLE_MS) {
    sampledAt=now;
    for (uint8_t i=0;i<3;i++) {
      long target=(long)readPot(POT_PINS[i])*16;
      filtered[i]+=(target-filtered[i])/4;
    }
    for (uint8_t i=0;i<2;i++) {
      bool pressed=digitalRead(BUTTON_PINS[i])==LOW;
      if (pressed!=rawButton[i]) { rawButton[i]=pressed; changedAt[i]=now; }
      if (now-changedAt[i]>=DEBOUNCE_MS) stableButton[i]=rawButton[i];
    }
  }
  if (now-sentAt>=SEND_MS) {
    sentAt=now;
    Serial.print("CTRL ");
    for (uint8_t i=0;i<3;i++) { Serial.print(potValue(i)); Serial.print(" "); }
    Serial.print(stableButton[0]?1:0); Serial.print(" ");
    Serial.println(stableButton[1]?1:0);
  }
}

