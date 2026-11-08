#ifndef ANDRUINO_ATTINY_BUS_NODE_H
#define ANDRUINO_ATTINY_BUS_NODE_H

#include <Arduino.h>
#include <EEPROM.h>
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>

#ifndef NODE_ADDRESS
#error "Define NODE_ADDRESS as 1, 2, or 3 in the sketch"
#endif

#if NODE_ADDRESS < 1 || NODE_ADDRESS > 3
#error "NODE_ADDRESS must be 1, 2, or 3"
#endif

namespace {
const uint16_t LINK_BAUD = 2400;
const uint16_t BUS_TURNAROUND_MS = 10;
const uint16_t ALLOWED_BLINK_INTERVALS[] = {200, 1000};
const uint8_t FLASH_SELECT_PIN = PIN_PB0;
const uint8_t EEPROM_MODE_ADDRESS = 0;
const uint8_t EEPROM_SELECTED_ADDRESS = 1;
const uint8_t EEPROM_MAGIC_ADDRESS = 2;
const uint8_t EEPROM_MAGIC = 0xA7;

enum NodeMode { NODE_COMM, NODE_FLASH };
NodeMode nodeMode = NODE_COMM;
uint8_t selectedNode = 0;
uint16_t blinkIntervalMs = 1000;
uint32_t lastBlinkAt = 0;
bool ledOn = false;
bool valveOn = false;
bool motorForward = false;
bool motorReverse = false;
uint16_t motorRemainingMs = 0;
uint32_t motorStartedAt = 0;
uint16_t motorDurationMs = 0;

void setNodeLed(bool on) {
  digitalWrite(PIN_PA5, on ? LOW : HIGH);
}

bool nodeLedOn() {
  return nodeMode == NODE_FLASH ? selectedNode == NODE_ADDRESS : ledOn;
}

void updateFlashSelectPin() {
  if (nodeMode != NODE_FLASH) {
    digitalWrite(FLASH_SELECT_PIN, LOW);
    return;
  }
  digitalWrite(FLASH_SELECT_PIN, ledOn ? LOW : HIGH);
}

void releaseBus() {
  USART0.CTRLB &= (uint8_t)~USART_TXEN_bm;
  pinMode(PIN_PB2, INPUT);
}

void startTransmit() {
  digitalWrite(PIN_PB2, HIGH);
  pinMode(PIN_PB2, OUTPUT);
  USART0.CTRLB |= USART_TXEN_bm;
}

void sendResponse(const __FlashStringHelper *payload) {
  delay(BUS_TURNAROUND_MS);
  startTransmit();
  Serial.print(F("NODE "));
  Serial.print(NODE_ADDRESS);
  Serial.print(' ');
  Serial.println(payload);
  Serial.flush();
  releaseBus();
}

bool parseBlinkInterval(const char *text, uint16_t &interval) {
  if (text == NULL || *text == '\0') return false;
  char *end = NULL;
  const long parsed = strtol(text, &end, 10);
  if (*end != '\0') return false;
  for (uint8_t i = 0; i < sizeof(ALLOWED_BLINK_INTERVALS) / sizeof(ALLOWED_BLINK_INTERVALS[0]); ++i) {
    if (parsed == ALLOWED_BLINK_INTERVALS[i]) {
      interval = (uint16_t)parsed;
      return true;
    }
  }
  return false;
}

void stopVirtualMotor() {
  motorForward = false;
  motorReverse = false;
  motorRemainingMs = 0;
}

void sendStatus() {
  delay(BUS_TURNAROUND_MS);
  startTransmit();
  Serial.print(F("NODE ")); Serial.print(NODE_ADDRESS);
  Serial.print(F(" STATUS MODE ")); Serial.print(nodeMode == NODE_COMM ? F("COMM") : F("FLASH"));
  Serial.print(F(" VALVE ")); Serial.print(valveOn ? F("ON") : F("OFF"));
  Serial.print(F(" MOTOR ")); Serial.print(motorForward ? F("FORWARD") : motorReverse ? F("REVERSE") : F("OFF"));
  Serial.print(F(" LED ")); Serial.print(nodeLedOn() ? F("ON") : F("OFF"));
  Serial.print(F(" UPDI ")); Serial.println(digitalRead(FLASH_SELECT_PIN) == LOW ? F("ENABLED") : F("DISABLED"));
  Serial.flush();
  releaseBus();
}

void updateVirtualMotor() {
  if (!motorForward && !motorReverse) return;
  if (motorDurationMs == 0 || millis() - motorStartedAt < motorDurationMs) return;
  stopVirtualMotor();
  motorDurationMs = 0;
}

void handleBusCommand(char *line) {
  char *addressText = strtok(line, " \t");
  char *command = strtok(NULL, " \t");
  if (addressText == NULL || command == NULL ||
      strtol(addressText, NULL, 10) != NODE_ADDRESS) return;

  if (strcmp(command, "PING") == 0 && strtok(NULL, " \t") == NULL) {
    sendResponse(F("PONG"));
    return;
  }
  if (strcmp(command, "VERSION") == 0 && strtok(NULL, " \t") == NULL) {
    sendResponse(F("VERSION ANDRUINO-ATTINY1614-PB0-6"));
    return;
  }
  if (strcmp(command, "UPDI") == 0) {
    char *action = strtok(NULL, " \t");
    if (action != NULL && strcmp(action, "ENABLE") == 0 && strtok(NULL, " \t") == NULL) {
      digitalWrite(FLASH_SELECT_PIN, LOW);
      sendResponse(F("UPDI ENABLED"));
      return;
    }
    if (action != NULL && strcmp(action, "DISABLE") == 0 && strtok(NULL, " \t") == NULL) {
      digitalWrite(FLASH_SELECT_PIN, HIGH);
      sendResponse(F("UPDI DISABLED"));
      return;
    }
  }
  if (strcmp(command, "STATUS") == 0 && strtok(NULL, " \t") == NULL) {
    sendStatus();
    return;
  }
  if (strcmp(command, "MODE") == 0) {
    char *mode = strtok(NULL, " \t");
    if (mode != NULL && strcmp(mode, "COMM") == 0 && strtok(NULL, " \t") == NULL) {
      nodeMode = NODE_COMM;
      EEPROM.update(EEPROM_MODE_ADDRESS, nodeMode);
      EEPROM.update(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC);
      ledOn = false;
      setNodeLed(false);
      updateFlashSelectPin();
      lastBlinkAt = millis();
      sendResponse(F("MODE COMM"));
      return;
    }
    if (mode != NULL && strcmp(mode, "FLASH") == 0 && strtok(NULL, " \t") == NULL) {
      nodeMode = NODE_FLASH;
      EEPROM.update(EEPROM_MODE_ADDRESS, nodeMode);
      EEPROM.update(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC);
      ledOn = selectedNode == NODE_ADDRESS;
      setNodeLed(ledOn);
      updateFlashSelectPin();
      sendResponse(F("MODE FLASH"));
      return;
    }
  }
  if (strcmp(command, "SELECT") == 0) {
    char *selectedText = strtok(NULL, " \t");
    char *end = NULL;
    const long requested = selectedText == NULL ? -1 : strtol(selectedText, &end, 10);
    if (selectedText == NULL || *end != '\0' || requested < 0 || requested > 3 ||
        strtok(NULL, " \t") != NULL) {
      sendResponse(F("ERR SELECT USES NODE 0, 1, 2, OR 3"));
      return;
    }
    selectedNode = (uint8_t)requested;
    EEPROM.update(EEPROM_SELECTED_ADDRESS, selectedNode);
    EEPROM.update(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC);
    if (nodeMode == NODE_FLASH) {
      ledOn = selectedNode == NODE_ADDRESS;
      setNodeLed(ledOn);
    }
    updateFlashSelectPin();
    sendResponse(selectedNode == NODE_ADDRESS ? F("SELECTED") : F("NOT SELECTED"));
    return;
  }
  if (strcmp(command, "STOP") == 0 && strtok(NULL, " \t") == NULL) {
    stopVirtualMotor();
    valveOn = false;
    sendResponse(F("STOPPED VALVE OFF MOTOR OFF"));
    return;
  }
  if (strcmp(command, "VALVE") == 0) {
    char *state = strtok(NULL, " \t");
    if (state != NULL && strtok(NULL, " \t") == NULL &&
        (strcmp(state, "ON") == 0 || strcmp(state, "OFF") == 0)) {
      valveOn = strcmp(state, "ON") == 0;
      sendResponse(valveOn ? F("VALVE ON") : F("VALVE OFF"));
      return;
    }
  }
  if (strcmp(command, "MOTOR") == 0) {
    char *state = strtok(NULL, " \t");
    if (state != NULL && strcmp(state, "OFF") == 0 && strtok(NULL, " \t") == NULL) {
      stopVirtualMotor();
      motorDurationMs = 0;
      sendResponse(F("MOTOR OFF"));
      return;
    }
    char *durationText = strtok(NULL, " \t");
    char *end = NULL;
    if (state != NULL && durationText != NULL &&
        (strcmp(state, "FORWARD") == 0 || strcmp(state, "REVERSE") == 0)) {
      const long duration = strtol(durationText, &end, 10);
      if (*durationText != '\0' && *end == '\0' && duration >= 1 && duration <= 60000 &&
          strtok(NULL, " \t") == NULL) {
        motorForward = strcmp(state, "FORWARD") == 0;
        motorReverse = strcmp(state, "REVERSE") == 0;
        motorDurationMs = (uint16_t)duration;
        motorStartedAt = millis();
        motorRemainingMs = motorDurationMs;
        delay(BUS_TURNAROUND_MS);
        startTransmit();
        Serial.print(F("NODE ")); Serial.print(NODE_ADDRESS);
        Serial.print(F(" MOTOR ")); Serial.print(state);
        Serial.print(F(" STARTED MS ")); Serial.println(duration);
        Serial.flush(); releaseBus();
        return;
      }
    }
  }
  if (strcmp(command, "MOTION_STATUS") == 0 && strtok(NULL, " \t") == NULL) {
    delay(BUS_TURNAROUND_MS);
    startTransmit();
    Serial.print(F("NODE ")); Serial.print(NODE_ADDRESS);
    Serial.print(F(" MOTION MOTOR ")); Serial.print(motorForward ? F("FORWARD") : motorReverse ? F("REVERSE") : F("OFF"));
    Serial.print(F(" VALVE ")); Serial.println(valveOn ? F("ON") : F("OFF"));
    Serial.flush(); releaseBus();
    return;
  }
  if ((strcmp(command, "READ_ENCODER") == 0 || strcmp(command, "ANGLE") == 0 ||
       strcmp(command, "RESET_ENCODER") == 0) && strtok(NULL, " \t") == NULL) {
    if (strcmp(command, "ANGLE") == 0) sendResponse(F("ANGLE -1"));
    else if (strcmp(command, "RESET_ENCODER") == 0) sendResponse(F("ENCODER 0"));
    else sendResponse(F("ENCODER -999999"));
    return;
  }
  if (strcmp(command, "BLINK") == 0) {
    uint16_t requestedInterval;
    if (nodeMode == NODE_COMM &&
        parseBlinkInterval(strtok(NULL, " \t"), requestedInterval) &&
        strtok(NULL, " \t") == NULL) {
      blinkIntervalMs = requestedInterval;
      lastBlinkAt = millis();
      ledOn = false;
      setNodeLed(false);
      delay(BUS_TURNAROUND_MS);
      startTransmit();
      Serial.print(F("NODE "));
      Serial.print(NODE_ADDRESS);
      Serial.print(F(" BLINK "));
      Serial.println(blinkIntervalMs);
      Serial.flush();
      releaseBus();
      return;
    }
  }
  sendResponse(F("ERR COMMAND"));
}

void updateBlink() {
  if (nodeMode == NODE_FLASH) {
    ledOn = selectedNode == NODE_ADDRESS;
    setNodeLed(ledOn);
    updateFlashSelectPin();
    return;
  }
  const uint32_t now = millis();
  if (now - lastBlinkAt >= blinkIntervalMs) {
    lastBlinkAt = now;
    ledOn = !ledOn;
    setNodeLed(ledOn);
  }
}
}

