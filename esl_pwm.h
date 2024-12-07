#ifndef ESL_PWM_H
#define ESL_PWM_H

#include "nrfx_pwm.h"
#include "esl_gpio.h"
#include <stdint.h>

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

typedef struct
{
    uint16_t hue;
    uint8_t saturation;
    uint8_t brightness;
} esl_pwm_hsv_t;

typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} esl_pwm_rgb_t;

typedef struct {
    const nrfx_pwm_t * pwm_instance;
    nrf_pwm_values_individual_t pwm_seq_values;
    nrf_pwm_sequence_t pwm_sequence;
    esl_pwm_in_mode_t current_input_mode;
    esl_pwm_blink_mode_t current_blink_mode;
    esl_pwm_hsv_t hsv_state;
    esl_pwm_rgb_t rgb_state;
} esl_pwm_context_t;


void esl_pwm_init(esl_pwm_context_t *ctx);
void esl_pwm_update_duty_cycle(esl_pwm_context_t *ctx, esl_io_pin_t out_pin, uint8_t val);
void esl_pwm_update_hsv(esl_pwm_context_t *ctx);
void esl_pwm_update_led1(esl_pwm_context_t *ctx);
void esl_pwm_update_rgb(esl_pwm_context_t *ctx);
void esl_pwm_play_seq(esl_pwm_context_t *ctx);

#endif