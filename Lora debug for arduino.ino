/*
  LoRa Debug Arduino (UNO) — RX sniffer + optional TX ping/commands
  Werkt met je bestaande kippenluik-code:
  - Frequentie: 868 MHz
  - Syncword: 0x12
  - SF7, BW 125 kHz, CRC aan
  - Pinnen (UNO + RFM95W): NSS=7, RST=6, DIO0=3, SPI=11/12/13

  Serial Monitor: 115200 baud, "Newline" aan.

  Commando’s in Serial Monitor:
  - PING              -> stuurt "PING <counter>" als tekst
  - S                 -> stuurt 2-byte command 'S' (statusrequest) zoals je kippenluik verwacht
  - T=600             -> stuurt 2-byte command 'T' + 0..255 (mapped) om threshold te zetten op kippenluik
  - HELP              -> toont help
*/

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

// --- Zelfde LoRa instellingen als je kippenluik ---
const long LORA_FREQUENCY = 868E6;
const byte LORA_SYNC_WORD = 0x12;

const int LORA_NSS  = 7;
const int LORA_RST  = 6;
const int LORA_DIO0 = 3;

unsigned long lastPing = 0;
uint32_t pingCounter = 0;

// buffer voor seriële input
String line;

static void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  PING        -> send text ping");
  Serial.println("  S           -> send 2-byte cmd 'S' (status request)");
  Serial.println("  T=600       -> send 2-byte cmd 'T' + value (0..1023 mapped to 0..255)");
  Serial.println("  HELP        -> this help");
  Serial.println();
}

static void loraSendText(const String& payload) {
  LoRa.beginPacket();
  LoRa.print(payload);
  int rc = LoRa.endPacket(); // 1=ok, 0=fout (lib afhankelijk)
  Serial.print("[TX] text bytes=");
  Serial.print(payload.length());
  Serial.print(" endPacket=");
  Serial.println(rc);
}

static void loraSendCmd2(uint8_t cmd, uint8_t val) {
  LoRa.beginPacket();
  LoRa.write(cmd);
  LoRa.write(val);
  int rc = LoRa.endPacket();
  Serial.print("[TX] cmd=");
  Serial.print((char)cmd);
  Serial.print(" val=");
  Serial.print(val);
  Serial.print(" endPacket=");
  Serial.println(rc);
}

static void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line.trim();
      if (line.length() == 0) return;

      if (line.equalsIgnoreCase("HELP")) {
        printHelp();
      } else if (line.equalsIgnoreCase("PING")) {
        pingCounter++;
        String msg = "PING " + String(pingCounter);
        loraSendText(msg);
      } else if (line.equalsIgnoreCase("S")) {
        // kippenluik verwacht 2 bytes: 'S' + val (val wordt genegeerd in jouw code)
        loraSendCmd2('S', 0);
      } else if (line.startsWith("T=") || line.startsWith("t=")) {
        int thr = line.substring(2).toInt();
        thr = constrain(thr, 0, 1023);
        uint8_t v = (uint8_t)map(thr, 0, 1023, 0, 255);
        loraSendCmd2('T', v);
        Serial.print("[INFO] threshold ");
        Serial.print(thr);
        Serial.print(" mapped to raw ");
        Serial.println(v);
      } else {
        Serial.println("Unknown command. Type HELP.");
      }

      line = "";
      return;
    } else {
      // simpele line buffer
      if (line.length() < 80) line += c;
    }
  }
}

static void handleLoRaRx() {
  int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) return;

  // metadata
  int rssi = LoRa.packetRssi();
  float snr = LoRa.packetSnr();

  Serial.print("[RX] size=");
  Serial.print(packetSize);
  Serial.print(" RSSI=");
  Serial.print(rssi);
  Serial.print(" dBm SNR=");
  Serial.print(snr, 1);
  Serial.print(" dB  ");

  // lees payload
  // We proberen:
  // 1) als het JSON lijkt, print als tekst
  // 2) als het 2-byte cmd lijkt, toon cmd/val
  // 3) anders toon hex dump + ascii
  String text;
  text.reserve(packetSize);

  uint8_t b0 = 0, b1 = 0;
  bool have0 = false, have1 = false;

  while (LoRa.available()) {
    uint8_t b = (uint8_t)LoRa.read();
    if (!have0) { b0 = b; have0 = true; }
    else if (!have1) { b1 = b; have1 = true; }
    text += (char)b;
  }

  // heuristic: JSON/status van kippenluik
  if (text.length() > 0 && text[0] == '{') {
    Serial.println("JSON:");
    Serial.println(text);
  }
  // 2-byte command
  else if (packetSize == 2 && have0 && have1 && (b0 == 'S' || b0 == 'T')) {
    Serial.print("CMD: ");
    Serial.print((char)b0);
    Serial.print(" VAL: ");
    Serial.println((int)b1);
  }
  else {
    // dump
    Serial.println("DATA:");
    Serial.print("  ascii: ");
    Serial.println(text);

    Serial.print("  hex : ");
    for (size_t i = 0; i < text.length(); i++) {
      uint8_t v = (uint8_t)text[i];
      if (v < 16) Serial.print('0');
      Serial.print(v, HEX);
      Serial.print(' ');
    }
    Serial.println();
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("LoRa Debug UNO starting...");
  Serial.println("Wiring: NSS=7 RST=6 DIO0=3, SPI 11/12/13. Freq=868MHz Sync=0x12 SF7 BW125 CRC");
  printHelp();

  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa.begin FAILED. Check wiring/power/module type.");
    while (true) delay(1000);
  }

  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.enableCrc();

  // optioneel: meer gevoelig (langzamer) of minder:
  // LoRa.setSpreadingFactor(9); // als je later range wilt pushen, maar moet dan ook op kippenluik aanpassen

  Serial.println("LoRa OK. Listening...");
}

void loop() {
  handleSerial();
  handleLoRaRx();

  // optioneel: automatische ping elke 5s (zet aan indien je wilt)
  /*
  if (millis() - lastPing > 5000) {
    lastPing = millis();
    pingCounter++;
    loraSendText("PING " + String(pingCounter));
  }
  */
}
