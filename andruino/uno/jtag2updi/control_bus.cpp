#include "control_bus.h"

#include <Arduino.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "JICE_io.h"

namespace {
const uint8_t BUS_PIN = 5;
const uint8_t FIRST_NODE = 1;
const uint8_t LAST_NODE = 7;
uint8_t activeNodeMask = (_BV(1) | _BV(2) | _BV(3) | _BV(4) | _BV(5) | _BV(6) | _BV(7));
const uint16_t BUS_BAUD = 2400;
const uint16_t RESPONSE_TIMEOUT_MS = 700;
const uint8_t REQUEST_ATTEMPTS = 3;
const uint8_t RETRY_DELAY_MS = 20;
const uint8_t RESPONSE_SIZE = 96;
const uint8_t EEPROM_MODE_ADDRESS = 0;
const uint8_t EEPROM_SELECTED_ADDRESS = 1;
const uint8_t EEPROM_MAGIC_ADDRESS = 2;
const uint8_t EEPROM_MAGIC = 0xA7;

SoftwareSerial busSerial(BUS_PIN, BUS_PIN);
bool communicationMode = true;
uint8_t selectedNode = 0;

struct BusRuntimeState {
  uint8_t sreg;
  uint8_t tccr0a;
  uint8_t tccr0b;
  uint8_t tcnt0;
  uint8_t ocr0a;
  uint8_t tifr0;
  uint8_t timsk0;
};

BusRuntimeState beginBusRuntime() {
  BusRuntimeState state;
  state.sreg = SREG;
  cli();
  state.tccr0a = TCCR0A;
  state.tccr0b = TCCR0B;
  state.tcnt0 = TCNT0;
  state.ocr0a = OCR0A;
  state.tifr0 = TIFR0;
  state.timsk0 = TIMSK0;
  TCCR0A = _BV(WGM01) | _BV(WGM00);
  TCCR0B = _BV(CS01) | _BV(CS00);
  TCNT0 = 0;
  OCR0A = 0;
  TIFR0 = _BV(TOV0);
  TIMSK0 |= _BV(TOIE0);
  sei();
  return state;
}

void endBusRuntime(const BusRuntimeState &state) {
  cli();
  TCCR0A = state.tccr0a;
  TCCR0B = state.tccr0b;
  TCNT0 = state.tcnt0;
  OCR0A = state.ocr0a;
  TIFR0 = state.tifr0;
  TIMSK0 = state.timsk0;
  SREG = state.sreg;
}

void releaseBus() {
  pinMode(BUS_PIN, INPUT_PULLUP);
  busSerial.listen();
}

void sendBusCommand(uint8_t node, const char *command) {
  busSerial.stopListening();
  pinMode(BUS_PIN, OUTPUT);
  digitalWrite(BUS_PIN, HIGH);
  busSerial.print(node);
  busSerial.print(' ');
  busSerial.println(command);
  busSerial.flush();
  releaseBus();
}

bool responseMatchesCommand(uint8_t node, const char *command, const char *response) {
  if (strncmp(command, "PING", 4) == 0) {
    char expected[16];
    snprintf(expected, sizeof(expected), "NODE %u PONG", node);
    return strcmp(response, expected) == 0;
  }
  if (strncmp(command, "BLINK ", 6) == 0) {
    char expected[24];
    snprintf(expected, sizeof(expected), "NODE %u BLINK %s", node, command + 6);
    return strcmp(response, expected) == 0;
  }
  if (strncmp(command, "LED ", 4) == 0) {
    char expected[24];
    snprintf(expected, sizeof(expected), "NODE %u LED %s", node, command + 4);
    return strcmp(response, expected) == 0;
  }
  char prefix[12];
  snprintf(prefix, sizeof(prefix), "NODE %u ", node);
  return strncmp(response, prefix, strlen(prefix)) == 0;
}

bool readBusResponse(uint8_t node, const char *command, char *response, size_t responseSize) {
  size_t length = 0;
  const uint32_t deadline = millis() + RESPONSE_TIMEOUT_MS;
  while ((int32_t)(millis() - deadline) < 0) {
    while (busSerial.available()) {
      const char character = (char)busSerial.read();
      if (character == '\r') continue;
      if (character == '\n') {
        response[length] = '\0';
        if (responseMatchesCommand(node, command, response)) return true;
        length = 0;
        continue;
      }
      if ((uint8_t)character < 0x20 || (uint8_t)character > 0x7e) continue;
      if (length + 1 >= responseSize) {
        length = 0;
        continue;
      }
      response[length++] = character;
    }
  }
  response[0] = '\0';
  return false;
}

bool nodeIsActive(uint8_t node) {
  return node >= FIRST_NODE && node <= LAST_NODE && (activeNodeMask & _BV(node)) != 0;
}

bool requestNode(uint8_t node, const char *command, char *response, size_t responseSize) {
  if (node < FIRST_NODE || node > LAST_NODE) return false;
  for (uint8_t attempt = 0; attempt < REQUEST_ATTEMPTS; ++attempt) {
    while (busSerial.available()) busSerial.read();
    sendBusCommand(node, command);
    if (readBusResponse(node, command, response, responseSize)) return true;
    if (attempt + 1 < REQUEST_ATTEMPTS) delay(RETRY_DELAY_MS);
  }
  return false;
}

void writeHostLine(const char *line) {
  while (*line) JICE_io::put(*line++);
  JICE_io::put('\r');
  JICE_io::put('\n');
  JICE_io::flush();
}

bool parseNode(const char *text, uint8_t &node) {
  if (text == NULL || *text == '\0') return false;
  char *end = NULL;
  const long parsed = strtol(text, &end, 10);
  if (*end != '\0' || parsed < FIRST_NODE || parsed > LAST_NODE) return false;
  node = (uint8_t)parsed;
  return true;
}

void makeNodeError(uint8_t node, char *response, size_t responseSize) {
  snprintf(response, responseSize, "ERR NO RESPONSE FROM NODE %u", node);
}

bool setNodesMode(bool communication) {
  char modeCommand[12] = "MODE COMM";
  if (!communication) strcpy(modeCommand, "MODE FLASH");
  char response[RESPONSE_SIZE];
  for (uint8_t node = FIRST_NODE; node <= LAST_NODE; ++node) {
    if (!nodeIsActive(node)) continue;
    if (!requestNode(node, modeCommand, response, sizeof(response))) {
      makeNodeError(node, response, sizeof(response));
      writeHostLine(response);
      return false;
    }
  }
  communicationMode = communication;
  EEPROM.update(EEPROM_MODE_ADDRESS, communication ? 0 : 1);
  EEPROM.update(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC);
  return true;
}

void writeSystemModeResponse() {
  char result[RESPONSE_SIZE];
  if (selectedNode == 0) {
    snprintf(result, sizeof(result), "OK MODE %s; NODES 1,2,3,4,5,6,7 MODE %s; SELECT NONE",
             communicationMode ? "COMM" : "FLASH",
             communicationMode ? "COMM" : "FLASH");
  } else {
    snprintf(result, sizeof(result),
             "OK MODE %s; NODES 1,2,3,4,5,6,7 MODE %s; SELECT NODE %u",
             communicationMode ? "COMM" : "FLASH",
             communicationMode ? "COMM" : "FLASH",
             selectedNode);
  }
  writeHostLine(result);
}

void handleCommand(char *line) {
  for (char *character = line; *character; ++character) {
    *character = (char)toupper((unsigned char)*character);
  }

  char *command = strtok(line, " \t");
  if (command == NULL) {
    writeHostLine("ERR EMPTY COMMAND");
    return;
  }

  if (strcmp(command, "UPDI") == 0) {
    char *action = strtok(NULL, " \t");
    char *nodeText = strtok(NULL, " \t");
    uint8_t node;
    if (action == NULL || nodeText == NULL || strtok(NULL, " \t") != NULL ||
        (strcmp(action, "ENABLE") != 0 && strcmp(action, "DISABLE") != 0) ||
        !parseNode(nodeText, node)) {
      writeHostLine("ERR UPDI USES ENABLE|DISABLE NODE 1 THROUGH 7");
      return;
    }
    char nodeCommand[16];
    snprintf(nodeCommand, sizeof(nodeCommand), "UPDI %s", action);
    char response[RESPONSE_SIZE];
    if (!requestNode(node, nodeCommand, response, sizeof(response))) {
      makeNodeError(node, response, sizeof(response));
    }
    writeHostLine(response);
    return;
  }

  if (strcmp(command, "MODE") == 0) {
    char *mode = strtok(NULL, " \t");
    if (mode == NULL || strtok(NULL, " \t") != NULL) {
      writeHostLine("ERR FORMAT");
      return;
    }
    if (strcmp(mode, "COMM") == 0) {
      if (setNodesMode(true)) writeSystemModeResponse();
      return;
    }
    if (strcmp(mode, "FLASH") == 0) {
      if (setNodesMode(false)) writeSystemModeResponse();
      return;
    }
    writeHostLine("ERR MODE USE COMM OR FLASH");
    return;
  }

  if (strcmp(command, "RESTORE") == 0) {
    if (strtok(NULL, " \t") != NULL) {
      writeHostLine("ERR FORMAT");
      return;
    }
    if (!setNodesMode(communicationMode)) return;
    char response[RESPONSE_SIZE];
    char selectCommand[12];
    snprintf(selectCommand, sizeof(selectCommand), "SELECT %u", selectedNode);
    for (uint8_t node = FIRST_NODE; node <= LAST_NODE; ++node) {
      if (!nodeIsActive(node)) continue;
      if (!requestNode(node, selectCommand, response, sizeof(response))) {
        makeNodeError(node, response, sizeof(response));
        writeHostLine(response);
        return;
      }
    }
    writeSystemModeResponse();
    return;
  }

  if (strcmp(command, "DISCOVER") == 0) {
    if (strtok(NULL, " \t") != NULL) {
      writeHostLine("ERR FORMAT");
      return;
    }
    char response[RESPONSE_SIZE];
    activeNodeMask = 0;
    char active[32] = "OK NODES";
    size_t activeLength = strlen(active);
    for (uint8_t node = FIRST_NODE; node <= LAST_NODE; ++node) {
      if (requestNode(node, "PING", response, sizeof(response))) {
        activeNodeMask |= _BV(node);
        activeLength += snprintf(active + activeLength,
                                 sizeof(active) - activeLength,
                                 "%s%u", activeLength == 8 ? " " : ",", node);
      }
    }
    if (activeNodeMask == 0) strcpy(active, "OK NODES NONE");
    writeHostLine(active);
    return;
  }

  if (strcmp(command, "SELECT") == 0) {
    uint8_t selected;
    char *nodeText = strtok(NULL, " \t");
    if (!parseNode(nodeText, selected) || strtok(NULL, " \t") != NULL) {
      writeHostLine("ERR SELECT USES NODE 1 THROUGH 7");
      return;
    }
    char response[RESPONSE_SIZE];
    if (selected == 3 && !nodeIsActive(3)) {
      if (!requestNode(3, "PING", response, sizeof(response))) {
        writeHostLine("ERR NODE 3 NOT DETECTED");
        return;
      }
      activeNodeMask |= _BV(3);
    }
    char selectCommand[12];
    snprintf(selectCommand, sizeof(selectCommand), "SELECT %u", selected);
    for (uint8_t node = FIRST_NODE; node <= LAST_NODE; ++node) {
      if (!nodeIsActive(node)) continue;
      if (!requestNode(node, selectCommand, response, sizeof(response))) {
        makeNodeError(node, response, sizeof(response));
        writeHostLine(response);
        return;
      }
    }
    selectedNode = selected;
    EEPROM.update(EEPROM_SELECTED_ADDRESS, selectedNode);
    EEPROM.update(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC);
    char result[RESPONSE_SIZE];
    snprintf(result, sizeof(result), "OK SELECT NODE %u; SYSTEM MODE %s",
             selected, communicationMode ? "COMM" : "FLASH");
    writeHostLine(result);
    return;
  }

  uint8_t node;
  char *nodeText = strtok(NULL, " \t");
  if (!parseNode(nodeText, node)) {
    writeHostLine("ERR NODE USES 1 THROUGH 7");
    return;
  }

  char forwardedCommand[48];
  if (strcmp(command, "PING") == 0 || strcmp(command, "STATUS") == 0 ||
      strcmp(command, "VERSION") == 0 || strcmp(command, "STOP") == 0 ||
      strcmp(command, "READ_ENCODER") == 0 || strcmp(command, "RESET_ENCODER") == 0 ||
      strcmp(command, "ANGLE") == 0 ||
      strcmp(command, "MOTION_STATUS") == 0) {
    if (strtok(NULL, " \t") != NULL) {
      writeHostLine("ERR FORMAT");
      return;
    }
    strcpy(forwardedCommand, command);
  } else if (strcmp(command, "LED") == 0) {
    char *state = strtok(NULL, " \t");
    char *tail = strtok(NULL, " \t");
    if (state != NULL && tail == NULL && (strcmp(state, "ON") == 0 || strcmp(state, "OFF") == 0)) {
      snprintf(forwardedCommand, sizeof(forwardedCommand), "LED %s", state);
    } else {
      writeHostLine("ERR LED USES ON|OFF");
      return;
    }
  } else if (strcmp(command, "BLINK") == 0) {
    char *interval = strtok(NULL, " \t");
    char *tail = strtok(NULL, " \t");
    char *end = NULL;
    const long blinkMs = interval == NULL ? -1 : strtol(interval, &end, 10);
    if (interval == NULL || *end != '\0' || blinkMs < 50 || blinkMs > 60000 || tail != NULL) {
      writeHostLine("ERR BLINK USES 50..60000 MS");
      return;
    }
    snprintf(forwardedCommand, sizeof(forwardedCommand), "BLINK %s", interval);
  } else if (strcmp(command, "VALVE") == 0 || strcmp(command, "MOTOR") == 0) {
    char *argument = strtok(NULL, " \t");
    char *extra = strtok(NULL, " \t");
    bool validValve = strcmp(command, "VALVE") == 0 && argument != NULL &&
                      (strcmp(argument, "ON") == 0 || strcmp(argument, "OFF") == 0) && extra == NULL;
    bool validMotorOff = strcmp(command, "MOTOR") == 0 && argument != NULL &&
                         strcmp(argument, "OFF") == 0 && extra == NULL;
    bool validMotorRun = false;
    if (strcmp(command, "MOTOR") == 0 && argument != NULL &&
        (strcmp(argument, "FORWARD") == 0 || strcmp(argument, "REVERSE") == 0)) {
      char *duration = extra;
      char *tail = duration == NULL ? NULL : strtok(NULL, " \t");
      char *end = NULL;
      const long ms = duration == NULL ? -1 : strtol(duration, &end, 10);
      validMotorRun = duration != NULL && *end == '\0' && ms >= 1 && ms <= 60000 && tail == NULL;
    }
    if (!validValve && !validMotorOff && !validMotorRun) {
      writeHostLine("ERR USE VALVE <ON|OFF> OR MOTOR <OFF|FORWARD|REVERSE> [1..60000 MS]");
      return;
    }
    if (validMotorRun) {
      snprintf(forwardedCommand, sizeof(forwardedCommand), "%s %s %s", command, argument,
               extra);
    } else {
      snprintf(forwardedCommand, sizeof(forwardedCommand), "%s %s", command, argument);
    }
  } else {
    writeHostLine("ERR COMMAND");
    return;
  }

  char response[RESPONSE_SIZE];
  if (!requestNode(node, forwardedCommand, response, sizeof(response))) {
    makeNodeError(node, response, sizeof(response));
  }
  writeHostLine(response);
}
}

