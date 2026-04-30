#include "mchp_serial.h"

#include <zephyr/sys/printk.h>
#include <stdio.h>
#include <string.h>

void mchp_serial_send(const char *tag, const float *values, int count)
{
    char buf[128];
    int pos = 0;

    pos += snprintf(buf + pos, sizeof(buf) - pos, "$MCHP,%s", tag);

    for (int i = 0; i < count && pos < (int)sizeof(buf) - 16; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, ",%.2f", (double)values[i]);
    }

    uint8_t cksum = 0;
    for (int i = 1; i < pos; i++) {
        cksum ^= (uint8_t)buf[i];
    }

    snprintf(buf + pos, sizeof(buf) - pos, "*%02X\n", cksum);
    printk("%s", buf);
}
