#include "esl_gpio.h"

// Configure pins for LEDs and switch
void cfg_pins() {
    nrf_gpio_cfg_output(LED1);
    nrf_gpio_cfg_output(LED_R);
    nrf_gpio_cfg_output(LED_G);
    nrf_gpio_cfg_output(LED_B);
    nrf_gpio_cfg_input(SW1, NRF_GPIO_PIN_PULLUP);
}

// Turn off all LEDs
void led_off_all() {
    nrf_gpio_pin_write(LED1, 1);
    nrf_gpio_pin_write(LED_R, 1);
    nrf_gpio_pin_write(LED_G, 1);
    nrf_gpio_pin_write(LED_B, 1);
}

// Turn on a specified LED
void led_on(esl_io_pin_t pin) {
    nrf_gpio_pin_write(pin, 0);
}

// Turn off a specified LED
void led_off(esl_io_pin_t pin) {
    nrf_gpio_pin_write(pin, 1);
}

bool is_pressed() {
    return !nrf_gpio_pin_read(SW1);
}
