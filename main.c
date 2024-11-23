#include "esl_gpio.h"
#include "esl_utils.h"

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
#define PWM_TOP_VAL                 255
#define HSV_STEP                    1

#define DEBOUNCE_DELAY_MS           50
#define DOUBLE_CLICK_DELAY_MS       300
#define DEBOUNCE_DELAY              APP_TIMER_TICKS(DEBOUNCE_DELAY_MS)
#define DOUBLE_CLICK_DELAY          APP_TIMER_TICKS(DOUBLE_CLICK_DELAY_MS)

static volatile bool awaiting_second_click = false;
static nrfx_pwm_t pwm_instance = NRFX_PWM_INSTANCE(0);

// default values for HSV
static uint8_t brightness = 100;  // a.k.a Value
static uint8_t saturation = 100;
static uint16_t hue = 63;  // in degrees

// default values based on HSV
static uint8_t r_val = 242;
static uint8_t g_val = 255;
static uint8_t b_val = 0;

static nrf_pwm_values_individual_t pwm_seq_values;
static nrf_pwm_sequence_t const pwm_sequnce = {
    .values.p_individual = &pwm_seq_values,
    .length              = NRF_PWM_VALUES_LENGTH(pwm_seq_values),
    .repeats             = 0,
    .end_delay           = 0
};

typedef enum {
    ESL_PWM_IN_NO_INPUT    =   0,
    ESL_PWM_IN_HUE         =   1,
    ESL_PWM_IN_SATURATION  =   2,
    ESL_PWM_IN_BRIGHTNESS  =   3,
} esl_pwm_in_mode_t;

typedef enum {
    ESL_PWM_CONST_OFF   = 0,
    ESL_PWM_BLINK_SLOW  = 1,
    ESL_PWM_BLINK_FAST  = 2,
    ESL_PWM_CONST_ON    = 3,
} esl_pwm_blink_mode_t;

static volatile esl_pwm_in_mode_t current_input_mode = ESL_PWM_IN_NO_INPUT;
static volatile esl_pwm_blink_mode_t current_blink_mode = ESL_PWM_CONST_OFF;

APP_TIMER_DEF(debounce_timer_id);
APP_TIMER_DEF(double_click_timer_id);

void debounce_timeout_handler(void *p_context);
void double_click_timeout_handler(void *p_context);

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

static void init_pwm() {
    nrfx_pwm_config_t pwm_config = NRFX_PWM_DEFAULT_CONFIG;
    
    // configure output pins
    pwm_config.output_pins[0] = LED1;
    pwm_config.output_pins[1] = LED_R;
    pwm_config.output_pins[2] = LED_G;
    pwm_config.output_pins[3] = LED_B;

    // configure top_value
    pwm_config.top_value = PWM_TOP_VAL;
    // configure step mode
    pwm_config.step_mode = NRF_PWM_STEP_AUTO;
    // configure load mode
    pwm_config.load_mode = NRF_PWM_LOAD_INDIVIDUAL;
    pwm_config.base_clock = NRF_PWM_CLK_500kHz;

    nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
}

static void esl_pwm_update_duty_cycle(esl_io_pin_t out_pin, uint8_t val) {
    switch (out_pin)
    {
    case LED1:
        pwm_seq_values.channel_0 = val > PWM_TOP_VAL ? PWM_TOP_VAL : val;
        break;
    case LED_R:
        pwm_seq_values.channel_1 = val > PWM_TOP_VAL ? PWM_TOP_VAL : val;
        break;
    case LED_G:
        pwm_seq_values.channel_2 = val > PWM_TOP_VAL ? PWM_TOP_VAL : val;
        break;
    case LED_B:
        pwm_seq_values.channel_3 = val > PWM_TOP_VAL ? PWM_TOP_VAL : val;
        break;
    default:
        break;
    }
}

