/*
  Smart Adaptive Street Light Controller — Firmware
  ---------------------------------------------------
  Hardware:
    LDR module   D0 -> D5   (digital day/night, threshold set by onboard trimmer)
    IR sensor 1  OUT -> D2  (zone 1 / closest)
    IR sensor 2  OUT -> D3  (zone 2 / middle)
    IR sensor 3  OUT -> D4  (zone 3 / farthest)
    LED 1        D9  (PWM, 220ohm resistor in series)
    LED 2        D10 (PWM, 220ohm resistor in series)
    LED 3        D11 (PWM, 220ohm resistor in series)

  This firmware is the authoritative real-time controller. It keeps running
  the automatic logic even if no dashboard is connected (FR-10). The
  dashboard only sends high-level mode/override commands over USB serial.

  Modules below are separated by section, matching the PRD's firmware
  architecture (Sensor Manager / Traffic Analyzer / Predictive Lighting
  Manager / Weather Manager / Emergency Manager / LED Controller /
  Serial Communication Manager), kept in one file for a beginner-friendly
  single-sketch build.
*/

// ---------- Pin configuration ----------
const int LDR_PIN = 5;
const int IR_PINS[3] = {2, 3, 4};
const int LED_PINS[3] = {9, 10, 11};

// SET based on your LDR test: does covering it (dark) give HIGH or LOW?
const bool LDR_HIGH_MEANS_NIGHT = true;

// ---------- Timing ----------
const unsigned long DEBOUNCE_MS = 40;          // sensor debounce window
const unsigned long PREDICTION_HOLD_MS = 2500; // how long a predicted zone stays pre-illuminated
const unsigned long SEND_INTERVAL_MS = 200;    // telemetry rate to dashboard

// ---------- Brightness levels (0-100%) ----------
const int BRIGHTNESS_OFF = 0;
const int BRIGHTNESS_FULL = 100;

// =====================================================================
// SENSOR MANAGER — debounced reads of LDR + 3 IR sensors
// =====================================================================
bool irRawState[3];         // true = object detected (post-debounce)
bool irLastReading[3];      // last raw pin reading, for debounce tracking
unsigned long irLastChange[3];

bool ldrRawIsNight;
bool ldrLastReading;
unsigned long ldrLastChange;

void sensorManager_setup() {
  pinMode(LDR_PIN, INPUT);
  for (int i = 0; i < 3; i++) {
    pinMode(IR_PINS[i], INPUT);
    irLastReading[i] = digitalRead(IR_PINS[i]) == LOW; // active-LOW modules
    irRawState[i] = irLastReading[i];
    irLastChange[i] = millis();
  }
  bool ldrPin = digitalRead(LDR_PIN);
  ldrLastReading = LDR_HIGH_MEANS_NIGHT ? (ldrPin == HIGH) : (ldrPin == LOW);
  ldrRawIsNight = ldrLastReading;
  ldrLastChange = millis();
}

// Returns true if IR sensor i just transitioned from clear -> detected this cycle
bool sensorManager_update(bool risingEdge[3]) {
  unsigned long now = millis();
  bool anyChange = false;

  for (int i = 0; i < 3; i++) {
    risingEdge[i] = false;
    bool reading = digitalRead(IR_PINS[i]) == LOW; // active-LOW
    if (reading != irLastReading[i]) {
      irLastReading[i] = reading;
      irLastChange[i] = now;
    }
    if ((now - irLastChange[i]) > DEBOUNCE_MS && irRawState[i] != irLastReading[i]) {
      bool prev = irRawState[i];
      irRawState[i] = irLastReading[i];
      if (!prev && irRawState[i]) risingEdge[i] = true; // clear -> detected
      anyChange = true;
    }
  }

  bool ldrPin = digitalRead(LDR_PIN);
  bool reading = LDR_HIGH_MEANS_NIGHT ? (ldrPin == HIGH) : (ldrPin == LOW);
  if (reading != ldrLastReading) {
    ldrLastReading = reading;
    ldrLastChange = now;
  }
  if ((now - ldrLastChange) > DEBOUNCE_MS && ldrRawIsNight != ldrLastReading) {
    ldrRawIsNight = ldrLastReading;
    anyChange = true;
  }

  return anyChange;
}

// =====================================================================
// TRAFFIC ANALYZER — active-zone count -> Low/Medium/High
// =====================================================================
// Plain int constants instead of an enum — Arduino's auto-generated function
// prototypes are inserted at the very top of the file, before any custom
// type would be defined, which breaks compilation if a function signature
// uses that custom type. Built-in types (int) sidestep the issue entirely.
const int TRAFFIC_LOW = 0;
const int TRAFFIC_MEDIUM = 1;
const int TRAFFIC_HIGH = 2;
unsigned long sessionDetectionCount = 0;

