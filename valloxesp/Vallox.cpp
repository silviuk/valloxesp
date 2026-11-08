// =======================================
// VALLOX DIGIT SE COMMUNICATION PROTOCOL
// =======================================

#include "Vallox.h"

// First four are live values; the remaining five are diagnostic settings.
const byte extraRegisters[EXTRA_REGISTER_COUNT] = {
  VX_VARIABLE_FAULT_CODE, VX_EXTRA_FUNC_COUNTER,
  VX_VARIABLE_POST_HEATING_TARGET, VX_VARIABLE_ALARM_FLAGS,
  VX_VARIABLE_BYPASS_SETPOINT, VX_VARIABLE_PREHEAT_SETPOINT,
  VX_VARIABLE_DEFROST_SETPOINT, VX_VARIABLE_DEFROST_HYSTERESIS,
  VX_VARIABLE_MAX_FAN_SPEED
};

// VX fan speed (1-8) conversion table
const uint8_t vxFanSpeeds[] = {
  VX_FAN_SPEED_1,
  VX_FAN_SPEED_2,
  VX_FAN_SPEED_3,
  VX_FAN_SPEED_4,
  VX_FAN_SPEED_5,
  VX_FAN_SPEED_6,
  VX_FAN_SPEED_7,
  VX_FAN_SPEED_8
};

// VX NTC temperature conversion table
const int8_t vxTemps[] = {
  -74, -70, -66, -62, -59, -56, -54, -52, -50, -48, // 0x00 - 0x09
  -47, -46, -44, -43, -42, -41, -40, -39, -38, -37, // 0x0a - 0x13
  -36, -35, -34, -33, -33, -32, -31, -30, -30, -29, // 0x14 - 0x1d
  -28, -28, -27, -27, -26, -25, -25, -24, -24, -23, // 0x1e - 0x27
  -23, -22, -22, -21, -21, -20, -20, -19, -19, -19, // 0x28 - 0x31
  -18, -18, -17, -17, -16, -16, -16, -15, -15, -14, // 0x32 - 0x3b
  -14, -14, -13, -13, -12, -12, -12, -11, -11, -11, // 0x3c - 0x45
  -10, -10, -9, -9, -9, -8, -8, -8, -7, -7,         // 0x46 - 0x4f
  -7, -6, -6, -6, -5, -5, -5, -4, -4, -4,           // 0x50 - 0x59
  -3, -3, -3, -2, -2, -2, -1, -1, -1, -1,           // 0x5a - 0x63
  0,  0,  0,  1,  1,  1,  2,  2,  2,  3,            // 0x64 - 0x6d
  3,  3,  4,  4,  4,  5,  5,  5,  5,  6,            // 0x6e - 0x77
  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,            // 0x78 - 0x81
  9, 10, 10, 10, 11, 11, 11, 12, 12, 12,            // 0x82 - 0x8b
  13, 13, 13, 14, 14, 14, 15, 15, 15, 16,           // 0x8c - 0x95
  16, 16, 17, 17, 18, 18, 18, 19, 19, 19,           // 0x96 - 0x9f
  20, 20, 21, 21, 21, 22, 22, 22, 23, 23,           // 0xa0 - 0xa9
  24, 24, 24, 25, 25, 26, 26, 27, 27, 27,           // 0xaa - 0xb3
  28, 28, 29, 29, 30, 30, 31, 31, 32, 32,           // 0xb4 - 0xbd
  33, 33, 34, 34, 35, 35, 36, 36, 37, 37,           // 0xbe - 0xc7
  38, 38, 39, 40, 40, 41, 41, 42, 43, 43,           // 0xc8 - 0xd1
  44, 45, 45, 46, 47, 48, 48, 49, 50, 51,           // 0xd2 - 0xdb
  52, 53, 53, 54, 55, 56, 57, 59, 60, 61,           // 0xdc - 0xe5
  62, 63, 65, 66, 68, 69, 71, 73, 75, 77,           // 0xe6 - 0xef
  79, 81, 82, 86, 90, 93, 97, 100, 100, 100,        // 0xf0 - 0xf9
  100, 100, 100, 100, 100, 100                      // 0xfa - 0xff
};

// public

Vallox::Vallox() {
  Vallox(false);
}

Vallox::Vallox(boolean debug) {
  isDebug = debug;
}

bool Vallox::connect(HardwareSerial *s) {
  serial = s;
  serial->begin(9600, SERIAL_8N1);

  fullInitDone = false;

  requestConfig();

  return true;
}

void Vallox::requestConfig() {
  sendStatusReq();
  sendIO08Req();
  sendFanSpeedReq();
  sendDefaultFanSpeedReq();
  sendRhReq();
  sendServicePeriodReq();
  sendServiceCounterReq();
  sendHeatingTargetReq();

  sendFlags06Req();
  sendProgramReq();

  // Temperature values are not needed to request, they are updated automatically
  // RH values are not needed to request, they are updated automatically

  // Set request time for all configurations
  unsigned long now = millis();
  data.updated = millis();
  lastRequested = now;
}

