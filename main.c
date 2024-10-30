/**
 * Copyright (c) 2014 - 2021, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/** @file
 *
 * @defgroup blinky_example_main main.c
 * @{
 * @ingroup blinky_example
 * @brief Blinky Example Application main file.
 *
 * This file contains the source code for a sample application to blink LEDs.
 *
 */


/**
 * @brief Function for application main entry.
 */

#if 0
#include <stdbool.h>
#include <stdint.h>
#include "nrf_delay.h"
#include "boards.h"
#include <stdio.h>

int main(void)
{
    /* Configure board. */
    bsp_board_init(BSP_INIT_LEDS);
    char* dev_id = "4163";

    /* Toggle LEDs. */
    while (true)
    {
        for (int i = 0; i < LEDS_NUMBER; i++)
        {
            for (int j = 0; j < dev_id[i] - '0'; ++j)
            {
                printf("%d", dev_id[i] - '0');
                bsp_board_led_invert(i);
                nrf_delay_ms(200);
                bsp_board_led_invert(i);  // Turn off the LED
                nrf_delay_ms(200);
            }

            // Pause after blinking for the current LED
            nrf_delay_ms(1000);
        }
    }
}
#else
#include "nrf_gpio.h"

typedef enum {
    SW1         =   NRF_GPIO_PIN_MAP(1,6),   // P1.06
    LED1        =   NRF_GPIO_PIN_MAP(0,6),   // P0.06
    LED2_R      =   NRF_GPIO_PIN_MAP(0,8),   // P0.08
    LED2_G      =   NRF_GPIO_PIN_MAP(1,9),   // P1.09
    LED2_B      =   NRF_GPIO_PIN_MAP(0,12),  // P0.12
    NOT_FOUND   =   -1
} nrf_528400_io_pin_t;

void cfg_pins() {
    nrf_gpio_cfg_output(LED1);
    nrf_gpio_cfg_output(LED2_R);
    nrf_gpio_cfg_output(LED2_G);
    nrf_gpio_cfg_output(LED2_B);
    nrf_gpio_cfg_input(SW1, NRF_GPIO_PIN_PULLUP);
}

void led_on(nrf_528400_io_pin_t pin) {
    nrf_gpio_pin_write(pin, 0);
}

/**
 * @brief Function to turn of all LED pins 
*/
void led_off() {
    nrf_gpio_pin_write(LED1, 1);
    nrf_gpio_pin_write(LED2_R, 1);
    nrf_gpio_pin_write(LED2_G, 1);
    nrf_gpio_pin_write(LED2_B, 1);
}

void led_off(nrf_528400_io_pin_t pin) {
    nrf_gpio_pin_write(pin, 1);
}

void led_invert(nrf_528400_io_pin_t pin) {
    nrf_gpio_pin_write(pin, !nrf_gpio_pin_read(pin));
}

bool is_pressed(nrf_528400_io_pin_t pin) {
    return !(nrf_gpio_pin_read(pin) && 1);
}

nrf_528400_io_pin_t get_pin (char color) {
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
    char* seq = "RRGGGB";
    cfg_pins();
    

    while (true)
    {
        while (is_pressed(SW1)) {
            nrf_528400_io_pin_t led_pin = get_pin(*seq);  // GET PIN NO OF THE CURRENT COLOR
            if (led_pin != NOT_FOUND) {
                led_off();  // TURN OF ALL COLORS
                led_on(led_pin);
            }
            seq++;  // GO TO NEXT COLOR
        }
    }
    return 0;
}
#endif