#ifndef ANDRUINO_ATTINY_BUS_NODE_H
#define ANDRUINO_ATTINY_BUS_NODE_H

#include <Arduino.h>
#include <EEPROM.h>
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>

#ifndef NODE_ADDRESS
#error "Define NODE_ADDRESS as 1 or 2 in the sketch"
#endif

#if NODE_ADDRESS < 1 || NODE_ADDRESS > 2
#error "NODE_ADDRESS must be 1 or 2"
#endif

namespace {
const uint16_t LINK_BAUD = 2400;
const uint16_t BUS_TURNAROUND_MS = 10;
const uint16_t ALLOWED_BLINK_INTERVALS[] = {200, 1000};
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

void setNodeLed(bool on) {
  digitalWrite(PIN_PA5, on ? LOW : HIGH);
}

bool nodeLedOn() {
  return nodeMode == NODE_FLASH ? selectedNode == NODE_ADDRESS : ledOn;
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

void sendStatus() {
  delay(BUS_TURNAROUND_MS);
  startTransmit();
  Serial.print(F("NODE "));
  Serial.print(NODE_ADDRESS);
  Serial.print(F(" STATUS MODE "));
  Serial.print(nodeMode == NODE_COMM ? F("COMM") : F("FLASH"));
  Serial.print(F(" BLINK "));
  Serial.print(blinkIntervalMs);
  Serial.print(F(" SELECTION "));
  Serial.print(selectedNode == NODE_ADDRESS ? F("SELECTED") : F("NOT_SELECTED"));
  Serial.print(F(" LED "));
  Serial.println(nodeLedOn() ? F("ON") : F("OFF"));
  Serial.flush();
  releaseBus();
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
    sendResponse(F("VERSION ANDRUINO-ATTINY1614-BUS-2"));
    return;
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
      lastBlinkAt = millis();
      sendResponse(F("MODE COMM"));
      return;
    }
    if (mode != NULL && strcmp(mode, "FLASH") == 0 && strtok(NULL, " \t") == NULL) {
      nodeMode = NODE_FLASH;
      EEPROM.update(EEPROM_MODE_ADDRESS, nodeMode);
      EEPROM.update(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC);
      ledOn = false;
      setNodeLed(false);
      sendResponse(F("MODE FLASH"));
      return;
    }
  }
  if (strcmp(command, "SELECT") == 0) {
    char *selectedText = strtok(NULL, " \t");
    char *end = NULL;
    const long requested = selectedText == NULL ? -1 : strtol(selectedText, &end, 10);
    if (selectedText == NULL || *end != '\0' || requested < 1 || requested > 2 ||
        strtok(NULL, " \t") != NULL) {
      sendResponse(F("ERR SELECT USES NODE 1 OR 2"));
      return;
    }
    selectedNode = (uint8_t)requested;
    EEPROM.update(EEPROM_SELECTED_ADDRESS, selectedNode);
    EEPROM.update(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC);
    if (nodeMode == NODE_FLASH) {
      ledOn = selectedNode == NODE_ADDRESS;
      setNodeLed(ledOn);
    }
    sendResponse(selectedNode == NODE_ADDRESS ? F("SELECTED") : F("NOT SELECTED"));
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
  if (EEPROM.read(EEPROM_MAGIC_ADDRESS) == EEPROM_MAGIC) {
    const uint8_t storedMode = EEPROM.read(EEPROM_MODE_ADDRESS);
    const uint8_t storedSelection = EEPROM.read(EEPROM_SELECTED_ADDRESS);
    if (storedMode <= NODE_FLASH && storedSelection <= 2) {
      nodeMode = (NodeMode)storedMode;
      selectedNode = storedSelection;
    }
  } else {
    EEPROM.update(EEPROM_MODE_ADDRESS, NODE_COMM);
    EEPROM.update(EEPROM_SELECTED_ADDRESS, 0);
    EEPROM.update(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC);
  }
  pinMode(PIN_PA5, OUTPUT);
  ledOn = nodeMode == NODE_FLASH && selectedNode == NODE_ADDRESS;
  setNodeLed(ledOn);
  Serial.begin(LINK_BAUD);
  releaseBus();
  lastBlinkAt = millis();
}

void loop() {
  static char line[32];
  static uint8_t length = 0;
  static bool overflow = false;

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
