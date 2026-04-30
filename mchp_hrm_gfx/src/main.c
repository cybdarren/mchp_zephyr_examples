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
#include "afe4404.h"
#include "hrm_text.h"
#include "hrm_chart.h"
#ifdef CONFIG_USE_WS2812
#include <ws2812_common.h>
#endif
#ifdef CONFIG_USE_MCHP_SERIAL
#include <mchp_serial.h>
#endif

#define SLEEP_TIME_MS 10  /* Main loop sleep time */

static volatile int irq_from_button = 0;  /* Flag set by button interrupt */
static bool button_pressed = false;  /* Current button state */

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const struct gpio_dt_spec switch0 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec switch1 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw1), gpios, {0});
static const struct device *hrm_dev = DEVICE_DT_GET_ANY(ti_afe4404);

/* Static storage for the LVGL screens */
static lv_obj_t *text_screen = NULL;
static lv_obj_t *chart_screen = NULL;

#define SCREEN_COUNT 2

typedef enum {
    SCREEN_TEXT = 0,
    SCREEN_CHART
} screen_mode_t;

static screen_mode_t current_screen = SCREEN_TEXT;

/* --- Heart rate processing state --- */
#define SATURATION_THRESHOLD  2000000  /* LED saturated above this */
#define FINGER_ON_THRESHOLD   20000    /* ALED drops below this when finger present */
#define FINGER_OFF_THRESHOLD  100000   /* ALED rises above this when finger removed */
#define MIN_PEAK_INTERVAL_MS  300      /* 200 BPM max */
#define MAX_PEAK_INTERVAL_MS  2000     /* 30 BPM min */
#define BPM_SMOOTHING         0.3f     /* EMA smoothing for BPM output */

static int bpm = 0;
static bool finger_on = false;

/* DC removal filter state */
static float dc_level = 0;

/* Bandpass filter state (simple IIR) */
static float hp_prev_in = 0;
static float hp_prev_out = 0;
static float lp_out = 0;

/* Peak detection state */
static float prev_samples[3] = {0};
static uint32_t last_peak_time = 0;
static float smooth_bpm = 0;

void process_ppg(int32_t led1, int32_t aled1, int *out_bpm)
{
    /*
     * Finger detection strategy:
     * - LED1 saturates at ~2.1M when finger present (too much reflected light)
     * - ALED1 drops to ~25k when finger blocks ambient light
     * - Use ALED1 for PPG since it has variation (~500 counts) when finger present
     */
    bool led_saturated = (led1 > SATURATION_THRESHOLD);
    bool ambient_blocked = (aled1 < FINGER_OFF_THRESHOLD);

    /* Detect finger placement */
    if (!finger_on && led_saturated && ambient_blocked) {
        finger_on = true;
        dc_level = aled1;
        hp_prev_in = hp_prev_out = 0;
        lp_out = 0;
        prev_samples[0] = prev_samples[1] = prev_samples[2] = 0;
        last_peak_time = 0;
        smooth_bpm = 0;
        *out_bpm = 0;
        return;
    }

    /* Detect finger removal */
    if (finger_on && (!led_saturated || !ambient_blocked)) {
        finger_on = false;
        *out_bpm = 0;
        return;
    }

    if (!finger_on) {
        *out_bpm = 0;
        return;
    }

    /*
     * Use ALED1 (ambient) for PPG processing.
     * When LED is saturated, the small variation in ambient channel
     * contains the PPG signal from LED light bleeding through.
     */
    float raw = (float)aled1;

    /* DC removal with slow-tracking IIR (tau ~ 2-3 seconds at 100Hz) */
    dc_level = 0.995f * dc_level + 0.005f * raw;
    float ac = raw - dc_level;

    /* High-pass filter: remove drift below ~0.5Hz */
    float hp_out = 0.95f * (hp_prev_out + ac - hp_prev_in);
    hp_prev_in = ac;
    hp_prev_out = hp_out;

    /* Low-pass filter: smooth noise above ~5Hz */
    lp_out = 0.8f * lp_out + 0.2f * hp_out;

    /* Shift sample history */
    prev_samples[2] = prev_samples[1];
    prev_samples[1] = prev_samples[0];
    prev_samples[0] = lp_out;

    /* Peak detection: look for local maximum
     * Note: threshold lowered since ALED variation is small (~500 counts)
     */
    if (prev_samples[1] > prev_samples[2] &&
        prev_samples[1] > prev_samples[0] &&
        prev_samples[1] > 5.0f) {  /* Lower threshold for small signal */

        uint32_t now = k_uptime_get();

        if (last_peak_time != 0) {
            uint32_t interval = now - last_peak_time;

            if (interval >= MIN_PEAK_INTERVAL_MS && interval <= MAX_PEAK_INTERVAL_MS) {
                float instant_bpm = 60000.0f / interval;

                /* Smooth BPM with EMA, reject outliers */
                if (smooth_bpm == 0) {
                    smooth_bpm = instant_bpm;
                } else if (instant_bpm > 40 && instant_bpm < 200) {
                    smooth_bpm = BPM_SMOOTHING * instant_bpm +
                                 (1.0f - BPM_SMOOTHING) * smooth_bpm;
                }

                *out_bpm = (int)(smooth_bpm + 0.5f);
            }
        }
        last_peak_time = now;
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
    struct sensor_value led1, aled1;

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

    if (!device_is_ready(hrm_dev)) {
        printk("AFE4404 device not ready\n");
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
    hrm_text_init(text_screen);

    chart_screen = lv_obj_create(NULL);
    hrm_chart_init(chart_screen);

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
                current_screen = (current_screen + 1) % SCREEN_COUNT;

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

        if (sensor_sample_fetch(hrm_dev) == 0) {
            /* Read only the channels needed for PPG */
            sensor_channel_get(hrm_dev, SENSOR_CHAN_AFE4404_LED1, &led1);
            sensor_channel_get(hrm_dev, SENSOR_CHAN_AFE4404_ALED1, &aled1);

            process_ppg(led1.val1, aled1.val1, &bpm);

#ifdef CONFIG_USE_MCHP_SERIAL
            float vals[2] = { (float)bpm, lp_out };
            mchp_serial_send("HRM", vals, 2);
#endif

            switch(current_screen) {
                case SCREEN_TEXT:
                    update_hrm_text(bpm);
                    break;
                case SCREEN_CHART:
                    /* Pass filtered AC signal for ECG-style display */
                    update_hrm_chart((int32_t)lp_out);
                    break;
            }
        }

        /* Run LVGL timer handler for UI updates */
        lv_timer_handler();

        k_msleep(SLEEP_TIME_MS);
    }

    return 0;
}
