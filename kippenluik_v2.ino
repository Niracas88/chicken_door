#include <Arduino.h>
#include <EEPROM.h>

// --- Pinconfiguratie ---
const int ENA = 10;
const int IN1 = 9;
const int IN2 = 8;

const int reedTop    = 2;
const int reedBottom = 4;
const int LDRpin     = A0;

// --- Parameters ---
const unsigned long MOVE_TIMEOUT_MS  = 10000; // 10 s failsafe
const unsigned long DEBOUNCE_MS      = 25;
const unsigned long BACKOFF_MS       = 300;   // korte tegenbeweging om reed “weer te pakken”
const int EEPROM_ADDR                = 0;
const int PWM_SPEED_NORMAL           = 200;
const int PWM_SPEED_SLOW             = 140;   // optioneel zacht inrollen
const int LIGHT_DEFAULT              = 500;

// --- Variabelen ---
int lightThreshold = LIGHT_DEFAULT;
unsigned long lastPrint = 0;

enum DoorState { UNKNOWN, OPEN, CLOSED, MOVING_UP, MOVING_DOWN, ERROR_STATE };
DoorState state = UNKNOWN;

// --- Helpers ---
void motorStop() {
  analogWrite(ENA, 0);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
}

void motorUp(int pwm) {
  analogWrite(ENA, pwm);
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
}

void motorDown(int pwm) {
  analogWrite(ENA, pwm);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
}

// eenvoudige debounce: lees pin stabiel gedurende DEBOUNCE_MS
bool readReedStable(int pin) {
  int first = digitalRead(pin);
  unsigned long t0 = millis();
  while (millis() - t0 < DEBOUNCE_MS) {
    if (digitalRead(pin) != first) {
      t0 = millis();
      first = digitalRead(pin);
    }
  }
  return (first == LOW); // LOW = geactiveerd bij INPUT_PULLUP
}

void loadThresholdFromEEPROM() {
  int val; EEPROM.get(EEPROM_ADDR, val);
  if (val >= 0 && val <= 1023) lightThreshold = val;
  else { lightThreshold = LIGHT_DEFAULT; EEPROM.put(EEPROM_ADDR, lightThreshold); }
}

void saveThresholdToEEPROM(int val) {
  if (val < 0) val = 0; if (val > 1023) val = 1023;
  EEPROM.put(EEPROM_ADDR, val);
  lightThreshold = val;
}

void backoffUp() {     // korte tik omhoog
  motorUp(PWM_SPEED_SLOW);
  delay(BACKOFF_MS);
  motorStop();
}
void backoffDown() {   // korte tik omlaag
  motorDown(PWM_SPEED_SLOW);
  delay(BACKOFF_MS);
  motorStop();
}

// rijdt omhoog tot top geactiveerd of timeout. Retour: true = gehaald
bool seekTopWithRecovery() {
  state = MOVING_UP;
  unsigned long t0 = millis();
  motorUp(PWM_SPEED_NORMAL);

  bool seenTop = false;
  while (millis() - t0 < MOVE_TIMEOUT_MS) {
    bool top = readReedStable(reedTop);
    if (top) { // eindstop geraakt
      motorStop();
      // borg dat we niet voorbij rolden: als top meteen weer 'weg' valt, backoff omlaag
      delay(30);
      if (!readReedStable(reedTop)) {
        backoffDown();
      }
      state = OPEN;
      return true;
    }
    delay(5);
  }

  // Timeout → één recovery-poging: ontlasten en opnieuw kort proberen
  motorStop();
  backoffDown();
  t0 = millis();
  motorUp(PWM_SPEED_NORMAL);
  while (millis() - t0 < MOVE_TIMEOUT_MS / 2) {
    if (readReedStable(reedTop)) {
      motorStop();
      delay(30);
      if (!readReedStable(reedTop)) backoffDown();
      state = OPEN;
      return true;
    }
    delay(5);
  }
  motorStop();
  state = ERROR_STATE;
  return false;
}

