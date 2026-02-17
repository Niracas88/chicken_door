#include <Arduino.h>
#include <EEPROM.h>

// ---------- Types (MOET bovenaan) ----------
typedef struct {
  bool top;
  bool bottom;
} Reeds;

// ---------- Forward declarations (veilig op Arduino IDE) ----------
bool reedRaw(int pin);
bool reedStable(int pin, unsigned long ms = 30);
Reeds readReeds();
void syncStateFromReeds(const Reeds& r);
void printStatus(int lightValue, const Reeds& r);
void handleSerial();
void calibrate();
bool moveTo(bool toOpen);
void updateLightMode(int lightValue);
bool motorActive = false;


// =====================================================
// MINIMALE, ROBUUSTE KIPPENLUIK-CODE (zonder LoRa)
// =====================================================

// -----------------------------
// Pinconfig
// -----------------------------
const int ENA = 10, IN1 = 9, IN2 = 8;
const int reedTop = 2, reedBottom = 4, LDRpin = A0;
const uint8_t PWM_SPEED = 200;

// -----------------------------
// EEPROM adressen
// -----------------------------
const int EEPROM_ADDR_THRESHOLD = 0;
const int EEPROM_ADDR_OPEN_MS   = 4;
const int EEPROM_ADDR_CLOSE_MS  = 8;

// -----------------------------
// Lichtdrempel
// -----------------------------
const int LIGHT_DEFAULT = 500;
int lightThreshold = LIGHT_DEFAULT;

// Hysterese + stabiliteit
const int LIGHT_HYST = 30;
const unsigned long LIGHT_STABLE_MS = 15000UL;

// -----------------------------
// Timeouts
// -----------------------------
const unsigned long DEFAULT_OPEN_MS  = 10000UL;
const unsigned long DEFAULT_CLOSE_MS = 10000UL;
const unsigned long MIN_TIMEOUT_MS   = 100000UL;
const unsigned long MAX_TIMEOUT_MS   = 100000UL;

const float         CAL_MARGIN_FACTOR = 1.25f;
const unsigned long CAL_MARGIN_MS     = 1000UL;

unsigned long maxOpenTime  = DEFAULT_OPEN_MS;
unsigned long maxCloseTime = DEFAULT_CLOSE_MS;

// -----------------------------
// State
// -----------------------------
enum DoorState { UNKNOWN, OPEN, CLOSED, MOVING_UP, MOVING_DOWN, ERROR_STATE };
DoorState state = UNKNOWN;

// -----------------------------
// Motor helpers
// -----------------------------
void motorStop() {
  analogWrite(ENA, 0);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  motorActive = false;

}
void motorUp() {
  analogWrite(ENA, PWM_SPEED);
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  motorActive = true;

}
void motorDown() {
  analogWrite(ENA, PWM_SPEED);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  motorActive = true;

}

// -----------------------------
// Reed helpers
// -----------------------------
bool reedRaw(int pin) { return digitalRead(pin) == LOW; }

bool reedStable(int pin, unsigned long ms) {
  bool v = reedRaw(pin);
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    bool v2 = reedRaw(pin);
    if (v2 != v) { v = v2; t0 = millis(); }
  }
  return v;
}

Reeds readReeds() {
  Reeds r;
  r.top = reedStable(reedTop, 30);
  r.bottom = reedStable(reedBottom, 30);
  return r;
}

// -----------------------------
// EEPROM load/save
// -----------------------------
unsigned long clampTimeout(unsigned long t) {
  if (t < MIN_TIMEOUT_MS) return MIN_TIMEOUT_MS;
  if (t > MAX_TIMEOUT_MS) return MAX_TIMEOUT_MS;
  return t;
}

void saveThreshold(int v) {
  v = constrain(v, 0, 1023);
  EEPROM.put(EEPROM_ADDR_THRESHOLD, v);
  lightThreshold = v;
  Serial.print("Threshold saved: "); Serial.println(lightThreshold);
}

void saveTimes(unsigned long openMs, unsigned long closeMs) {
  openMs = clampTimeout(openMs);
  closeMs = clampTimeout(closeMs);
  EEPROM.put(EEPROM_ADDR_OPEN_MS, openMs);
  EEPROM.put(EEPROM_ADDR_CLOSE_MS, closeMs);
  maxOpenTime = openMs;
  maxCloseTime = closeMs;
  Serial.print("Times saved: open="); Serial.print(maxOpenTime);
  Serial.print(" close="); Serial.println(maxCloseTime);
}

