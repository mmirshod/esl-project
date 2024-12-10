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
    ESL_ERROR                   = 0x4000,
} esl_ret_code_t;

typedef struct {
    uint8_t magic_number;
    uint8_t r_val;
    uint8_t g_val;
    uint8_t b_val;
} esl_nvmc_rgb_data_t;

typedef union {
    struct
    {
        esl_nvmc_rgb_data_t rgb_data;
        char                color_name[32];
    } fields;
    uint8_t bits[36];
} __attribute__((packed)) esl_nvmc_saved_color_t;

typedef enum {
    ESL_USB_MSG_TYPE_SUCCESS    = 0,
    ESL_USB_MSG_TYPE_ERROR      = 1,
    ESL_USB_MSG_TYPE_INPUT      = 2,
    ESL_USB_MSG_TYPE_WARNING    = 3
} esl_usb_msg_type_t;

typedef char* esl_cli_cmd_arg_t;

typedef esl_ret_code_t (*esl_cli_cmd_handler_t)(esl_cli_cmd_arg_t *args, int arg_count);

typedef struct {
    const char *command_name;
    const char *command_description;
    esl_cli_cmd_handler_t handler;
    int args_count;                  // Number of expected args
} esl_cli_cmd_entry_t;

/**
 * Static & Global Variables
 */
static esl_pwm_context_t pwm_ctx;

// NVMC
static uint32_t curr_addr = SAVED_COLORS_PG_ADDR;
static esl_nvmc_saved_color_t * saved_colors;
static uint32_t saved_colors_count = 0;

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
static void lfclk_request(void);
static void esl_nvmc_init();

// HANDLERS
void debounce_timeout_handler(void *p_context);
void double_click_timeout_handler(void *p_context);
static void esl_usb_ev_handler(app_usbd_class_inst_t const * p_inst,
                           app_usbd_cdc_acm_user_event_t event);
void button_gpiote_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action);
void led_timer_timeout_handler(void * p_context);

// NVMC Functions
static esl_ret_code_t esl_nvmc_write(uint32_t addr, const void *src);
static esl_ret_code_t esl_nvmc_save_curr_rgb();
static esl_ret_code_t esl_nvmc_read(uint32_t addr, void *buffer, size_t size);

// USB Functions
void esl_cli_process_cmd();
void esl_usb_msg_write(const char* msg, esl_usb_msg_type_t msg_type);
// static uint32_t esl_nvmc_get_curr_addr(uint32_t start_pg_addr);

// Command handlers
esl_ret_code_t esl_cli_cmd_rgb(esl_cli_cmd_arg_t *args, int arg_count);
esl_ret_code_t esl_cli_cmd_hsv(esl_cli_cmd_arg_t *args, int arg_count);
esl_ret_code_t esl_cli_cmd_add_rgb_color(esl_cli_cmd_arg_t *args, int arg_count) {return ESL_ERROR;}
esl_ret_code_t esl_cli_cmd_add_hsv_color(esl_cli_cmd_arg_t *args, int arg_count) {return ESL_ERROR;}
esl_ret_code_t esl_cli_cmd_add_current_color(esl_cli_cmd_arg_t *args, int arg_count);
esl_ret_code_t esl_cli_cmd_list_colors(esl_cli_cmd_arg_t *args, int arg_count);
esl_ret_code_t esl_cli_cmd_help(esl_cli_cmd_arg_t *args, int arg_count);
esl_ret_code_t esl_cli_cmd_apply_color(esl_cli_cmd_arg_t *args, int arg_count) {return ESL_ERROR;}

esl_cli_cmd_handler_t esl_cli_cmd_handler_find(char* cmd_name);

static esl_cli_cmd_entry_t command_table[] = {
    { "rgb", "rgb <R> <G> <B>: set new color based on RGB\n\r", esl_cli_cmd_rgb, 3 },
    { "hsv", "hsv <H> <S> <V>: set new color based on HSV\n\r", esl_cli_cmd_hsv, 3 },
    { "add_rgb_color", "add_rgb_color <R> <G> <B> <color_name>: save RGB color\n\r", esl_cli_cmd_add_rgb_color, 4 },
    { "add_hsv_color", "add_hsv_color <H> <S> <V> <color_name>: save HSV color\n\r", esl_cli_cmd_add_hsv_color, 4 },
    { "add_current_color", "add_current_color <color_name>: save current color\n\r", esl_cli_cmd_add_current_color, 1 },
    { "apply_color", "apply_color <color_name>: apply saved color\n\r", esl_cli_cmd_apply_color, 1 },
    { "list_colors", "list_colors: display all saved colors\n\r", esl_cli_cmd_list_colors, 0 },
    { "help", "help: show list of commands\n\r", esl_cli_cmd_help, 0 }
};

