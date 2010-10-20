#include "config.h"

#define F_CPU       8000000UL
#define F_TIMER1    (F_CPU/8)

// Convert micro seconds to timer ticks
// For 8Mhz and 1:8 timer prescaler this will be 1 to 1,000,000
#define USEC(usec)  (F_TIMER1/1000000UL*(usec))

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>
#include <util/delay.h>

// FUSEs
FUSES =
{
    .low = FUSE_CKSEL0 & FUSE_CKSEL2 & FUSE_CKSEL3 & FUSE_SUT0,
    .high = HFUSE_DEFAULT,
    .extended = EFUSE_DEFAULT,
};

// Inputs
#define VIN         PORTA3  // Voltage input

#define THROTTLE    PORTB0
#define GYRO        PORTB1

// Outputs
#ifndef RUD_INVERT
    #define ESC2        PORTA5 // OC1B
    #define ESC1        PORTA6 // OC1A

    #define ESC2VAL     OCR1B
    #define ESC1VAL     OCR1A
#else
    #define ESC1        PORTA5 // OC1B
    #define ESC2        PORTA6 // OC1A

    #define ESC1VAL     OCR1B
    #define ESC2VAL     OCR1A
#endif
#define RED         PORTA7

#define GREEN   PORTB2

// LED control
#define RED_ON      PORTA &= ~(1<<RED)
#define RED_OFF     PORTA |=  (1<<RED)

#define GREEN_ON    PORTB &= ~(1<<GREEN)
#define GREEN_OFF   PORTB |=  (1<<GREEN)

// Uncalibrated periods
#define TOLERANCE   USEC(300)
#define MIN         USEC(1000) - TOLERANCE
#define MAX         USEC(2000) + TOLERANCE
#define MID         USEC(1500)
#define PERIOD      USEC(20000)

// Current throttle and gyro values
// Note that pin change interrupt does not sanitize values, so they could be out of range.
volatile uint16_t g_throttle = 0;
volatile uint16_t g_gyro = 0;

#ifdef FO_ENABLED
volatile uint8_t g_wait = 0;
#endif

// range
typedef struct range_t {
    uint16_t min;
    uint16_t max;
} range_t;

static inline int in_range(int16_t value, range_t range) {
    return value >= range.min && value <= range.max;
}

// ranges for two channels
typedef struct calibration_t {
    range_t throttle;
    range_t gyro;
} calibration_t;

EEMEM calibration_t e_calibration = { 
    .throttle = { .min = MID, .max = MID }, 
    .gyro     = { .min = MID, .max = MID } 
};

// calibrated values replicated to RAM
calibration_t g_calibration = { 
    .throttle = { .min = MIN, .max = MAX }, 
    .gyro     = { .min = MIN, .max = MAX } 
};

static void setup_io();
static void setup_timer();
static void wait_input();
static void calibrate();
static void process_input();

int main(void) {
    wdt_disable();
    
    setup_io();
    setup_timer();

    // initial status
    GREEN_OFF;
    RED_OFF;
    
    sei();

    wait_input();

    if (g_throttle > MID) {
        calibrate();
    } else {
        // load calibrated values
        eeprom_read_block(&g_calibration, &e_calibration, sizeof(e_calibration));

#ifdef FO_ENABLED
        // ready to go, enable watchdog for throttle values
        wdt_enable(FO_PERIOD);
        WDTCSR |= _BV(WDIE);
#endif

        while(1)
            process_input();
    }
    return 0;
}

// Capture input pin changes
ISR(PCINT1_vect, ISR_BLOCK) {
    static uint8_t last;            // last pins state
    static uint16_t throttle_start; // throttle signal started at
    static uint16_t gyro_start;     // gyro signal started at
   
    uint8_t current = PINB;
    uint16_t timer = TCNT1;

    // if THROTTLE pin changed since last interrupt
    if ((last ^ current) & _BV(THROTTLE)) {
        if (current & _BV(THROTTLE)) {
            // start pulse
            throttle_start = timer;
        } else {
            // end pulse
            int16_t throttle = timer - throttle_start;

            // check for timer overflow
            if (throttle < 0)
                throttle += PERIOD;

#ifdef FO_ENABLED
            // if throttle value looks OK, reset watchdog
            if (in_range(throttle, g_calibration.throttle))
                wdt_reset();
#endif

            g_throttle = (uint16_t) throttle;
        }
    }
    // if GYRO pin changed since last interrupt
    if ((last ^ current) & _BV(GYRO)) {
        if (current & _BV(GYRO)) {
            // start pulse
            gyro_start = timer;
        } else {
            // end pulse
            int16_t gyro = timer - gyro_start;

            // check for timer overflow
            if (gyro < 0)
                gyro += PERIOD;

            g_gyro = (uint16_t) gyro;
        }
    }
    last = current;
}

