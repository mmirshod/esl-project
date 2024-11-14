#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrfx_pwm.h"
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

static volatile bool awaiting_second_click = false;
nrfx_pwm_t pwm_instance = NRFX_PWM_INSTANCE(0);
nrf_pwm_values_individual_t pwm_duty_cycle_values;
nrf_pwm_sequence_t pwm_sequnce;

typedef enum {
    NO_INPUT    =   0,
    HUE         =   1,
    SATURATION  =   2,
    BRIGHTNESS  =   3,
} esl_in_mode_t;

static volatile esl_in_mode_t current_input_mode = NO_INPUT;

// static int brightness = 100;
// static int saturation = 100;
// static int hue = 360 * 0.63;

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

void pwm_handler(void);

void init_pwm() {
    nrf_delay_ms(5000);
    led_on(LED1);
    nrf_delay_ms(500);
    led_off_all();
    nrfx_pwm_config_t pwm_config = NRFX_PWM_DEFAULT_CONFIG;
    
    // configure output pins
    pwm_config.output_pins[0] = LED1;
    pwm_config.output_pins[1] = LED_R;
    pwm_config.output_pins[2] = LED_G;
    pwm_config.output_pins[3] = LED_B;

    // configure top_value
    pwm_config.top_value = 100;

    // configure sequence
    pwm_sequnce.values.p_individual = &pwm_duty_cycle_values;
    pwm_sequnce.length = NRF_PWM_VALUES_LENGTH(pwm_duty_cycle_values);
    pwm_sequnce.repeats = 0;
    pwm_sequnce.end_delay = 0;

    nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);

    led_on(LED1);
    nrf_delay_ms(500);
    led_off_all();
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
        // led_on(LED1);
        // nrf_delay_ms(500);
        // led_off(LED1);
        awaiting_second_click = true;
        app_timer_start(double_click_timer_id, DOUBLE_CLICK_DELAY, NULL);
    } else {
        // Second click detected within the delay
        awaiting_second_click = false;
        // led_on(LED_R);
        // nrf_delay_ms(1000);
        // led_off_all();
        if (current_input_mode++ == BRIGHTNESS) {
            current_input_mode = NO_INPUT;
        }
        app_timer_stop(double_click_timer_id);  // Stop double-click timer
    }
}

void double_click_timeout_handler(void *p_context) {
    // Timeout expired without a second click
    awaiting_second_click = false;
}

void led_sequence() {
    static uint32_t last_blink_time = 0;
    static bool led_state = false; // Track whether LED is on or off

    // Determine how frequently the LED blinks based on the current input mode
    uint32_t blink_delay = 0;

    switch (current_input_mode) {
        case NO_INPUT:
            pwm_duty_cycle_values.channel_0 = 0; // LED off
            blink_delay = 1000; // Delay for a longer period
            break;
        case HUE:
            pwm_duty_cycle_values.channel_0 = 25; // 25% brightness
            blink_delay = 500; // Blink every 500ms
            break;
        case SATURATION:
            pwm_duty_cycle_values.channel_0 = 75; // 75% brightness
            blink_delay = 200; // Blink every 200ms
            break;
        case BRIGHTNESS:
            pwm_duty_cycle_values.channel_0 = 100; // Full brightness
            blink_delay = 100; // Blink every 100ms
            break;
        default:
            pwm_duty_cycle_values.channel_0 = 0; // Default to LED off
            blink_delay = 1000; // Longer delay for default
            break;
    }

    // Toggle the LED state based on time
    uint32_t current_time = app_timer_cnt_get(); // Get current time in ticks

    if (current_time - last_blink_time >= blink_delay) {
        led_state = !led_state;  // Toggle LED state
        last_blink_time = current_time;

        // Set the duty cycle based on the LED state (on or off)
        if (led_state) {
            pwm_duty_cycle_values.channel_0 = pwm_duty_cycle_values.channel_0; // Brightness level
        } else {
            pwm_duty_cycle_values.channel_0 = 0; // LED off
        }

        // Update the PWM sequence with the new duty cycle values
        pwm_sequnce.values.p_individual = &pwm_duty_cycle_values;
        pwm_sequnce.length = NRF_PWM_VALUES_LENGTH(pwm_duty_cycle_values);
        pwm_sequnce.repeats = 0;
        pwm_sequnce.end_delay = 0;

        // Play the PWM sequence with looping to create blinking effect
        nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequnce, 1, NRFX_PWM_FLAG_LOOP);
    }
}

// void led_pwm_control(esl_io_pin_t pin, uint32_t freq, uint8_t duty_cycle) {
    
//     uint32_t period = 16000 / PWM_FREQ;
//     uint32_t on_time = (period * duty_cycle) / 100;
//     uint32_t off_time = period - on_time;
//     nrfx_systick_state_t systick_context;

//     // PWM ON phase
//     led_on(pin);
//     nrfx_systick_get(&systick_context);
//     while (!nrfx_systick_test(&systick_context, on_time * 1000)) {
//         if (!main_loop_active) return;
//     }

//     // PWM OFF phase
//     led_off(pin);
//     nrfx_systick_get(&systick_context);
//     while (!nrfx_systick_test(&systick_context, off_time * 1000)) {
//         if (!main_loop_active) return;
//     }
// }

int main(void) {
    lfclk_request();
    init_timers();
    init_button_interrupt();
    cfg_pins();
    led_off_all();
    init_pwm();

    while (true) {
        led_sequence();
        nrf_delay_ms(10);
    }

    return 0;
}