namespace ControlBus {
void init() {
  if (EEPROM.read(EEPROM_MAGIC_ADDRESS) == EEPROM_MAGIC) {
    const uint8_t storedMode = EEPROM.read(EEPROM_MODE_ADDRESS);
    const uint8_t storedSelection = EEPROM.read(EEPROM_SELECTED_ADDRESS);
    if (storedMode <= 1 && storedSelection <= LAST_NODE) {
      communicationMode = storedMode == 0;
      selectedNode = storedSelection;
    } else {
      communicationMode = true;
      selectedNode = 0;
    }
  } else {
    communicationMode = true;
    selectedNode = 0;
    EEPROM.update(EEPROM_MODE_ADDRESS, 0);
    EEPROM.update(EEPROM_SELECTED_ADDRESS, 0);
    EEPROM.update(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC);
  }
  busSerial.begin(BUS_BAUD);
  releaseBus();
}

void handleHostCommand() {
  const BusRuntimeState runtimeState = beginBusRuntime();
  char line[48];
  size_t length = 0;
  bool overflow = false;
  const uint32_t deadline = millis() + 250;
  while ((int32_t)(millis() - deadline) < 0) {
    const char character = (char)JICE_io::get();
    if (character == '\n') break;
    if (character == '\r') continue;
    if (length + 1 < sizeof(line)) {
      line[length++] = character;
    } else {
      overflow = true;
    }
  }
  line[length] = '\0';
  if (overflow || length == 0) {
    writeHostLine("ERR COMMAND TOO LONG OR INCOMPLETE");
  } else {
    handleCommand(line);
  }
  endBusRuntime(runtimeState);
}
}
