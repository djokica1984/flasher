#include <Arduino.h>
#include <Wire.h>
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>

const uint8_t NODE_ADDRESS = 1;
const uint8_t UPDI_GATE_PIN = PIN_PB0;
const uint8_t BUS_TX_PIN = PIN_PB2;
const uint16_t LINK_BAUD = 2400;
const uint16_t BUS_TURNAROUND_MS = 10;
const uint8_t AS5600_ADDRESS = 0x36;
const uint8_t AS5600_RAW_ANGLE_HIGH = 0x0C;
const uint16_t AS5600_COUNTS_PER_TURN = 4096;
const uint16_t ENCODER_SAMPLE_INTERVAL_MS = 5;

enum NodeMode { NODE_COMM, NODE_FLASH };

NodeMode nodeMode = NODE_COMM;
uint8_t selectedNode = 0;
bool encoderAvailable = false;
bool encoderInitialized = false;
uint16_t lastRawAngle = 0;
int32_t encoderPosition = 0;
uint32_t lastEncoderSampleAt = 0;
uint8_t lastI2cError = 0;
uint8_t lastI2cDetail = 0;

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

void updateUpdiGate() {
  const bool enabled = nodeMode == NODE_COMM || selectedNode == NODE_ADDRESS;
  digitalWrite(UPDI_GATE_PIN, enabled ? LOW : HIGH);
}

bool readAs5600RawAngle(uint16_t &angle) {
  Wire.beginTransmission(AS5600_ADDRESS);
  Wire.write(AS5600_RAW_ANGLE_HIGH);
  const uint8_t writeStatus = Wire.endTransmission(false);
  if (writeStatus != 0) {
    lastI2cError = 1;
    lastI2cDetail = writeStatus;
    return false;
  }
  const uint8_t received = Wire.requestFrom(AS5600_ADDRESS, (uint8_t)2);
  if (received != 2) {
    lastI2cError = 2;
    lastI2cDetail = received;
    return false;
  }
  if (Wire.available() < 2) {
    lastI2cError = 3;
    lastI2cDetail = Wire.available();
    return false;
  }

  const uint16_t high = (uint8_t)Wire.read();
  const uint16_t low = (uint8_t)Wire.read();
  angle = ((high << 8) | low) & 0x0FFF;
  lastI2cError = 0;
  lastI2cDetail = 0;
  return true;
}

void recordAngle(uint16_t angle) {
  encoderAvailable = true;
  if (!encoderInitialized) {
    lastRawAngle = angle;
    encoderInitialized = true;
    return;
  }

  int16_t delta = (int16_t)angle - (int16_t)lastRawAngle;
  if (delta > (int16_t)(AS5600_COUNTS_PER_TURN / 2)) {
    delta -= (int16_t)AS5600_COUNTS_PER_TURN;
  } else if (delta < -(int16_t)(AS5600_COUNTS_PER_TURN / 2)) {
    delta += (int16_t)AS5600_COUNTS_PER_TURN;
  }
  encoderPosition += delta;
  lastRawAngle = angle;
}

void sampleEncoder() {
  uint16_t angle;
  if (!readAs5600RawAngle(angle)) {
    encoderAvailable = false;
    encoderInitialized = false;
    return;
  }
  recordAngle(angle);
}

void reportAngle() {
  uint16_t angle;
  responseHeader(F("ANGLE "));
  if (readAs5600RawAngle(angle)) {
    recordAngle(angle);
    Serial.print(angle);
  } else {
    encoderAvailable = false;
    encoderInitialized = false;
    Serial.print(-1);
    if (lastI2cError == 1) {
      Serial.print(F(" I2C_WRITE_STATUS "));
      Serial.print(lastI2cDetail);
    } else if (lastI2cError == 2) {
      Serial.print(F(" I2C_READ_COUNT "));
      Serial.print(lastI2cDetail);
    } else {
      Serial.print(F(" I2C_DATA_COUNT "));
      Serial.print(lastI2cDetail);
    }
  }
  finishResponse();
}

