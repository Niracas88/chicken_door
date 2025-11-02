#include <Arduino.h>
#include <EEPROM.h>

// --- Pinconfig ---
const int ENA = 10, IN1 = 9, IN2 = 8;
const int reedTop = 2, reedBottom = 4, LDRpin = A0;
const int PWM_SPEED = 200;
const int EEPROM_ADDR_THRESHOLD = 0;
const int EEPROM_ADDR_OPEN = 4;
const int EEPROM_ADDR_CLOSE = 8;
const int LIGHT_DEFAULT = 500;

int lightThreshold = LIGHT_DEFAULT;
unsigned long maxOpenTime = 10000;
unsigned long maxCloseTime = 10000;

enum DoorState { UNKNOWN, OPEN, CLOSED, MOVING_UP, MOVING_DOWN, ERROR_STATE };
DoorState state = UNKNOWN;
unsigned long lastPrint = 0;

// --- Motor ---
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

// --- Reed helpers ---
bool reedActiveRaw(int pin) {
  return digitalRead(pin) == LOW; // LOW = actief bij INPUT_PULLUP
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

// --- EEPROM ---
void saveThreshold(int val) {
  val = constrain(val, 0, 1023);
  EEPROM.put(EEPROM_ADDR_THRESHOLD, val);
  lightThreshold = val;
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

// --- Status ---
void printStatus(int lightValue) {
  Serial.print("Licht="); Serial.print(lightValue);
  Serial.print("  Drempel="); Serial.print(lightThreshold);
  Serial.print("  Top="); Serial.print(reedActiveRaw(reedTop) ? "GESLOTEN" : "OPEN");
  Serial.print("  Bottom="); Serial.print(reedActiveRaw(reedBottom) ? "GESLOTEN" : "OPEN");
  Serial.print("  Luik=");
  switch (state) {
    case OPEN: Serial.println("OPEN"); break;
    case CLOSED: Serial.println("GESLOTEN"); break;
    case MOVING_UP: Serial.println("OPENT"); break;
    case MOVING_DOWN: Serial.println("SLUIT"); break;
    case ERROR_STATE: Serial.println("FOUT"); break;
    default: Serial.println("ONBEKEND");
  }
}

// --- Beweging met failsafe ---
bool moveUntilReedOrTimeout(bool omhoog) {
  unsigned long start = millis();
  unsigned long limit = omhoog ? maxOpenTime : maxCloseTime;
  const char* actie = omhoog ? "openen" : "sluiten";
  int stopPin = omhoog ? reedTop : reedBottom;

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

// --- Calibratie zonder tijdslimiet ---
void calibrateTimes() {
  Serial.println("Start automatische calibratie...");
  unsigned long t0, durClose, durOpen;

  // Eerst volledig sluiten
  if (!reedActiveStable(reedBottom)) {
    Serial.println("Sluiten tot onderste reed...");
    motorDown();
    t0 = millis();
    while (!reedActiveStable(reedBottom)) {
      if ((millis() - t0) % 1000 < 10) {
        Serial.print("DBG Top="); Serial.print(reedActiveRaw(reedTop));
        Serial.print(" Bottom="); Serial.println(reedActiveRaw(reedBottom));
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
      Serial.print("DBG Top="); Serial.print(reedActiveRaw(reedTop));
      Serial.print(" Bottom="); Serial.println(reedActiveRaw(reedBottom));
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
      Serial.print("DBG Top="); Serial.print(reedActiveRaw(reedTop));
      Serial.print(" Bottom="); Serial.println(reedActiveRaw(reedBottom));
    }
    delay(10);
  }
  durClose = millis() - t0;
  motorStop();
  Serial.print("Onderste reed opnieuw bereikt na "); Serial.print(durClose); Serial.println(" ms.");

  // Opslaan met 10% marge
  maxOpenTime  = durOpen  * 1.1;
  maxCloseTime = durClose * 1.1;
  EEPROM.put(EEPROM_ADDR_OPEN, maxOpenTime);
  EEPROM.put(EEPROM_ADDR_CLOSE, maxCloseTime);

  Serial.print("Calibratie voltooid. Open="); Serial.print(durOpen);
  Serial.print(" ms, Dicht="); Serial.print(durClose);
  Serial.println(" ms (met 10% marge).");
  state = CLOSED;
}

// --- Seriële commando's ---
void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.startsWith("T=")) {
    int v = line.substring(2).toInt();
    saveThreshold(v);
    Serial.print("Nieuwe drempel: "); Serial.println(lightThreshold);
  } else if (line == "S?") {
    printStatus(analogRead(LDRpin));
  } else if (line == "CAL") {
    calibrateTimes();
  } else {
    Serial.println("Commando's: T=xxx | S? | CAL");
  }
}

// --- Setup ---
void setup() {
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(reedTop, INPUT_PULLUP);
  pinMode(reedBottom, INPUT_PULLUP);

  Serial.begin(9600);
  delay(100);
  loadConfig();

  Serial.println("Kippenluik met stabiele reed-detectie, homing en foutstatus.");
  Serial.println("Commando's: T=xxx (drempel) | S? (status) | CAL (kalibratie)");
  Serial.print("Ingestelde drempel: "); Serial.println(lightThreshold);

  if (reedActiveStable(reedBottom)) state = CLOSED;
  else if (reedActiveStable(reedTop)) state = OPEN;
  else state = UNKNOWN;

  // Homing bij onbekende startpositie
  if (state == UNKNOWN) {
    Serial.println("Homing: ga omlaag tot onderste reed...");
    moveUntilReedOrTimeout(false);
    state = CLOSED;
  }
}

// --- Hoofdlus ---
void loop() {
  handleSerial();
  int lightValue = analogRead(LDRpin);

  if (millis() - lastPrint > 5000) {
    printStatus(lightValue);
    lastPrint = millis();
  }

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

  delay(200);
}
