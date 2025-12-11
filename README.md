# LoRa Chicken Door – Arduino + ESP32 WROOM32D + Home Assistant

Dit project bestaat uit twee onderdelen:

1. **Arduino UNO + RFM95W (LoRa-zender/ontvanger)**
2. **ESP32 WROOM32D + RFM95W (LoRa-brug naar Home Assistant via ESPHome)**

Samen vormen ze een draadloze, betrouwbare oplossing om een kippenluik automatisch te openen/sluiten op basis van lichtsterkte, met statusrapportering, remote instellingen en diagnostiek.

---

## 📡 Overzicht van de werking

### Arduino-zijde
- Stuurt een motor die het kippenluik opent/sluit.
- Leest sensoren:
  - LDR lichtsensor  
  - Bovenste en onderste reedcontacten  
- Bepaalt automatisch:
  - **OPEN / CLOSED / MOVING / ERROR**  
- Verzendt periodiek een **JSON-statuspakket** via LoRa:

```json
{
  "light": 512,
  "threshold": 600,
  "door": "OPEN",
  "reedTop": 0,
  "reedBottom": 1
}

Verwerkt inkomende LoRa-commando’s:

'T' + value → stel nieuwe lichtdrempel in

'S' → zend status terug

🛰 ESP32 LoRa Bridge (ESPHome)

De ESP32 ontvangt LoRa-berichten van de Arduino en publiceert die als entiteiten in Home Assistant:

Lichtwaarde

Drempelwaarde

LoRa RSSI en SNR

Reed-statussen

Luikstatus

Timestamps van RX/TX

Via Home Assistant kun je:

De lichtdrempel instellen (0–255, LoRa-schaal)

Handmatig een statusrequest naar de Arduino sturen

De ESP32 gebruikt een aangepaste sx127x driver via:

external_components:
  - source: github://pr#7490
    components: [ sx127x ]

🔧 Hardwarepinnen
Arduino UNO → RFM95W
Signaal	Pin
NSS	7
RST	6
DIO0	3
MOSI	11
MISO	12
SCK	13
ESP32 WROOM32D → RFM95W
Signaal	GPIO
NSS	5
RST	14
DIO0	26
MOSI	23
MISO	19
SCK	18
🧱 Dependencies
Arduino

LoRa library

EEPROM (standaard)

SPI (standaard)

ESPHome

ESPHome 2025.10.0 of recenter

Externe sx127x component via PR

Home Assistant (optioneel maar aanbevolen)

🧪 Testen

Start de Arduino → zou LoRa gereed melden.

Start de ESP32 → ESPHome console moet RX-pakketten tonen.

In Home Assistant verschijnen automatisch:

lichtsensor

drempel

reed-statussen

luikstatus

Verander drempel → ESP32 stuurt 'T'-pakket → Arduino past waarde aan.

📁 Bestanden

/arduino/kippenluik.ino

/esp32/esp32-wroom-chicken-rx-tx.yaml

/docs/README.md (dit document)

📜 Licentie

Vrij te gebruiken en aan te passen voor niet-commerciële projecten.

💬 Contact / Issues

Open een GitHub issue bij fouten, suggesties of vragen.
