#include <Arduino.h>
#include <EEPROM.h>
#include <SPI.h>
#include <LoRa.h>

// -----------------------------
// Pinconfig kippenluik
// -----------------------------
const int ENA = 10, IN1 = 9, IN2 = 8;
const int reedTop = 2, reedBottom = 4, LDRpin = A0;
const int PWM_SPEED = 200;

const int EEPROM_ADDR_THRESHOLD = 0;
const int EEPROM_ADDR_OPEN      = 4;
const int EEPROM_ADDR_CLOSE     = 8;
const int LIGHT_DEFAULT         = 500;

int lightThreshold = LIGHT_DEFAULT;
unsigned long maxOpenTime  = 10000;
unsigned long maxCloseTime = 10000;

enum DoorState { UNKNOWN, OPEN, CLOSED, MOVING_UP, MOVING_DOWN, ERROR_STATE };
DoorState state = UNKNOWN;
unsigned long lastPrint = 0;

// -----------------------------
// LoRa-config
// -----------------------------
const long LORA_FREQUENCY = 868E6;   // 868 MHz (EU)
const byte LORA_SYNC_WORD  = 0x12;   // private sync word

// RFM95W-pinnen op de Arduino
const int LORA_NSS  = 7;
const int LORA_RST  = 6;
const int LORA_DIO0 = 3;

unsigned long lastLoraSend = 0;
const unsigned long LORA_INTERVAL = 5000; // elke 5 s een pakket sturen
bool loraOk = false;

// -----------------------------
// Motorhelpers
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
// Reed helpers
// -----------------------------
bool reedActiveRaw(int pin) {
  // LOW = actief bij INPUT_PULLUP
  return digitalRead(pin) == LOW;
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

// -----------------------------
// EEPROM
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

  EEPROM.get(EEPROM_ADDR_OPEN, maxOpenTime);
  EEPROM.get(EEPROM_ADDR_CLOSE, maxCloseTime);
  if (maxOpenTime == 0 || maxCloseTime == 0) {
    maxOpenTime = maxCloseTime = 10000;
  }
}

// -----------------------------
// Statushelpers
// -----------------------------
const char* doorStateToString(DoorState s) {
  switch (s) {
    case OPEN:         return "OPEN";
    case CLOSED:       return "CLOSED";
    case MOVING_UP:    return "MOVING_UP";
    case MOVING_DOWN:  return "MOVING_DOWN";
    case ERROR_STATE:  return "ERROR";
    default:           return "UNKNOWN";
  }
}

void printStatus(int lightValue) {
  Serial.print("Licht=");      Serial.print(lightValue);
  Serial.print("  Drempel=");  Serial.print(lightThreshold);
  Serial.print("  Top=");
  Serial.print(reedActiveRaw(reedTop) ? "GESLOTEN" : "OPEN");
  Serial.print("  Bottom=");
  Serial.print(reedActiveRaw(reedBottom) ? "GESLOTEN" : "OPEN");
  Serial.print("  Luik=");
  Serial.println(doorStateToString(state));
}

// -----------------------------
// Beweging met failsafe
// -----------------------------
bool moveUntilReedOrTimeout(bool omhoog) {
  unsigned long start = millis();
  unsigned long limit = omhoog ? maxOpenTime : maxCloseTime;
  const char* actie   = omhoog ? "openen" : "sluiten";
  int stopPin         = omhoog ? reedTop : reedBottom;

  Serial.print("Actie: "); Serial.println(actie);
  if (omhoog) motorUp(); else motorDown();

  while (millis() - start < limit) {
    if (reedActiveStable(stopPin)) {
      motorStop();
      Serial.print("Reed gedetecteerd bij "); Serial.println(actie);
      return true;
    }
    delay(10);
  }

  motorStop();
  Serial.print("Tijdslimiet bereikt bij "); Serial.println(actie);
  state = ERROR_STATE;
  return false;
}