void Vallox::loop() {
  byte message[VX_MSG_LENGTH];

  // read and decode as long as messages are available
  while (readMessage(message)) {
    // Inform with callback about message
    decodeMessage(message);
  }
  if (statusDirty && fullInitDone && statusChangedCallback) statusChangedCallback();
  if (temperatureDirty && isTemperatureInitDone() && temperatureChangedCallback) temperatureChangedCallback();
  statusDirty = temperatureDirty = false;

  // query for data that can change without notice
  unsigned long now = millis();
  if (isStatusInitDone() && !extraPollingStarted) {
    extraPollingStarted = true;
    lastLiveRequested = now;
    requestLiveRegisters();
    requestSettingRegisters();
  }
  if (extraPollingStarted && now - lastLiveRequested >= LIVE_QUERY_INTERVAL) {
    lastLiveRequested = now;
    requestLiveRegisters();
  }
  if (now - lastRequested > QUERY_INTERVAL) {
    lastRequested = now;

    if (isStatusInitDone()) {
      sendIO08Req();
      sendServiceCounterReq();
      requestSettingRegisters();
    }
  }

  // Fallback if no status reply arrives: release the lock after a short
  // timeout instead of waiting for the 5 s retry loop.
  if (statusMutex && now - statusMutexAt > STATUS_LOCK_TIMEOUT) {
    statusMutex = false;
    data.status.lastReceived = 0; // Require a fresh read before another RMW write.
    sendStatusReq();
  }
  if (now - lastRetryLoop > RETRY_INTERVAL) {
    lastRetryLoop = now;
    retryLoop();
  }
  processDeferredWrite();
  processRequestQueue();
}

void Vallox::requestLiveRegisters() {
  for (byte i = 0; i < 4; ++i) requestVariable(extraRegisters[i]);
}

void Vallox::requestSettingRegisters() {
  for (byte i = 4; i < EXTRA_REGISTER_COUNT; ++i) requestVariable(extraRegisters[i]);
}

int Vallox::getReadOnlyValue(byte variable) {
  if (variable == VX_VARIABLE_STATUS)
    return data.status.lastReceived ? (int)data.is_filter.value : NOT_SET;
  if (variable == VX_VARIABLE_IO_08)
    return data.variable08.lastReceived ? (int)data.is_error.value : NOT_SET;
  for (byte i = 0; i < EXTRA_REGISTER_COUNT; ++i) {
    if (extraRegisters[i] == variable) return extraValues[i];
  }
  return NOT_SET;
}

// setters
// these will set data both in the bus and cache
void Vallox::setFanSpeed(int speed) {
  if (speed >= VX_MIN_FAN_SPEED && speed <= VX_MAX_FAN_SPEED) {
    if (!setVariable(VX_VARIABLE_FAN_SPEED, fanSpeed2Hex(speed))) return;
    data.fan_speed.value = speed;
    if (statusChangedCallback) statusChangedCallback();
  }
}

void Vallox::setDefaultFanSpeed(int speed) {
  if (speed >= VX_MIN_FAN_SPEED && speed <= VX_MAX_FAN_SPEED) {
    if (!setVariable(VX_VARIABLE_DEFAULT_FAN_SPEED, fanSpeed2Hex(speed))) return;
    data.default_fan_speed.value = speed;
    if (statusChangedCallback) statusChangedCallback();
  }
}

// Status variables
void Vallox::setOn() {

  if (setStatusVariable(VX_VARIABLE_STATUS, data.status.value | VX_STATUS_FLAG_POWER)) {
    data.is_on.value = true;
    if (statusChangedCallback) statusChangedCallback();
  }
}

void Vallox::setOff() {
  if (setStatusVariable(VX_VARIABLE_STATUS, data.status.value & ~VX_STATUS_FLAG_POWER)) {
    data.is_on.value = false;
    if (statusChangedCallback) statusChangedCallback();
  }
}

void Vallox::setRhModeOn() {
  if (setStatusVariable(VX_VARIABLE_STATUS, data.status.value | VX_STATUS_FLAG_RH)) {
    data.is_rh_mode.value = true;
    if (statusChangedCallback) statusChangedCallback();
  }
}

void Vallox::setRhModeOff() {
  if (setStatusVariable(VX_VARIABLE_STATUS, data.status.value & ~VX_STATUS_FLAG_RH)) {
    data.is_rh_mode.value = false;
    if (statusChangedCallback) statusChangedCallback();
  }
}

void Vallox::setHeatingModeOn() {
  // Don't set if already active. Vallox seems to reset to default speed if same mode is set twice
  if ((data.status.value & VX_STATUS_FLAG_HEATING_MODE) && isOn()) {
    if (debugPrintCallback) debugPrintCallback("Heating mode is already on!");
    if (statusChangedCallback) statusChangedCallback();
  }
  else if (setStatusVariable(VX_VARIABLE_STATUS, data.status.value | VX_STATUS_FLAG_HEATING_MODE | VX_STATUS_FLAG_POWER)) {
    data.is_heating_mode.value = true;
    if (statusChangedCallback) statusChangedCallback();
  }
}