int trafficAnalyzer_level() {
  int activeCount = (irRawState[0] ? 1 : 0) + (irRawState[1] ? 1 : 0) + (irRawState[2] ? 1 : 0);
  if (activeCount >= 3) return TRAFFIC_HIGH;
  if (activeCount == 2) return TRAFFIC_MEDIUM;
  return TRAFFIC_LOW; // 0 or 1 active
}

int trafficAnalyzer_baseline(int level) {
  switch (level) {
    case TRAFFIC_HIGH: return 100;
    case TRAFFIC_MEDIUM: return 50;
    default: return 20;
  }
}

const char* trafficAnalyzer_name(int level) {
  switch (level) {
    case TRAFFIC_HIGH: return "HIGH";
    case TRAFFIC_MEDIUM: return "MEDIUM";
    default: return "LOW";
  }
}

// =====================================================================
// PREDICTIVE LIGHTING MANAGER — direction inference + next-zone hold
// =====================================================================
int zoneHistory[2] = {0, 0};        // [0]=older activation, [1]=most recent activation (1-3, 0=none yet)
unsigned long predictionHoldUntil = 0;
int predictedZone = 0;              // 0 = no prediction active

void predictive_onDetection(int zone /* 1-3 */) {
  if (zone == zoneHistory[1]) return; // same zone re-triggering doesn't shift history
  zoneHistory[0] = zoneHistory[1];
  zoneHistory[1] = zone;

  int direction = 0;
  if (zoneHistory[0] != 0) {
    int delta = zoneHistory[1] - zoneHistory[0];
    if (delta == 1 || delta == -1) direction = delta; // reliable forward/backward transition
  }

  if (direction != 0) {
    int next = zoneHistory[1] + direction;
    if (next >= 1 && next <= 3) {
      predictedZone = next;
      predictionHoldUntil = millis() + PREDICTION_HOLD_MS;
      return;
    }
  }
  // Direction not reliably established, or predicted zone out of range -> safe local response only
  predictedZone = 0;
}

bool predictive_zoneIsPredicted(int zone /* 1-3 */) {
  if (predictedZone == 0) return false;
  if (millis() > predictionHoldUntil) { predictedZone = 0; return false; }
  return predictedZone == zone;
}

// =====================================================================
// WEATHER MANAGER
// =====================================================================
const int WEATHER_CLEAR = 0;
const int WEATHER_RAIN = 1;
const int WEATHER_HEAVY = 2;
const int WEATHER_FOG = 3;
int currentWeather = WEATHER_CLEAR;

int weatherManager_baseline() {
  switch (currentWeather) {
    case WEATHER_RAIN: return 50;
    case WEATHER_HEAVY: return 100;
    case WEATHER_FOG: return 100;
    default: return 20; // CLEAR
  }
}

const char* weatherManager_name() {
  switch (currentWeather) {
    case WEATHER_RAIN: return "RAIN";
    case WEATHER_HEAVY: return "HEAVY";
    case WEATHER_FOG: return "FOG";
    default: return "CLEAR";
  }
}

// =====================================================================
// EMERGENCY MANAGER
// =====================================================================
bool emergencyActive = false;

// =====================================================================
// LED CONTROLLER — percentage (0-100) -> constrained PWM (0-255)
// =====================================================================
void ledController_apply(int zoneIndex /* 0-2 */, int percent) {
  percent = constrain(percent, 0, 100);
  int pwmValue = map(percent, 0, 100, 0, 255);
  analogWrite(LED_PINS[zoneIndex], pwmValue);
}

// =====================================================================
// SERIAL COMMUNICATION MANAGER
// =====================================================================
String serialBuffer = "";
int lastSentBrightness[3] = {-1, -1, -1}; // for telemetry only, not control

void serialComm_handleCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "MODE:AUTO") {
    emergencyActive = false;
  } else if (cmd == "MODE:EMERGENCY" || cmd == "EMERGENCY:ON") {
    emergencyActive = true;
  } else if (cmd == "EMERGENCY:OFF") {
    emergencyActive = false;
  } else if (cmd == "MODE:WEATHER") {
    // Weather mode is always active as a baseline input; nothing to toggle separately.
  } else if (cmd == "WEATHER:CLEAR") {
    currentWeather = WEATHER_CLEAR;
  } else if (cmd == "WEATHER:RAIN") {
    currentWeather = WEATHER_RAIN;
  } else if (cmd == "WEATHER:HEAVY") {
    currentWeather = WEATHER_HEAVY;
  } else if (cmd == "WEATHER:FOG") {
    currentWeather = WEATHER_FOG;
  }
  // Unrecognized/malformed commands are silently ignored (NFR: tolerate malformed input).
}

