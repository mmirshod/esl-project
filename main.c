#include "nrf_gpio.h"
#include "nrf_delay.h"
#include <stdint.h>
#include <string.h>


/**
 * GPIO Related Functions
 */

typedef enum {
    SW1         =   NRF_GPIO_PIN_MAP(1,6),   // P1.06
    LED1        =   NRF_GPIO_PIN_MAP(0,6),   // P0.06
    LED2_R      =   NRF_GPIO_PIN_MAP(0,8),   // P0.08
    LED2_G      =   NRF_GPIO_PIN_MAP(1,9),   // P1.09
    LED2_B      =   NRF_GPIO_PIN_MAP(0,12),  // P0.12
    NOT_FOUND   =   -1
} esl_io_pin_t;

void cfg_pins() {
    nrf_gpio_cfg_output(LED1);
    nrf_gpio_cfg_output(LED2_R);
    nrf_gpio_cfg_output(LED2_G);
    nrf_gpio_cfg_output(LED2_B);
    nrf_gpio_cfg_input(SW1, NRF_GPIO_PIN_PULLUP);
}

/**
 * @brief Function to turn of all LED pins 
*/
void led_off_all() {
    nrf_gpio_pin_write(LED1, 1);
    nrf_gpio_pin_write(LED2_R, 1);
    nrf_gpio_pin_write(LED2_G, 1);
    nrf_gpio_pin_write(LED2_B, 1);
}

// change all names
void led_on(esl_io_pin_t pin) {
    nrf_gpio_pin_write(pin, 0);
}

void led_off(esl_io_pin_t pin) {
    nrf_gpio_pin_write(pin, 1);
}

void led_invert(esl_io_pin_t pin) {
    nrf_gpio_pin_write(pin, !nrf_gpio_pin_read(pin));
}

bool is_pressed(esl_io_pin_t pin) {
    return !(nrf_gpio_pin_read(pin) && 1);
}

esl_io_pin_t get_pin (char color) {
    switch (color)
    {
    case 'r':
    case 'R':
        return LED2_R;
    
    case 'g':
    case 'G':
        return LED2_G;
    
    case 'b':
    case 'B':
        return LED2_B;

    default:
        return NOT_FOUND;
    }
}


int main(int argc, char const *argv[])
{
    char* color_sequence = "RRGGGB";
    size_t color_sequence_length = strlen(color_sequence);

    cfg_pins();
    led_off_all();

    while (true)
    {
        while (is_pressed(SW1)) {
            esl_io_pin_t led_pin = get_pin(*color_sequence);  // GET PIN NO OF THE CURRENT COLOR
            if (led_pin != NOT_FOUND) {
                led_off_all();
                led_on(led_pin);
                nrf_delay_ms(1000);  // wait 1 second
            }
            color_sequence++;  // GO TO NEXT COLOR
            if (*color_sequence == '\0') {
                color_sequence -= color_sequence_length;
            }
        }
    }
    return 0;
}