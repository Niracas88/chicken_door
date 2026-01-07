#include <Arduino.h>
#include <EEPROM.h>
#include <SPI.h>
#include <LoRa.h>

// -----------------------------
// Pinconfig
// -----------------------------
const int ENA = 10, IN1 = 9, IN2 = 8;
const int reedTop = 2, reedBottom = 4, LDRpin = A0;
const uint8_t PWM_SPEED = 200;

// -----------------------------
// EEPROM / threshold
// -----------------------------
const int EEPROM_ADDR_THRESHOLD = 0;
const int LIGHT_DEFAULT = 500;
int lightThreshold = LIGHT_DEFAULT;

// -----------------------------
// Hysterese + stabiliteitstijd (anti-flutter)
// -----------------------------
const int LIGHT_HYST = 30;                         // 0 = uit
const unsigned long LIGHT_STABLE_MS = 20000UL;     // 20 s stabiel voor actie

// -----------------------------
// Timeouts (adaptief)
// -----------------------------
const unsigned long TIMEOUT_DEFAULT_OPEN  = 10000UL;
const unsigned long TIMEOUT_DEFAULT_CLOSE = 10000UL;
const unsigned long TIMEOUT_MIN = 3000UL;
const unsigned long TIMEOUT_MAX = 30000UL;
const unsigned long TIMEOUT_MARGIN_MS = 1500UL;    // vaste marge bovenop gemeten duur
const float         TIMEOUT_MARGIN_FACTOR = 1.25f; // factor bovenop gemeten duur

unsigned long maxOpenTime  = TIMEOUT_DEFAULT_OPEN;
unsigned long maxCloseTime = TIMEOUT_DEFAULT_CLOSE;

// -----------------------------
// Door state
// -----------------------------
enum DoorState { UNKNOWN, OPEN, CLOSED, MOVING_UP, MOVING_DOWN, ERROR_STATE };
DoorState state = UNKNOWN;

// -----------------------------
// LoRa
// -----------------------------
const long LORA_FREQUENCY = 868E6;
const byte LORA_SYNC_WORD  = 0x12;
const int LORA_NSS  = 7;
const int LORA_RST  = 6;
const int LORA_DIO0 = 3;

bool loraOk = false;
unsigned long lastLoraSend = 0;
const unsigned long LORA_INTERVAL = 5000UL;

// -----------------------------
// Timing / debug
// -----------------------------
unsigned long lastPrint = 0;

// -----------------------------
// Interne helpers: reeds/positie
// -----------------------------
bool reedActiveRaw(int pin) {
  return digitalRead(pin) == LOW; // INPUT_PULLUP => LOW = active
}

bool reedActiveStable(int pin, unsigned long ms = 30) {
  bool first = reedActiveRaw(pin);
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    if (reedActiveRaw(pin) != first) {
      first = reedActiveRaw(pin);
      t0 = millis();
    }
  }
  return first;
}

// Lees reeds 1x per loop (consistente snapshot)
struct ReedSnapshot {
  bool top;
  bool bottom;
};
ReedSnapshot readReedsStable() {
  ReedSnapshot r;
  r.top = reedActiveStable(reedTop);
  r.bottom = reedActiveStable(reedBottom);
  return r;
}

// -----------------------------
// Motor helpers
// -----------------------------
void motorStop() {
  analogWrite(ENA, 0);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
}
void motorUp() {
  analogWrite(ENA, PWM_SPEED);
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
}
void motorDown() {
  analogWrite(ENA, PWM_SPEED);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
}

// -----------------------------
// EEPROM threshold
// -----------------------------
void saveThreshold(int val) {
  val = constrain(val, 0, 1023);
  EEPROM.put(EEPROM_ADDR_THRESHOLD, val);
  lightThreshold = val;
  Serial.print("Drempel opgeslagen (EEPROM) = ");
  Serial.println(lightThreshold);
}

void loadConfig() {
  int val;
  EEPROM.get(EEPROM_ADDR_THRESHOLD, val);
  if (val < 0 || val > 1023) val = LIGHT_DEFAULT;
  lightThreshold = val;
}

