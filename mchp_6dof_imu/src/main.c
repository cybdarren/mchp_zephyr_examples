/*
 * Main application for IMU visualization on a round display.
 *
 * This program initializes an IMU sensor, display, and LVGL UI to show
 * accelerometer data in three modes: bubble level, rolling chart, and text.
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
#include "bubble_level.h"
#include "imu_chart.h"
#include "imu_text.h"
#ifdef CONFIG_USE_WS2812
#include <ws2812_common.h>
#endif

#define SLEEP_TIME_MS 10  /* Main loop sleep time */

static struct sensor_trigger imu_trigger;
static volatile int irq_from_sensor = 0;  /* Flag set by IMU interrupt */

static volatile int irq_from_button = 0;  /* Flag set by button interrupt */
static bool button_pressed = false;  /* Current button state */

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const struct gpio_dt_spec switch0 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec switch1 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw1), gpios, {0});
static const struct device *imu = DEVICE_DT_GET(DT_ALIAS(imusensor));

/* Static storage for the LVGL screens */
static lv_obj_t *bubble_screen = NULL;
static lv_obj_t *chart_screen = NULL;
static lv_obj_t *text_screen = NULL;

typedef enum {
    SCREEN_LEVEL = 0,
    SCREEN_CHART,
    SCREEN_TEXT
} screen_mode_t;

static screen_mode_t current_screen = SCREEN_LEVEL;

/*
 * IMU data-ready interrupt handler.
 * Fetches sensor data when triggered.
 */
static void handle_imu_trigger(const struct device *dev, const struct sensor_trigger *trig)
{
    if (trig->type == SENSOR_TRIG_DATA_READY) {
        int rc = sensor_sample_fetch_chan(dev, trig->chan);

        if (rc < 0) {
            printk("Failed to fetch sensor data: %d\n", rc);
            return;
        } else if (rc == 0) {
            irq_from_sensor = 1;  /* Signal main loop to process data */
        }
    }
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

#ifdef CONFIG_USE_WS2812
#define LED_MAX_BRIGHTNESS 24
#define TILT_5DEG   805   /* ~5 degrees using L1 norm metric */
#define TILT_30DEG  3660  /* ~30 degrees */
#define TILT_MAX    10000 /* 90 degrees */

static void update_tilt_leds(struct sensor_value *ax_sv,
                             struct sensor_value *ay_sv,
                             struct sensor_value *az_sv)
{
    int64_t ax = (int64_t)ax_sv->val1 * 1000000 + ax_sv->val2;
    int64_t ay = (int64_t)ay_sv->val1 * 1000000 + ay_sv->val2;
    int64_t az = (int64_t)az_sv->val1 * 1000000 + az_sv->val2;

    int64_t mag = llabs(ax) + llabs(ay) + llabs(az);
    if (mag == 0) {
        return;
    }

    int64_t tilt = (llabs(ax) + llabs(ay)) * 10000 / mag;
    uint8_t r = 0, g = 0;

    if (tilt < TILT_5DEG) {
        g = LED_MAX_BRIGHTNESS;
    } else if (tilt >= TILT_30DEG) {
        int64_t range = TILT_MAX - TILT_30DEG;
        int64_t pos = tilt > TILT_MAX ? range : tilt - TILT_30DEG;
        r = LED_MAX_BRIGHTNESS;
        g = (uint8_t)((range - pos) * (LED_MAX_BRIGHTNESS / 3) / range);
    }

    ws2812_set_pixel(0, 0, r, g, 0);
    ws2812_set_pixel(1, 0, r, g, 0);
    ws2812_set_pixel(1, 1, r, g, 0);
    ws2812_update(0);
    ws2812_update(1);
}
#endif

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
    struct sensor_value accel[3];
    struct sensor_value gyro[3];
    struct sensor_value temperature;
    int ret;

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

    /* Configure the IMU device */
    if (imu == NULL) {
        printk("IMU device not found\n");
        return 0;
    }
    printf("Device %p name is %s\n", imu, imu->name);

    if (device_is_ready(imu)) {
        printk("IMU device is ready\n");
    } else {
        printk("IMU device is not ready\n");
        return 0;
    }

    /* Set up IMU data-ready trigger */
    imu_trigger = (struct sensor_trigger){
        .type = SENSOR_TRIG_DATA_READY,
        .chan = SENSOR_CHAN_ALL,
    };
    ret = sensor_trigger_set(imu, &imu_trigger, handle_imu_trigger);
    if (ret == 0) {
        printk("IMU trigger set successfully\n");
    } else {
        printk("Failed to set IMU trigger (%d)\n", ret);
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
    /* Create bubble level screen */
    bubble_screen = lv_obj_create(NULL);
    lv_scr_load(bubble_screen);
    bubble_level_init(bubble_screen);

    /* Temporary next button */
    lv_obj_t *btn = lv_button_create(lv_screen_active());
    lv_obj_set_size(btn, 80, 40);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 60);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Next");
    lv_obj_center(btn_label);

    /* Create chart screen */
    chart_screen = lv_obj_create(NULL);
    imu_chart_init(chart_screen);

    /* Create text screen */
    text_screen = lv_obj_create(NULL);
    imu_text_init(text_screen);

    /* Main application loop */
    while (1) {
        /* Process IMU data if available */
        if (irq_from_sensor) {
            sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, accel);
            sensor_channel_get(imu, SENSOR_CHAN_GYRO_XYZ, gyro);
            sensor_channel_get(imu, SENSOR_CHAN_DIE_TEMP, &temperature);

            /* Update the current screen with accel data */
            switch(current_screen) {
                case SCREEN_LEVEL:
                    update_bubble_physics(&accel[0], &accel[1], &accel[2]);
                    break;
                case SCREEN_CHART:
                    update_imu_chart(&accel[0], &accel[1], &accel[2]);
                    break;
                case SCREEN_TEXT:
                    update_imu_text(&accel[0], &accel[1], &accel[2]);
                    break;
            }
#ifdef CONFIG_USE_WS2812
            update_tilt_leds(&accel[0], &accel[1], &accel[2]);
#endif
            irq_from_sensor = 0;
        }

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
                    case SCREEN_LEVEL:
                        lv_scr_load(bubble_screen);
                        break;
                    case SCREEN_CHART:
                        lv_scr_load(chart_screen);
                        break;
                    case SCREEN_TEXT:
                        lv_scr_load(text_screen);
                        break;
                }
            }
        }

        /* Run LVGL timer handler for UI updates */
        lv_timer_handler();

        k_msleep(SLEEP_TIME_MS);
    }

    return 0;
}
