/*
 * tcclc_attiny88_adjusted.c
 *
 * Target: ATtiny88 @ 16 MHz external crystal
 *
 * FEATURES
 * --------
 * - Vehicle speed via Timer1 Input Capture (PB0 / ICP1)
 * - Engine speed via PD4 (PCINT)
 * - Throttle switch via PA3
 * - Clutch output via PD0
 *
 * - All thresholds defined in REAL UNITS (mph, rpm)
 * - Compile-time conversion to integer constants (no runtime math)
 * - Adjustable gate time (GATE_MS)
 *
 * Corrective changes from the prior version:
 * - Timer0 now uses the derived OCR0A_VALUE, so gate timing matches the
 *   configured F_CPU, prescaler, and T0_TICK_MS.
 * - Added ABS input timeout so stale vehicle-speed data is cleared if the
 *   capture signal disappears.
 * - First Timer1 capture after startup or timeout is ignored so the period
 *   value is always based on a true edge-to-edge interval.
 * - Added explicit throttle input polarity / pull-up configuration.

   Copyright 2026 Bjorn Burton

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <stdint.h>

/* ============================================================
   ====================== TUNING BLOCK =========================
   ============================================================ */

/* -------- Gate timing -------- */
#define GATE_MS            300UL   /* user adjustable gate time */
#define T0_TICK_MS          10UL   /* base scheduler tick */
#define TIMER0_PRESCALE   1024UL

/* Derived */
#define GATE_TICKS         (GATE_MS / T0_TICK_MS)

/*
 * Timer0 compare value:
 *
 *   OCR0A = F_CPU / (prescale * tick_rate) - 1
 *
 *   tick_rate = 1000 / T0_TICK_MS
 */
#define OCR0A_VALUE \
((F_CPU / (TIMER0_PRESCALE * (1000UL / T0_TICK_MS))) - 1UL)

/* -------- Compile-time safety checks -------- */

#if (GATE_MS % T0_TICK_MS) != 0
#error "GATE_MS must be an integer multiple of T0_TICK_MS"
#endif

#if OCR0A_VALUE > 255
#error "OCR0A exceeds 8-bit range — adjust tick or prescaler"
#endif

/* -------- Signal scaling -------- */

/*
 * ABS: 2.2 Hz per mph -> 22/10
 */
#define ABS_HZ_PER_MPH_NUM 22UL
#define ABS_HZ_PER_MPH_DEN 10UL

/*
 * Engine: 1/5 Hz per rpm
 */
#define ENGINE_HZ_PER_RPM_DEN 5UL

/*
 * Timer1 period measurement:
 *
 * period_counts = F_CPU / (TIMER1_PRESCALE * frequency)
 *
 * frequency = mph * (22/10)
 */
#define TIMER1_PRESCALE 64UL

#define MPH_TO_PERIOD(mph) \
((uint16_t)((F_CPU * ABS_HZ_PER_MPH_DEN) / \
(TIMER1_PRESCALE * ABS_HZ_PER_MPH_NUM * (mph))))

/*
 * rpm -> counts within gate
 *
 * count = rpm/5 * (GATE_MS/1000)
 */
#define RPM_TO_COUNT(rpm) \
((uint16_t)(((rpm) * GATE_MS) / (1000UL * ENGINE_HZ_PER_RPM_DEN)))

/* -------- USER THRESHOLDS (in real units) -------- */

#define PERIOD_FORCE_ENGAGE   MPH_TO_PERIOD(27UL)  /* > 27 mph */
#define PERIOD_ENGAGE         MPH_TO_PERIOD(9UL)   /* > 9 mph */
#define PERIOD_MAX_ENGAGE    MPH_TO_PERIOD(135UL)  /* < 135 mph */

#define ENGINE_MIN_COUNT      RPM_TO_COUNT(550UL)  /* < 550 rpm */
#define ENGINE_LOW_COUNT      RPM_TO_COUNT(735UL)  /* < 735 rpm */

/* -------- ABS timeout handling --------
 *
 * If no valid Timer1 captures are seen for this many complete gates,
 * the vehicle-speed period is declared stale and forced invalid.
 *
 * One gate is sufficient for this application because all meaningful
 * engage thresholds are well above any period that would approach 300 ms.
 */
#define ABS_TIMEOUT_GATES     1u

/* -------- Throttle input behavior --------
 *
 * THROTTLE_PULLUP_ENABLE:
 *   0 = external bias provided by hardware
 *   1 = enable internal pull-up
 *
 * THROTTLE_ACTIVE_HIGH:
 *   1 = switch asserted when PA3 reads high
 *   0 = switch asserted when PA3 reads low
 */
#define THROTTLE_PULLUP_ENABLE 1u
#define THROTTLE_ACTIVE_HIGH   1u

/* ============================================================
   ====================== PIN MAPPING ==========================
   ============================================================ */

#define CLUTCH_PIN     PD0
#define ENGINE_PIN     PD4
#define THROTTLE_PIN   PA3

/* ============================================================
   ====================== STATE ================================
   ============================================================ */

static volatile uint16_t last_capture = 0u;
static volatile uint16_t period_counts = 0u;
static volatile uint8_t  capture_armed = 0u;
static volatile uint8_t  period_valid = 0u;
static volatile uint8_t  abs_stale_gates = ABS_TIMEOUT_GATES;

static volatile uint16_t engine_count = 0u;
static volatile uint8_t  last_engine = 0u;

static volatile uint8_t gate_ticks = 0u;

/* ============================================================
   ====================== ISR: INPUT CAPTURE ===================
   ============================================================ */