// -----------------------------
// Calibratie zonder harde tijdslimiet
// -----------------------------
void calibrateTimes() {
  Serial.println("Start automatische calibratie...");
  unsigned long t0, durClose = 0, durOpen = 0;

  // Eerst volledig sluiten
  if (!reedActiveStable(reedBottom)) {
    Serial.println("Sluiten tot onderste reed...");
    motorDown();
    t0 = millis();
    while (!reedActiveStable(reedBottom)) {
      if ((millis() - t0) % 1000 < 10) {
        Serial.print("DBG Top=");    Serial.print(reedActiveRaw(reedTop));
        Serial.print(" Bottom=");    Serial.println(reedActiveRaw(reedBottom));
      }
      delay(10);
    }
    durClose = millis() - t0;
    motorStop();
    Serial.print("Onderste reed bereikt na "); Serial.print(durClose); Serial.println(" ms.");
  } else {
    durClose = 0;
    Serial.println("Onderste reed was al actief bij start.");
  }

  delay(1000);

  // Beweging omhoog
  Serial.println("Beweging omhoog voor kalibratie...");
  t0 = millis();
  motorUp();
  while (!reedActiveStable(reedTop)) {
    if ((millis() - t0) % 1000 < 10) {
      Serial.print("DBG Top=");    Serial.print(reedActiveRaw(reedTop));
      Serial.print(" Bottom=");    Serial.println(reedActiveRaw(reedBottom));
    }
    delay(10);
  }
  durOpen = millis() - t0;
  motorStop();
  Serial.print("Bovenste reed bereikt na "); Serial.print(durOpen); Serial.println(" ms.");

  delay(1000);

  // Beweging omlaag
  Serial.println("Beweging omlaag voor kalibratie...");
  t0 = millis();
  motorDown();
  while (!reedActiveStable(reedBottom)) {
    if ((millis() - t0) % 1000 < 10) {
      Serial.print("DBG Top=");    Serial.print(reedActiveRaw(reedTop));
      Serial.print(" Bottom=");    Serial.println(reedActiveRaw(reedBottom));
    }
    delay(10);
  }
  durClose = millis() - t0;
  motorStop();
  Serial.print("Onderste reed opnieuw bereikt na "); Serial.print(durClose); Serial.println(" ms.");

  // Opslaan met 10% marge
  maxOpenTime  = (unsigned long)(durOpen  * 1.1);
  maxCloseTime = (unsigned long)(durClose * 1.1);
  EEPROM.put(EEPROM_ADDR_OPEN,  maxOpenTime);
  EEPROM.put(EEPROM_ADDR_CLOSE, maxCloseTime);

  Serial.print("Calibratie voltooid. Open=");  Serial.print(durOpen);
  Serial.print(" ms, Dicht=");                 Serial.print(durClose);
  Serial.println(" ms (met 10% marge).");
  state = CLOSED;
}

// -----------------------------
// Seriële commando's
// -----------------------------
void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();

  if (line.startsWith("T=")) {
    int v = line.substring(2).toInt();
    saveThreshold(v);
    Serial.print("Nieuwe drempel via Serial: "); Serial.println(lightThreshold);
  } else if (line == "S?") {
    int lightValue = analogRead(LDRpin);
    printStatus(lightValue);
  } else if (line == "CAL") {
    calibrateTimes();
  } else {
    Serial.println("Commando's: T=xxx | S? | CAL");
  }
}

