#include <Arduino.h>
#include <Wire.h>
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>

const uint8_t NODE_ADDRESS = 1;
const uint8_t UPDI_GATE_PIN = PIN_PB0;
const uint8_t BUS_TX_PIN = PIN_PB2;
const uint8_t STATUS_LED_PIN = PIN_PA5;
const uint8_t MOTOR_IN1_PIN = PIN_PA6;
const uint8_t MOTOR_IN2_PIN = PIN_PA7;
const uint8_t VALVE_IN1_PIN = PIN_PA4;
const uint8_t VALVE_IN2_PIN = PIN_PB1;
const uint16_t LINK_BAUD = 2400;
const uint16_t BUS_TURNAROUND_MS = 10;
const uint16_t MAX_MOTOR_MS = 2000;
const uint16_t MAX_VALVE_ON_MS = 5000;
const uint16_t DEFAULT_BLINK_MS = 1000;
const uint8_t AS5600_ADDRESS = 0x36;
const uint8_t AS5600_RAW_ANGLE_HIGH = 0x0C;
const uint16_t AS5600_HALF_RANGE = 2048;

enum MotorDirection { MOTOR_OFF, MOTOR_FORWARD, MOTOR_REVERSE };
enum NodeMode { NODE_COMM, NODE_FLASH };

MotorDirection motorDirection = MOTOR_OFF;
NodeMode nodeMode = NODE_COMM;
bool valveOn = false;
bool encoderAvailable = false;
bool encoderInitialized = false;
bool ledBlinking = true;
bool ledOn = false;
uint8_t selectedNode = 0;
uint16_t motorRunDuration = 0;
uint32_t motorStartedAt = 0;
uint32_t valveStartedAt = 0;
uint16_t lastRawAngle = 0;
int32_t encoderPosition = 0;
uint16_t blinkIntervalMs = DEFAULT_BLINK_MS;
uint32_t lastBlinkAt = 0;

void releaseBus() {
  USART0.CTRLB &= (uint8_t)~USART_TXEN_bm;
  pinMode(BUS_TX_PIN, INPUT);
}

void startTransmit() {
  digitalWrite(BUS_TX_PIN, HIGH);
  pinMode(BUS_TX_PIN, OUTPUT);
  USART0.CTRLB |= USART_TXEN_bm;
}

void responseHeader(const __FlashStringHelper *text) {
  delay(BUS_TURNAROUND_MS);
  startTransmit();
  Serial.print(F("NODE "));
  Serial.print(NODE_ADDRESS);
  Serial.print(' ');
  Serial.print(text);
}

void finishResponse() {
  Serial.println();
  Serial.flush();
  releaseBus();
}

void sendResponse(const __FlashStringHelper *text) {
  responseHeader(text);
  finishResponse();
}

void setNodeLed(bool on) {
  ledOn = on;
  digitalWrite(STATUS_LED_PIN, on ? LOW : HIGH);
}

void updateUpdiGate() {
  const bool enabled = nodeMode == NODE_COMM || selectedNode == NODE_ADDRESS;
  digitalWrite(UPDI_GATE_PIN, enabled ? LOW : HIGH);
}

void updateFlashIndicators() {
  if (nodeMode == NODE_FLASH) {
    ledBlinking = false;
    setNodeLed(selectedNode == NODE_ADDRESS);
  }
  updateUpdiGate();
}

void updateStatusLed() {
  if (nodeMode == NODE_FLASH) {
    updateFlashIndicators();
    return;
  }
  if (!ledBlinking) return;
  const uint32_t now = millis();
  if (now - lastBlinkAt >= blinkIntervalMs) {
    lastBlinkAt = now;
    setNodeLed(!ledOn);
  }
}

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
  if (!readAs5600RawAngle(angle)) {
    encoderAvailable = false;
    return;
  }
  encoderAvailable = true;
  if (!encoderInitialized) {
    lastRawAngle = angle;
    encoderInitialized = true;
    return;
  }
  int16_t delta = (int16_t)(angle - lastRawAngle);
  if (delta > (int16_t)AS5600_HALF_RANGE) delta -= 4096;
  if (delta < -(int16_t)AS5600_HALF_RANGE) delta += 4096;
  encoderPosition += delta;
  lastRawAngle = angle;
}

void stopMotor() {
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, LOW);
  motorDirection = MOTOR_OFF;
  motorRunDuration = 0;
}

void startMotor(MotorDirection direction, uint16_t duration) {
  stopMotor();
  delayMicroseconds(200);
  if (direction == MOTOR_FORWARD) digitalWrite(MOTOR_IN1_PIN, HIGH);
  else digitalWrite(MOTOR_IN2_PIN, HIGH);
  motorDirection = direction;
  motorRunDuration = duration;
  motorStartedAt = millis();
}

