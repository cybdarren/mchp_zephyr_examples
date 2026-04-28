#define DT_DRV_COMPAT custom_ws2812

#include "ws2812_common.h"

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/devicetree/gpio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <soc.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_LEDS   16

struct ws2812_strip {
    struct gpio_dt_spec gpio;
    uint32_t pin_mask;
    uint8_t num_leds;

    volatile uint32_t *outset;
    volatile uint32_t *outclr;

    uint8_t colors[3 * MAX_LEDS];  /* GRB buffer */
};

#define WS2812_STRIP_INIT(inst)                                         \
    {                                                                   \
        .gpio = GPIO_DT_SPEC_GET_OR(DT_INST(inst, custom_ws2812), gpios, {0}),  \
        .num_leds = DT_INST_PROP_OR(inst, num_leds, 0),                 \
        .pin_mask = 0,                                                  \
        .outset = NULL,                                                 \
        .outclr = NULL,                                                 \
    },

#define NUM_STRIPS DT_NUM_INST_STATUS_OKAY(custom_ws2812)

static struct ws2812_strip strips[NUM_STRIPS] = {
    DT_INST_FOREACH_STATUS_OKAY(WS2812_STRIP_INIT)
};

/* ---------------------------------------------------------- */
/* Low-level bit send (timing critical)                       */
/* ---------------------------------------------------------- */
volatile uint32_t *get_outset(const struct gpio_dt_spec *gpio)
{
    if (gpio->port == DEVICE_DT_GET(DT_NODELABEL(porta))) return &PORT->Group[0].OUTSET.reg;
    if (gpio->port == DEVICE_DT_GET(DT_NODELABEL(portb))) return &PORT->Group[1].OUTSET.reg;
    if (gpio->port == DEVICE_DT_GET(DT_NODELABEL(portc))) return &PORT->Group[2].OUTSET.reg;
    return NULL; // unsupported
}

volatile uint32_t *get_outclr(const struct gpio_dt_spec *gpio)
{
    if (gpio->port == DEVICE_DT_GET(DT_NODELABEL(porta))) return &PORT->Group[0].OUTCLR.reg;
    if (gpio->port == DEVICE_DT_GET(DT_NODELABEL(portb))) return &PORT->Group[1].OUTCLR.reg;
    if (gpio->port == DEVICE_DT_GET(DT_NODELABEL(portc))) return &PORT->Group[2].OUTCLR.reg;
    return NULL; // unsupported
}

/************************************************************
 * The WS2812 timing is very tight, so we use inline assembly 
 * to ensure consistent timing regardless of compiler optimizations.
 * This is based upon a CPU running at 120MHz 
 ************************************************************/
#define NOPS(i, _) "nop\n\t"
#define NOP_N_TIMES(n) LISTIFY(n, NOPS, ())

__attribute__((always_inline)) static inline void ws2812_send_bit_asm(volatile uint32_t *outset,
                                                                      volatile uint32_t *outclr,
                                                                      uint32_t mask,
                                                                      bool bit)
{
    if (bit) {
        __asm__ volatile (
            "str %[mask], [%[outset]] \n\t"   // Set pin high
            NOP_N_TIMES(81)
            "str %[mask], [%[outclr]] \n\t"   // Set pin low
            NOP_N_TIMES(40) 
            :
            : [outset]"r"(outset), [outclr]"r"(outclr), [mask]"r"(mask)
            : "memory"
        );
    } else {
        __asm__ volatile (
            "str %[mask], [%[outset]] \n\t"   // Set pin high
            NOP_N_TIMES(41)
            "str %[mask], [%[outclr]] \n\t"   // Set pin low
            NOP_N_TIMES(79) 
            :
            : [outset]"r"(outset), [outclr]"r"(outclr), [mask]"r"(mask)
            : "memory"
        );
    }
}