ISR(TIMER1_CAPT_vect)
{
    uint16_t now = ICR1;

    /*
     * Ignore the first edge after startup or after a timeout.
     * The next edge then produces a real edge-to-edge period.
     */
    if (capture_armed == 0u)
    {
        last_capture = now;
        capture_armed = 1u;
        period_valid = 0u;
    }
    else
    {
        period_counts = (uint16_t)(now - last_capture);
        last_capture = now;
        period_valid = 1u;
    }

    abs_stale_gates = 0u;
}

/* ============================================================
   ====================== ISR: ENGINE COUNT ====================
   ============================================================ */

ISR(PCINT2_vect)
{
    uint8_t pins = PIND;
    uint8_t e = (uint8_t)((pins >> ENGINE_PIN) & 1u);

    if ((last_engine == 0u) && (e != 0u))
    {
        engine_count++;
    }

    last_engine = e;
}

/* ============================================================
   ====================== ISR: TIMER0 ==========================
   ============================================================ */

ISR(TIMER0_COMPA_vect)
{
    wdt_reset(); // pet the dog
    gate_ticks++;

    if (gate_ticks < GATE_TICKS)
    {
        return;
    }

    /* ----- Gate interval complete ----- */
    gate_ticks = 0u;

    uint16_t eng = engine_count;
    engine_count = 0u;

    /*
     * If the ABS capture has gone stale, clear period validity and re-arm
     * capture synchronization so the next valid period uses two fresh edges.
     */
    if (abs_stale_gates < 255u)
    {
        abs_stale_gates++;
    }

    if (abs_stale_gates >= ABS_TIMEOUT_GATES)
    {
        period_valid = 0u;
        capture_armed = 0u;
    }

    uint16_t period = period_counts;

    uint8_t throttle_raw = (uint8_t)((PINA >> THROTTLE_PIN) & 1u);
    uint8_t throttle = THROTTLE_ACTIVE_HIGH ? throttle_raw : (uint8_t)(!throttle_raw);

    /* ========================================================
       ================= CONTROL LOGIC =========================
       ======================================================== */

    /* IF vehicle_speed > 27 mph && vehicle_speed < 135 mph */
    if ((period_valid != 0u) &&
        (period <= PERIOD_FORCE_ENGAGE) &&
        (period >= PERIOD_MAX_ENGAGE))
    {
        PORTD |= (uint8_t)(1u << CLUTCH_PIN);
    }
    /* ELSE IF engine_speed < 550 rpm */
    else if (eng < ENGINE_MIN_COUNT)
    {
        PORTD &= (uint8_t)~(1u << CLUTCH_PIN);
    }
    /* ELSE IF throttle && engine_speed < 735 rpm */
    else if ((throttle != 0u) && (eng < ENGINE_LOW_COUNT))
    {
        PORTD &= (uint8_t)~(1u << CLUTCH_PIN);
    }
    /* ELSE IF vehicle_speed > 9 mph */
    else if ((period_valid != 0u) && (period <= PERIOD_ENGAGE))
    {
        if (throttle != 0u)
        {
            PORTD |= (uint8_t)(1u << CLUTCH_PIN);
        }
    }
    else
    {
        /* Maintain previous state */
    }
}

/* ============================================================
   ====================== INITIALIZATION =======================
   ============================================================ */

static void io_init(void)
{
    /* Output */
    DDRD |= (uint8_t)(1u << CLUTCH_PIN);
    PORTD &= (uint8_t)~(1u << CLUTCH_PIN);

    /* Engine input */
    DDRD &= (uint8_t)~(1u << ENGINE_PIN);

    /* Throttle input */
    DDRA &= (uint8_t)~(1u << THROTTLE_PIN);
#if THROTTLE_PULLUP_ENABLE
    PORTA |= (uint8_t)(1u << THROTTLE_PIN);
#else
    PORTA &= (uint8_t)~(1u << THROTTLE_PIN);
#endif

    last_engine = (uint8_t)((PIND >> ENGINE_PIN) & 1u);

    /* Enable PCINT on PD4 */
    PCMSK2 |= (uint8_t)(1u << PCINT20);
    PCICR  |= (uint8_t)(1u << PCIE2);
}

static void timer1_init(void)
{
    /*
     * Timer1:
     * - Input capture on ICP1 (PB0)
     * - Prescaler = 64
     * - Rising-edge trigger
     */
    TCCR1A = 0u;
    TCCR1B = (uint8_t)((1u << ICES1) | (1u << CS11) | (1u << CS10));
    TIMSK1 = (uint8_t)(1u << ICIE1);

    last_capture = 0u;
    period_counts = 0u;
    capture_armed = 0u;
    period_valid = 0u;
    abs_stale_gates = ABS_TIMEOUT_GATES;
}

static void timer0_init(void)
{
    /*
     * Timer0:
     * - CTC mode
     * - OCR0A derived from F_CPU, prescaler, and T0_TICK_MS
     * - Compare interrupt every T0_TICK_MS
     *
     * For this ATtiny88 header set:
     *   - CTC mode is selected with CTC0 in TCCR0A
     *   - clock select bits CS02:0 are also in TCCR0A
     */
    TCCR0A = (uint8_t)(1u << CTC0);

    OCR0A = (uint8_t)OCR0A_VALUE;

    TIMSK0 |= (uint8_t)(1u << OCIE0A);

    TCCR0A |= (uint8_t)((1u << CS02) | (1u << CS00));
}

/* ============================================================
   ====================== MAIN ================================
   ============================================================ */

int main(void)
{
    io_init();
    timer1_init();
    timer0_init();

    wdt_disable();
    wdt_enable(WDTO_2S);
    wdt_reset();

    set_sleep_mode(SLEEP_MODE_IDLE);
    sei();

    for (;;)
    {
        sleep_mode();
    }
}
