#include "esl_utils.h"

void hsv_to_rgb(uint32_t hue, uint32_t saturation, uint32_t value, uint32_t *r, uint32_t *g, uint32_t *b ) {
    float h = hue / 60.0;  // Sector of 60 degrees
    float s = saturation / 100.0;
    float v = value / 100.0;

    int i = (int) h;
    float f = h - i;
    float p = v * (1.0 - s);
    float q = v * (1.0 - s * f);
    float t = v * (1.0 - s * (1.0 - f));

    switch (i) {
        case 0: *r = v * 255; *g = t * 255; *b = p * 255; break;
        case 1: *r = q * 255; *g = v * 255; *b = p * 255; break;
        case 2: *r = p * 255; *g = v * 255; *b = t * 255; break;
        case 3: *r = p * 255; *g = q * 255; *b = v * 255; break;
        case 4: *r = t * 255; *g = p * 255; *b = v * 255; break;
        case 5: *r = v * 255; *g = p * 255; *b = q * 255; break;
    }
}