// -----------------------------
// Status helpers
// -----------------------------
const char* doorStateToString(DoorState s) {
  switch (s) {
    case OPEN: return "OPEN";
    case CLOSED: return "CLOSED";
    case MOVING_UP: return "MOVING_UP";
    case MOVING_DOWN: return "MOVING_DOWN";
    case ERROR_STATE: return "ERROR";
    default: return "UNKNOWN";
  }
}

void printStatus(int lightValue, const ReedSnapshot& r) {
  Serial.print("Licht="); Serial.print(lightValue);
  Serial.print("  Drempel="); Serial.print(lightThreshold);
  Serial.print("  Top="); Serial.print(r.top ? "GESLOTEN" : "OPEN");
  Serial.print("  Bottom="); Serial.print(r.bottom ? "GESLOTEN" : "OPEN");
  Serial.print("  Luik="); Serial.print(doorStateToString(state));
  Serial.print("  Topen="); Serial.print(maxOpenTime);
  Serial.print("  Tdicht="); Serial.println(maxCloseTime);
}

// -----------------------------
// Adaptive timeout update
// -----------------------------
unsigned long clampTimeout(unsigned long t) {
  if (t < TIMEOUT_MIN) return TIMEOUT_MIN;
  if (t > TIMEOUT_MAX) return TIMEOUT_MAX;
  return t;
}

void updateTimeoutFromDuration(bool omhoog, unsigned long durationMs) {
  // Nieuwe limiet = max(duration*factor + marge, huidige*0.8) (niet te agressief dalen)
  unsigned long proposed = (unsigned long)(durationMs * TIMEOUT_MARGIN_FACTOR) + TIMEOUT_MARGIN_MS;
  proposed = clampTimeout(proposed);

  if (omhoog) {
    unsigned long floorVal = (unsigned long)(maxOpenTime * 0.8f);
    if (proposed < floorVal) proposed = floorVal;
    maxOpenTime = proposed;
  } else {
    unsigned long floorVal = (unsigned long)(maxCloseTime * 0.8f);
    if (proposed < floorVal) proposed = floorVal;
    maxCloseTime = proposed;
  }
}

// -----------------------------
// Movement with failsafe (met meting voor adaptieve timeouts)
// -----------------------------
bool moveUntilReedOrTimeout(bool omhoog) {
  unsigned long start = millis();
  unsigned long limit = omhoog ? maxOpenTime : maxCloseTime;
  int stopPin = omhoog ? reedTop : reedBottom;

  Serial.print("Actie: ");
  Serial.println(omhoog ? "openen" : "sluiten");

  if (omhoog) motorUp(); else motorDown();

  while (millis() - start < limit) {
    if (reedActiveStable(stopPin)) {
      motorStop();
      unsigned long dur = millis() - start;
      Serial.print("Reed gedetecteerd, duur="); Serial.println(dur);
      updateTimeoutFromDuration(omhoog, dur);
      return true;
    }
    delay(10);
  }

  motorStop();
  Serial.println("Tijdslimiet bereikt -> ERROR_STATE");
  state = ERROR_STATE;
  return false;
}

// -----------------------------
// Serial handling (geen String)
// -----------------------------
void handleSerial() {
  if (!Serial.available()) return;
  char line[48];
  int len = Serial.readBytesUntil('\n', line, sizeof(line) - 1);
  if (len <= 0) return;

  if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';
  else line[len] = '\0';

  char *p = line;
  while (*p == ' ' || *p == '\t') p++;

  if (strncmp(p, "T=", 2) == 0) {
    int v = atoi(p + 2);
    saveThreshold(v);
    Serial.print("Nieuwe drempel via Serial: "); Serial.println(lightThreshold);
  } else if (strcmp(p, "S?") == 0) {
    int lightValue = analogRead(LDRpin);
    ReedSnapshot r = readReedsStable();
    printStatus(lightValue, r);
  } else {
    Serial.println("Commando's: T=xxx | S?");
  }
}