/* ---------------------------------------------------------- */
/* Init                                                       */
/* ---------------------------------------------------------- */
void ws2812_init(void)
{
    for (int i = 0; i < NUM_STRIPS; i++) {
        printk("Initializing strip %d: GPIO %s pin %d, num_leds=%d\n",
               i, strips[i].gpio.port->name, strips[i].gpio.pin, strips[i].num_leds);

        if (!device_is_ready(strips[i].gpio.port)) {
            return;
        }

        gpio_pin_configure_dt(&strips[i].gpio, GPIO_OUTPUT);

        strips[i].pin_mask = (1U << strips[i].gpio.pin);
        strips[i].outset = get_outset(&strips[i].gpio);
        strips[i].outclr = get_outclr(&strips[i].gpio);

        /* Clear buffer */
        for (int j = 0; j < 3 * strips[i].num_leds; j++) {
            strips[i].colors[j] = 0;
        }
    }
}

/* ---------------------------------------------------------- */
/* Pixel control                                              */
/* ---------------------------------------------------------- */
void ws2812_set_pixel(int strip_idx, int led_idx,
                      uint8_t red, uint8_t green, uint8_t blue)
{
    if (strip_idx >= NUM_STRIPS) return;
    if (led_idx >= strips[strip_idx].num_leds) return;

    uint8_t *buf = strips[strip_idx].colors;

    /* WS2812 uses GRB order */
    buf[led_idx * 3 + 0] = green;
    buf[led_idx * 3 + 1] = red;
    buf[led_idx * 3 + 2] = blue;
}

bool ws2812_get_pixel(int strip_idx, int led_idx,
                      uint8_t *red, uint8_t *green, uint8_t *blue)
{
    if (strip_idx >= NUM_STRIPS) return false;
    if (led_idx >= strips[strip_idx].num_leds) return false;
    if (!red || !green || !blue) return false;

    uint8_t *buf = strips[strip_idx].colors;

    *green = buf[led_idx * 3 + 0];
    *red   = buf[led_idx * 3 + 1];
    *blue  = buf[led_idx * 3 + 2];

    return true;
}

void ws2812_clear(int strip_idx)
{
    if (strip_idx >= NUM_STRIPS) return;

    for (int i = 0; i < 3 * strips[strip_idx].num_leds; i++) {
        strips[strip_idx].colors[i] = 0;
    }
}

/* ---------------------------------------------------------- */
/* Update (send data)                                         */
/* ---------------------------------------------------------- */
void ws2812_update(int strip_idx)
{
    if (strip_idx >= NUM_STRIPS) {
        return;
    }

    struct ws2812_strip *s = &strips[strip_idx];
    volatile uint32_t *outset = s->outset;
    volatile uint32_t *outclr = s->outclr;

    uint32_t mask = s->pin_mask;

    /* Disable interrupts for strict timing */
    unsigned int key = irq_lock();

    for (int led = 0; led < s->num_leds; led++) {

        /* WS2812 expects GRB order */
        uint8_t g = s->colors[led * 3 + 0];
        uint8_t r = s->colors[led * 3 + 1];
        uint8_t b = s->colors[led * 3 + 2];

        /* Send Green */
        ws2812_send_bit_asm(outset, outclr, mask, (g & 0x80) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (g & 0x40) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (g & 0x20) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (g & 0x10) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (g & 0x08) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (g & 0x04) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (g & 0x02) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (g & 0x01) != 0);
  
        /* Send Red */
        ws2812_send_bit_asm(outset, outclr, mask, (r & 0x80) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (r & 0x40) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (r & 0x20) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (r & 0x10) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (r & 0x08) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (r & 0x04) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (r & 0x02) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (r & 0x01) != 0);

        /* Send Blue */
        ws2812_send_bit_asm(outset, outclr, mask, (b & 0x80) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (b & 0x40) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (b & 0x20) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (b & 0x10) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (b & 0x08) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (b & 0x04) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (b & 0x02) != 0);
        ws2812_send_bit_asm(outset, outclr, mask, (b & 0x01) != 0);        
    }

    /* Re-enable interrupts */
    irq_unlock(key);

    /* Reset pulse: >50 µs */
    k_busy_wait(60);  // ~60 µs
}
