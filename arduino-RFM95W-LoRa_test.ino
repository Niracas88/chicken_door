#include <SPI.h>
#include <LoRa.h>

const long LORA_FREQUENCY = 868E6;   // 868 MHz (EU)
const byte LORA_SYNC_WORD = 0x12;    // standaard private sync word arduino-LoRa

// pinnen volgens arduino-LoRa README:
const int PIN_NSS   = 10;
const int PIN_RST   = 9;
const int PIN_DIO0  = 2;

int counter = 0;

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // wacht tot seriële monitor klaar is (voor sommige boards)
  }

  Serial.println("LoRa Sender init");

  // vertel de LoRa-library welke pins je gebruikt
  LoRa.setPins(PIN_NSS, PIN_RST, PIN_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa init failed. Check wiring.");
    while (true) {
      delay(1000);
    }
  }

  // zorg dat sync word overeenkomt met ESPHome-config
  LoRa.setSyncWord(LORA_SYNC_WORD);

  // we laten de standaardwaarden staan:
  // SF7, BW 125 kHz, CR 4/5, preamble 8, CRC uit
  Serial.println("LoRa Sender ready.");
}

void loop() {
  Serial.print("Sending packet: ");
  Serial.println(counter);

  LoRa.beginPacket();
  LoRa.print("HELLO ");
  LoRa.print(counter);
  LoRa.endPacket();  // blokkerend tot verzending klaar is

  counter++;
  delay(1000);
}
