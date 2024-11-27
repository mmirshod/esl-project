#include "esl_utils.h"

void hsv_to_rgb(uint16_t hue, uint8_t saturation, uint8_t value, uint8_t *r, uint8_t *g, uint8_t *b ) {
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

void rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, uint16_t* hue, uint8_t* saturation, uint8_t* value) {
    uint8_t max = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
    uint8_t min = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);
    uint8_t delta = max - min;

    // Calculate value (V)
    *value = max;

    // Calculate saturation (S)
    *saturation = (max == 0) ? 0 : (delta * 255) / max;

    // Calculate hue (H)
    if (delta == 0) {
        *hue = 0;  // Undefined hue for grayscale (r = g = b)
    } else {
        if (max == r) {
            *hue = (60 * ((g - b) * 100 / delta) + 36000) / 100;  // Ensure positive hue
        } else if (max == g) {
            *hue = (60 * ((b - r) * 100 / delta) + 12000) / 100;
        } else {
            *hue = (60 * ((r - g) * 100 / delta) + 24000) / 100;
        }
        if (*hue >= 360) *hue -= 360;
    }
}