// rijdt omlaag tot bottom geactiveerd of timeout. Retour: true = gehaald
bool seekBottomWithRecovery() {
  state = MOVING_DOWN;
  unsigned long t0 = millis();
  motorDown(PWM_SPEED_NORMAL);

  while (millis() - t0 < MOVE_TIMEOUT_MS) {
    bool bottom = readReedStable(reedBottom);
    if (bottom) {
      motorStop();
      delay(30);
      if (!readReedStable(reedBottom)) {
        backoffUp();
      }
      state = CLOSED;
      return true;
    }
    delay(5);
  }

  // Timeout → recovery
  motorStop();
  backoffUp();
  t0 = millis();
  motorDown(PWM_SPEED_NORMAL);
  while (millis() - t0 < MOVE_TIMEOUT_MS / 2) {
    if (readReedStable(reedBottom)) {
      motorStop();
      delay(30);
      if (!readReedStable(reedBottom)) backoffUp();
      state = CLOSED;
      return true;
    }
    delay(5);
  }
  motorStop();
  state = ERROR_STATE;
  return false;
}

// Homing bij opstart: ga omlaag tot bottom (veiligste richting)
void homingIfUnknown() {
  bool top = readReedStable(reedTop);
  bool bottom = readReedStable(reedBottom);

  if (top && !bottom) { state = OPEN; return; }
  if (!top && bottom) { state = CLOSED; return; }
  if (top && bottom) { // onwaarschijnlijk; kies veilige sluitbeweging
    backoffUp();
  }
  // beide open → ga naar beneden tot bottom
  seekBottomWithRecovery();
}

// Serial commands: T=xxx en S?
void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.startsWith("T=")) {
    int v = line.substring(2).toInt();
    saveThresholdToEEPROM(v);
    Serial.print("Nieuwe drempel: "); Serial.println(lightThreshold);
  } else if (line == "S?") {
    int lv = analogRead(LDRpin);
    bool top = readReedStable(reedTop);
    bool bottom = readReedStable(reedBottom);
    Serial.print("Licht="); Serial.print(lv);
    Serial.print("  Drempel="); Serial.print(lightThreshold);
    Serial.print("  Top="); Serial.print(top ? "CLOSE" : "OPEN ");
    Serial.print("  Bottom="); Serial.print(bottom ? "CLOSE" : "OPEN ");
    Serial.print("  State="); Serial.println(state);
  } else {
    Serial.println("Onbekend commando. Gebruik: T=xxx of S?");
  }
}

void setup() {
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(reedTop, INPUT_PULLUP);
  pinMode(reedBottom, INPUT_PULLUP);

  Serial.begin(9600);
  delay(100);
  loadThresholdFromEEPROM();

  Serial.println("Kippenluik controller gestart.");
  Serial.println("Commando's: T=xxx (0..1023) | S? (status)");
  Serial.print("Drempel (EEPROM): "); Serial.println(lightThreshold);

  homingIfUnknown();
}

void loop() {
  handleSerial();

  int lightValue = analogRead(LDRpin);
  bool top = readReedStable(reedTop);
  bool bottom = readReedStable(reedBottom);

  // Statuslog elke 5 s
  if (millis() - lastPrint > 5000) {
    Serial.print("Licht=");   Serial.print(lightValue);
    Serial.print("  Drempel="); Serial.print(lightThreshold);
    Serial.print("  Top=");   Serial.print(top ? "CLOSE" : "OPEN ");
    Serial.print("  Bottom=");Serial.print(bottom ? "CLOSE" : "OPEN ");
    Serial.print("  State="); Serial.println(state);
    lastPrint = millis();
  }

  // Besturingslogica met herstel
  if (lightValue > lightThreshold) {         // dag → open
    if (!top) {                               // niet op top → omhoog
      if (!seekTopWithRecovery()) {
        Serial.println("Fout: open mislukt");
      }
    } else {
      state = OPEN;
    }
  } else {                                   // nacht → dicht
    if (!bottom) {                            // niet op bottom → omlaag
      if (!seekBottomWithRecovery()) {
        Serial.println("Fout: dicht mislukt");
      }
    } else {
      state = CLOSED;
    }
  }

  delay(500); // korte ademruimte; geen lange 5s pauzes meer tijdens beweging
}
