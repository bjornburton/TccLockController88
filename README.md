
# TCC Lock Controller (ATtiny88)

# 

## Overview

This firmware implements a torque‑converter clutch (TCC) lock controller
using an ATtiny88 microcontroller.

The controller:

- Measures **vehicle speed** using Timer1 input capture (period measurement) from ABS.
- Measures **engine speed** by counting pulses over a gated interval.
- Reads a **digital throttle idle switch** with internal pull-up.
- Evaluates control logic every **300 ms**.
- Initializes in a safe **clutch disengaged** state at power-up.
- Runs primarily in interrupt-driven sleep mode.

---

## Hardware Platform

### Microcontroller

- Device: **ATtiny88**
- Clock: **Internal 16 MHz crystal oscillator**
- `F_CPU = 16000000UL`

Recommended fuse configuration:

| Fuse | Value | Purpose |
|------|-------|---------|
| LFUSE | `0xC0` | External full-swing crystal oscillator selected; no clock division (`CKDIV8` unprogrammed), so the MCU runs at the full 16 MHz crystal frequency; no startup delay selected for stable crystal startup |
| HFUSE | `0xDC` | Brown-out detection enabled at approximately 4.3 V; ISP programming enabled (`SPIEN` programmed); reset pin remains a reset pin (`RSTDISBL` unprogrammed); debugWIRE disabled; EEPROM is erased during chip erase |
| EFUSE | `0xFF` | No bootloader/self-programming (`SELFPRGEN` unprogrammed); reserved bits left at their default `1` values |

---

### Board

- **MH-Tiny Clone**

---

## Pin Mapping

| Function | ATtiny88 Pin | MH-Tiny Pin | Description |
|---------|--------------|-------------|-------------|
| Throttle Input | PA3 | 16 | Internal pull-up |
| Engine Speed Input | PD4 | 4 | Engine RPM signal (TTL) |
| Vehicle Speed Input | PB0 | 8 | ABS signal via ICP1 |
| Clutch Output | PD0 | 0 | TCC control output + LED |


| PCB Label | ATtiny88 | Notes |
|-----------|----------|-------|
| 0 | PD0 | On-board LED |
| 1 | PD1 | USB D+ |
| 2 | PD2 | USB D- |
| 3 | PD3 | GPIO |
| 4 | PD4 | GPIO |
| 5 | PD5 | GPIO |
| 6 | PD6 | GPIO |
| 7 | PD7 | GPIO |
| 8 | PB0 | GPIO |
| 9 | PB1 | GPIO |
| 10 | PB2 | GPIO |
| 11 | PB3 | ISP MOSI |
| 12 | PB4 | ISP MISO |
| 13 | PB5 | ISP SCK |
| 14 | PB7 | GPIO |
| 15 | PA2 | GPIO |
| 16 | PA3 | GPIO |
| A0 | PC0 | Analog/GPIO |
| A1 | PC1 | Analog/GPIO |
| A2 | PC2 | Analog/GPIO |
| A3 | PC3 | Analog/GPIO |
| A4 | PC4 | Analog/GPIO |
| A5 | PC5 | Analog/GPIO |
| A6 | PA0 | Analog/GPIO |
| A7 | PA1 | Analog/GPIO |
| 25 | PC7 | GPIO |
| RST | PC6 | Reset |
| — | PB6 | 16 MHz oscillator |


---

#### Atmel‑ICE to MH-Tiny Programming Connections

| Atmel‑ICE AVR Port Pin | Mini‑Squid Pin | MH-Tiny Pin Assignment |
|-----------------------|---------------|-----------------------|
| Pin 1 (TCK) | 1 | ISP-3 / 13 (SCK) |
| Pin 2 (GND) | 2 | ISP-6 / GND (GND) |
| Pin 3 (TDO) | 3 | ISP-1 / 12 (MISO) |
| Pin 4 (VTG) | 4 | ISP-2 / 5V (VCC) |
| Pin 6 (nSRST) | 6 | ISP-5 / RST (RST) |
| Pin 9 (TDI) | 9 | ISP-4 / 11 (MOSI) |

---

## Signal Ratios

The firmware converts frequency measurements to vehicle speed and engine RPM
implicitly using the following ratios:


| Signal | Ratio |
|------|------|
| ABS Frequency | **2.2 Hz per mph** |
| Engine Frequency | **1/5 Hz per rpm** |

---

## Measurement Methods

### Vehicle Speed

- Measured using **Timer1 input capture**
- Uses **period measurement**, not counting
- Higher resolution and faster response than gated counting

### Engine Speed

- Counted over a **300 ms gate interval**
- Uses pin-change interrupt on PD4

---

## Control Logic

Evaluated every **300 ms** in this exact order:

```
IF vehicle_speed > 27 mph:
    clutch = ENGAGED

ELSE IF engine_speed < 550 rpm:
    clutch = DISENGAGED

ELSE IF throttle == ACTIVE AND engine_speed < 735 rpm:
    clutch = DISENGAGED

ELSE IF vehicle_speed > 9 mph:
    IF throttle == ACTIVE:
        clutch = ENGAGED

ELSE:
    clutch = HOLD PREVIOUS STATE
```

### Behavioral Summary

- **High-speed override (>27 mph)** forces clutch engaged
- **Engine stall protection (<550 rpm)** forces disengage
- **Low RPM + throttle condition (<735 rpm)** prevents lugging
- **Normal engagement (>9 mph + throttle)**
- Otherwise, state is maintained

---
## Interrupts

### TIMER1_CAPT_vect
- Captures vehicle speed period

### PCINT2_vect
- Counts engine pulses

### TIMER0_COMPA_vect
- Executes control logic

---

## Notes

- Internal pull-up is enabled on throttle input (PA3)
- Vehicle speed uses **period measurement**, so loss-of-signal is detected via timeout
- First capture is discarded to ensure valid period measurement

---

## Build

Example compilation:

```bash
avr-gcc -mmcu=attiny88 -std=c99 -DF_CPU=16000000UL -Os -o tcclc.elf tcclc.c
avr-objcopy -O ihex tcclc.elf tcclc.hex
```

---

## Programming (Atmel‑ICE)

Flash firmware:

```bash
avrdude -p t88 -c atmelice_isp -P usb -B 100 -U flash:w:tcclc.hex:i
```

Program fuses:

```bash
avrdude -p t88 -c atmelice_isp -P usb -B 100 \
-U lfuse:w:0xC0:m \
-U hfuse:w:0xDC:m \
-U efuse:w:0xFF:m
```

---

## Design Characteristics

### Advantages

- Deterministic timing
- Interrupt‑driven architecture
- Integer arithmetic only
- Low CPU load
- Robust startup behavior
- Very small flash footprint

### Limitations

- Frequency resolution limited by 300 ms gate
- Threshold accuracy depends on RC oscillator tolerance
- Speed thresholds quantized to integer pulse counts

---

## Summary

This firmware implements a deterministic, low-overhead TCC controller using:

- Timer1 period measurement for vehicle speed
- Gated counting for engine speed
- Ordered rule-based control logic

The design prioritizes reliability, simplicity, and precise timing.
                    