void setValve(bool on) {
  valveOn = on;
  valveStartedAt = on ? millis() : 0;
  digitalWrite(VALVE_IN1_PIN, on ? HIGH : LOW);
  digitalWrite(VALVE_IN2_PIN, LOW);
}

void stopAll() {
  stopMotor();
  setValve(false);
}

bool parseUnsigned(const char *text, uint16_t minimum, uint16_t maximum,
                   uint16_t &value) {
  if (!text || !*text) return false;
  uint32_t parsed = 0;
  while (*text >= '0' && *text <= '9') {
    parsed = parsed * 10 + (uint32_t)(*text++ - '0');
    if (parsed > maximum) return false;
  }
  if (*text || parsed < minimum) return false;
  value = (uint16_t)parsed;
  return true;
}

void sendStatus() {
  updateEncoder();
  responseHeader(F("STATUS MOTOR "));
  Serial.print(motorDirection == MOTOR_FORWARD ? F("FORWARD") :
      motorDirection == MOTOR_REVERSE ? F("REVERSE") : F("OFF"));
  Serial.print(F(" VALVE "));
  Serial.print(valveOn ? F("ON") : F("OFF"));
  Serial.print(F(" ENCODER "));
  Serial.print(encoderAvailable ? encoderPosition : -999999);
  Serial.print(F(" UPDI "));
  Serial.print(digitalRead(UPDI_GATE_PIN) == LOW ? F("ENABLED") : F("DISABLED"));
  finishResponse();
}

void handleModeCommand(char *argument) {
  if (!argument || strtok(NULL, " \t")) {
    sendResponse(F("ERR COMMAND"));
    return;
  }
  if (!strcmp(argument, "COMM")) {
    nodeMode = NODE_COMM;
    ledBlinking = true;
    setNodeLed(false);
    lastBlinkAt = millis();
    updateUpdiGate();
    sendResponse(F("MODE COMM"));
    return;
  }
  if (!strcmp(argument, "FLASH")) {
    nodeMode = NODE_FLASH;
    updateFlashIndicators();
    sendResponse(F("MODE FLASH"));
    return;
  }
  sendResponse(F("ERR COMMAND"));
}

void handleSelectCommand(char *argument) {
  uint16_t requested;
  if (!parseUnsigned(argument, 0, 7, requested) || strtok(NULL, " \t")) {
    sendResponse(F("ERR SELECT USES NODE 0 THROUGH 7"));
    return;
  }
  selectedNode = (uint8_t)requested;
  updateFlashIndicators();
  sendResponse(selectedNode == NODE_ADDRESS ? F("SELECTED") : F("NOT SELECTED"));
}

