#define F_CPU       8000000UL
#define F_TIMER1    (F_CPU/8)

// Convert micro seconds to timer ticks
// For 8Mhz and 1:8 timer prescaler this will be 1 to 1,000,000
#define USEC(usec)  (F_TIMER1/1000000UL*(usec))

#include <avr/io.h>
#include <avr/interrupt.h>
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
#define ESC2        PORTA5 // OC1B
#define ESC1        PORTA6 // OC1A
#define RED         PORTA7

#define GREEN   PORTB2

// LED control
#define RED_ON      PORTA &= ~(1<<RED)
#define RED_OFF     PORTA |=  (1<<RED)

#define GREEN_ON    PORTB &= ~(1<<GREEN)
#define GREEN_OFF   PORTB |=  (1<<GREEN)

// Standard periods
#define MIN         USEC(1000)
#define MID         USEC(1500)
#define MAX         USEC(2000)
#define PERIOD      USEC(20000)

// Current throttle and gyro values
// Note that pin change interrupt does not sanitize values, so they could be:
// 1) negative (if timer was overflowed between start and end)
// 2) out of range 1000-2000m (1ms-2ms)

volatile int16_t g_throttle = 0;
volatile int16_t g_gyro = 0;

inline int16_t clamp(int16_t value) {
    if (value < 0) {
        value += PERIOD; // add 20ms
    }

    if (value > MAX) {
        if (value < MAX + USEC(500)) {
            // pulse is in 2ms to 2.5ms, clamp to 2ms
            value = MAX; 
        } else {
            // too much, return safe (off) value
            value = MIN;
        }
    } else if (value < MIN) {
        value = MIN;
    }
    return value;
}

int main(void) {
    // Setup port B
    DDRB =   _BV(GREEN);
    PORTB = ~_BV(GREEN);

    // Setup port A
    DDRA =              _BV(ESC1) | _BV(ESC2) | _BV(RED);
    PORTA = (uint8_t) ~(_BV(ESC1) | _BV(ESC2) | _BV(RED));

    // Setup Timer1
    ICR1 = PERIOD; // TOP

    // Both engines are off
    OCR1A = MIN;
    OCR1B = MIN;

    // COM1A1:0 is 10 (clear OC1A on match, set at BOTTOM)
    // COM1B1:0 is 10 (clear OC1B on match, set at BOTTOM)
    // WGM13:10 is 1110 (Fast PWM, TOP at ICR1)
    // CS12:10 is 010 (clkIO / 8 == 1Mhz)
    TCCR1A = _BV(COM1A1) | _BV(COM1B1) | _BV(WGM11);
    TCCR1B = _BV(WGM13)  | _BV(WGM12)  | _BV(CS11); 
    //TIMSK1 = TOIE1; // enable interrupt on overflow

    PCMSK1 = _BV(PCINT8) | _BV(PCINT9); // enable interrupt on THROTTLE/GYRO change
    GIMSK =  _BV(PCIE1); // enable interrupots on PORTB pins (THROTTLE/GYRO)

    sei();

    // Show status
    GREEN_OFF;
    RED_OFF;

    // Wait for throttle and gyro
    while(1) {
        cli();
        if (g_throttle >= MIN && g_throttle <= MAX &&
                g_gyro >= MIN && g_gyro     <= MAX)
            break;
        sei();

        _delay_ms(1);
    }
    sei();

    while(1) {
        cli();
        // Make local copies
        int16_t throttle = g_throttle;
        int16_t gyro = g_gyro;
        sei();

        // Clamp values
        throttle = clamp(throttle);
        gyro = clamp(gyro);

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

        if (left  >= MIN && left  <= MAX && 
            right >= MIN && right <= MAX) {
            // Ok, pass to the engines
            GREEN_ON;
            RED_OFF;
            OCR1A = left;
            OCR1B = right;
        } else {
            // Not OK, leave previous values
            GREEN_OFF;
            RED_ON;
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
            g_throttle = timer - throttle_start;
        }
    }
    // if GYRO pin changed since last interrupt
    if ((last ^ current) & _BV(GYRO)) {
        if (current & _BV(GYRO)) {
            // start pulse
            gyro_start = timer;
        } else {
            // end pulse
            g_gyro = timer - gyro_start;
        }
    }
    last = current;
}

// FIXME: remove later
ISR(BADISR_vect) { }
