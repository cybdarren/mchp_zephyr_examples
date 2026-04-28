/*
 * Main application for testing hw features of Zephyr handheld platform
 *
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/storage/disk_access.h>
#include <ff.h>
#include <diskio.h>
#include <lvgl.h>
#include <ws2812_common.h>
#include "img_util.h"

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec switch0 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec switch1 = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);
volatile bool switch0_event = false;
volatile bool switch1_event = false;

typedef struct {
    const char *disk_name;
} fat_disk_t;

#define SLEEP_TIME_MS 20  /* Main loop sleep time */

static FATFS fat_fs;

#define NUM_LEDS 3
#define MAX_BRIGHTNESS 64
#define FADE_STEP 4
static int pos = 0;
static uint8_t brightness = MAX_BRIGHTNESS;

/* render to hardware */
static void render(void)
{
    /* set active LED to blue level */
    switch (pos) {
        case 0:
            ws2812_set_pixel(0, 0, 0, 0, brightness);  
            ws2812_set_pixel(1, 0, 0, 0, 0);  
            ws2812_set_pixel(1, 1, 0, 0, 0);  
            break;
        case 1:
            ws2812_set_pixel(0, 0, 0, 0, 0);  
            ws2812_set_pixel(1, 0, 0, 0, brightness); 
            ws2812_set_pixel(1, 1, 0, 0, 0);  
            break;
        case 2:
            ws2812_set_pixel(0, 0, 0, 0, 0);
            ws2812_set_pixel(1, 0, 0, 0, 0);  
            ws2812_set_pixel(1, 1,0, 0, brightness); 
            break;
    }

    ws2812_update(0);
    ws2812_update(1);
}

/* animation step */
static void step(void)
{
    render();

    /* fade down */
    if (brightness > FADE_STEP) {
        brightness -= FADE_STEP;
    } else {
        /* move to next LED */
        brightness = MAX_BRIGHTNESS;
        pos = (pos + 1) % NUM_LEDS;
    }
}

/* timer */
static void timer_handler(struct k_timer *dummy)
{
    step();
}

K_TIMER_DEFINE(anim_timer, timer_handler, NULL);

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

static void button_input_cb(struct input_event *evt, void *user_data)
{
    if (evt->sync == 0) {
        return;
    }

    if ((evt->code == INPUT_KEY_1) && (evt->value == 1)) {
        switch0_event = true;
    } else if ((evt->code == INPUT_KEY_2) && (evt->value == 1)) {
        switch1_event = true;
    }

    printk("Button %d %s\n", evt->code, evt->value ? "pressed" : "released");
}

INPUT_CALLBACK_DEFINE(NULL, button_input_cb, NULL);

void display_splash_screen(const struct device *display_dev)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN); // Make it black
    lv_refr_now(NULL);  // Force immediate refresh to show white background before loading image

    display_bin_image(display_dev, "0:/mchplogo.bin", 8, 15);
    k_msleep(3000);  // Show splash screen for 3 seconds
}

int main(void)
{
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

    // enable button interrupts
    if (device_is_ready(switch0.port)) {
        gpio_pin_interrupt_configure_dt(&switch0, GPIO_INPUT);  
    }
 
    if (device_is_ready(switch1.port)) {
        gpio_pin_interrupt_configure_dt(&switch1, GPIO_INPUT);  
    }   

    /* Set the intial colors of the WS2812 LED strips */
    ws2812_init();
    ws2812_set_pixel(0, 0, 64, 0, 0);  /* Set first LED of strip 0 to red */
    ws2812_update(0); 
    ws2812_set_pixel(1, 0, 0, 64, 0);  /* Set first LED of strip 1 to green */
    ws2812_set_pixel(1, 1, 0, 0, 64);  /* Set second LED of strip 1 to blue */
    ws2812_update(1);

    /* Initialize display and LVGL */
    const struct device *display_dev;

    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        printk("Device not ready, aborting test\n");
        return 0;
    }

    display_set_pixel_format(display_dev, PIXEL_FORMAT_RGB_565);
    display_blanking_off(display_dev);

    /* Small delay to allow card to power up reliably */
    k_msleep(100);
    printk("SD card ready, mounting FatFS...\n");

    FRESULT fr = f_mount(&fat_fs, "0:", 1);  // "0:" is logical drive for FatFS
    if (fr == FR_OK) {
        printk("FATFS mounted\n");
        display_splash_screen(display_dev);
    } else {
        printk("f_mount failed: %d\n", fr);
        return 0;
    }

    /* ======== LVGL Screen Initialization ======== */
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), LV_PART_MAIN);

    /* Temporary welcome label */
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Zephyr Basic Test");
    lv_obj_center(label);

    k_timer_start(&anim_timer, K_MSEC(60), K_MSEC(60));

    /* Main application loop */
    while (1) {
        if (switch0_event) {
            lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), LV_PART_MAIN);
            lv_refr_now(NULL);  // Force immediate refresh to show white background before loading image
            if (display_bin_image(display_dev, "0:/zlogo2.bin", 4, 59) == -1) {
                lv_label_set_text(label, "Failed to load image");
            }          
            switch0_event = false;
        }

        if (switch1_event) {
            lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), LV_PART_MAIN);
            lv_refr_now(NULL);  // Force immediate refresh to show white background before loading image
            if (display_bin_image(display_dev, "0:/mchplogo.bin", 8, 15) == -1) {
                lv_label_set_text(label, "Failed to load image");
            }   
            switch1_event = false;
        }

        /* Run LVGL timer handler for UI updates */
        lv_timer_handler();

        k_msleep(SLEEP_TIME_MS);
    }

    return 0;
}
