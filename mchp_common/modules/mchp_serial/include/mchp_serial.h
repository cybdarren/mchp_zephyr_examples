#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void mchp_serial_send(const char *tag, const float *values, int count);

#ifdef __cplusplus
}
#endif
