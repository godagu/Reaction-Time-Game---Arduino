# Reaction Time Game (Arduino)

**Author:** Goda Gutparakyte  
**Course:** Intro to Robotics @ Vilnius University  
**Assignment:** HW2 — Reaction Time Game  
**Last Updated:** 2025-10-06  

---

## Overview
This project implements a **reaction time tester** using an Arduino Uno, two buttons, LEDs, a buzzer, and an LCD display.  

- Press the main button to start.  
- Wait for the green LED to turn on — then press as fast as possible.
- The display shows your reaction time in milliseconds.  
- Your best results are saved automatically in EEPROM.  
- Press the menu button to view the **Top scores**.

---

## Picture of the circuit
![Circuit picture](circuit.jpeg)

---

## Build Steps
1. **Connect all components** according to the wiring table below.  
2. Open the `.ino` file in **Arduino IDE**.  
3. Select **Board:** Arduino Uno.  
4. Select the correct **COM port**.  
5. Click **Upload**.  
---

## Timer Configuration
The system uses **Timer1** (16-bit hardware timer) to generate a **1 ms interrupt** used for:
- Measuring time precisely.
- Implementing reaction time measurement.
- Managing non-blocking delays for display and sound.

**Timer settings:**
- **CPU Clock:** 16 MHz  
- **Prescaler:** 64  
- **OCR1A:** 249 → produces 1 ms tick rate  
- **Mode:** CTC (Clear Timer on Compare Match)  
- **ISR:** `ISR(TIMER1_COMPA_vect)` increments `ms_ticks`  

---

## Interrupt Service Routines (ISR Roles)

| ISR | Trigger | Pin | Description |
|------|----------|------|-------------|
| `button_ISR()` | CHANGE | 2 | Detects presses of the **main play button**, handles reaction timing and debouncing. |
| `menu_ISR()` | FALLING | 3 | Detects **menu button** press to show the scoreboard. |
| `TIMER1_COMPA_vect` | 1 ms timer | — | Updates global millisecond counter (`ms_ticks`) for all timing logic. |

---

##  EEPROM Layout
Top scores are stored persistently in EEPROM to survive power cycles.  

| Address | Size (bytes) | Description |
|----------|---------------|-------------|
| `0–1` | 2 | **Magic number** (`0x5254`, marks valid save) |
| `2` | 1 | **EEPROM version** |
| `3` | 1 | **Score count** (number of valid scores) |
| `4–...` | ... | **Top scores list** (n scores × 2 bytes each) |

**Constants:**
- `EMPTY_SCORE = 0xFFFF` — unused score slot  
- `MAX_SCORES = 10` — top 10 fastest reaction times  (can be adjusted) 

---
## Component list
| Name | Quantity | Component |
|------|-----------|------------|
| U1 | 1 | Arduino Uno R3 |
| D1 | 1 | Red LED |
| D2 | 1 | Green LED |
| R1, R2 | 2 | 220 Ω Resistor |
| S1, S2 | 2 | Pushbutton |
| R3, R4 | 2 | 1 kΩ Resistor |
| U2 | 1 | LCD 16x2 Display |
| PIEZO1 | 1 | Piezo Buzzer |

##  Wiring

![Wiring schematic](wiring_schematic.pdf)



 
