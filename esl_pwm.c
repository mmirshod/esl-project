#include "esl_pwm.h"

void esl_pwm_init(esl_pwm_context_t *ctx) {
    static const nrfx_pwm_t pwm0_instance = NRFX_PWM_INSTANCE(0); // Declare PWM instance
    nrfx_pwm_config_t pwm_config = NRFX_PWM_DEFAULT_CONFIG;
    
    // configure output pins
    pwm_config.output_pins[0] = LED1;
    pwm_config.output_pins[1] = LED_R;
    pwm_config.output_pins[2] = LED_G;
    pwm_config.output_pins[3] = LED_B;

#ifdef PWM_TOP_VAL
    // configure top_value
    pwm_config.top_value = PWM_TOP_VAL;
#endif
    // configure step mode
    pwm_config.step_mode = NRF_PWM_STEP_AUTO;
    // configure load mode
    pwm_config.load_mode = NRF_PWM_LOAD_INDIVIDUAL;
    pwm_config.base_clock = NRF_PWM_CLK_500kHz;

    nrfx_pwm_init(&pwm0_instance, &pwm_config, NULL);
    ctx->pwm_instance = &pwm0_instance;    
    
    ctx->pwm_seq_values = (nrf_pwm_values_individual_t){0};
    ctx->pwm_sequence = (nrf_pwm_sequence_t){
        .values.p_individual = &ctx->pwm_seq_values,
        .length = NRF_PWM_VALUES_LENGTH(ctx->pwm_seq_values),
        .repeats = 0,
        .end_delay = 0
    };
    
    ctx->current_input_mode = ESL_PWM_IN_NO_INPUT;
    ctx->current_blink_mode = ESL_PWM_CONST_OFF;
    
    ctx->hsv_state.hue = 63;
    ctx->hsv_state.saturation = 100;
    ctx->hsv_state.brightness = 100;

    ctx->rgb_state.red = 242;
    ctx->rgb_state.green = 255;
    ctx->rgb_state.blue = 0;
}

#ifdef PWM_TOP_VAL
void esl_pwm_update_duty_cycle(esl_pwm_context_t *ctx, esl_io_pin_t out_pin, uint8_t val) {
    switch (out_pin) {
        case LED1:
            ctx->pwm_seq_values.channel_0 = val > PWM_TOP_VAL ? PWM_TOP_VAL : val;
            break;
        case LED_R:
            ctx->pwm_seq_values.channel_1 = val > PWM_TOP_VAL ? PWM_TOP_VAL : val;
            break;
        case LED_G:
            ctx->pwm_seq_values.channel_2 = val > PWM_TOP_VAL ? PWM_TOP_VAL : val;
            break;
        case LED_B:
            ctx->pwm_seq_values.channel_3 = val > PWM_TOP_VAL ? PWM_TOP_VAL : val;
            break;
        default:
            break;
    }
}

void esl_pwm_update_led1(esl_pwm_context_t *ctx) {
    static bool led1_direction = true;
    static int led1_duty_cycle = 0;
    int step = (ctx->current_blink_mode == ESL_PWM_BLINK_SLOW) ? 5 : 20;

    switch (ctx->current_blink_mode) {
        case ESL_PWM_CONST_OFF:
            esl_pwm_update_duty_cycle(ctx, LED1, 0);
            break;

        case ESL_PWM_CONST_ON:
            esl_pwm_update_duty_cycle(ctx, LED1, PWM_TOP_VAL * 0.9);
            break;

        case ESL_PWM_BLINK_SLOW:
        case ESL_PWM_BLINK_FAST:
            if (led1_direction) {
                led1_duty_cycle += step;
            } else {
                led1_duty_cycle -= step;
            }

            if (led1_duty_cycle >= PWM_TOP_VAL * 0.9) {
                led1_duty_cycle = PWM_TOP_VAL * 0.9;
                led1_direction = false;
            } else if (led1_duty_cycle <= 0) {
                led1_duty_cycle = 0;
                led1_direction = true;
            }
            esl_pwm_update_duty_cycle(ctx, LED1, led1_duty_cycle);
            break;

        default:
            break;
    }
}

#endif // PWM_TOP_VAL

#ifdef HSV_STEP
void esl_pwm_update_hsv(esl_pwm_context_t *ctx) {
    static bool hue_direction = true;
    static bool saturation_direction = true;
    static bool brightness_direction = true;

    switch (ctx->current_input_mode) {
        case ESL_PWM_IN_HUE:
            if (hue_direction) {
                ctx->hsv_state.hue += HSV_STEP;
                if (ctx->hsv_state.hue >= 360) {
                    ctx->hsv_state.hue = 360;
                    hue_direction = false;
                }
            } else {
                ctx->hsv_state.hue -= HSV_STEP;
                if (ctx->hsv_state.hue <= 0) {
                    ctx->hsv_state.hue = 0;
                    hue_direction = true;
                }
            }
            break;
        case ESL_PWM_IN_SATURATION:
            if (saturation_direction) {
                ctx->hsv_state.saturation += HSV_STEP;
                if (ctx->hsv_state.saturation >= 100) {
                    ctx->hsv_state.saturation = 100;
                    saturation_direction = false;
                }
            } else {
                ctx->hsv_state.saturation -= HSV_STEP;
                if (ctx->hsv_state.saturation <= 0) {
                    ctx->hsv_state.saturation = 0;
                    saturation_direction = true;
                }
            }
            break;
        case ESL_PWM_IN_BRIGHTNESS:
            if (brightness_direction) {
                ctx->hsv_state.brightness += HSV_STEP;
                if (ctx->hsv_state.brightness >= 100) {
                    ctx->hsv_state.brightness = 100;
                    brightness_direction = false;
                }
            } else {
                ctx->hsv_state.brightness -= HSV_STEP;
                if (ctx->hsv_state.brightness <= 0) {
                    ctx->hsv_state.brightness = 0;
                    brightness_direction = true;
                }
            }
            break;
        default:
            break;
    }
}

#endif // HSV_STEP

void esl_pwm_update_rgb(esl_pwm_context_t *ctx) {
    esl_pwm_update_duty_cycle(ctx, LED_R, ctx->rgb_state.red);
    esl_pwm_update_duty_cycle(ctx, LED_G, ctx->rgb_state.green);
    esl_pwm_update_duty_cycle(ctx, LED_B, ctx->rgb_state.blue);
}

void esl_pwm_play_seq(esl_pwm_context_t *ctx) {
    nrfx_pwm_simple_playback(ctx->pwm_instance, &ctx->pwm_sequence, 1, NRFX_PWM_FLAG_LOOP);
}