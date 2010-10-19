#include "config.h"

#define F_CPU       8000000UL
#define F_TIMER1    (F_CPU/8)

// Convert micro seconds to timer ticks
// For 8Mhz and 1:8 timer prescaler this will be 1 to 1,000,000
#define USEC(usec)  (F_TIMER1/1000000UL*(usec))

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
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

// Standard periods
#define TOLERANCE   USEC(300)
#define MIN         USEC(1000) - TOLERANCE
#define MAX         USEC(2000) + TOLERANCE
#define MID         USEC(1500)
#define PERIOD      USEC(20000)

// Current throttle and gyro values
// Note that pin change interrupt does not sanitize values, so they could be out of range.
volatile int16_t g_throttle = 0;
volatile int16_t g_gyro = 0;

#ifdef FO_ENABLED
volatile uint8_t g_wait = 0;
#endif

int main(void) {
    wdt_disable();

    // Setup port B
    DDRB =   _BV(GREEN);
    PORTB = ~_BV(GREEN);

    // Setup port A
    DDRA =              _BV(ESC1) | _BV(ESC2) | _BV(RED);
    PORTA = (uint8_t) ~(_BV(ESC1) | _BV(ESC2) | _BV(RED));

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
    
    // enable interrupt on THROTTLE/GYRO change
    PCMSK1 = _BV(PCINT8) | _BV(PCINT9); 
    GIMSK =  _BV(PCIE1);

    sei();

    // initial status
    GREEN_OFF;
    RED_OFF;

    // Wait for throttle and gyro
    while (1) {
        cli();
        if (g_throttle >= MIN && g_throttle <= MAX && g_gyro >= MIN && g_gyro <= MAX)
            break;
        sei();

        _delay_ms(1);
    }
    sei();

#ifdef FO_ENABLED
    // ready to go, enable watchdog for throttle values
    wdt_enable(FO_PERIOD);
    WDTCSR |= _BV(WDIE);
#endif

    while (1) {
        cli();
        // Make local copies
        int16_t throttle = g_throttle;
        int16_t gyro = g_gyro;
        sei();

#ifdef FO_ENABLED
        // skip few periods after signal was restored
        while (g_wait > 0) {
            g_wait--;

            ESC1VAL = MIN;
            ESC2VAL = MIN;
            _delay_ms(20);
        }
#endif

        if (throttle <= MIN || throttle >= MAX || gyro <= MIN || gyro >= MAX) {

            continue;
        }

        // Scale gyro
        gyro = (gyro - MID) / 2;
        
        int16_t left = throttle + gyro;
        int16_t right = throttle - gyro;

        if (left < MIN) {
            // add RPM to left, remove same amount from right
            right -= (MIN - left);
            left = MIN;
        } else if (right < MIN) {
            // add RPM to right, remove same amount from left
            left -= (MIN - right);
            right = MIN;
        }

        // If in range, pass to the engines otherwise, use previous values
        if (left  >= MIN && left  <= MAX && right >= MIN && right <= MAX) {
            GREEN_ON;

            ESC1VAL = left;
            ESC2VAL = right;
        }
    }
    return 0;
}

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
            if (throttle >= MIN && throttle <= MAX)
                wdt_reset();
#endif

            g_throttle = throttle;
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

            g_gyro = gyro;
        }
    }
    last = current;
}

#ifdef FO_ENABLED
ISR(WDT_vect, ISR_BLOCK) {
    GREEN_OFF;

    // Skip few periods after signal was restored
    g_wait = FO_WAIT;

    // re-enable interrupt on watchdog timeout, which is disabled automatically
    WDTCSR |= _BV(WDIE);
}
#endif
