#ifndef ESL_LEDS_H
#define ESL_LEDS_H

#include "nrf_gpio.h"
#include <stdint.h>
#include <stdbool.h>

// Enumeration for I/O pins
typedef enum {
    SW1         = NRF_GPIO_PIN_MAP(1,6),   // P1.06
    LED1        = NRF_GPIO_PIN_MAP(0,6),   // P0.06
    LED_R       = NRF_GPIO_PIN_MAP(0,8),   // P0.08
    LED_G       = NRF_GPIO_PIN_MAP(1,9),   // P1.09
    LED_B       = NRF_GPIO_PIN_MAP(0,12),  // P0.12
    NOT_FOUND   = -1
} esl_io_pin_t;

// Function prototypes
void cfg_pins(void);                // Configure pins
void led_off_all(void);             // Turn off all LEDs
void led_on(esl_io_pin_t pin);      // Turn on a specified LED
void led_off(esl_io_pin_t pin);     // Turn off a specified LED

#endif // LEDS_H