// -----------------------------
// LoRa: JSON-statuspakket zenden
// -----------------------------
void sendLoraStatus(int lightValue) {
  if (!loraOk) return;

  // JSON-payload
  // {"light":512,"threshold":600,"door":"OPEN","reedTop":0,"reedBottom":1}
  LoRa.beginPacket();
  LoRa.print("{\"light\":");
  LoRa.print(lightValue);
  LoRa.print(",\"threshold\":");
  LoRa.print(lightThreshold);
  LoRa.print(",\"door\":\"");
  LoRa.print(doorStateToString(state));
  LoRa.print("\",\"reedTop\":");
  LoRa.print(reedActiveRaw(reedTop) ? 1 : 0);
  LoRa.print(",\"reedBottom\":");
  LoRa.print(reedActiveRaw(reedBottom) ? 1 : 0);
  LoRa.print("}");
  LoRa.endPacket();  // blokkerend tot verzending klaar is
}

// -----------------------------
// LoRa: RX-commando's verwerken
// -----------------------------
void handleLoraRx(int currentLight) {
  if (!loraOk) return;

  int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) return;

  Serial.print("LoRa-pakket ontvangen, size=");
  Serial.println(packetSize);

  if (packetSize >= 2) {
    uint8_t cmd = LoRa.read();   // eerste byte
    uint8_t val = LoRa.read();   // tweede byte

    // rest van het pakket (indien aanwezig) weggooien
    while (LoRa.available()) {
      LoRa.read();
    }

    if (cmd == 'T') {
      // schaal 0-255 naar 0-1023
      int newThreshold = map(val, 0, 255, 0, 1023);
      saveThreshold(newThreshold);

      Serial.print("Nieuwe drempel via LoRa (raw val=");
      Serial.print(val);
      Serial.print(") -> ");
      Serial.println(newThreshold);
    } else if (cmd == 'S') {
      // Statusrequest via LoRa
      Serial.println("LoRa-statusrequest ontvangen ('S'), stuur status terug.");
      sendLoraStatus(currentLight);
    } else {
      Serial.print("Onbekend LoRa-commando: ");
      Serial.println((char)cmd);
    }
  } else {
    // te klein pakket, alles weggooien
    while (LoRa.available()) LoRa.read();
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

  Serial.println("Kippenluik met stabiele reed-detectie, homing, foutstatus en LoRa I/O.");
  Serial.println("Commando's: T=xxx (drempel) | S? (status) | CAL (kalibratie)");
  Serial.print("Ingestelde drempel: "); Serial.println(lightThreshold);

  if (reedActiveStable(reedBottom))      state = CLOSED;
  else if (reedActiveStable(reedTop))    state = OPEN;
  else                                   state = UNKNOWN;

  // Homing bij onbekende startpositie
  if (state == UNKNOWN) {
    Serial.println("Homing: ga omlaag tot onderste reed...");
    moveUntilReedOrTimeout(false);
    state = CLOSED;
  }

  // ---------- LoRa-init ----------
  Serial.println("LoRa init...");
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa init failed. Kippenluik werkt verder zonder LoRa.");
    loraOk = false;
  } else {
    LoRa.setSyncWord(LORA_SYNC_WORD);
    Serial.println("LoRa gereed.");
    loraOk = true;
  }
}

// -----------------------------
// Hoofdlus
// -----------------------------
void loop() {
  handleSerial();

  int lightValue = analogRead(LDRpin);

  // inkomende LoRa-commando's verwerken (met actuele lichtwaarde)
  handleLoraRx(lightValue);

  if (millis() - lastPrint > 5000) {
    printStatus(lightValue);
    lastPrint = millis();
  }

  // Logica voor openen/sluiten luik
  if (lightValue > lightThreshold && state != OPEN) {
    state = MOVING_UP;
    if (moveUntilReedOrTimeout(true)) state = OPEN;
    else state = ERROR_STATE;
  }

  if (lightValue <= lightThreshold && state != CLOSED) {
    state = MOVING_DOWN;
    if (moveUntilReedOrTimeout(false)) state = CLOSED;
    else state = ERROR_STATE;
  }

  // Periodiek LoRa-statuspakket versturen
  if (millis() - lastLoraSend > LORA_INTERVAL) {
    lastLoraSend = millis();
    sendLoraStatus(lightValue);
  }

  delay(200);
}
