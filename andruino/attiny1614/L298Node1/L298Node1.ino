// ATtiny1614 node 1: addressed Uno bus, valve NMOS, DRV8871, and AS5600.
#include <Arduino.h>
#include <Wire.h>
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>

const uint8_t NODE_ADDRESS = 1;
const uint8_t UPDI_GATE_PIN = PIN_PB0;
const uint8_t BUS_TX_PIN = PIN_PB2;
const uint8_t STATUS_LED_PIN = PIN_PA5;
const uint8_t VALVE_GATE_PIN = PIN_PA4;
const uint8_t DRV8871_IN1_PIN = PIN_PA6;
const uint8_t DRV8871_IN2_PIN = PIN_PA7;
const uint8_t DRV8871_SLEEP_PIN = PIN_PB1;
const uint16_t LINK_BAUD = 2400;
const uint16_t BUS_TURNAROUND_MS = 10;
const uint16_t MAX_MOTOR_MS = 60000;
const uint8_t AS5600_ADDRESS = 0x36;
const uint8_t AS5600_RAW_ANGLE_HIGH = 0x0C;
const uint8_t AS5600_STATUS = 0x0B;
const uint16_t AS5600_HALF_RANGE = 2048;

enum MotorDirection { MOTOR_OFF, MOTOR_FORWARD, MOTOR_REVERSE };
MotorDirection motorDirection = MOTOR_OFF;
bool valveOn = false;
bool encoderAvailable = false;
uint16_t motorRunDuration = 0;
uint32_t motorStartedAt = 0;
uint16_t lastRawAngle = 0;
int32_t encoderPosition = 0;

void releaseBus() { USART0.CTRLB &= (uint8_t)~USART_TXEN_bm; pinMode(BUS_TX_PIN, INPUT); }
void startTransmit() { digitalWrite(BUS_TX_PIN, HIGH); pinMode(BUS_TX_PIN, OUTPUT); USART0.CTRLB |= USART_TXEN_bm; }
void responseHeader(const __FlashStringHelper *text) {
  delay(BUS_TURNAROUND_MS); startTransmit();
  Serial.print(F("NODE ")); Serial.print(NODE_ADDRESS); Serial.print(' '); Serial.print(text);
}
void finishResponse() { Serial.println(); Serial.flush(); releaseBus(); }

bool readAs5600RawAngle(uint16_t &angle) {
  Wire.beginTransmission(AS5600_ADDRESS);
  Wire.write(AS5600_RAW_ANGLE_HIGH);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(AS5600_ADDRESS, 2) != 2) return false;
  const uint16_t high = Wire.read();
  const uint16_t low = Wire.read();
  angle = ((high << 8) | low) & 0x0FFF;
  return true;
}
void updateEncoder() {
  uint16_t angle;
  if (!readAs5600RawAngle(angle)) { encoderAvailable = false; return; }
  encoderAvailable = true;
  if (lastRawAngle == 0 && encoderPosition == 0) { lastRawAngle = angle; return; }
  int16_t delta = (int16_t)(angle - lastRawAngle);
  if (delta > (int16_t)AS5600_HALF_RANGE) delta -= 4096;
  if (delta < -(int16_t)AS5600_HALF_RANGE) delta += 4096;
  encoderPosition += delta;
  lastRawAngle = angle;
}
void stopMotor() {
  digitalWrite(DRV8871_IN1_PIN, LOW); digitalWrite(DRV8871_IN2_PIN, LOW);
  motorDirection = MOTOR_OFF;
}
void startMotor(MotorDirection direction, uint16_t duration) {
  digitalWrite(DRV8871_IN1_PIN, LOW); digitalWrite(DRV8871_IN2_PIN, LOW);
  digitalWrite(DRV8871_SLEEP_PIN, HIGH);
  if (direction == MOTOR_FORWARD) digitalWrite(DRV8871_IN1_PIN, HIGH);
  else digitalWrite(DRV8871_IN2_PIN, HIGH);
  motorDirection = direction; motorRunDuration = duration; motorStartedAt = millis();
}
void setValve(bool on) { valveOn = on; digitalWrite(VALVE_GATE_PIN, on ? HIGH : LOW); }
void stopAll() { stopMotor(); setValve(false); }
bool parseDuration(const char *text, uint16_t &value) {
  if (!text || !*text) return false;
  uint32_t parsed = 0;
  while (*text >= '0' && *text <= '9') { parsed = parsed * 10 + (*text++ - '0'); if (parsed > MAX_MOTOR_MS) return false; }
  if (*text || parsed == 0) return false; value = (uint16_t)parsed; return true;
}