// -----------------------------
// LoRa send (kleinere JSON om snprintf op AVR te ontlasten)
// -----------------------------
void sendLoraStatus(int lightValue, const ReedSnapshot& r) {
  if (!loraOk) return;

  // Compacte keys: {"l":..,"t":..,"d":"OPEN","rt":0,"rb":1}
  char buf[96];
  int n = snprintf(buf, sizeof(buf),
    "{\"l\":%d,\"t\":%d,\"d\":\"%s\",\"rt\":%d,\"rb\":%d}",
    lightValue, lightThreshold, doorStateToString(state),
    r.top ? 1 : 0, r.bottom ? 1 : 0
  );
  if (n <= 0 || n >= (int)sizeof(buf)) return;

  LoRa.beginPacket();
  LoRa.print(buf);
  LoRa.endPacket();
}

// -----------------------------
// LoRa RX (robuster: packetSize checks)
// Protocol: 'T' <1 byte val>  of  'S'
// -----------------------------
void handleLoraRx(int currentLight, const ReedSnapshot& r) {
  if (!loraOk) return;

  int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) return;

  // Minstens 1 byte nodig (cmd)
  if (packetSize < 1) {
    while (LoRa.available()) LoRa.read();
    return;
  }

  int cmd = LoRa.read();

  if (cmd == 'T') {
    // Verwacht exact: cmd + 1 byte value => minstens 2 bytes
    if (packetSize < 2 || !LoRa.available()) {
      Serial.println("LoRa 'T' ongeldig (te klein).");
    } else {
      int val = LoRa.read(); // 0..255
      int newThreshold = map(val, 0, 255, 0, 1023);
      saveThreshold(newThreshold);
      Serial.print("Nieuwe drempel via LoRa (raw=");
      Serial.print(val);
      Serial.print(") -> ");
      Serial.println(newThreshold);
    }
  } else if (cmd == 'S') {
    Serial.println("LoRa status request ontvangen, stuur status.");
    sendLoraStatus(currentLight, r);
  } else {
    Serial.print("Onbekend LoRa-commando: ");
    Serial.println((char)cmd);
  }

  while (LoRa.available()) LoRa.read();
}

// -----------------------------
// State update per loop + error recovery policy
// -----------------------------
// Doel:
// - state wordt elke loop herberekend op basis van reeds (positie “waarheid”)
// - ERROR_STATE blijft “latched”, maar kan automatisch herstellen als een eindpositie stabiel is
const unsigned long ERROR_RECOVER_STABLE_MS = 800UL;
unsigned long errorRecoverStart = 0;

void updateStateEveryLoop(const ReedSnapshot& r) {
  // Als we fysiek in een eindpositie zijn, is dat de waarheid.
  // Tijdens beweging zitten we in blocking moveUntil..., dus deze functie draait dan niet.
  if (r.top && !r.bottom) {
    // bovenste positie
    if (state == ERROR_STATE) {
      if (errorRecoverStart == 0) errorRecoverStart = millis();
      if (millis() - errorRecoverStart >= ERROR_RECOVER_STABLE_MS) {
        state = OPEN;
      }
    } else {
      state = OPEN;
    }
    return;
  }

  if (r.bottom && !r.top) {
    // onderste positie
    if (state == ERROR_STATE) {
      if (errorRecoverStart == 0) errorRecoverStart = millis();
      if (millis() - errorRecoverStart >= ERROR_RECOVER_STABLE_MS) {
        state = CLOSED;
      }
    } else {
      state = CLOSED;
    }
    return;
  }

  // Onmogelijke toestand: beide reeds actief -> fout houden (bedrading/magneten)
  if (r.top && r.bottom) {
    state = ERROR_STATE;
    return;
  }

  // Geen eindpositie: reset recovery timer, laat state ongemoeid (UNKNOWN of ERROR blijft)
  errorRecoverStart = 0;
}

// -----------------------------
// Lichtbeslissing met hysterese + “stabiel gedurende X ms”
// -----------------------------
enum LightDecision { LIGHT_UNKNOWN, LIGHT_BRIGHT, LIGHT_DARK };
LightDecision lightDecision = LIGHT_UNKNOWN;
unsigned long lightCandidateStart = 0;
LightDecision lightCandidate = LIGHT_UNKNOWN;

