# Automatisch kippenluik – Arduino


<img width="1254" height="1254" alt="Arduino kippenluik schakeling" src="https://github.com/user-attachments/assets/5c53d97e-90c2-4f23-bb8c-0cff1f940b5f" />


Dit programma bestuurt een automatisch kippenluik op basis van de hoeveelheid omgevingslicht, gemeten met een LDR (lichtsensor).

## Werking

- **Automatisch openen en sluiten:** wanneer de gemeten lichtwaarde boven de ingestelde drempel komt, opent het luik. Bij een waarde onder of gelijk aan de drempel sluit het.
- **Eindpositiedetectie:** twee reedcontacten detecteren wanneer het luik volledig geopend of gesloten is. De motor stopt automatisch bij het bereiken van de eindpositie.
- **Homing:** bij het opstarten controleert de Arduino de positie van het luik. Is deze onbekend, dan beweegt het luik naar de onderste eindpositie.
- **Motorbeveiliging:** de motor stopt wanneer de ingestelde maximale bewegingstijd wordt overschreden.
- **Automatische kalibratie:** het programma meet de benodigde openings- en sluitingstijd en bewaart deze met 10% veiligheidsmarge.
- **EEPROM:** de ingestelde lichtdrempel en maximale bewegingstijden blijven bewaard na een stroomonderbreking.
- **Seriële monitoring:** elke 5 seconden worden de lichtwaarde, ingestelde drempel, reedcontacten en luikstatus weergegeven.

## Seriële commando's

Via de Serial Monitor (9600 baud):

| Commando | Functie |
|----------|---------|
| `T=500` | Stelt de lichtdrempel in (0–1023). |
| `S?` | Toont de actuele status. |
| `CAL` | Start de automatische kalibratie. |

## Hardware

- Arduino Uno
- H-brug motordriver (ENA: 10, IN1: 9, IN2: 8)
- Twee reedcontacten (boven: 2, onder: 4)
- LDR-lichtsensor (A0)
- DC-motor voor het openen en sluiten