void loadConfig() {
  int thr;
  EEPROM.get(EEPROM_ADDR_THRESHOLD, thr);
  if (thr < 0 || thr > 1023) thr = LIGHT_DEFAULT;
  lightThreshold = thr;

  EEPROM.get(EEPROM_ADDR_OPEN_MS, maxOpenTime);
  EEPROM.get(EEPROM_ADDR_CLOSE_MS, maxCloseTime);

  if (maxOpenTime == 0 || maxOpenTime > 0xFFFFFFUL) maxOpenTime = DEFAULT_OPEN_MS;
  if (maxCloseTime == 0 || maxCloseTime > 0xFFFFFFUL) maxCloseTime = DEFAULT_CLOSE_MS;

  maxOpenTime = clampTimeout(maxOpenTime);
  maxCloseTime = clampTimeout(maxCloseTime);
}

// -----------------------------
// Status / state sync
// -----------------------------
const char* stateStr(DoorState s) {
  switch (s) {
    case OPEN: return "OPEN";
    case CLOSED: return "CLOSED";
    case MOVING_UP: return "MOVING_UP";
    case MOVING_DOWN: return "MOVING_DOWN";
    case ERROR_STATE: return "ERROR";
    default: return "UNKNOWN";
  }
}

void syncStateFromReeds(const Reeds& r) {
  if (r.top && !r.bottom) state = OPEN;
  else if (r.bottom && !r.top) state = CLOSED;
  else if (r.top && r.bottom) state = ERROR_STATE;
}

void printStatus(int lightValue, const Reeds& r) {
  Serial.print("Light="); Serial.print(lightValue);
  Serial.print(" Thr="); Serial.print(lightThreshold);
  Serial.print(" Top="); Serial.print(r.top ? "1" : "0");
  Serial.print(" Bot="); Serial.print(r.bottom ? "1" : "0");
  Serial.print(" State="); Serial.print(stateStr(state));
  Serial.print(" Motor=");
  Serial.print(motorActive ? "ON" : "OFF");
  Serial.print(" Topen="); Serial.print(maxOpenTime);
  Serial.print(" Tclose="); Serial.println(maxCloseTime);
}

// -----------------------------
// Movement with timeout
// -----------------------------
bool moveTo(bool toOpen) {
  unsigned long start = millis();
  unsigned long limit = toOpen ? maxOpenTime : maxCloseTime;
  int stopPin = toOpen ? reedTop : reedBottom;

  state = toOpen ? MOVING_UP : MOVING_DOWN;
  if (toOpen) motorUp(); else motorDown();

  while (millis() - start < limit) {
    if (reedStable(stopPin, 30)) {
      motorStop();
      state = toOpen ? OPEN : CLOSED;
      return true;
    }
    delay(10);
  }

  motorStop();
  state = ERROR_STATE;
  return false;
}

// -----------------------------
// Light decision
// -----------------------------
enum LightMode { LM_UNKNOWN, LM_DAY, LM_NIGHT };
LightMode lightMode = LM_UNKNOWN;
LightMode candidate = LM_UNKNOWN;
unsigned long candidateStart = 0;

void updateLightMode(int lightValue) {
  int openTh  = lightThreshold + LIGHT_HYST;
  int closeTh = lightThreshold - LIGHT_HYST;

  LightMode now;
  if (lightValue > openTh) now = LM_DAY;
  else if (lightValue < closeTh) now = LM_NIGHT;
  else now = LM_UNKNOWN;

  if (now == LM_UNKNOWN) {
    candidate = LM_UNKNOWN;
    candidateStart = 0;
    return;
  }

  if (now != candidate) {
    candidate = now;
    candidateStart = millis();
    return;
  }

  if (candidateStart && (millis() - candidateStart >= LIGHT_STABLE_MS)) {
    lightMode = candidate;
    candidate = LM_UNKNOWN;
    candidateStart = 0;
  }
}

