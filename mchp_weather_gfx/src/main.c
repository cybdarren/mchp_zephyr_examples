/*
 * Main application for BME280 visualization on a round display.
 *
 * This program initializes a BME280 sensor, display, and LVGL UI to show
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
#include <lvgl.h>
#include <soc.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef CONFIG_USE_WS2812
#include <ws2812_common.h>
#endif
#ifdef CONFIG_USE_MCHP_SERIAL
#include <mchp_serial.h>
#endif
#include "bme280_text.h"
#include "bme280_chart.h"

#define SLEEP_TIME_MS 10  /* Main loop sleep time */

static volatile int irq_from_button = 0;  /* Flag set by button interrupt */
static bool button_pressed = false;  /* Current button state */

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const struct gpio_dt_spec switch0 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec switch1 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw1), gpios, {0});
static const struct device *bme280 = DEVICE_DT_GET(DT_ALIAS(bme280sensor));

/* Static storage for the LVGL screens */
static lv_obj_t *text_screen = NULL;
static lv_obj_t *chart_screen = NULL;

typedef enum {
    SCREEN_TEXT = 0,
    SCREEN_CHART
} screen_mode_t;

static screen_mode_t current_screen = SCREEN_TEXT;

static struct k_timer bme_timer;
static struct k_work bme_work;
static struct sensor_value temperature, pressure, humidity;

/* a message queue to send data to the main thread*/
K_MSGQ_DEFINE(bme_msgq, sizeof(struct sensor_value) * 3, 4, 4);

static void bme_timer_handler(struct k_timer *dummy)
{
    k_work_submit(&bme_work);
}

static void bme_work_handler(struct k_work *work)
{
    if (sensor_sample_fetch(bme280) < 0) {
        printk("Failed to fetch BME280 sample\n");
        return;
    }   

    sensor_channel_get(bme280, SENSOR_CHAN_AMBIENT_TEMP, &temperature);
    sensor_channel_get(bme280, SENSOR_CHAN_PRESS, &pressure);
    sensor_channel_get(bme280, SENSOR_CHAN_HUMIDITY, &humidity);

    struct sensor_value data[3] = {
        temperature, 
        pressure, 
        humidity
    };

    k_msgq_put(&bme_msgq, &data, K_NO_WAIT);
}

void bme_init(void)
{
    k_work_init(&bme_work, bme_work_handler);
    k_timer_init(&bme_timer, bme_timer_handler, NULL);
    k_timer_start(&bme_timer, K_MSEC(250), K_MSEC(250));
}

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
 * Button input event callback
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
    struct sensor_value data[3];

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

    /* Configure the BME280 device */
    if (bme280 == NULL) {
        printk("BME280 device not found\n");
        return 0;
    }
    printf("Device %p name is %s\n", bme280, bme280->name);

    if (device_is_ready(bme280)) {
        printk("BME280 device is ready\n");
        bme_init();
    } else {
        printk("BME280 device is not ready\n");
        return 0;
    }

    const struct device *sercom_dev = DEVICE_DT_GET(DT_NODELABEL(sercom3));
    if (!device_is_ready(sercom_dev)) {
        printk("SERCOM3 I2C NOT READY\n");
    } else {
        printk("SERCOM3 I2C READY\n");
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

    struct display_capabilities caps;
    display_get_capabilities(display_dev, &caps);
    uint16_t width = caps.x_resolution;
    uint16_t height = caps.y_resolution;
    printk("Display size: %dx%d\n", width, height);

    /* ======== LVGL Screen Initialization ======== */
    /* Create text screen */
    text_screen = lv_obj_create(NULL);
    lv_scr_load(text_screen);
    bme280_text_init(text_screen);

    /* Create chart screen */
    chart_screen = lv_obj_create(NULL);
    bme280_chart_init(chart_screen);

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
                current_screen = (current_screen + 1) % 2;

                /* Load the new screen */
                switch(current_screen) {
                    case SCREEN_TEXT:
                        lv_scr_load(text_screen);
                        break;
                    case SCREEN_CHART:
                        lv_scr_load(chart_screen);
                        break;
                }
            }
        }

        if (k_msgq_get(&bme_msgq, &data, K_NO_WAIT) == 0) {
#ifdef CONFIG_USE_MCHP_SERIAL
            float wx_vals[3] = {
                sensor_value_to_float(&data[0]),
                sensor_value_to_float(&data[1]),
                sensor_value_to_float(&data[2])
            };
            mchp_serial_send("WX", wx_vals, 3);
#endif
            if (current_screen == SCREEN_TEXT) {
                update_bme280_text(&data[0], &data[1], &data[2]);
            } else {
                update_bme280_chart(&data[0], &data[1], &data[2]);
            }
        }

        /* Run LVGL timer handler for UI updates */
        lv_timer_handler();

        k_msleep(SLEEP_TIME_MS);
    }

    return 0;
}
