#include "esl_gpio.h"
#include "esl_utils.h"
#include "esl_pwm.h"

#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrfx_gpiote.h"
#include "app_timer.h"
#include "nrfx_clock.h"
#include "nrf_drv_clock.h"
#include "nrfx_nvmc.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_log_backend_usb.h"
#include "app_usbd.h"
#include "app_usbd_serial_num.h"
#include "app_usbd_cdc_acm.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/**
 * Custom data types
 */

#define DEBOUNCE_DELAY              APP_TIMER_TICKS(DEBOUNCE_DELAY_MS)
#define DOUBLE_CLICK_DELAY          APP_TIMER_TICKS(DOUBLE_CLICK_DELAY_MS)
#define BOOTLOADER_START_ADDR       (0x000E0000)
#define PAGE_SIZE                   (0x1000)
#define APP_DATA_END_ADDR           BOOTLOADER_START_ADDR
#define APP_DATA_START_ADDR         BOOTLOADER_START_ADDR - 3 * PAGE_SIZE
#define SAVED_COLORS_PG_ADDR        APP_DATA_START_ADDR + PAGE_SIZE
#define LAST_COLOR_PG_ADDR          APP_DATA_START_ADDR

typedef enum {
    ESL_SUCCESS                 = 0x0000,
    ESL_ERR_NVMC_MEMORY_FULL    = 0x1000,
    ESL_ERR_NVMC_NOT_WRITABLE   = 0x1001,
    ESL_ERR_CLI_VALUE_ERROR     = 0x2000,
} esl_ret_code_t;

typedef struct {
        uint8_t magic_number;
        uint8_t r_val;
        uint8_t g_val;
        uint8_t b_val;
    } fields;
    uint32_t bits; 
} esl_nvmc_rgb_data;

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

typedef enum {
    ESL_USB_MSG_TYPE_SUCCESS    = 0,
    ESL_USB_MSG_TYPE_ERROR      = 1,
    ESL_USB_MSG_TYPE_INPUT      = 2,
    ESL_USB_MSG_TYPE_WARNING    = 3
} esl_usb_msg_type_t;

/**
 * Static & Global Variables
 */

// COLORS
// default values for HSV
static uint8_t brightness = 100;  // a.k.a Value
static uint8_t saturation = 100;
static uint16_t hue = 63;  // in degrees

// default values based on HSV
static uint8_t r_val = 242;
static uint8_t g_val = 255;
static uint8_t b_val = 0;

// NVMC
static esl_nvmc_rgb_data rgb_flash = {
    .fields = {
        .magic_number = ESL_NVMC_BLOCK_VALID,
        .r_val = 242,
        .g_val = 255,
        .b_val = 0
    }
};
static uint32_t pg_addr = 0;

// PWM
static nrfx_pwm_t pwm_instance = NRFX_PWM_INSTANCE(0);
static nrf_pwm_values_individual_t pwm_seq_values;
static nrf_pwm_sequence_t const pwm_sequnce = {
    .values.p_individual = &pwm_seq_values,
    .length              = NRF_PWM_VALUES_LENGTH(pwm_seq_values),
    .repeats             = 0,
    .end_delay           = 0
};
static volatile esl_pwm_in_mode_t current_input_mode = ESL_PWM_IN_NO_INPUT;
static volatile esl_pwm_blink_mode_t current_blink_mode = ESL_PWM_CONST_OFF;

// TIMERS
APP_TIMER_DEF(debounce_timer_id);
APP_TIMER_DEF(double_click_timer_id);
APP_TIMER_DEF(led_timer_id);

// IRQ
static volatile bool awaiting_second_click = false;
static volatile bool single_click_processed = false;

// USB
static char m_rx_buffer[READ_SIZE];
static volatile bool esl_usb_tx_done;
static char command_buffer[ESL_USB_COMM_BUFFER_SIZE];
static volatile char* current_command = command_buffer;

/**
 * Functions' Forward Declarations
 */

// INIT FUNCTIONS
void init_button_interrupt();
void init_timers();
static void init_pwm();
static void lfclk_request(void);
static void esl_nvmc_init();

// HANDLERS
void debounce_timeout_handler(void *p_context);
void double_click_timeout_handler(void *p_context);
static void esl_usb_ev_handler(app_usbd_class_inst_t const * p_inst,
                           app_usbd_cdc_acm_user_event_t event);
void button_gpiote_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action);
void led_timer_timeout_handler(void * p_context);

// PWM Functions
static void esl_pwm_update_duty_cycle(esl_io_pin_t out_pin, uint8_t val);
void esl_pwm_update_hsv();
static void esl_pwm_update_led1();
static void esl_pwm_update_rgb();
static void esl_pwm_play_seq();

// NVMC Functions
static void esl_nvmc_write_rgb();