void Vallox::setHeatingModeOff() {
  // Don't set if already active. Vallox seems to reset to default speed if same mode is set twice
  if (!(data.status.value & VX_STATUS_FLAG_HEATING_MODE) && isOn()) {
    if (debugPrintCallback) debugPrintCallback("Heating mode is already off!");
    if (statusChangedCallback) statusChangedCallback();
  }
  else if (setStatusVariable(VX_VARIABLE_STATUS, (data.status.value & ~VX_STATUS_FLAG_HEATING_MODE) | VX_STATUS_FLAG_POWER)) {
    data.is_heating_mode.value = false;
    if (statusChangedCallback) statusChangedCallback();
  }
}

boolean Vallox::setStatusVariable(byte variable, byte value) {
  if (!statusMutex && data.status.lastReceived) {
    if (!setVariable(variable, value, VX_MSG_MAINBOARD_1)) return false;
    statusMutex = true; // lock sending status again
    statusMutexAt = millis();
    // Status is only allowed to send to specific mainboard

    data.is_on.value = (value & VX_STATUS_FLAG_POWER) != 0;
    // Ask for the resulting status so the reply clears the lock promptly.
    requestVariable(VX_VARIABLE_STATUS);

    // Clear the retry loop to prevent retry loops to break in before getting reply
    lastRetryLoop = millis();
    return true;
  }

  return false;
}

void Vallox::setServicePeriod(int months) {
  if (months >= 0 && months < 256) {
    if (!setVariable(VX_VARIABLE_SERVICE_PERIOD, months)) return;
    data.service_period.value = months;
    if (statusChangedCallback) statusChangedCallback();
  }
}

void Vallox::setServiceCounter(int months) {
  if (months >= 0 && months < 256) {
    if (!setVariable(VX_VARIABLE_SERVICE_COUNTER, months)) return;
    data.service_counter.value = months;
    if (statusChangedCallback) statusChangedCallback();
  }
}

void Vallox::setHeatingTarget(int cel) {
  if (cel >= 10 && cel <= 27) {
    byte hex = cel2Ntc(cel);
    if (!setVariable(VX_VARIABLE_HEATING_TARGET, hex)) return;
    data.heating_target.value = cel;
    if (statusChangedCallback) statusChangedCallback();
  }
}

void Vallox::setSwitchOn() {
  if (!data.flags06.lastReceived) { sendFlags06Req(); return; }
  // Activate boost/fireplace
  setVariable(VX_VARIABLE_FLAGS_06, data.flags06.value | VX_06_FIREPLACE_FLAG_ACTIVATE);
}

void Vallox::setDebug(bool debug) {
  isDebug = debug;
  if (statusChangedCallback) statusChangedCallback();
}

boolean Vallox::isInitOk() {
  return fullInitDone;
}

//Callback setters
void Vallox::setPacketCallback(PACKET_CALLBACK_SIGNATURE) {
  this->packetCallback = packetCallback;
}

void Vallox::setStatusChangedCallback(STATUS_CHANGED_CALLBACK_SIGNATURE) {
  this->statusChangedCallback = statusChangedCallback;
}

void Vallox::setDebugPrintCallback(DEBUG_PRINT_CALLBACK_SIGNATURE) {
  this->debugPrintCallback = debugPrintCallback;
}

void Vallox::setTemperatureChangedCallback(TEMPERATURE_CHANGED_CALLBACK_SIGNATURE) {
  this->temperatureChangedCallback = temperatureChangedCallback;
}

// Getters
unsigned long Vallox::getUpdated() {
  return data.updated;
}

int Vallox::getInsideTemp() {
  // NOT_SET until a real reading arrives (never publish placeholders).
  return data.t_inside.lastReceived ? data.t_inside.value : NOT_SET;
}

int Vallox::getOutsideTemp() {
  // NOT_SET until a real reading arrives (never publish placeholders).
  return data.t_outside.lastReceived ? data.t_outside.value : NOT_SET;
}

int Vallox::getIncomingTemp() {
  // NOT_SET until a real reading arrives (never publish placeholders).
  return data.t_incoming.lastReceived ? data.t_incoming.value : NOT_SET;
}

int Vallox::getExhaustTemp() {
  // NOT_SET until a real reading arrives (never publish placeholders).
  return data.t_exhaust.lastReceived ? data.t_exhaust.value : NOT_SET;
}

boolean Vallox::isOn() {
  return data.is_on.value;
}

boolean Vallox::isRhMode() {
  return data.is_rh_mode.value;
}

boolean Vallox::isHeatingMode() {
  return data.is_heating_mode.value;
}

boolean Vallox::isSwitchActive() {
  return data.is_switch_active.value;
}

boolean Vallox::isSummerMode() {
  return data.is_summer_mode.value;
}

boolean Vallox::isErrorRelay() {
  return data.is_error.value;
}

boolean Vallox::isMotorIn() {
  return data.is_in_motor.value;
}

boolean Vallox::isFrontHeating() {
  return data.is_front_heating.value;
}

boolean Vallox::isMotorOut() {
  return data.is_out_motor.value;
}

boolean Vallox::isHeating() {
  return data.is_heating.value;
}

boolean Vallox::isFault() {
  return data.is_fault.value;
}

boolean Vallox::isServiceNeeded() {
  return data.is_service.value;
}

