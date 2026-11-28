#include <Arduino.h>
#include "rig_tests.h"

// These taps show execution of the protocol harness, not a physical C/Q bus.
extern "C" void rig_emit(const char *text) { Serial.println(text); }
extern "C" void rig_stage(unsigned stage) {
  digitalWrite(PA6, stage == 1 ? HIGH : LOW);
  digitalWrite(PA7, stage == 2 ? HIGH : LOW);
}
static bool passed = false;
void setup() {
  pinMode(PA5, OUTPUT);
  pinMode(PA6, OUTPUT);
  pinMode(PA7, OUTPUT);
  Serial.begin(115200);
  Serial.println("IO-LINK / REAL STACK PROTOCOL HARNESS");
  Serial.println("Virtual PHY byte injection; no wire tA, IAR or Beckhoff claim.");
  passed = (rig_run() == 0);
  digitalWrite(PA5, passed ? HIGH : LOW);
}
void loop() { delay(1000); }