void handleCommand(char *line) {
  char *command = strtok(line, " \t"); char *argument = strtok(NULL, " \t");
  if (!strcmp(command, "PING") && !argument) { responseHeader(F("PONG")); finishResponse(); return; }
  if (!strcmp(command, "VERSION") && !argument) { responseHeader(F("VERSION attiny1614-drv8871-as5600 v1.2")); finishResponse(); return; }
  if (!strcmp(command, "UPDI") && argument && !strtok(NULL, " \t") &&
      (!strcmp(argument, "ENABLE") || !strcmp(argument, "DISABLE"))) {
    digitalWrite(UPDI_GATE_PIN, !strcmp(argument, "ENABLE") ? LOW : HIGH);
    responseHeader(F("UPDI ")); Serial.print(!strcmp(argument, "ENABLE") ? F("ENABLED") : F("DISABLED")); finishResponse(); return;
  }
  if (!strcmp(command, "STOP") && !argument) { stopAll(); responseHeader(F("STOPPED")); finishResponse(); return; }
  if (!strcmp(command, "VALVE") && argument && !strtok(NULL, " \t") && (!strcmp(argument, "ON") || !strcmp(argument, "OFF"))) {
    setValve(!strcmp(argument, "ON")); responseHeader(F("VALVE ")); Serial.print(valveOn ? F("ON") : F("OFF")); finishResponse(); return;
  }
  if (!strcmp(command, "MOTOR") && argument) {
    if (!strcmp(argument, "OFF") && !strtok(NULL, " \t")) { stopMotor(); responseHeader(F("MOTOR OFF")); finishResponse(); return; }
    char *durationText = strtok(NULL, " \t"); uint16_t duration;
    if ((!strcmp(argument, "FORWARD") || !strcmp(argument, "REVERSE")) && parseDuration(durationText, duration) && !strtok(NULL, " \t")) {
      startMotor(!strcmp(argument, "FORWARD") ? MOTOR_FORWARD : MOTOR_REVERSE, duration);
      responseHeader(F("MOTOR ")); Serial.print(argument); Serial.print(F(" STARTED MS ")); Serial.print(duration); finishResponse(); return;
    }
  }
  if (!strcmp(command, "READ_ENCODER") && !argument) {
    updateEncoder(); responseHeader(F("ENCODER ")); Serial.print(encoderAvailable ? encoderPosition : -999999); finishResponse(); return;
  }
  if (!strcmp(command, "ANGLE") && !argument) {
    uint16_t angle; bool ok = readAs5600RawAngle(angle);
    responseHeader(F("ANGLE ")); if (ok) Serial.print(angle); else Serial.print(-1); finishResponse(); return;
  }
  if (!strcmp(command, "RESET_ENCODER") && !argument) {
    encoderPosition = 0; uint16_t angle; if (readAs5600RawAngle(angle)) lastRawAngle = angle;
    responseHeader(F("ENCODER 0")); finishResponse(); return;
  }
  if (!strcmp(command, "STATUS") && !argument) {
    updateEncoder(); responseHeader(F("STATUS MOTOR ")); Serial.print(motorDirection == MOTOR_FORWARD ? F("FORWARD") : motorDirection == MOTOR_REVERSE ? F("REVERSE") : F("OFF"));
    Serial.print(F(" VALVE ")); Serial.print(valveOn ? F("ON") : F("OFF")); Serial.print(F(" ENCODER ")); Serial.print(encoderAvailable ? encoderPosition : -999999);
    Serial.print(F(" UPDI ")); Serial.print(digitalRead(UPDI_GATE_PIN) == LOW ? F("ENABLED") : F("DISABLED")); finishResponse(); return;
  }
  responseHeader(F("ERR COMMAND")); finishResponse();
}

void setup() {
  pinMode(UPDI_GATE_PIN, OUTPUT); digitalWrite(UPDI_GATE_PIN, LOW);
  pinMode(STATUS_LED_PIN, OUTPUT); digitalWrite(STATUS_LED_PIN, HIGH);
  pinMode(VALVE_GATE_PIN, OUTPUT); digitalWrite(VALVE_GATE_PIN, LOW);
  pinMode(DRV8871_IN1_PIN, OUTPUT); pinMode(DRV8871_IN2_PIN, OUTPUT); pinMode(DRV8871_SLEEP_PIN, OUTPUT);
  digitalWrite(DRV8871_IN1_PIN, LOW); digitalWrite(DRV8871_IN2_PIN, LOW); digitalWrite(DRV8871_SLEEP_PIN, LOW);
  Wire.begin(); Serial.begin(LINK_BAUD); releaseBus();
  encoderAvailable = readAs5600RawAngle(lastRawAngle); encoderPosition = 0;
}
void loop() {
  static char line[48]; static uint8_t length = 0; static bool overflow = false;
  if (motorDirection != MOTOR_OFF && motorRunDuration != 0 && millis() - motorStartedAt >= motorRunDuration) stopMotor();
  updateEncoder();
  while (Serial.available()) {
    char character = Serial.read();
    if (character == '\n') {
      if (!overflow && length) {
        line[length] = 0; char *address = strtok(line, " \t");
        if (address && atoi(address) == NODE_ADDRESS) { char *commandLine = address + strlen(address) + 1; while (*commandLine == ' ' || *commandLine == '\t') ++commandLine; handleCommand(commandLine); }
      }
      length = 0; overflow = false; releaseBus();
    } else if (character != '\r') {
      if (length < sizeof(line) - 1) line[length++] = character; else overflow = true;
    }
  }
}