int Vallox::getServicePeriod() {
  return data.service_period.value;
}

int Vallox::getServiceCounter() {
  return data.service_counter.value;
}

int Vallox::getFanSpeed() {
  return data.fan_speed.value;
}

int Vallox::getDefaultFanSpeed() {
  return data.default_fan_speed.value;
}

int Vallox::getRh1() {
  if (!data.rh1.lastReceived) {
    return NOT_SET;
  }
  return data.rh1.value;
}

int Vallox::getRh2() {
  if (!data.rh2.lastReceived) {
    return NOT_SET;
  }
  return data.rh2.value;
}

int Vallox::getCO2() {
  if (!data.co2.lastReceived) {
    return NOT_SET;
  }
  return data.co2.value;
}

int Vallox::getHeatingTarget() {
  return data.heating_target.value;
}

int Vallox::getSwitchType() {
  if (!settings.is_boost_setting.lastReceived) {
    return NOT_SET;
  }
  return settings.is_boost_setting.value ? 1 : 0;
}

// private

// Requests
void Vallox::sendInsideTempReq() {
  requestVariable(VX_VARIABLE_T_INSIDE);
}

void Vallox::sendOutsideTempReq() {
  requestVariable(VX_VARIABLE_T_OUTSIDE);
}

void Vallox::sendIncomingTempReq() {
  requestVariable(VX_VARIABLE_T_INCOMING);
}

void Vallox::sendExhaustTempReq() {
  requestVariable(VX_VARIABLE_T_EXHAUST);
}

void Vallox::sendStatusReq() {
  requestVariable(VX_VARIABLE_STATUS);
}

void Vallox::sendServicePeriodReq() {
  requestVariable(VX_VARIABLE_SERVICE_PERIOD);
}

void Vallox::sendFanSpeedReq() {
  requestVariable(VX_VARIABLE_FAN_SPEED);
}

void Vallox::sendDefaultFanSpeedReq() {
  requestVariable(VX_VARIABLE_DEFAULT_FAN_SPEED);
}

void Vallox::sendHeatingTargetReq() {
  requestVariable(VX_VARIABLE_HEATING_TARGET);
}

void Vallox::sendIO08Req() {
  requestVariable(VX_VARIABLE_IO_08);
}

void Vallox::sendFlags06Req() {
  requestVariable(VX_VARIABLE_FLAGS_06);
}

void Vallox::sendProgramReq() {
  requestVariable(VX_VARIABLE_PROGRAM);
}

void Vallox::sendServiceCounterReq() {
  requestVariable(VX_VARIABLE_SERVICE_COUNTER);
}

void Vallox::sendRhReq() {
  requestVariable(VX_VARIABLE_RH1);
}

// set generic variable value in all mainboards and panels
bool Vallox::setVariable(byte variable, byte value) {
  return setVariable(variable, value, VX_MSG_MAINBOARDS);
}

bool Vallox::setVariable(byte variable, byte value, byte target) {
  if (!serial) return false;
  // If the bus is busy, defer the command (one slot) instead of dropping it;
  // loop() sends it once the bus is idle, or after WRITE_DEFER_MAX_MS at the
  // latest (the original firmware always sent immediately).
  if (isBusBusy()) {
    if (deferredWrite && deferredVariable != variable) {
      if (debugPrintCallback) debugPrintCallback("Bus busy; command not sent");
      return false;
    }
    deferredWrite = true;
    deferredVariable = variable;
    deferredValue = value;
    deferredTarget = target;
    deferredAt = millis();
    return true;
  }
  transmitVariable(variable, value, target);
  return true;
}

boolean Vallox::isBusBusy() {
  unsigned long now = millis();
  return receiveCount || serial->available() || now - lastBusActivity < REQUEST_GAP ||
         (pendingRequest && now - requestSentAt < WRITE_REPLY_WAIT_MS);
}

void Vallox::processDeferredWrite() {
  if (!deferredWrite) return;
  if (isBusBusy() && millis() - deferredAt < WRITE_DEFER_MAX_MS) return;
  deferredWrite = false;
  transmitVariable(deferredVariable, deferredValue, deferredTarget);
}

void Vallox::transmitVariable(byte variable, byte value, byte target) {
  byte message[VX_MSG_LENGTH];
  message[0] = VX_MSG_DOMAIN;
  message[1] = VX_MSG_THIS_PANEL;
  message[2] = target;
  message[3] = variable;
  message[4] = value;
  message[5] = calculateCheckSum(message);

  // send to all mainboards
  for (int i = 0; i < VX_MSG_LENGTH; i++) {
    serial->write(message[i]);
  }

  if (isDebug && packetCallback) {
    // Callback that we got the message
    packetCallback(message, VX_MSG_LENGTH, (char*)"packetSent");
  }

  // Do not fabricate a status acknowledgement that can release our own lock.
  if (variable == VX_VARIABLE_STATUS) { lastBusActivity = millis(); return; }

  message[1] = VX_MSG_MAINBOARD_1;
  message[2] = VX_MSG_PANELS;
  message[5] = calculateCheckSum(message);

  // send to all panels
  for (int i = 0; i < VX_MSG_LENGTH; i++) {
    serial->write(message[i]);
  }
  lastBusActivity = millis();
}