#ifdef FO_ENABLED
// Signal watchdog handler
ISR(WDT_vect, ISR_BLOCK) {
    GREEN_OFF;

    // Skip few periods after signal was restored
    g_wait = FO_WAIT;

    // re-enable interrupt on watchdog timeout, which is disabled automatically
    WDTCSR |= _BV(WDIE);
}
    
// Skip few periods after signal was restored
static void fo_skip_frames() {
    while (g_wait > 0) {
        g_wait--;

        ESC1VAL = g_calibration.throttle.min;
        ESC2VAL = g_calibration.throttle.min;
        _delay_ms(20);
    }
}
#else
static void fo_skip_frames() { }
#endif

// Setup input/output ports
static void setup_io() {
    // Setup port B
    DDRB =   _BV(GREEN);
    PORTB = ~_BV(GREEN);

    // Setup port A
    DDRA =              _BV(ESC1) | _BV(ESC2) | _BV(RED);
    PORTA = (uint8_t) ~(_BV(ESC1) | _BV(ESC2) | _BV(RED));

    // enable interrupt on THROTTLE/GYRO change
    PCMSK1 = _BV(PCINT8) | _BV(PCINT9); 
    GIMSK =  _BV(PCIE1);
}

// Setup 16-bit timer for PWM on two channels with frequency 50Hz
static void setup_timer() {
    // Setup Timer1
    ICR1 = PERIOD; // TOP

    // Both engines are off
    ESC1VAL = MIN;
    ESC2VAL = MIN;

    // COM1A1:0 is 10 (clear OC1A on match, set at BOTTOM)
    // COM1B1:0 is 10 (clear OC1B on match, set at BOTTOM)
    // WGM13:10 is 1110 (Fast PWM, TOP at ICR1)
    // CS12:10 is 010 (clkIO / 8 == 1Mhz)
    TCCR1A = _BV(COM1A1) | _BV(COM1B1) | _BV(WGM11);
    TCCR1B = _BV(WGM13)  | _BV(WGM12)  | _BV(CS11); 
}

// Wait for valid throttle and gyro input
static void wait_input() {
    while (1) {
        cli();
        if (g_throttle >= MIN && g_throttle <= MAX && g_gyro >= MIN && g_gyro <= MAX)
            break;
        sei();

        _delay_ms(1);
    }
    sei();
}

// Calibrate min/max values for throttle/gyro
static void calibrate() {
    RED_ON;
    GREEN_ON;

    calibration_t calib = { .throttle = { .min = MID, .max = MID }, 
                            .gyro     = { .min = MID, .max = MID } };

    for (uint8_t i = 0; i < CALIBRATE_PERIODS; ++i) {
        cli();
        uint16_t t = g_throttle;
        uint16_t g = g_gyro;
        sei();

        if (t < calib.throttle.min)
            calib.throttle.min = t;
        else if (t > calib.throttle.max)
            calib.throttle.max = t;

        if (g < calib.gyro.min)
            calib.gyro.min = g;
        else if (g > calib.gyro.max)
            calib.gyro.max = g;

        _delay_ms(20);
    }

    eeprom_write_block(&calib, &e_calibration, sizeof(e_calibration));

    RED_OFF;
    GREEN_OFF;
}

static void process_input() {
    cli();
    // Make local copies
    uint16_t throttle = g_throttle;
    uint16_t gyro = g_gyro;
    sei();

    // failover
    fo_skip_frames();

    if (!in_range(throttle, g_calibration.throttle) || !in_range(gyro, g_calibration.gyro)) {
        return;
    }

    // Scale gyro
    gyro = (uint16_t)(gyro - MID) >> 1;
        
    uint16_t left = throttle + gyro;
    uint16_t right = throttle - gyro;

    const uint16_t tmin = g_calibration.throttle.min;
    if (left < tmin) {
        // add RPM to left, remove same amount from right
        right -= (tmin - left);
        left = tmin;
    } else if (right < tmin) {
        // add RPM to right, remove same amount from left
        left -= (tmin - right);
        right = tmin;
    }

    // If in range, pass to the engines otherwise, use previous values
    if (in_range(left, g_calibration.throttle) && in_range(right, g_calibration.throttle)) {
        GREEN_ON;

        ESC1VAL = left;
        ESC2VAL = right;
    }
}