void setup() {
  // A node reset or application replacement must always leave UPDI and the
  // communication bus enabled. The Uno explicitly restores FLASH/SELECT
  // state after startup when that state is required.
  nodeMode = NODE_COMM;
  selectedNode = 0;
  EEPROM.update(EEPROM_MODE_ADDRESS, NODE_COMM);
  EEPROM.update(EEPROM_SELECTED_ADDRESS, 0);
  EEPROM.update(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC);
  pinMode(PIN_PA5, OUTPUT);
  pinMode(FLASH_SELECT_PIN, OUTPUT);
  ledOn = nodeMode == NODE_FLASH && selectedNode == NODE_ADDRESS;
  setNodeLed(ledOn);
  updateFlashSelectPin();
  Serial.begin(LINK_BAUD);
  releaseBus();
  lastBlinkAt = millis();
}

void loop() {
  static char line[32];
  static uint8_t length = 0;
  static bool overflow = false;
  static uint32_t lastUpdiRefreshAt = 0;

  const uint32_t now = millis();
  if (nodeMode == NODE_COMM && now - lastUpdiRefreshAt >= 100) {
    digitalWrite(FLASH_SELECT_PIN, LOW);
    lastUpdiRefreshAt = now;
  }
  updateVirtualMotor();
  updateBlink();
  while (Serial.available()) {
    const char character = (char)Serial.read();
    if (character == '\n') {
      if (!overflow && length > 0) {
        line[length] = '\0';
        handleBusCommand(line);
      }
      length = 0;
      overflow = false;
      releaseBus();
    } else if (character != '\r') {
      if (length < (uint8_t)(sizeof(line) - 1)) line[length++] = character;
      else overflow = true;
    }
    updateBlink();
  }
}

#endif