#define ESL_CLI_CMD_MAX_ARGS 4
#define COMMAND_TABLE_SIZE (sizeof(command_table) / sizeof(command_table[0]))

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
    esl_pwm_init(&pwm_ctx);
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

static void lfclk_request(void)
{
    ret_code_t err_code = nrf_drv_clock_init();
    APP_ERROR_CHECK(err_code);
    nrf_drv_clock_lfclk_request(NULL);
}

static void esl_nvmc_init() {
    // Retrieve last saved color
    esl_nvmc_rgb_data_t retrieved_rgb;
    esl_nvmc_read(LAST_COLOR_PG_ADDR, &retrieved_rgb, sizeof(retrieved_rgb));
    if (retrieved_rgb.magic_number == ESL_NVMC_BYTE_VALID) {
        pwm_ctx.rgb_state.red = retrieved_rgb.r_val;
        pwm_ctx.rgb_state.green = retrieved_rgb.g_val;
        pwm_ctx.rgb_state.blue = retrieved_rgb.b_val;

        rgb_to_hsv(
            pwm_ctx.rgb_state.red,
            pwm_ctx.rgb_state.green,
            pwm_ctx.rgb_state.blue,
            &pwm_ctx.hsv_state.hue,
            &pwm_ctx.hsv_state.saturation,
            &pwm_ctx.hsv_state.brightness
        );
        esl_pwm_update_rgb(&pwm_ctx);

    } else if (retrieved_rgb.magic_number != ESL_NVMC_BYTE_VALID) {
        nrfx_nvmc_page_erase(LAST_COLOR_PG_ADDR);
    }

    // Retrieve all user saved colors
    saved_colors = (esl_nvmc_saved_color_t *)malloc(PAGE_SIZE);

    if (saved_colors == NULL) {
        NRF_LOG_ERROR("Memory allocation failed!");
        return;
    }

    while (curr_addr < SAVED_COLORS_PG_ADDR + PAGE_SIZE) {
        esl_nvmc_saved_color_t retrieved_color;
        esl_nvmc_read(curr_addr, &retrieved_color, sizeof(retrieved_color));

        if (retrieved_color.fields.rgb_data.magic_number == ESL_NVMC_BYTE_VALID) {
            saved_colors[saved_colors_count++] = retrieved_color;
            curr_addr += sizeof(esl_nvmc_saved_color_t);
            NRF_LOG_INFO("Color name: %s", NRF_LOG_PUSH(retrieved_color.fields.color_name));
            NRF_LOG_INFO("r: %d, g: %d, b: %d", retrieved_color.fields.rgb_data.r_val, retrieved_color.fields.rgb_data.g_val, retrieved_color.fields.rgb_data.b_val);
        } else if (retrieved_color.fields.rgb_data.magic_number == ESL_NVMC_BYTE_NOT_INIT) {
            NRF_LOG_INFO("Retrieved all saved colors! count: %d", saved_colors_count);
            return;
        } else {
            NRF_LOG_ERROR("Memory Corrupted");
            nrfx_nvmc_page_erase(SAVED_COLORS_PG_ADDR);
            break;
        }
    }
}

