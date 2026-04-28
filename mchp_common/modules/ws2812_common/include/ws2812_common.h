#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the WS2812 strips from DTS */
void ws2812_init(void);

/* Set the color for a single LED in a strip (0-based index) */
void ws2812_set_pixel(int strip_idx, int led_idx, uint8_t red, uint8_t green, uint8_t blue);

/* Get the color of a single LED in a strip (0-based index) */
bool ws2812_get_pixel(int strip_idx, int led_idx,
                      uint8_t *red, uint8_t *green, uint8_t *blue);

/* Update the strip, sending data to the LEDs */
void ws2812_update(int strip_idx);

/* Helper to set all LEDs off */
void ws2812_clear(int strip_idx);

#ifdef __cplusplus
}
#endif