// USB Functions
void process_command();
void esl_usb_msg_write(const char* msg, esl_usb_msg_type_t msg_type);

APP_USBD_CDC_ACM_GLOBAL_DEF(
    esl_usb_cdc_acm,
    esl_usb_ev_handler,
    CDC_ACM_COMM_INTERFACE,
    CDC_ACM_DATA_INTERFACE,
    CDC_ACM_COMM_EPIN,
    CDC_ACM_DATA_EPIN,
    CDC_ACM_DATA_EPOUT,
    APP_USBD_CDC_COMM_PROTOCOL_NONE
);

int main(void) {
    ret_code_t ret = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(ret);

    lfclk_request();
    init_timers();
    init_button_interrupt();
    NRF_LOG_DEFAULT_BACKENDS_INIT();
    cfg_pins();
    led_off_all();
    init_pwm();
    esl_nvmc_init();

    app_usbd_class_inst_t const * class_cdc_acm = app_usbd_cdc_acm_class_inst_get(&esl_usb_cdc_acm);
    ret = app_usbd_class_append(class_cdc_acm);
    APP_ERROR_CHECK(ret);

    ret = app_timer_start(led_timer_id, APP_TIMER_TICKS(10), NULL);
    APP_ERROR_CHECK(ret);
    while (1) {

        while (app_usbd_event_queue_process())
        {
        }

        LOG_BACKEND_USB_PROCESS();
        if (!NRF_LOG_PROCESS())
        {
        }
    }
    return 0;
}

/**
 * Function Definitions
 */

void init_timers() {
    app_timer_init();
    app_timer_create(&debounce_timer_id, APP_TIMER_MODE_SINGLE_SHOT, debounce_timeout_handler);
    app_timer_create(&double_click_timer_id, APP_TIMER_MODE_SINGLE_SHOT, double_click_timeout_handler);
    app_timer_create(&led_timer_id, APP_TIMER_MODE_REPEATED, led_timer_timeout_handler);
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

static void lfclk_request(void)
{
    ret_code_t err_code = nrf_drv_clock_init();
    APP_ERROR_CHECK(err_code);
    nrf_drv_clock_lfclk_request(NULL);
}

static void esl_nvmc_init() {
    uint32_t BOOTLOADER_START_ADDR = 0x000E0000;
    uint32_t PAGE_SIZE = 0x1000;

    pg_addr = BOOTLOADER_START_ADDR - 3 * PAGE_SIZE;
    
    esl_nvmc_rgb_data* retrieved_rgb = (esl_nvmc_rgb_data*)pg_addr;

    if (retrieved_rgb->fields.magic_number == ESL_NVMC_BLOCK_VALID) {
        NRF_LOG_INFO("Written RGB Values\nR: %d, G: %d, B: %d\nAddress: 0x%x, MAGIC NUMBER: 0x%x", retrieved_rgb->fields.r_val, retrieved_rgb->fields.g_val, retrieved_rgb->fields.b_val, pg_addr, retrieved_rgb->fields.magic_number);
        r_val = retrieved_rgb->fields.r_val;
        g_val = retrieved_rgb->fields.g_val;
        b_val = retrieved_rgb->fields.b_val;
        NRF_LOG_INFO("Updating HSV...");   
        rgb_to_hsv(r_val, g_val, b_val, &hue, &saturation, &brightness);
        NRF_LOG_INFO("NEW HSV: %d, %d, %d", hue, saturation, brightness);
    }

    nrfx_nvmc_page_erase(pg_addr);
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

// HANDLERS

void button_gpiote_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
    if (pin == SW1) {
        single_click_processed = false;
        app_timer_start(debounce_timer_id, DEBOUNCE_DELAY, NULL);
    }
}



void debounce_timeout_handler(void *p_context) {
    // Start the double-click timer to detect a second click
    if (!awaiting_second_click) {
        awaiting_second_click = true;
        app_timer_start(double_click_timer_id, DOUBLE_CLICK_DELAY, NULL);
    } else {
        // Second click detected within the delay
        awaiting_second_click = false;
        if (current_input_mode++ == ESL_PWM_IN_BRIGHTNESS) {
            current_input_mode = ESL_PWM_IN_NO_INPUT;
            esl_nvmc_write_rgb();
        }
        if (current_blink_mode++ == ESL_PWM_CONST_ON) {
            current_blink_mode = ESL_PWM_CONST_OFF;
        }
        NRF_LOG_INFO("INPUT MODE CHANGED: %d", current_input_mode);
        app_timer_stop(double_click_timer_id);
    }
}

void double_click_timeout_handler(void *p_context) {
    // Timeout expired without a second click
    awaiting_second_click = false;
    single_click_processed = true;
}

static void esl_usb_ev_handler(app_usbd_class_inst_t const * p_inst,
                           app_usbd_cdc_acm_user_event_t event)
{
    switch (event)
    {
    case APP_USBD_CDC_ACM_USER_EVT_PORT_OPEN:
    {
        ret_code_t ret;
        NRF_LOG_INFO("PORT IS OPEN");
        ret = app_usbd_cdc_acm_read(&esl_usb_cdc_acm, m_rx_buffer, READ_SIZE);
        UNUSED_VARIABLE(ret);
        break;
    }
    case APP_USBD_CDC_ACM_USER_EVT_PORT_CLOSE:
    {
        NRF_LOG_WARNING("PORT IS CLOSED");
        break;
    }
    case APP_USBD_CDC_ACM_USER_EVT_TX_DONE:
    {
        NRF_LOG_INFO("TX DONE!");
        esl_usb_tx_done = true;
        break;
    }
    case APP_USBD_CDC_ACM_USER_EVT_RX_DONE:
    {
        ret_code_t ret;
        do
        {
            if (m_rx_buffer[0] == '\r' || m_rx_buffer[0] == '\n')
            {                
                app_usbd_cdc_acm_write(&esl_usb_cdc_acm, "\r\n", 2);
                
                *current_command = '\0';
                current_command = command_buffer;                
                process_command();
            }
            else
            {
                if ((current_command - command_buffer) < ESL_USB_COMM_BUFFER_SIZE - 1)
                {
                    *current_command = m_rx_buffer[0];
                    current_command++;
                } else {
                    NRF_LOG_ERROR("Too long command");
                    current_command = command_buffer;
                }

                app_usbd_cdc_acm_write(&esl_usb_cdc_acm,
                                       m_rx_buffer,
                                       READ_SIZE);
            }

            /* Fetch data until internal buffer is empty */
            ret = app_usbd_cdc_acm_read(&esl_usb_cdc_acm,
                                        m_rx_buffer,
                                        READ_SIZE);
        } while (ret == NRF_SUCCESS);
        break;
    }
    default:
        break;
    }
}

void led_timer_timeout_handler(void * p_context) {
    switch (current_input_mode) {
        case ESL_PWM_IN_NO_INPUT:
            esl_pwm_update_led1();
            esl_pwm_play_seq();
            break;
        case ESL_PWM_IN_HUE:
        case ESL_PWM_IN_SATURATION:
        case ESL_PWM_IN_BRIGHTNESS:
            esl_pwm_update_led1();
            if (is_pressed() && single_click_processed) {
                esl_pwm_update_hsv();
                hsv_to_rgb(hue, saturation, brightness, &r_val, &g_val, &b_val);
                esl_pwm_update_rgb();
            }
            esl_pwm_play_seq();
            break;
        default:
            break;
    }
}

// PWM Functions
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

void esl_pwm_update_hsv() {
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
    esl_pwm_update_duty_cycle(LED_R, r_val);
    esl_pwm_update_duty_cycle(LED_G, g_val);
    esl_pwm_update_duty_cycle(LED_B, b_val);
}

static void esl_pwm_play_seq() {
    (void)nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequnce, 1, NRFX_PWM_FLAG_LOOP); 
}

