#ifndef ANDRUINO_ATTINY_BUS_NODE_H
#define ANDRUINO_ATTINY_BUS_NODE_H

#include <Arduino.h>
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

enum NodeMode { NODE_COMM, NODE_FLASH };
NodeMode nodeMode = NODE_COMM;
uint16_t blinkIntervalMs = 1000;
uint32_t lastBlinkAt = 0;
bool ledOn = false;

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
  Serial.println(blinkIntervalMs);
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
    sendResponse(F("VERSION ANDRUINO-ATTINY1614-BUS-1"));
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
      ledOn = false;
      digitalWrite(PIN_PA5, LOW);
      lastBlinkAt = millis();
      sendResponse(F("MODE COMM"));
      return;
    }
    if (mode != NULL && strcmp(mode, "FLASH") == 0 && strtok(NULL, " \t") == NULL) {
      nodeMode = NODE_FLASH;
      ledOn = false;
      digitalWrite(PIN_PA5, LOW);
      sendResponse(F("MODE FLASH"));
      return;
    }
  }
  if (strcmp(command, "BLINK") == 0) {
    uint16_t requestedInterval;
    if (nodeMode == NODE_COMM &&
        parseBlinkInterval(strtok(NULL, " \t"), requestedInterval) &&
        strtok(NULL, " \t") == NULL) {
      blinkIntervalMs = requestedInterval;
      lastBlinkAt = millis();
      ledOn = false;
      digitalWrite(PIN_PA5, LOW);
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
  if (nodeMode != NODE_COMM) {
    if (ledOn) {
      ledOn = false;
      digitalWrite(PIN_PA5, LOW);
    }
    return;
  }
  const uint32_t now = millis();
  if (now - lastBlinkAt >= blinkIntervalMs) {
    lastBlinkAt = now;
    ledOn = !ledOn;
    digitalWrite(PIN_PA5, ledOn ? HIGH : LOW);
  }
}
}

void setup() {
  pinMode(PIN_PA5, OUTPUT);
  digitalWrite(PIN_PA5, LOW);
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