void updateLightDecision(int lightValue) {
  int openTh  = lightThreshold + LIGHT_HYST;
  int closeTh = lightThreshold - LIGHT_HYST;

  // Bepaal kandidaat
  LightDecision nowCand = LIGHT_UNKNOWN;
  if (lightValue > openTh) nowCand = LIGHT_BRIGHT;
  else if (lightValue < closeTh) nowCand = LIGHT_DARK;
  else nowCand = LIGHT_UNKNOWN; // in band: geen verandering afdwingen

  // In band: kandidaat resetten, behoud huidige beslissing
  if (nowCand == LIGHT_UNKNOWN) {
    lightCandidate = LIGHT_UNKNOWN;
    lightCandidateStart = 0;
    return;
  }

  // Nieuwe kandidaat gestart?
  if (nowCand != lightCandidate) {
    lightCandidate = nowCand;
    lightCandidateStart = millis();
    return;
  }

  // Kandidaat is dezelfde: lang genoeg?
  if (lightCandidateStart != 0 && (millis() - lightCandidateStart >= LIGHT_STABLE_MS)) {
    lightDecision = lightCandidate;
    // Laat candidate lopen of reset; reset geeft minder spam in transitions
    lightCandidate = LIGHT_UNKNOWN;
    lightCandidateStart = 0;
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

  Serial.begin(9600);
  delay(100);
  loadConfig();

  Serial.println("Kippenluik (robuster) gestart.");
  Serial.print("Ingestelde drempel: "); Serial.println(lightThreshold);

  // init state from reeds
  ReedSnapshot r = readReedsStable();
  if (r.bottom && !r.top) state = CLOSED;
  else if (r.top && !r.bottom) state = OPEN;
  else state = UNKNOWN;

  // homing indien unknown (fout niet maskeren)
  if (state == UNKNOWN) {
    Serial.println("Homing: naar beneden tot onderste reed...");
    state = MOVING_DOWN;
    if (moveUntilReedOrTimeout(false)) state = CLOSED;
    // bij timeout: state is ERROR_STATE
  }

  // init timeouts conservatief
  maxOpenTime  = TIMEOUT_DEFAULT_OPEN;
  maxCloseTime = TIMEOUT_DEFAULT_CLOSE;

  // LoRa init
  Serial.println("LoRa init...");
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa init failed. Werkt verder zonder LoRa.");
    loraOk = false;
  } else {
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.enableCrc();
    Serial.println("LoRa gereed.");
    loraOk = true;
  }
}

// -----------------------------
// Main loop
// -----------------------------
void loop() {
  handleSerial();

  int lightValue = analogRead(LDRpin);
  ReedSnapshot r = readReedsStable();

  // 1) state update per loop (en gecontroleerde error-recovery)
  updateStateEveryLoop(r);

  // 2) LoRa RX robuuster
  handleLoraRx(lightValue, r);

  // 3) debug
  if (millis() - lastPrint > 5000) {
    printStatus(lightValue, r);
    lastPrint = millis();
  }

  // 4) lichtbeslissing met hysterese + stabiele tijd
  updateLightDecision(lightValue);

  // 5) bewegingen enkel als niet in ERROR (latched) en enkel op stabiele lightDecision
  //    - in ERROR_STATE: eerst fysiek eindpositie bereiken (auto-recovery), of reset/handmatig
  if (state != ERROR_STATE) {
    if (lightDecision == LIGHT_BRIGHT && state != OPEN) {
      state = MOVING_UP;
      if (moveUntilReedOrTimeout(true)) state = OPEN;
      else state = ERROR_STATE;
    } else if (lightDecision == LIGHT_DARK && state != CLOSED) {
      state = MOVING_DOWN;
      if (moveUntilReedOrTimeout(false)) state = CLOSED;
      else state = ERROR_STATE;
    }
  }

  // 6) periodiek LoRa status
  if (millis() - lastLoraSend > LORA_INTERVAL) {
    lastLoraSend = millis();
    sendLoraStatus(lightValue, r);
  }

  delay(200);
}