void Vallox::requestVariable(byte variable) {
  // Deduplicate requests, including those already awaiting a reply.
  if (pendingRequest == variable) return;
  for (byte i = 0; i < requestCount; ++i) {
    if (requestQueue[i] == variable) {
      if (variable == VX_VARIABLE_STATUS) {
        for (byte j = i; j > 0; --j) requestQueue[j] = requestQueue[j - 1];
        requestQueue[0] = variable;
      }
      return;
    }
  }
  if (requestCount < REQUEST_QUEUE_LENGTH) {
    if (variable == VX_VARIABLE_STATUS) {
      for (byte i = requestCount; i > 0; --i) requestQueue[i] = requestQueue[i - 1];
      requestQueue[0] = variable;
      ++requestCount;
    } else requestQueue[requestCount++] = variable;
  }
}

int Vallox::extraIndex(byte variable) {
  for (byte i = 0; i < EXTRA_REGISTER_COUNT; ++i) {
    if (extraRegisters[i] == variable) return i;
  }
  return -1;
}

void Vallox::noteRequestTimeout(byte variable, unsigned long now) {
  int i = extraIndex(variable);
  if (i < 0) return; // core registers keep their existing retry behaviour
  if (extraMisses[i] < 255) ++extraMisses[i];
  if (extraMisses[i] >= EXTRA_MAX_MISSES) extraBackoffAt[i] = now;
}

boolean Vallox::isBackedOff(byte variable, unsigned long now) {
  int i = extraIndex(variable);
  if (i < 0 || extraMisses[i] < EXTRA_MAX_MISSES) return false;
  // Unanswered N times in a row: probe only once per backoff period.
  if (now - extraBackoffAt[i] < EXTRA_BACKOFF_INTERVAL) return true;
  extraBackoffAt[i] = now;
  return false;
}

void Vallox::processRequestQueue() {
  unsigned long now = millis();
  // A missing response releases the slot after the existing retry interval.
  // Optional registers retry on their normal 60 s / 5 min schedule.
  if (pendingRequest && now - requestSentAt < REQUEST_TIMEOUT) return;
  if (pendingRequest) {
    // Timed out: count consecutive misses for optional registers.
    noteRequestTimeout(pendingRequest, now);
    pendingRequest = 0;
  }
  if (deferredWrite || !requestCount || receiveCount || serial->available() ||
      now - lastBusActivity < REQUEST_GAP || now - requestSentAt < REQUEST_GAP) return;
  byte variable = requestQueue[0];
  for (byte i = 1; i < requestCount; ++i) requestQueue[i - 1] = requestQueue[i];
  --requestCount;
  if (isBackedOff(variable, now)) return; // skip unanswered register for now
  pendingRequest = variable;
  requestSentAt = now;
  byte message[VX_MSG_LENGTH];
  message[0] = VX_MSG_DOMAIN;
  message[1] = VX_MSG_THIS_PANEL;
  message[2] = VX_MSG_MAINBOARD_1;
  message[3] = VX_MSG_POLL_BYTE;
  message[4] = variable;
  message[5] = calculateCheckSum(message);


  if (isDebug && packetCallback) {
    // Callback that we got the message
    packetCallback(message, VX_MSG_LENGTH, (char*)"packetSent");
  }

  for (int i = 0; i < VX_MSG_LENGTH; i++) {
    serial->write(message[i]);
  }

  lastBusActivity = millis();
}

// tries to read one full message
// returns true if a message was read, false otherwise
boolean Vallox::readMessage(byte message[]) {
  if (!serial) return false;
  if (receiveCount && millis() - lastBusActivity > 20) receiveCount = 0;
  while (serial->available()) {
    byte value = serial->read();
    lastBusActivity = millis();
    if (!receiveCount && value != VX_MSG_DOMAIN) continue;
    receiveBuffer[receiveCount++] = value;
    if (receiveCount < VX_MSG_LENGTH) continue;
    if (calculateCheckSum(receiveBuffer) == receiveBuffer[5]) {
      for (byte i = 0; i < VX_MSG_LENGTH; ++i) message[i] = receiveBuffer[i];
      receiveCount = 0;
      if (isDebug && packetCallback) packetCallback(message, VX_MSG_LENGTH, (char*)"packetRecv");
      return true;
    }
    // Slide one byte on corruption; a valid frame may start inside this one.
    byte start = 1;
    while (start < VX_MSG_LENGTH && receiveBuffer[start] != VX_MSG_DOMAIN) ++start;
    receiveCount = VX_MSG_LENGTH - start;
    for (byte i = 0; i < receiveCount; ++i) receiveBuffer[i] = receiveBuffer[start + i];
  }
  return false;
}