// NVMC
static void esl_nvmc_write_rgb()
{
    nrfx_nvmc_page_erase(pg_addr);
    rgb_flash.fields.r_val = r_val;
    rgb_flash.fields.g_val = g_val;
    rgb_flash.fields.b_val = b_val;
    NRF_LOG_INFO("Writing RGB Values...\nR: %d, G: %d, B: %d\nAddress: 0x%x", r_val, g_val, b_val, pg_addr);

    if (!nrfx_nvmc_word_writable_check(pg_addr, rgb_flash.bits)) {
        NRF_LOG_ERROR("Memory address is not writable!");
        return;
    }

    nrfx_nvmc_word_write(pg_addr, rgb_flash.bits);

    while (!nrfx_nvmc_write_done_check()) {}

    NRF_LOG_INFO("RGB values written to flash memory!");
}

// USB
void process_command() {
    char temp_cmd[ESL_USB_COMM_BUFFER_SIZE + 1];
    strncpy(temp_cmd, (const char*)current_command, sizeof(temp_cmd) - 1);
    temp_cmd[sizeof(temp_cmd) - 1] = '\0';

    // Split the command
    char* token = strtok(temp_cmd, " ");
    if (!token) {
        esl_usb_msg_write("No command provided", ESL_USB_MSG_TYPE_ERROR);
        return;
    }

    char* command = malloc(strlen(token) + 1);
    strcpy(command, token);

    int vals[3] = {0};
    int i = 0;

    // Parse values
    while ((token = strtok(NULL, " ")) != NULL && i < 3) {
        vals[i] = atoi(token);
        i++;
    }

    if (strcmp(command, "rgb") == 0) {
        if (i == 3) {
            if (vals[0] >= 0 && vals[0] <= 255 &&
                vals[1] >= 0 && vals[1] <= 255 &&
                vals[2] >= 0 && vals[2] <= 255) {
                r_val = vals[0];
                g_val = vals[1];
                b_val = vals[2];
                rgb_to_hsv(r_val, g_val, b_val, &hue, &saturation, &brightness);
                esl_pwm_update_rgb();
                char rgb_msg[100];
                snprintf(rgb_msg, sizeof(rgb_msg), "RGB updated: R=%d, G=%d, B=%d", r_val, g_val, b_val);
                esl_usb_msg_write(rgb_msg, ESL_USB_MSG_TYPE_SUCCESS);
            } else {
                esl_usb_msg_write("RGB values out of range (0-255)", ESL_USB_MSG_TYPE_ERROR);
            }
        } else {
            esl_usb_msg_write("RGB command requires 3 arguments", ESL_USB_MSG_TYPE_ERROR);
        }
    }
    else if (strcmp(command, "hsv") == 0) {
        if (i == 3) {
            if (vals[0] >= 0 && vals[0] <= 360 &&
                vals[1] >= 0 && vals[1] <= 100 &&
                vals[2] >= 0 && vals[2] <= 100) {
                NRF_LOG_INFO("h: %d s: %d v: %d", vals[0], vals[1], vals[2]);
                hue = vals[0];
                saturation = vals[1];
                brightness = vals[2];
                hsv_to_rgb(hue, saturation, brightness, &r_val, &g_val, &b_val);
                esl_pwm_update_rgb();
                char hsv_msg[100];
                snprintf(hsv_msg, sizeof(hsv_msg), "HSV updated: H=%d, S=%d, V=%d", hue, saturation, brightness);
                esl_usb_msg_write(hsv_msg, ESL_USB_MSG_TYPE_SUCCESS);
            } else {
                esl_usb_msg_write("HSV values out of range (H: 0-360, S/V: 0-100)", ESL_USB_MSG_TYPE_ERROR);
            }
        } else {
            esl_usb_msg_write("HSV command requires 3 arguments", ESL_USB_MSG_TYPE_ERROR);
        }
    }
    else if (strcmp(command, "help") == 0) {
        const char* help_msg = "Available commands:\n\rrgb <R> <G> <B>: set new color based on RGB\n\rhsv <H> <S> <V>: set new color based on HSV\n\rhelp - show this message";
        esl_usb_msg_write(help_msg, ESL_USB_MSG_TYPE_SUCCESS);
    }
    else {
        esl_usb_msg_write("Unknown command", ESL_USB_MSG_TYPE_ERROR);
    }

    free(command);
}