void init_button_interrupt() {
    if(!nrfx_gpiote_is_init()) {
        nrfx_gpiote_init();
    }

    nrfx_gpiote_in_config_t in_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(true);
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
        if (pwm_ctx.current_input_mode++ == ESL_PWM_IN_BRIGHTNESS) {
            pwm_ctx.current_input_mode = ESL_PWM_IN_NO_INPUT;
            esl_ret_code_t res = esl_nvmc_save_curr_rgb();
            if (res == ESL_SUCCESS) {
                NRF_LOG_INFO("Current Color saved");
            }
        }
        if (pwm_ctx.current_blink_mode++ == ESL_PWM_CONST_ON) {
            pwm_ctx.current_blink_mode = ESL_PWM_CONST_OFF;
        }
        NRF_LOG_INFO("INPUT MODE CHANGED: %d", pwm_ctx.current_input_mode);
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
                esl_cli_process_cmd();
            }
            else
            {
                if ((current_command - command_buffer) < ESL_USB_COMM_BUFFER_SIZE - 1)
                {
                    *current_command = m_rx_buffer[0];
                    current_command++;
                } else {
                    esl_usb_msg_write("Too long command", ESL_USB_MSG_TYPE_ERROR);
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
    esl_pwm_update_led1(&pwm_ctx);
    if (pwm_ctx.current_input_mode != ESL_PWM_IN_NO_INPUT) {
        if (btn_is_pressed() && single_click_processed) {
            esl_pwm_update_hsv(&pwm_ctx);
            hsv_to_rgb(
                pwm_ctx.hsv_state.hue,
                pwm_ctx.hsv_state.saturation,
                pwm_ctx.hsv_state.brightness,
                &pwm_ctx.rgb_state.red,
                &pwm_ctx.rgb_state.green,
                &pwm_ctx.rgb_state.blue
            );
            esl_pwm_update_rgb(&pwm_ctx);
        }
    }

    esl_pwm_play_seq(&pwm_ctx);
}

// NVMC
static esl_ret_code_t esl_nvmc_write(uint32_t addr, void const * src)
{
    size_t size = sizeof(src);
    if (!nrfx_nvmc_word_writable_check(addr, size)) {
        return ESL_ERR_NVMC_NOT_WRITABLE;
    }

    nrfx_nvmc_words_write(addr, src, 9);

    while (!nrfx_nvmc_write_done_check()) {}

    return ESL_SUCCESS;
}

static esl_ret_code_t esl_nvmc_read(uint32_t addr, void *buffer, size_t size) {
    if (addr % sizeof(uint32_t) != 0) {
        NRF_LOG_ERROR("Address is not aligned!");
        return ESL_ERROR;
    }
    memcpy(buffer, (const void *)addr, size);
    return ESL_SUCCESS;
}


static esl_ret_code_t esl_nvmc_save_curr_rgb() {
    esl_nvmc_rgb_data_t curr_rgb = {
        .magic_number = ESL_NVMC_BYTE_VALID,
        .r_val = pwm_ctx.rgb_state.red,
        .g_val = pwm_ctx.rgb_state.green,
        .b_val = pwm_ctx.rgb_state.blue
    };

    uint32_t bits = 0;
    memcpy(&bits, &curr_rgb, sizeof(curr_rgb));

    nrfx_nvmc_page_erase(LAST_COLOR_PG_ADDR);

    if (nrfx_nvmc_word_writable_check(LAST_COLOR_PG_ADDR, bits)) {
        nrfx_nvmc_word_write(LAST_COLOR_PG_ADDR, bits);

        NRF_LOG_INFO("Address: 0x%x", LAST_COLOR_PG_ADDR);
        NRF_LOG_INFO("RGB Data: %d %d %d", curr_rgb.r_val, curr_rgb.g_val, curr_rgb.b_val);
        return ESL_SUCCESS;
    } else {
        NRF_LOG_ERROR("Not writable");
    }

    return ESL_ERROR;
}

// USB
esl_cli_cmd_handler_t esl_cli_cmd_handler_find(char* cmd_name) {
    if (cmd_name == NULL) {
        return NULL;
    }

    for (int cmd_idx = 0; cmd_idx < COMMAND_TABLE_SIZE; cmd_idx++) {
        if (strcmp(command_table[cmd_idx].command_name, cmd_name) == 0) {
            return command_table[cmd_idx].handler;
        }
    }
    return NULL;
}

void esl_cli_process_cmd() {
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

    esl_cli_cmd_arg_t args[ESL_CLI_CMD_MAX_ARGS];
    int arg_count = 0;

    // Parse values
    while ((token = strtok(NULL, " ")) != NULL) {
        if (arg_count >= ESL_CLI_CMD_MAX_ARGS) {
            esl_usb_msg_write("Too many args", ESL_USB_MSG_TYPE_ERROR);
            return;
        }
        NRF_LOG_INFO("args[%d]: %s", arg_count, NRF_LOG_PUSH(token));
        args[arg_count] = token;
        arg_count++;
    }

    esl_cli_cmd_handler_t cmd_handler = esl_cli_cmd_handler_find(command);
    if (cmd_handler) {
        NRF_LOG_INFO("Args count: %d", arg_count);
        esl_ret_code_t res = cmd_handler(args, arg_count);
        if (res != ESL_SUCCESS) {
            esl_usb_msg_write("Error occurred", ESL_USB_MSG_TYPE_ERROR);
        }
    } else {
        esl_usb_msg_write("Command not found", ESL_USB_MSG_TYPE_ERROR);
    }

    free(command);
}

// CLI command handlers
esl_ret_code_t esl_cli_cmd_rgb(esl_cli_cmd_arg_t* args, int args_count) {
    if (args_count == 3) {
        uint8_t r_val = atoi(args[0]);
        uint8_t g_val = atoi(args[1]);
        uint8_t b_val = atoi(args[2]);

        if (
            r_val >= 0 && r_val <= 255 &&
            g_val >= 0 && g_val <= 255 &&
            b_val >= 0 && b_val <= 255
        ) {
            pwm_ctx.rgb_state.red = r_val;
            pwm_ctx.rgb_state.green = g_val;
            pwm_ctx.rgb_state.blue = b_val;

            rgb_to_hsv(
                pwm_ctx.rgb_state.red,
                pwm_ctx.rgb_state.green,
                pwm_ctx.rgb_state.blue,
                &pwm_ctx.hsv_state.hue,
                &pwm_ctx.hsv_state.saturation,
                &pwm_ctx.hsv_state.brightness
            );
            esl_pwm_update_rgb(&pwm_ctx);
            char rgb_msg[100];
            snprintf(
                rgb_msg, sizeof(rgb_msg), "RGB updated: R=%d, G=%d, B=%d",
                pwm_ctx.rgb_state.red,
                pwm_ctx.rgb_state.green,
                pwm_ctx.rgb_state.blue
            );
            esl_usb_msg_write(rgb_msg, ESL_USB_MSG_TYPE_SUCCESS);
            return ESL_SUCCESS;
        } else {
            esl_usb_msg_write("RGB values out of range (0-255)", ESL_USB_MSG_TYPE_ERROR);
            return ESL_ERROR;
        }
    } else {
       esl_usb_msg_write("RGB command requires 3 args", ESL_USB_MSG_TYPE_ERROR);
       return ESL_ERROR;
    }
    return ESL_ERROR;
}

esl_ret_code_t esl_cli_cmd_hsv(esl_cli_cmd_arg_t* args, int args_count) {
    if (args_count == 3) {
        uint16_t hue = atoi(args[0]);
        uint8_t saturation = atoi(args[1]);
        uint8_t brightness = atoi(args[2]);
        if (
            hue >= 0 && hue <= 360 &&
            saturation >= 0 && saturation <= 100 &&
            brightness >= 0 && brightness <= 100
        ) {
            pwm_ctx.hsv_state.hue = hue;
            pwm_ctx.hsv_state.saturation = saturation;
            pwm_ctx.hsv_state.brightness = brightness;
            hsv_to_rgb(
                pwm_ctx.hsv_state.hue,
                pwm_ctx.hsv_state.saturation,
                pwm_ctx.hsv_state.brightness,
                &pwm_ctx.rgb_state.red,
                &pwm_ctx.rgb_state.green,
                &pwm_ctx.rgb_state.blue
            );
            esl_pwm_update_rgb(&pwm_ctx);
            char hsv_msg[100];
            snprintf(
                hsv_msg, sizeof(hsv_msg), "HSV updated: H=%d, S=%d, V=%d",
                pwm_ctx.hsv_state.hue,
                pwm_ctx.hsv_state.saturation, pwm_ctx.hsv_state.
                brightness
            );
            esl_usb_msg_write(hsv_msg, ESL_USB_MSG_TYPE_SUCCESS);
            return ESL_SUCCESS;
        } else {
            esl_usb_msg_write("HSV values out of range (H: 0-360, S/V: 0-100)", ESL_USB_MSG_TYPE_ERROR);
            return ESL_ERROR;
        }
    } else {
       esl_usb_msg_write("HSV command requires 3 args", ESL_USB_MSG_TYPE_ERROR);
       return ESL_ERROR;
    }
    return ESL_ERROR;
}

esl_ret_code_t esl_cli_cmd_add_current_color(esl_cli_cmd_arg_t* args, int args_count) {
    if (args_count == 1) {
        if (sizeof(args[0]) > 32) {
            esl_usb_msg_write("Color name has to be max 32 characters", ESL_USB_MSG_TYPE_ERROR);
            return ESL_ERROR;
        }
        esl_nvmc_saved_color_t new_color = {
            .fields = {
                .rgb_data = {
                    .r_val = pwm_ctx.rgb_state.red,
                    .g_val = pwm_ctx.rgb_state.green,
                    .b_val = pwm_ctx.rgb_state.blue,
                    .magic_number = ESL_NVMC_BYTE_VALID
                },
            }
        };

        strncpy(new_color.fields.color_name, args[0], sizeof(new_color.fields.color_name) - 1);
        new_color.fields.color_name[sizeof(new_color.fields.color_name) - 1] = '\0';

        esl_ret_code_t res = esl_nvmc_write(curr_addr, &new_color);
        if (res != ESL_SUCCESS) {
            esl_usb_msg_write("Couldn't save current color", ESL_USB_MSG_TYPE_ERROR);
            return ESL_ERROR;
        }
        saved_colors[saved_colors_count++] = new_color;
        curr_addr += sizeof(esl_nvmc_saved_color_t);

        char ret_msg[100];
        snprintf(
            ret_msg, sizeof(ret_msg), "New Color saved:\n\rName: %s, R=%d, G=%d, B=%d",
            new_color.fields.color_name,
            new_color.fields.rgb_data.r_val,
            new_color.fields.rgb_data.g_val,
            new_color.fields.rgb_data.b_val
        );
        esl_usb_msg_write(ret_msg, ESL_USB_MSG_TYPE_SUCCESS);
        return ESL_SUCCESS;
    } else {
       esl_usb_msg_write("Command requires 1 arg", ESL_USB_MSG_TYPE_ERROR);
       return ESL_ERROR;
    }
    return ESL_ERROR;
}

esl_ret_code_t esl_cli_cmd_list_colors(esl_cli_cmd_arg_t *args, int arg_count) {
    if (arg_count == 0) {
        NRF_LOG_INFO("Colors count: %d", saved_colors_count);
        char ret_msg[1024] = "Saved Colors:\n\r";
        for (int color_idx = 0; color_idx < saved_colors_count; ++color_idx) {
            esl_nvmc_saved_color_t color = saved_colors[color_idx];
            NRF_LOG_INFO("Current idx: %d", color_idx);
            NRF_LOG_INFO("Color Name: %s", NRF_LOG_PUSH(color.fields.color_name));

            char temp_buf[100];
            snprintf(
                temp_buf, sizeof(temp_buf), "%d. Name: %s | R: %d, G: %d, B: %d\n\r",
                color_idx + 1,
                color.fields.color_name,
                color.fields.rgb_data.r_val,
                color.fields.rgb_data.g_val,
                color.fields.rgb_data.b_val
            );
            strncat(ret_msg, temp_buf, sizeof(ret_msg) - strlen(ret_msg) - 1);
        }
        esl_usb_msg_write(ret_msg, ESL_USB_MSG_TYPE_SUCCESS);
        return ESL_SUCCESS;
    } else {
        esl_usb_msg_write("Command requires 0 args", ESL_USB_MSG_TYPE_ERROR);
        return ESL_ERROR;
    }
    
    return ESL_ERROR;
}

esl_ret_code_t esl_cli_cmd_help(esl_cli_cmd_arg_t *args, int arg_count) {
    if (arg_count != 0) {
        esl_usb_msg_write("help: No arguments expected", ESL_USB_MSG_TYPE_ERROR);
        return ESL_ERROR;
    }

    char help_msg[1024] = "Available Commands:\n\r";

    for (size_t cmd_idx = 0; cmd_idx < COMMAND_TABLE_SIZE; cmd_idx++) {
        strncat(help_msg, command_table[cmd_idx].command_description, sizeof(help_msg) - strlen(help_msg) - 1);
    }

    esl_usb_msg_write(help_msg, ESL_USB_MSG_TYPE_SUCCESS);
    return ESL_SUCCESS;
}

void esl_usb_msg_write(const char* msg, esl_usb_msg_type_t msg_type) {
    char formatted_msg[1500]; // Buffer for the formatted message

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