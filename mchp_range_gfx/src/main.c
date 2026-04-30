/*
 * Main application for VL6180 visualization on a round display.
 *
 * This program initializes a VL6180 sensor, display, and LVGL UI to show
 * sensor data in two modes: text display, rolling chart.
 * Button presses cycle between screens.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/i2c.h>
#include <lvgl.h>
#include <soc.h>
#include <stdio.h>
#include <stdlib.h>
#include "vl6180x.h"
#include "range_text.h"
#include "range_chart.h"
#include "range_dial.h"
#ifdef CONFIG_USE_WS2812
#include <ws2812_common.h>
#endif
#ifdef CONFIG_USE_MCHP_SERIAL
#include <mchp_serial.h>
#endif

#define SLEEP_TIME_MS 100  /* Main loop sleep time */

static volatile int irq_from_button = 0;  /* Flag set by button interrupt */
static bool button_pressed = false;  /* Current button state */

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const struct gpio_dt_spec switch0 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec switch1 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw1), gpios, {0});
static const struct device *vl6180_dev = DEVICE_DT_GET_ANY(st_vl6180x);

/* Static storage for the LVGL screens */
static lv_obj_t *text_screen = NULL;
static lv_obj_t *chart_screen = NULL;
static lv_obj_t *dial_screen = NULL;

typedef enum {
    SCREEN_TEXT = 0,
    SCREEN_CHART,
    SCREEN_DIAL
} screen_mode_t;

static screen_mode_t current_screen = SCREEN_TEXT;

void heartbeat_thread(void)
{
    while (1) {
        gpio_pin_set_dt(&led, 1);
        k_msleep(100);

        gpio_pin_set_dt(&led, 0);
        k_msleep(100);

        gpio_pin_set_dt(&led, 1);
        k_msleep(100);

        gpio_pin_set_dt(&led, 0);
        k_msleep(700);
    }
}

K_THREAD_DEFINE(heartbeat_tid, 512, heartbeat_thread, NULL, NULL, NULL,
                7, 0, 0);

/*
 * LVGL input device read callback for button input.
 * Maps GPIO button state to LVGL touch events.
 */
static void lv_button_read(lv_indev_t * indev_drv, lv_indev_data_t * data)
{
    button_pressed = gpio_pin_get_dt(&switch0) == 1;
    data->state = button_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = 120;  /* Dummy touch coordinates */
    data->point.y = 160;
}

/*
 * Button input event callback (legacy, not used for LVGL).
 */
static void button_input_cb(struct input_event *evt, void *user_data)
{
    if (evt->sync == 0) {
        return;
    }

    irq_from_button = 1;
    printk("Button %d %s\n", evt->code, evt->value ? "pressed" : "released");
}

INPUT_CALLBACK_DEFINE(NULL, button_input_cb, NULL);

int main(void)
{
    int ret;
    struct sensor_value val;

    printk("Application started\n");

    /* Initialize LED GPIO */
    if (!gpio_is_ready_dt(&led)) {
        return 0;
    }

    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        return 0;
    }

    /* Enable button interrupts */
    if (device_is_ready(switch0.port)) {
        gpio_pin_interrupt_configure_dt(&switch0, GPIO_INPUT);
    }

    if (device_is_ready(switch1.port)) {
        gpio_pin_interrupt_configure_dt(&switch1, GPIO_INPUT);
    }

#ifdef CONFIG_USE_WS2812
    ws2812_init();
#endif

    if (!device_is_ready(vl6180_dev)) {
        printk("VL6180X device not ready\n");
        return 0;
    }

    /* Initialize display and LVGL */
    const struct device *display_dev;

    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        printk("Device not ready, aborting test\n");
        return 0;
    }

    display_set_pixel_format(display_dev, PIXEL_FORMAT_RGB_565);
    display_blanking_off(display_dev);

    /* Create LVGL input device for button */
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lv_button_read);

    /* ======== LVGL Screen Initialization ======== */
    /* Create text screen */
    text_screen = lv_obj_create(NULL);
    lv_scr_load(text_screen);
    range_text_init(text_screen);

    chart_screen = lv_obj_create(NULL);
    range_chart_init(chart_screen);

    dial_screen = lv_obj_create(NULL);
    range_dial_init(dial_screen);

    /* Temporary next button */
    lv_obj_t *btn = lv_button_create(lv_screen_active());
    lv_obj_set_size(btn, 80, 40);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 60);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Next");
    lv_obj_center(btn_label);

    /* Main application loop */
    while (1) {
        /* Handle button press to switch screens */
        if (irq_from_button) {            
            irq_from_button = 0;
            button_pressed = gpio_pin_get_dt(&switch0) == 1;

            /* Remove welcome UI elements on first press */
            if (btn != NULL) {
                lv_obj_del(btn);
                btn = NULL;
            }

            if (button_pressed) {
                current_screen = (current_screen + 1) % 3;

                /* Load the new screen */
                switch(current_screen) {
                    case SCREEN_TEXT:
                        lv_scr_load(text_screen);
                        break;
                    case SCREEN_CHART:
                        lv_scr_load(chart_screen);
                        break;
                    case SCREEN_DIAL:
                        lv_scr_load(dial_screen);
                        break;
                }
            }
        }

        if (sensor_sample_fetch(vl6180_dev) == 0) {
            sensor_channel_get(vl6180_dev, SENSOR_CHAN_DISTANCE, &val);
#ifdef CONFIG_USE_MCHP_SERIAL
            float rng_val = sensor_value_to_float(&val);
            mchp_serial_send("RNG", &rng_val, 1);
#endif
            switch(current_screen) {
                case SCREEN_TEXT:
                    update_range_text(&val);
                    break;
                case SCREEN_CHART:
                    update_range_chart(&val);
                    break;
                case SCREEN_DIAL:
                    update_range_dial(&val);
                    break;
            }
        }

        /* Run LVGL timer handler for UI updates */
        lv_timer_handler();

        k_msleep(SLEEP_TIME_MS);
    }

    return 0;
}