void Vallox::decodeMessage(const byte message[]) {
  // decode variable in message
  byte variable = message[3];
  byte value = message[4];
  unsigned long now = millis();

  // Check message checksum
  if (!validateCheckSum(message)) {
    return ;// Message invalid
  }

  // Commands and local echoes are not confirmed mainboard state.
  if (message[1] != VX_MSG_MAINBOARD_1 ||
      (message[2] != VX_MSG_PANELS && message[2] != VX_MSG_THIS_PANEL &&
       message[2] != VX_MSG_PANEL_1)) return;

  // Only a checksum-valid mainboard response addressed to this poller
  // completes an outstanding poll. Other bus traffic still updates the cache.
  if (message[1] == VX_MSG_MAINBOARD_1 &&
      message[2] == VX_MSG_THIS_PANEL && variable == pendingRequest) {
    pendingRequest = 0;
  }
  for (byte i = 0; i < EXTRA_REGISTER_COUNT; ++i) {
    if (extraRegisters[i] == variable) extraMisses[i] = 0; // answered: supported
  }

  for (byte i = 0; i < EXTRA_REGISTER_COUNT; ++i) {
    if (variable != extraRegisters[i]) continue;
    int decoded = value;
    if (variable == VX_VARIABLE_POST_HEATING_TARGET ||
        variable == VX_VARIABLE_BYPASS_SETPOINT ||
        variable == VX_VARIABLE_PREHEAT_SETPOINT ||
        variable == VX_VARIABLE_DEFROST_SETPOINT) {
      decoded = ntc2Cel(value);
    } else if (variable == VX_VARIABLE_MAX_FAN_SPEED) {
      decoded = hex2FanSpeed(value);
    } else if (variable == VX_VARIABLE_ALARM_FLAGS) {
      decoded = (value & VX_CELL_FREEZE_ALARM) != 0;
    }
    checkStatusChange(&extraValues[i], decoded);
    return;
  }

  // Temperature (status object)
  if (variable == VX_VARIABLE_T_OUTSIDE) { // OUTSIDE
    checkValueChange(&(data.t_outside.value), ntc2Cel(value), &(data.t_outside.lastReceived));
  } else if (variable == VX_VARIABLE_T_EXHAUST) { // EXHAUST
    checkValueChange(&(data.t_exhaust.value), ntc2Cel(value), &(data.t_exhaust.lastReceived));
  } else if (variable == VX_VARIABLE_T_INSIDE) { // INSIDE
    checkValueChange(&(data.t_inside.value), ntc2Cel(value), &(data.t_inside.lastReceived));
  } else if (variable == VX_VARIABLE_T_INCOMING) { // INCOMING
    checkValueChange(&(data.t_incoming.value), ntc2Cel(value), &(data.t_incoming.lastReceived));
  }

  // RH
  else if (variable == VX_VARIABLE_RH1) { 
    checkValueChange(&(data.rh1.value), hex2Rh(value), &(data.rh1.lastReceived));
  } else if (variable == VX_VARIABLE_RH2) {
    checkValueChange(&(data.rh2.value), hex2Rh(value), &(data.rh2.lastReceived));
  }

  // CO2
  // Let's assume that the timeinterval for the same value is something pre-defined..
  else if (variable == VX_VARIABLE_CO2_HI) {
    data.co2_hi.lastReceived = now ? now : 1;
    data.co2_hi.value = value;
    if (data.co2_lo.lastReceived && now - data.co2_lo.lastReceived < CO2_LIFE_TIME_MS) {
      handleCo2TotalValue(data.co2_hi.value, data.co2_lo.value);
    }
  } else if (variable == VX_VARIABLE_CO2_LO) {
    data.co2_lo.lastReceived = now ? now : 1;
    data.co2_lo.value = value;
    if (data.co2_hi.lastReceived && now - data.co2_hi.lastReceived < CO2_LIFE_TIME_MS) {
      handleCo2TotalValue(data.co2_hi.value, data.co2_lo.value);
    }
  }

  // Others (config object)
  else if (variable == VX_VARIABLE_FAN_SPEED) {
    if (hex2FanSpeed(value) == NOT_SET) return;
    data.fan_speed.lastReceived = now ? now : 1;
    checkStatusChange(&(data.fan_speed.value), hex2FanSpeed(value));
  } else if (variable == VX_VARIABLE_DEFAULT_FAN_SPEED) {
    if (hex2FanSpeed(value) == NOT_SET) return;
    data.default_fan_speed.lastReceived = now ? now : 1;
    checkStatusChange(&(data.default_fan_speed.value), hex2FanSpeed(value));
  } else if (variable == VX_VARIABLE_STATUS) {
    decodeStatus(value);
  } else if (variable == VX_VARIABLE_IO_08) {
    decodeVariable08(value);
  } else if (variable == VX_VARIABLE_FLAGS_06) {
    decodeFlags06(value);
  } else if (variable == VX_VARIABLE_SERVICE_PERIOD) {
    data.service_period.lastReceived = now ? now : 1;
    checkStatusChange(&(data.service_period.value), value);
  } else if (variable == VX_VARIABLE_SERVICE_COUNTER) {
    data.service_counter.lastReceived = now ? now : 1;
    checkStatusChange(&(data.service_counter.value), value);
  } else if (variable == VX_VARIABLE_HEATING_TARGET) {
    data.heating_target.lastReceived = now ? now : 1;
    checkStatusChange(&(data.heating_target.value), ntc2Cel(value));
  } else if (variable == VX_VARIABLE_PROGRAM) {
    decodeProgram(value);
  } else {
    // variable not recognized
  }

  if (!fullInitDone) { // Only send once after all the decoding has been successfully done
    fullInitDone = isStatusInitDone();
    if (fullInitDone) {
      statusDirty = true; // Inform only when full init is done to avoid non-set variables being presented
    }
  }
}

