
// =======================
// Sensor power control
// =======================
void sensorPowerOn() {
  digitalWrite(PIN_MOSFET, LOW);
}

void sensorPowerOff() {
  digitalWrite(PIN_MOSFET, HIGH);
}

void sensorRequestOn() {
  pinMode(PIN_1WIRE, INPUT);
  sensorPowerOn();

  sensorEnabled = true;
  lastTempValid = false;
  sensorState = SENSOR_POWER_ON_WAIT;
  sensorStateDeadlineMs = millis() + SENSOR_POWER_SETTLE_MS;
}

void sensorRequestOff() {
  sensorPowerOff();

  pinMode(PIN_1WIRE, OUTPUT);
  digitalWrite(PIN_1WIRE, LOW);

  sensorEnabled = false;
  lastTempValid = false;
  lastTempC = DEVICE_DISCONNECTED_C;
  sensorState = SENSOR_DISABLED;
}

void sensorStartRecovery() {
  sensorPowerOff();

  pinMode(PIN_1WIRE, OUTPUT);
  digitalWrite(PIN_1WIRE, LOW);

  sensorState = SENSOR_RECOVERY_POWER_OFF_WAIT;
  sensorStateDeadlineMs = millis() + SENSOR_RECOVERY_OFF_MS;
}

void sensorInitBus() {
  sensors.begin();
  sensors.setResolution(12);
  sensors.setWaitForConversion(false);
  tempKickMs = millis();
}

void sensorService() {
  uint32_t now = millis();

  switch (sensorState) {
    case SENSOR_DISABLED:
      break;

    case SENSOR_POWER_ON_WAIT:
      if (timeReached(now, sensorStateDeadlineMs)) {
        sensorInitBus();
        sensorState = SENSOR_READY;
      }
      break;

    case SENSOR_READY:
      if (timeReached(now, tempKickMs + TEMP_PERIOD_MS)) {
        tempKickMs = now;
        sensors.requestTemperatures();
        tempStartMs = now;
        sensorState = SENSOR_CONVERTING;
      }
      break;

    case SENSOR_CONVERTING:
      if (timeReached(now, tempStartMs + TEMP_CONV_MS)) {
        float t = sensors.getTempCByIndex(0);

        if (t == DEVICE_DISCONNECTED_C) {
          lastTempValid = false;
          lastTempC = DEVICE_DISCONNECTED_C;
          sensorStartRecovery();
        } else {
          lastTempC = t;
          lastTempValid = true;
          sensorState = SENSOR_READY;
        }
      }
      break;

    case SENSOR_RECOVERY_POWER_OFF_WAIT:
      if (timeReached(now, sensorStateDeadlineMs)) {
        pinMode(PIN_1WIRE, INPUT);
        sensorPowerOn();
        sensorState = SENSOR_RECOVERY_POWER_ON_WAIT;
        sensorStateDeadlineMs = now + SENSOR_RECOVERY_RESTART_MS;
      }
      break;

    case SENSOR_RECOVERY_POWER_ON_WAIT:
      if (timeReached(now, sensorStateDeadlineMs)) {
        sensorInitBus();
        sensorState = SENSOR_READY;
      }
      break;

    default:
      sensorState = SENSOR_DISABLED;
      break;
  }
}