void serialComm_poll() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n') {
      serialComm_handleCommand(serialBuffer);
      serialBuffer = "";
    } else if (c != '\r') {
      serialBuffer += c;
      if (serialBuffer.length() > 64) serialBuffer = ""; // guard against runaway input
    }
  }
}

unsigned long lastSendTime = 0;
int lastBrightness[3] = {0, 0, 0};
int lastPredicted = 0;
String lastDirection = "NONE";

void serialComm_sendTelemetry(bool isNight, int traffic) {
  unsigned long now = millis();
  if (now - lastSendTime < SEND_INTERVAL_MS) return;
  lastSendTime = now;

  Serial.print("DAYNIGHT:"); Serial.println(isNight ? "NIGHT" : "DAY");
  Serial.print("IR1:"); Serial.println(irRawState[0] ? 1 : 0);
  Serial.print("IR2:"); Serial.println(irRawState[1] ? 1 : 0);
  Serial.print("IR3:"); Serial.println(irRawState[2] ? 1 : 0);
  Serial.print("TRAFFIC:"); Serial.println(trafficAnalyzer_name(traffic));
  Serial.print("LED1:"); Serial.println(lastBrightness[0]);
  Serial.print("LED2:"); Serial.println(lastBrightness[1]);
  Serial.print("LED3:"); Serial.println(lastBrightness[2]);
  Serial.print("MODE:"); Serial.println(emergencyActive ? "EMERGENCY" : "AUTO");
  Serial.print("EMERGENCY:"); Serial.println(emergencyActive ? 1 : 0);
  Serial.print("WEATHER:"); Serial.println(weatherManager_name());
  Serial.print("DIRECTION:"); Serial.println(lastDirection);
  Serial.print("PREDICTED:"); Serial.println(lastPredicted == 0 ? "NONE" : String(lastPredicted));
  Serial.print("DETECTIONS:"); Serial.println(sessionDetectionCount);
  Serial.println("---"); // frame separator, makes line-grouping trivial on the browser side
}

// =====================================================================
// MAIN
// =====================================================================
void setup() {
  Serial.begin(9600);
  sensorManager_setup();
  for (int i = 0; i < 3; i++) pinMode(LED_PINS[i], OUTPUT);
}

void loop() {
  serialComm_poll();

  bool risingEdge[3];
  sensorManager_update(risingEdge);

  for (int i = 0; i < 3; i++) {
    if (risingEdge[i]) {
      sessionDetectionCount++;
      predictive_onDetection(i + 1);
    }
  }

  // Update the direction/prediction telemetry strings once per loop
  if (zoneHistory[0] != 0 && zoneHistory[1] != 0) {
    lastDirection = String(zoneHistory[0]) + "-" + String(zoneHistory[1]);
  } else {
    lastDirection = "NONE";
  }
  lastPredicted = predictedZone;

  bool isNight = ldrRawIsNight;
  int traffic = trafficAnalyzer_level();

  // ---- Priority logic (Section 10 of the PRD) ----
  if (emergencyActive) {
    for (int i = 0; i < 3; i++) {
      ledController_apply(i, BRIGHTNESS_FULL);
      lastBrightness[i] = BRIGHTNESS_FULL;
    }
  } else if (isNight) {
    int weatherBaseline = weatherManager_baseline();
    int trafficBaseline = trafficAnalyzer_baseline(traffic);
    int baseline = max(weatherBaseline, trafficBaseline);

    for (int i = 0; i < 3; i++) {
      int zone = i + 1;
      int target;
      if (irRawState[i]) {
        target = BRIGHTNESS_FULL;                         // night + detection
      } else if (predictive_zoneIsPredicted(zone)) {
        target = BRIGHTNESS_FULL;                         // pre-illuminate predicted next zone
      } else {
        target = baseline;                                 // energy-saving baseline
      }
      target = min(target, BRIGHTNESS_FULL);               // never exceed 100%
      ledController_apply(i, target);
      lastBrightness[i] = target;
    }
  } else {
    for (int i = 0; i < 3; i++) {
      ledController_apply(i, BRIGHTNESS_OFF);
      lastBrightness[i] = BRIGHTNESS_OFF;
    }
  }

  serialComm_sendTelemetry(isNight, traffic);
}