// For now, read only (no mutex needed)
void Vallox::decodeVariable08(byte variable08) {
  // The five-minute I/O poll also refreshes retained snapshots and availability.
  statusDirty = temperatureDirty = true;
  // flags of variable 08
  unsigned long now = millis();

  data.is_summer_mode.lastReceived = now ? now : 1;
  data.is_error.lastReceived = now ? now : 1;
  data.is_in_motor.lastReceived = now ? now : 1;
  data.is_front_heating.lastReceived = now ? now : 1;
  data.is_out_motor.lastReceived = now ? now : 1;
  data.is_extra_func.lastReceived = now ? now : 1;

  data.variable08.value = variable08;
  data.variable08.lastReceived = now ? now : 1;

  checkStatusChange(&(data.is_summer_mode.value), (variable08 & VX_08_FLAG_SUMMER_MODE) != 0x00);
  checkStatusChange(&(data.is_error.value), (variable08 & VX_08_FLAG_ERROR_RELAY) != 0x00);
  checkStatusChange(&(data.is_in_motor.value), (variable08 & VX_08_FLAG_MOTOR_IN) != 0x00);
  checkStatusChange(&(data.is_front_heating.value), (variable08 & VX_08_FLAG_FRONT_HEATING) != 0x00);
  checkStatusChange(&(data.is_out_motor.value), (variable08 & VX_08_FLAG_MOTOR_OUT) != 0x00);
  checkStatusChange(&(data.is_extra_func.value), (variable08 & VX_08_FLAG_EXTRA_FUNC) != 0x00);
}

// For now, read only (no mutex needed)
void Vallox::decodeFlags06(byte flags06) {
  // flags of variable 06
  unsigned long now = millis();
  data.is_switch_active.lastReceived = now ? now : 1;

  data.flags06.value = flags06;
  data.flags06.lastReceived = now ? now : 1;

  checkStatusChange(&(data.is_switch_active.value), (flags06 & VX_06_FIREPLACE_FLAG_IS_ACTIVE) != 0x00);
}

void Vallox::decodeProgram(byte program) {
  // flags of programs variable
  bool shoudInformCallback = !settings.is_boost_setting.lastReceived;

  unsigned long now = millis();
  settings.is_boost_setting.lastReceived = now ? now : 1;

  settings.program.value = program;
  settings.program.lastReceived = now ? now : 1;

  checkSettingsChange(&(settings.is_boost_setting.value), (program & VX_PROGRAM_SWITCH_TYPE) != 0x00);

  if (shoudInformCallback) {
    // Never received, publish
    statusDirty = true;
  }
}

void Vallox::decodeStatus(byte status) {
  unsigned long now = millis();

  data.is_on.lastReceived = now ? now : 1;
  data.is_rh_mode.lastReceived = now ? now : 1;
  data.is_heating_mode.lastReceived = now ? now : 1;
  data.is_filter.lastReceived = now ? now : 1;
  data.is_heating.lastReceived = now ? now : 1;
  data.is_fault.lastReceived = now ? now : 1;
  data.is_service.lastReceived = now ? now : 1;

  data.status.value = status; // This is the full data status
  data.status.lastReceived = now ? now : 1;

  checkStatusChange(&(data.is_on.value), (status & VX_STATUS_FLAG_POWER) != 0x00);
  checkStatusChange(&(data.is_rh_mode.value), (status & VX_STATUS_FLAG_RH) != 0x00);
  checkStatusChange(&(data.is_heating_mode.value), (status & VX_STATUS_FLAG_HEATING_MODE) != 0x00);
  checkStatusChange(&(data.is_filter.value), (status & VX_STATUS_FLAG_FILTER) != 0x00);
  checkStatusChange(&(data.is_heating.value), (status & VX_STATUS_FLAG_HEATING) != 0x00);
  checkStatusChange(&(data.is_fault.value), (status & VX_STATUS_FLAG_FAULT) != 0x00);
  checkStatusChange(&(data.is_service.value), (status & VX_STATUS_FLAG_SERVICE) != 0x00);

  statusMutex = false; // Clear the status mutex, allow to continue
}

//
// Settings
void Vallox::checkSettingsChange(boolean* oldValue, boolean newValue) {
  if (checkChange(oldValue, newValue)) {
    statusDirty = true;
  }
}

//
// Status
void Vallox::checkStatusChange(boolean* oldValue, boolean newValue) {
  if (checkChange(oldValue, newValue) && fullInitDone) {
    statusDirty = true;
  }
}

void Vallox::checkStatusChange(int* oldValue, int newValue) {
  if (checkChange(oldValue, newValue) && fullInitDone) {
    statusDirty = true;
  }
}

