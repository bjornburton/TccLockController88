
# TCC Lock Controller (ATtiny88)

# 

## Overview

This firmware implements a torque‑converter clutch (TCC) lock controller
using an ATtiny88 microcontroller.

The controller:

- Measures **vehicle speed** from an ABS square‑wave signal.
- Measures **engine speed** from a digital RPM signal.
- Computes signal frequency using a **300 ms gate interval**.
- Evaluates clutch engagement logic once per gate.
- Initializes in a safe **clutch disengaged** state at power‑up.
- Runs almost entirely interrupt‑driven while the CPU remains in sleep mode.

The system has been tested in a **Jeep Grand Cherokee WJ with a 42RE
automatic transmission**. The ABS signal is used because the transmission
output speed sensor provides an analog sine signal rather than a digital
pulse stream.

---

## Hardware Platform

### Microcontroller

- Device: **ATtiny88**
- Clock: **Internal 16 MHz crystal oscillator**
- `F_CPU = 16000000UL`

Recommended fuse configuration:

| Fuse | Value | Purpose |
|------|-------|---------|
| LFUSE | `0xFF` | External full-swing crystal oscillator selected; no clock division (`CKDIV8` unprogrammed), so the MCU runs at the full 16 MHz crystal frequency; long startup delay selected for stable crystal startup |
| HFUSE | `0xDC` | Brown-out detection enabled at approximately 4.3 V; ISP programming enabled (`SPIEN` programmed); reset pin remains a reset pin (`RSTDISBL` unprogrammed); debugWIRE disabled; EEPROM is erased during chip erase |
| EFUSE | `0xFF` | No bootloader/self-programming (`SELFPRGEN` unprogrammed); reserved bits left at their default `1` values |

---

### Board

UnTested on:

- **MH-Tiny Clone**

---

## Pin Mapping

| Function | ATtiny88 Pin | Description |
|---------|--------------|-------------|
| Throttle Input | PA3 | Internal pull-up |
| Engine Speed Input | PD4 | Engine RPM signal (TTL) |
| Vehical Speed Input | ??? | ABS signal (TTL) |
| Clutch Output | PD0 | TCC control output + LED |

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

#### Atmel‑ICE to xxTrinket Programming Connections

| Atmel‑ICE AVR Port Pin | Mini‑Squid Pin | MH-Tiny Pin Assignment |
|-----------------------|---------------|-----------------------|
| Pin 1 (TCK) | 1 | ISP-3 (SCK) |
| Pin 2 (GND) | 2 | ISP-6 (GND) |
| Pin 3 (TDO) | 3 | ISP-1 (MISO) |
| Pin 4 (VTG) | 4 | ISP-2 (VCC) |
| Pin 6 (nSRST) | 6 | ISP-5 (RST |
| Pin 9 (TDI) | 9 | ISP-4 (MOSI) |

---

## Signal Ratios

The firmware converts frequency measurements to vehicle speed and engine RPM
implicitly using the following ratios:

| Signal | Ratio |
|------|------|
| Engine Frequency | **1/5 Hz per rpm** |

---

## Frequency Measurement

Rising edges on the input pins are counted during a **300 ms gate interval**.

At the end of each gate:

1. Edge counts are latched.
2. Counters are reset.
3. Control logic evaluates clutch state.

This approach provides stable integer‑based frequency measurement with very
low CPU overhead.

---

## Threshold Conversion (300 ms Gate)

Using the signal ratios above:

| Condition | Frequency | Pulses in 300 ms | Firmware Constant |
|----------|-----------|------------------|------------------|
| 950 rpm | 190 Hz | 57 | `ENGINE_MIN_COUNT = 57` |

These values correspond directly to the constants defined in the firmware.

---

## Control Logic

The clutch state is evaluated **once every 300 ms gate** using the following
rule order:


This ordering ensures:

- High‑speed lockup always engages the clutch.
- Engine stall protection disengages the clutch.
- Normal engagement occurs above 15 mph.
- Otherwise the clutch state remains unchanged.

---

## Timer Configuration

Timer0 operates in **CTC (Clear‑Timer‑on‑Compare) mode**.

| Parameter | Value |
|----------|------|
| Prescaler | 1024 |
| OCR0A | 124 |
| Interrupt period | ≈16 ms |

The firmware accumulates these interrupts until **300 ms** has elapsed,
which defines the measurement gate.

---

## Interrupt Architecture

Two interrupts drive the firmware.

### Pin‑Change Interrupt (PCINT)

Triggered by PB4 and PB2.

Responsibilities:

- Detect rising edges on input signals
- Increment frequency counters

### Timer0 Compare Interrupt

Triggered approximately every 16 ms.

Responsibilities:

- Maintain the 300 ms measurement gate
- Snapshot frequency counters
- Execute clutch control logic

---

## Power Behavior

- Clutch output initializes **LOW** at power‑up.
- Clutch remains disengaged until the first valid decision cycle completes.

---

## Execution Model

The firmware is entirely interrupt‑driven.

The main loop simply sleeps:

```
while(1)
{
    sleep_mode();
}
```

The CPU wakes only for interrupts, minimizing power consumption and
reducing timing jitter.

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
avrdude -p t88 -c atmelice_isp -P usb -U flash:w:tcclc.hex:i
```

Program fuses:

```bash
avrdude -p t88 -c atmelice_isp -P usb \
-U lfuse:w:0xFF:m \
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

This project implements a compact and reliable torque converter clutch
controller using engine RPM signal. The firmware
prioritizes deterministic behavior, simplicity, and minimal resource
usage while remaining well suited for embedded automotive applications.

