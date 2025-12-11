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