void handleCommand(char *line) {
  char *address = strtok(line, " \t");
  char *command = strtok(NULL, " \t");
  if (!address || atoi(address) != NODE_ADDRESS) return;
  if (!command) {
    sendResponse(F("ERR COMMAND"));
    return;
  }
  char *argument = strtok(NULL, " \t");

  if (!strcmp(command, "PING") && !argument) {
    sendResponse(F("PONG"));
    return;
  }
  if (!strcmp(command, "VERSION") && !argument) {
    responseHeader(F("VERSION ATTINY1614-L298N-AS5600-1"));
    finishResponse();
    return;
  }
  if (!strcmp(command, "MODE")) {
    handleModeCommand(argument);
    return;
  }
  if (!strcmp(command, "SELECT")) {
    handleSelectCommand(argument);
    return;
  }
  if (!strcmp(command, "UPDI") && argument && !strtok(NULL, " \t") &&
      (!strcmp(argument, "ENABLE") || !strcmp(argument, "DISABLE"))) {
    digitalWrite(UPDI_GATE_PIN, !strcmp(argument, "ENABLE") ? LOW : HIGH);
    responseHeader(F("UPDI "));
    Serial.print(!strcmp(argument, "ENABLE") ? F("ENABLED") : F("DISABLED"));
    finishResponse();
    return;
  }
  if (!strcmp(command, "STOP") && !argument) {
    stopAll();
    sendResponse(F("STOPPED VALVE OFF MOTOR OFF"));
    return;
  }
  if (!strcmp(command, "LED") && argument && !strtok(NULL, " \t") &&
      (!strcmp(argument, "ON") || !strcmp(argument, "OFF")) &&
      nodeMode == NODE_COMM) {
    ledBlinking = false;
    setNodeLed(!strcmp(argument, "ON"));
    responseHeader(F("LED "));
    Serial.print(ledOn ? F("ON") : F("OFF"));
    finishResponse();
    return;
  }
  if (!strcmp(command, "BLINK")) {
    uint16_t interval;
    if (nodeMode == NODE_COMM &&
        parseUnsigned(argument, 50, 60000, interval) &&
        !strtok(NULL, " \t")) {
      blinkIntervalMs = interval;
      lastBlinkAt = millis();
      ledBlinking = true;
      setNodeLed(false);
      responseHeader(F("BLINK "));
      Serial.print(blinkIntervalMs);
      finishResponse();
      return;
    }
  }
  if (!strcmp(command, "VALVE") && argument && !strtok(NULL, " \t") &&
      (!strcmp(argument, "ON") || !strcmp(argument, "OFF"))) {
    const bool turnOn = !strcmp(argument, "ON");
    if (turnOn && motorDirection != MOTOR_OFF) {
      sendResponse(F("ERR BUSY"));
      return;
    }
    setValve(turnOn);
    responseHeader(F("VALVE "));
    Serial.print(valveOn ? F("ON") : F("OFF"));
    finishResponse();
    return;
  }
  if (!strcmp(command, "MOTOR") && argument) {
    if (!strcmp(argument, "OFF") && !strtok(NULL, " \t")) {
      stopMotor();
      sendResponse(F("MOTOR OFF"));
      return;
    }
    char *durationText = strtok(NULL, " \t");
    uint16_t duration;
    if ((!strcmp(argument, "FORWARD") || !strcmp(argument, "REVERSE")) &&
        parseUnsigned(durationText, 1, MAX_MOTOR_MS, duration) &&
        !strtok(NULL, " \t")) {
      if (valveOn) {
        sendResponse(F("ERR BUSY"));
        return;
      }
      const MotorDirection direction =
          !strcmp(argument, "FORWARD") ? MOTOR_FORWARD : MOTOR_REVERSE;
      startMotor(direction, duration);
      responseHeader(F("MOTOR "));
      Serial.print(argument);
      Serial.print(F(" STARTED MS "));
      Serial.print(duration);
      finishResponse();
      return;
    }
  }
  if (!strcmp(command, "MOTION_STATUS") && !argument) {
    responseHeader(F("MOTION MOTOR "));
    Serial.print(motorDirection == MOTOR_FORWARD ? F("FORWARD") :
        motorDirection == MOTOR_REVERSE ? F("REVERSE") : F("OFF"));
    Serial.print(F(" VALVE "));
    Serial.print(valveOn ? F("ON") : F("OFF"));
    finishResponse();
    return;
  }
  if (!strcmp(command, "ANGLE") && !argument) {
    uint16_t angle;
    responseHeader(F("ANGLE "));
    if (readAs5600RawAngle(angle)) Serial.print(angle);
    else Serial.print(-1);
    finishResponse();
    return;
  }
  if (!strcmp(command, "READ_ENCODER") && !argument) {
    updateEncoder();
    responseHeader(F("ENCODER "));
    Serial.print(encoderAvailable ? encoderPosition : -999999);
    finishResponse();
    return;
  }
  if (!strcmp(command, "RESET_ENCODER") && !argument) {
    uint16_t angle;
    if (!readAs5600RawAngle(angle)) {
      sendResponse(F("ERR ENCODER UNAVAILABLE"));
      return;
    }
    encoderPosition = 0;
    lastRawAngle = angle;
    encoderInitialized = true;
    encoderAvailable = true;
    sendResponse(F("ENCODER 0"));
    return;
  }
  if (!strcmp(command, "STATUS") && !argument) {
    sendStatus();
    return;
  }
  sendResponse(F("ERR COMMAND"));
}

void setup() {
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, LOW);
  digitalWrite(VALVE_IN1_PIN, LOW);
  digitalWrite(VALVE_IN2_PIN, LOW);
  pinMode(MOTOR_IN1_PIN, OUTPUT);
  pinMode(MOTOR_IN2_PIN, OUTPUT);
  pinMode(VALVE_IN1_PIN, OUTPUT);
  pinMode(VALVE_IN2_PIN, OUTPUT);

  digitalWrite(UPDI_GATE_PIN, LOW);
  pinMode(UPDI_GATE_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH);
  pinMode(STATUS_LED_PIN, OUTPUT);

  Wire.begin();
  Serial.begin(LINK_BAUD);
  releaseBus();
  updateEncoder();
  lastBlinkAt = millis();
}

void loop() {
  static char line[48];
  static uint8_t length = 0;
  static bool overflow = false;

  if (motorDirection != MOTOR_OFF && millis() - motorStartedAt >= motorRunDuration) {
    stopMotor();
  }
  if (valveOn && millis() - valveStartedAt >= MAX_VALVE_ON_MS) {
    setValve(false);
  }
  updateStatusLed();
  updateEncoder();
  while (Serial.available()) {
    const char character = (char)Serial.read();
    if (character == '\n') {
      if (!overflow && length > 0) {
        line[length] = '\0';
        handleCommand(line);
      }
      length = 0;
      overflow = false;
      releaseBus();
    } else if (character != '\r') {
      if (length < sizeof(line) - 1) line[length++] = character;
      else overflow = true;
    }
  }
}