//
// Temperature change
void Vallox::checkValueChange(int *oldValue, int newValue, unsigned long *lastReceived) {
  unsigned long now = millis();

  if (!*lastReceived) temperatureDirty = true;
  *lastReceived = now ? now : 1;
  checkValueChange(oldValue, newValue);
}

// Check for value change (Temperature, CO2, RH)
void Vallox::checkValueChange(int* oldValue, int newValue) {
  if (checkChange(oldValue, newValue) && isTemperatureInitDone()) { // Do not publish status, until base values has been received
    temperatureDirty = true;
  }
}


int Vallox::ntc2Cel(byte ntc) {
  const size_t i = ntc;
  return i < sizeof(vxTemps) / sizeof(vxTemps[0]) ? vxTemps[i] : NOT_SET;
}

byte Vallox::cel2Ntc(int cel) {
  for (size_t i = 0; i < sizeof(vxTemps) / sizeof(vxTemps[0]); i++) {
    if (vxTemps[i] == cel) {
      return i;
    }
  }

  // we should not be here, return 10 Cel as default
  return 0x83;
}

byte Vallox::fanSpeed2Hex(int fan) {
  if (fan > 0 && fan < 9) {
    return vxFanSpeeds[fan - 1];
  }

  // we should not be here, return speed 1 as default
  return VX_FAN_SPEED_1;
}

int Vallox::hex2FanSpeed(byte hex) {
  for (int i = 0; i < sizeof(vxFanSpeeds); i++) {
    if (vxFanSpeeds[i] == hex) {
      return i + 1;
    }
  }

  return NOT_SET;
}

int Vallox::hex2Rh(byte hex) {
  if (hex >= 51) {
    return (hex - 51) / 2.04;
  } else {
    return NOT_SET;
  }
}

byte Vallox::htCel2Hex(int htCel) {
  if (htCel < 13) {
    return 0x01;
  } else if (htCel < 15) {
    return 0x03;
  } else if (htCel < 18) {
    return 0x07;
  } else if (htCel < 20) {
    return 0x0F;
  } else if (htCel < 23) {
    return 0x1F;
  } else if (htCel < 25) {
    return 0x3F;
  } else if (htCel < 27) {
    return 0x7F;
  } else if (htCel == 27) {
    return 0xFF;
  } else {
    return 0x01;
  }
}

// calculate VX message checksum
byte Vallox::calculateCheckSum(const byte message[]) {
  byte ret = 0x00;
  for (int i = 0; i < VX_MSG_LENGTH - 1; i++) {
    ret += message[i];
  }

  return ret;
}

bool Vallox::validateCheckSum(const byte message[]) {
  byte calculated = calculateCheckSum(message); // Calculated check sum
  byte received = message[5];

  if (calculated != received) {
    if (debugPrintCallback) debugPrintCallback("Checksum comparison failed!");
    return false;
  }

  return true;
}


unsigned long Vallox::checkChange(boolean* oldValue, boolean newValue) {
  unsigned long changed = 0;

  if (*oldValue != newValue) {
    *oldValue = newValue;
    data.updated = millis();
    changed = 1;
  }

  return changed;
}

unsigned long Vallox::checkChange(int* oldValue, int newValue) {
  unsigned long changed = 0;
  if (*oldValue != newValue) {
    *oldValue = newValue;
    data.updated = millis();
    changed = 1;
  }

  return changed;
}

void Vallox::retryLoop() {
  sendMissingRequests();
}

void Vallox::sendMissingRequests() {
  if (!data.is_on.lastReceived) sendStatusReq();
  if (!data.variable08.lastReceived) sendIO08Req();
  if (!data.fan_speed.lastReceived) sendFanSpeedReq();
  if (!data.default_fan_speed.lastReceived) sendDefaultFanSpeedReq();
  if (!data.service_period.lastReceived) sendServicePeriodReq();
  if (!data.service_counter.lastReceived) sendServiceCounterReq();
  if (!data.heating_target.lastReceived) sendHeatingTargetReq();
  if (!data.flags06.lastReceived) sendFlags06Req();
  if (!settings.program.lastReceived) sendProgramReq();
}

boolean Vallox::isTemperatureInitDone() {
  return data.t_outside.lastReceived &&
         data.t_inside.lastReceived &&
         data.t_exhaust.lastReceived &&
         data.t_incoming.lastReceived;
}

boolean Vallox::isStatusInitDone() { // all initializations
  // Ensure that all data values has been received
  return
    data.is_on.lastReceived &&
    data.is_rh_mode.lastReceived &&
    data.is_heating_mode.lastReceived &&
    data.variable08.lastReceived &&
    data.is_filter.lastReceived &&
    data.is_heating.lastReceived &&
    data.is_fault.lastReceived &&
    data.is_service.lastReceived &&
    data.fan_speed.lastReceived &&
    data.default_fan_speed.lastReceived &&
    data.service_period.lastReceived &&
    data.service_counter.lastReceived &&
    data.heating_target.lastReceived;
}

void Vallox::handleCo2TotalValue(byte hi, byte low) {
  // Construct co2 value from hi and lo bytes
  uint16_t total = low + (hi << 8);
  checkValueChange(&(data.co2.value), total, &(data.co2.lastReceived));
}