void reportEncoder() {
  sampleEncoder();
  responseHeader(F("ENCODER "));
  Serial.print(encoderAvailable ? encoderPosition : -999999L);
  finishResponse();
}

void reportStatus() {
  sampleEncoder();
  responseHeader(F("STATUS ENCODER "));
  if (encoderAvailable) {
    Serial.print(F("OK ANGLE "));
    Serial.print(lastRawAngle);
    Serial.print(F(" COUNTS "));
    Serial.print(encoderPosition);
  } else {
    Serial.print(F("UNAVAILABLE ANGLE -1 COUNTS -999999"));
  }
  finishResponse();
}

bool parseNode(const char *text, uint8_t &node) {
  if (!text || !*text) return false;
  char *end = NULL;
  const long parsed = strtol(text, &end, 10);
  if (*end != '\0' || parsed < 0 || parsed > 7) return false;
  node = (uint8_t)parsed;
  return true;
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
    sendResponse(F("VERSION ATTINY1614-AS5600-1"));
    return;
  }
  if (!strcmp(command, "MODE")) {
    if (!argument || strtok(NULL, " \t")) {
      sendResponse(F("ERR COMMAND"));
    } else if (!strcmp(argument, "COMM")) {
      nodeMode = NODE_COMM;
      updateUpdiGate();
      sendResponse(F("MODE COMM"));
    } else if (!strcmp(argument, "FLASH")) {
      nodeMode = NODE_FLASH;
      updateUpdiGate();
      sendResponse(F("MODE FLASH"));
    } else {
      sendResponse(F("ERR COMMAND"));
    }
    return;
  }
  if (!strcmp(command, "SELECT")) {
    uint8_t requested;
    if (!parseNode(argument, requested) || strtok(NULL, " \t")) {
      sendResponse(F("ERR SELECT USES NODE 0 THROUGH 7"));
      return;
    }
    selectedNode = requested;
    updateUpdiGate();
    sendResponse(selectedNode == NODE_ADDRESS ? F("SELECTED") : F("NOT SELECTED"));
    return;
  }
  if (!strcmp(command, "UPDI")) {
    if (argument && !strtok(NULL, " \t") &&
        (!strcmp(argument, "ENABLE") || !strcmp(argument, "DISABLE"))) {
      const bool enabled = !strcmp(argument, "ENABLE");
      digitalWrite(UPDI_GATE_PIN, enabled ? LOW : HIGH);
      responseHeader(F("UPDI "));
      Serial.print(enabled ? F("ENABLED") : F("DISABLED"));
      finishResponse();
      return;
    }
  }
  if (!strcmp(command, "ANGLE") && !argument) {
    reportAngle();
    return;
  }
  if (!strcmp(command, "READ_ENCODER") && !argument) {
    reportEncoder();
    return;
  }
  if (!strcmp(command, "RESET_ENCODER") && !argument) {
    uint16_t angle;
    if (!readAs5600RawAngle(angle)) {
      sendResponse(F("ERR ENCODER UNAVAILABLE"));
      return;
    }
    encoderPosition = 0;
    encoderAvailable = true;
    encoderInitialized = true;
    lastRawAngle = angle;
    sendResponse(F("ENCODER 0"));
    return;
  }
  if (!strcmp(command, "STATUS") && !argument) {
    reportStatus();
    return;
  }
  sendResponse(F("ERR COMMAND"));
}

void setup() {
  digitalWrite(UPDI_GATE_PIN, LOW);
  pinMode(UPDI_GATE_PIN, OUTPUT);
  Wire.pins(PIN_PA1, PIN_PA2);
  Wire.begin();
  Serial.begin(LINK_BAUD);
  releaseBus();
  sampleEncoder();
  lastEncoderSampleAt = millis();
}

void loop() {
  static char line[40];
  static uint8_t length = 0;
  static bool overflow = false;
  const uint32_t now = millis();

  if (now - lastEncoderSampleAt >= ENCODER_SAMPLE_INTERVAL_MS) {
    lastEncoderSampleAt = now;
    sampleEncoder();
  }

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
      if (length < sizeof(line) - 1) {
        line[length++] = character;
      } else {
        overflow = true;
      }
    }
  }
}
