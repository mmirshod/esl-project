#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrfx_systick.h"
#include "nrfx_gpiote.h"
#include "app_timer.h"
#include "nrfx_clock.h"
#include "nrf_drv_clock.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define DEV_ID                      "4163"
#define PWM_FREQ                    1000    

#define DEBOUNCE_DELAY_MS           50
#define DOUBLE_CLICK_DELAY_MS       500
#define DEBOUNCE_DELAY              APP_TIMER_TICKS(DEBOUNCE_DELAY_MS)
#define DOUBLE_CLICK_DELAY          APP_TIMER_TICKS(DOUBLE_CLICK_DELAY_MS)

static  bool    main_loop_active        =   false;
static  bool    awaiting_second_click   =   false;

typedef enum {
    SW1         =   NRF_GPIO_PIN_MAP(1,6),   // P1.06
    LED1        =   NRF_GPIO_PIN_MAP(0,6),   // P0.06
    LED_R       =   NRF_GPIO_PIN_MAP(0,8),   // P0.08
    LED_G       =   NRF_GPIO_PIN_MAP(1,9),   // P1.09
    LED_B       =   NRF_GPIO_PIN_MAP(0,12),  // P0.12
    NOT_FOUND   =   -1
} esl_io_pin_t;

void cfg_pins() {
    nrf_gpio_cfg_output(LED1);
    nrf_gpio_cfg_output(LED_R);
    nrf_gpio_cfg_output(LED_G);
    nrf_gpio_cfg_output(LED_B);
    nrf_gpio_cfg_input(SW1, NRF_GPIO_PIN_PULLUP);
}

void led_off_all() {
    nrf_gpio_pin_write(LED1, 1);
    nrf_gpio_pin_write(LED_R, 1);
    nrf_gpio_pin_write(LED_G, 1);
    nrf_gpio_pin_write(LED_B, 1);
}

void led_on(esl_io_pin_t pin) {
    nrf_gpio_pin_write(pin, 0);
}

void led_off(esl_io_pin_t pin) {
    nrf_gpio_pin_write(pin, 1);
}

APP_TIMER_DEF(debounce_timer_id);
APP_TIMER_DEF(double_click_timer_id);

void debounce_timeout_handler(void *p_context);
void double_click_timeout_handler(void *p_context);
void led_sequence_loop(void);

// GPIOTE Functions
void button_gpiote_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
    if (pin == SW1) {
        app_timer_start(debounce_timer_id, DEBOUNCE_DELAY, NULL);
    }
}

void init_button_interrupt() {
    if(!nrfx_gpiote_is_init()) {
        nrfx_gpiote_init();
    }

    nrfx_gpiote_in_config_t in_config = NRFX_GPIOTE_CONFIG_IN_SENSE_HITOLO(true);
    in_config.pull = NRF_GPIO_PIN_PULLUP;
    nrfx_gpiote_in_init(SW1, &in_config, button_gpiote_handler);
    nrfx_gpiote_in_event_enable(SW1, true);
}

void init_timers() {
    app_timer_init();
    app_timer_create(&debounce_timer_id, APP_TIMER_MODE_SINGLE_SHOT, debounce_timeout_handler);
    app_timer_create(&double_click_timer_id, APP_TIMER_MODE_SINGLE_SHOT, double_click_timeout_handler);
}

static void lfclk_request(void)
{
    ret_code_t err_code = nrf_drv_clock_init();
    APP_ERROR_CHECK(err_code);
    nrf_drv_clock_lfclk_request(NULL);
}

void debounce_timeout_handler(void *p_context) {
    // Start the double-click timer to detect a second click
    if (!awaiting_second_click) {
        awaiting_second_click = true;
        app_timer_start(double_click_timer_id, DOUBLE_CLICK_DELAY, NULL);
    } else {
        // Second click detected within the delay
        main_loop_active = !main_loop_active;  // Toggle main loop active state
        awaiting_second_click = false;
        app_timer_stop(double_click_timer_id);  // Stop double-click timer
    }
}

void double_click_timeout_handler(void *p_context) {
    // Timeout expired without a second click
    awaiting_second_click = false;
}

// Works only with LED2
esl_io_pin_t get_pin_by_color (char color) {
    switch (color)
    {
    case 'r':
    case 'R':
        return LED_R;
    
    case 'g':
    case 'G':
        return LED_G;
    
    case 'b':
    case 'B':
        return LED_B;

    default:
        return NOT_FOUND;
    }
}

void led_pwm_control(esl_io_pin_t pin, uint32_t freq, uint8_t duty_cycle) {
    
    uint32_t period = 16000 / PWM_FREQ;
    uint32_t on_time = (period * duty_cycle) / 100;
    uint32_t off_time = period - on_time;
    nrfx_systick_state_t systick_context;

    // PWM ON phase
    led_on(pin);
    nrfx_systick_get(&systick_context);
    while (!nrfx_systick_test(&systick_context, on_time * 1000)) {
        if (!main_loop_active) return;
    }

    // PWM OFF phase
    led_off(pin);
    nrfx_systick_get(&systick_context);
    while (!nrfx_systick_test(&systick_context, off_time * 1000)) {
        if (!main_loop_active) return;
    }
}

void led_sequence_loop() {
    static char* color_sequence = "RRGGGB";
    size_t color_sequence_length = strlen(color_sequence);
    static int duty_cycle = 0;
    int fade_step = 1;

    while (main_loop_active) {
        if (!nrf_gpio_pin_read(SW1)) {
            esl_io_pin_t led_pin = get_pin_by_color(*color_sequence);
            if (led_pin != NOT_FOUND) {
                for (; duty_cycle <= 100; duty_cycle += fade_step) {
                    if (!main_loop_active) return;
                    led_pwm_control(led_pin, PWM_FREQ, duty_cycle);
                }
                for (; duty_cycle >= 0; duty_cycle -= fade_step) {
                    if (!main_loop_active) return;
                    led_pwm_control(led_pin, PWM_FREQ, duty_cycle);
                }
            }
            duty_cycle = 0;

            color_sequence++;
            if (*color_sequence == '\0') {
                color_sequence -= color_sequence_length;
            }
        }
    }
}

int main(void) {
    lfclk_request();
    init_timers();
    init_button_interrupt();
    cfg_pins();
    nrfx_systick_init();
    led_off_all();

    while (true) {
        led_sequence_loop();
        nrf_delay_ms(10);
    }

    return 0;
}