void esl_usb_msg_write(const char* msg, esl_usb_msg_type_t msg_type) {
    char formatted_msg[1024]; // Buffer for the formatted message

    switch (msg_type) {
    case ESL_USB_MSG_TYPE_INPUT:
        snprintf(formatted_msg, sizeof(formatted_msg), ANSI_COLOR_GREEN ">>> " ANSI_COLOR_WHITE "%s" ANSI_COLOR_RESET "\n\r", msg);
        break;

    case ESL_USB_MSG_TYPE_SUCCESS:
        snprintf(formatted_msg, sizeof(formatted_msg), ANSI_COLOR_GREEN "[SUCCESS] " ANSI_COLOR_WHITE "%s" ANSI_COLOR_RESET "\n\r", msg);
        break;

    case ESL_USB_MSG_TYPE_ERROR:
        snprintf(formatted_msg, sizeof(formatted_msg), ANSI_COLOR_RED "[ERROR] " ANSI_COLOR_WHITE "%s" ANSI_COLOR_RESET "\n\r", msg);
        break;

    case ESL_USB_MSG_TYPE_WARNING:
        snprintf(formatted_msg, sizeof(formatted_msg), ANSI_COLOR_YELLOW "[WARNING] " ANSI_COLOR_WHITE "%s" ANSI_COLOR_RESET "\n\r", msg);
        break;

    default:
        snprintf(formatted_msg, sizeof(formatted_msg), ANSI_COLOR_RED "[UNKNOWN] " ANSI_COLOR_WHITE "%s" ANSI_COLOR_RESET "\n\r", msg);
        break;
    }

    ret_code_t ret = app_usbd_cdc_acm_write(&esl_usb_cdc_acm, formatted_msg, strlen(formatted_msg));
    esl_usb_tx_done = false;
    if (ret == NRF_SUCCESS) {
        while (!esl_usb_tx_done)
        {
            while (app_usbd_event_queue_process())
            {
                /* Wait until we're ready to send the data again */
            }
        }
        
    }
}