// -----------------------------
// Calibration
// -----------------------------
void calibrate() {
  Serial.println("CAL: start");
  const unsigned long CAL_BACKSTOP = 100000UL;

  Serial.println("CAL: homing down...");
  motorDown();
  unsigned long t0 = millis();
  while (!reedStable(reedBottom, 30)) {
    if (millis() - t0 > CAL_BACKSTOP) {
      motorStop();
      Serial.println("CAL ERROR: bottom reed not reached (homing).");
      state = ERROR_STATE;
      return;
    }
    delay(10);
  }
  motorStop();
  state = CLOSED;
  delay(800);

  Serial.println("CAL: measuring open...");
  motorUp();
  t0 = millis();
  while (!reedStable(reedTop, 30)) {
    if (millis() - t0 > CAL_BACKSTOP) {
      motorStop();
      Serial.println("CAL ERROR: top reed not reached.");
      state = ERROR_STATE;
      return;
    }
    delay(10);
  }
  unsigned long openDur = millis() - t0;
  motorStop();
  state = OPEN;
  Serial.print("CAL: openDur="); Serial.println(openDur);
  delay(800);

  Serial.println("CAL: measuring close...");
  motorDown();
  t0 = millis();
  while (!reedStable(reedBottom, 30)) {
    if (millis() - t0 > CAL_BACKSTOP) {
      motorStop();
      Serial.println("CAL ERROR: bottom reed not reached.");
      state = ERROR_STATE;
      return;
    }
    delay(10);
  }
  unsigned long closeDur = millis() - t0;
  motorStop();
  state = CLOSED;
  Serial.print("CAL: closeDur="); Serial.println(closeDur);

  unsigned long newOpen  = (unsigned long)(openDur  * CAL_MARGIN_FACTOR) + CAL_MARGIN_MS;
  unsigned long newClose = (unsigned long)(closeDur * CAL_MARGIN_FACTOR) + CAL_MARGIN_MS;
  saveTimes(newOpen, newClose);

  Serial.println("CAL: done");
}

// -----------------------------
// Serial commands
// -----------------------------
void handleSerial() {
  if (!Serial.available()) return;

  char line[32];
  int len = Serial.readBytesUntil('\n', line, sizeof(line) - 1);
  if (len <= 0) return;

  if (line[len - 1] == '\r') len--;
  line[len] = '\0';

  char* p = line;
  while (*p == ' ' || *p == '\t') p++;

  if (strncmp(p, "T=", 2) == 0) {
    saveThreshold(atoi(p + 2));
  } else if (strcmp(p, "S?") == 0) {
    int lightValue = analogRead(LDRpin);
    Reeds r = readReeds();
    syncStateFromReeds(r);
    printStatus(lightValue, r);
  } else if (strcmp(p, "CAL") == 0) {
    calibrate();
  } else if (strcmp(p, "OPEN") == 0) {
    moveTo(true);
  } else if (strcmp(p, "CLOSE") == 0) {
    moveTo(false);
  } else {
    Serial.println("Cmd: T=xxx | S? | CAL | OPEN | CLOSE");
  }
}

// -----------------------------
// Setup
// -----------------------------
void setup() {
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(reedTop, INPUT_PULLUP);
  pinMode(reedBottom, INPUT_PULLUP);

  // Zorg dat motor nooit “zweeft” bij boot
  motorStop();

  Serial.begin(9600);
  delay(100);

  loadConfig();

  Serial.println("Minimal chicken door controller started.");
  Serial.print("Threshold="); Serial.print(lightThreshold);
  Serial.print("  OpenT="); Serial.print(maxOpenTime);
  Serial.print("  CloseT="); Serial.println(maxCloseTime);
  Serial.println("Cmd: T=xxx | S? | CAL | OPEN | CLOSE");

  Reeds r = readReeds();
  syncStateFromReeds(r);
  if (state == UNKNOWN) {
    Serial.println("Homing down at boot...");
    moveTo(false);
  }
}

// -----------------------------
// Main loop
// -----------------------------
void loop() {
  handleSerial();

  int lightValue = analogRead(LDRpin);
  Reeds r = readReeds();

  syncStateFromReeds(r);
  updateLightMode(lightValue);

  if (state != ERROR_STATE) {
    if (lightMode == LM_DAY) {
      if (state != OPEN) moveTo(true);
    } else if (lightMode == LM_NIGHT) {
      if (state != CLOSED) moveTo(false);
    }
  }

  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 1000UL) {
    printStatus(lightValue, r);
    lastStatus = millis();
  }

  delay(200);
}