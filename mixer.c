#define F_CPU 8000000UL
//#define F_CPU 1200000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define output_low(port,pin)    port    &= ~(1<<(pin))
#define output_high(port,pin)   port    |=  (1<<(pin))
#define set_input(portdir,pin)  portdir &= ~(1<<(pin))
#define set_output(portdir,pin) portdir |=  (1<<(pin))

// Inputs
#define VIN         PORTA3  // Voltage input

#define THROTTLE    PORTB0
#define GYRO        PORTB1

// Outputs
#define ESC2        PORTA5 // OC1B
#define ESC1        PORTA6 // OC1A
#define RED         PORTA7

#define GREEN   PORTB2

void delay_ms(uint16_t millis) {
    while (millis) {
        _delay_ms(1);
        millis--;
    }
}

#define RED_ON      PORTA &= ~(1<<RED)
#define RED_OFF     PORTA |=  (1<<RED)

#define GREEN_ON    PORTB &= ~(1<<GREEN)
#define GREEN_OFF   PORTB |=  (1<<GREEN)

// Current throttle and gyro values (could be out of range 1000-2000)
volatile int16_t g_throttle = 0;
volatile int16_t g_gyro = 0;

int main(void) {
    // Setup port B
    DDRB =   _BV(GREEN);
    PORTB = ~_BV(GREEN);

    // Setup port A
    DDRA =              _BV(ESC1) | _BV(ESC2) | _BV(RED);
    PORTA = (uint8_t) ~(_BV(ESC1) | _BV(ESC2) | _BV(RED));

    // Setup Timer1
    ICR1 = 20000; // TOP

    // Both engines are off
    OCR1A = 1000;
    OCR1B = 1000;

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
        if (g_throttle >= 1000 && g_throttle <= 2000 &&
            g_gyro >= 1000 && g_gyro <= 2000)
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

        // Clamp values to the range
        if (throttle < 1000)
            throttle = 1000;
        else if (throttle > 2000)
            throttle = 2000;

        if (gyro < 1000)
            gyro = 1000;
        else if (gyro > 2000)
            gyro = 2000;

        // Scale gyro
        gyro = (gyro - 1500) / 4;
        
        int16_t left = throttle + gyro;
        int16_t right = throttle - gyro;

        if (left < 1000) {
            right -= (1000 - left);
            left = 1000;
        } else if (right < 1000) {
            left -= (1000 - right);
            right = 1000;
        }

        if (left >= 1000 && left <= 2000 && right >= 1000 && right <= 2000) {
            GREEN_ON;
            RED_OFF;
            OCR1A = left;
            OCR1B = right;
        } else {
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

    if ((last ^ current) & _BV(THROTTLE)) {
        if (current & _BV(THROTTLE)) {
            throttle_start = timer;
        } else {
            int16_t throttle = timer - throttle_start;
            // timer overflow check
            // (note: only one overflow is checked)
            if (throttle < 0)
                throttle += 20000;

            g_throttle = throttle;
        }
    }
    if ((last ^ current) & _BV(GYRO)) {
        if (current & _BV(GYRO)) {
            gyro_start = timer;
        } else {
            int16_t gyro = timer - gyro_start;
            // timer overflow check
            // (note: only one overflow is checked)
            if (gyro < 0)
                gyro += 20000;

            g_gyro = gyro;
        }
    }
    last = current;
}

ISR(BADISR_vect) { }