void update_hsv() {
    static bool hue_direction = true;
    static bool saturation_direction = true;
    static bool brightness_direction = true;
    switch (current_input_mode)
    {
    case ESL_PWM_IN_HUE:
        if (hue_direction) {
            hue += HSV_STEP;
            if (hue >= 360) {
                hue = 360;
                hue_direction = !hue_direction; // Reverse direction
            }
        } else {
            hue -= HSV_STEP;
            if (hue <= 0) {
                hue = 0;
                hue_direction = !hue_direction; // Reverse direction
            }
        }
        break;

    case ESL_PWM_IN_SATURATION:
        if (saturation_direction) {
            saturation += HSV_STEP;
            if (saturation >= 100) {
                saturation = 100;
                saturation_direction = !saturation_direction; // Reverse direction
            }
        } else {
;
            saturation -= HSV_STEP;
            if (saturation <= 0) {
                saturation = 0;
                saturation_direction = !saturation_direction; // Reverse direction
            }
        }
    case ESL_PWM_IN_BRIGHTNESS:
        if (brightness_direction) {
            brightness += HSV_STEP;
            if (brightness >= 100) {
                brightness = 100;
                brightness_direction = !brightness_direction; // Reverse direction
            }
        } else {
            brightness -= HSV_STEP;
            if (brightness <= 0) {
                brightness = 0;
                brightness_direction = !brightness_direction; // Reverse direction
            }
        }
    default:
        break;
    }
}

static void esl_pwm_update_led1() {
    static bool led1_direction = true;
    static int led1_duty_cycle = 0;
    int step = current_blink_mode == ESL_PWM_BLINK_SLOW ? 5 : 20;
    
    if (current_blink_mode == ESL_PWM_CONST_OFF) {
        esl_pwm_update_duty_cycle(LED1, 0);
    } else if (current_blink_mode == ESL_PWM_CONST_ON) {
        esl_pwm_update_duty_cycle(LED1, PWM_TOP_VAL * 0.9);
    } else if (current_blink_mode == ESL_PWM_BLINK_SLOW || current_blink_mode == ESL_PWM_BLINK_FAST) {
        if (led1_direction) led1_duty_cycle += step;
        else led1_duty_cycle -= step;

        if (led1_duty_cycle >= PWM_TOP_VAL * 0.9) {
            led1_direction = !led1_direction;
            led1_duty_cycle = PWM_TOP_VAL * 0.9;
        } else if (led1_duty_cycle <= 0) {
            led1_direction = !led1_direction;
            led1_duty_cycle = 0;
        }
        esl_pwm_update_duty_cycle(LED1, led1_duty_cycle);
    }
}

static void esl_pwm_update_rgb() {
    update_hsv();
    hsv_to_rgb(hue, saturation, brightness, &r_val, &g_val, &b_val);
    esl_pwm_update_duty_cycle(LED_R, r_val);
    esl_pwm_update_duty_cycle(LED_G, g_val);
    esl_pwm_update_duty_cycle(LED_B, b_val);
}

static void esl_pwm_play_seq() {
    (void)nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequnce, 1, NRFX_PWM_FLAG_LOOP); 
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
        awaiting_second_click = false;
        if (current_input_mode++ == ESL_PWM_IN_BRIGHTNESS)
            current_input_mode = ESL_PWM_IN_NO_INPUT;
        if (current_blink_mode++ == ESL_PWM_CONST_ON) {
            current_blink_mode = ESL_PWM_CONST_OFF;
        }

        app_timer_stop(double_click_timer_id);
    }
}

void double_click_timeout_handler(void *p_context) {
    // Timeout expired without a second click
    awaiting_second_click = false;
}

void led_sequence() {
    switch (current_input_mode) {
        case ESL_PWM_IN_NO_INPUT:
            esl_pwm_update_led1(0);
            esl_pwm_play_seq();
        case ESL_PWM_IN_HUE:
            esl_pwm_update_led1(ESL_PWM_BLINK_SLOW);
            if (is_pressed()) {
                esl_pwm_update_rgb();
            }
            esl_pwm_play_seq();
            nrf_delay_ms(10);
            break;
        case ESL_PWM_IN_SATURATION:
            esl_pwm_update_led1(ESL_PWM_BLINK_FAST);
            if (is_pressed()) {
                esl_pwm_update_rgb();
            }
            esl_pwm_play_seq();
            nrf_delay_ms(10);
            break;
        case ESL_PWM_IN_BRIGHTNESS:
            esl_pwm_update_led1(ESL_PWM_CONST_ON);
            if (is_pressed()) {
                esl_pwm_update_rgb();
            }
            esl_pwm_play_seq();
            nrf_delay_ms(10);
            break;
    }
}

int main(void) {
    lfclk_request();
    init_timers();
    init_button_interrupt();
    cfg_pins();
    led_off_all();
    init_pwm();

    while (1) {
        led_sequence();


    }
    return 0;
}