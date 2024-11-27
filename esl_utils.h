#ifndef ESL_UTILS_H
#define ESL_UTILS_H

#include <stdint.h>

void hsv_to_rgb(uint16_t hue, uint8_t saturation, uint8_t value, uint8_t *r, uint8_t *g, uint8_t *b );
void rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, uint16_t* hue, uint8_t* saturation, uint8_t* value);

#endif