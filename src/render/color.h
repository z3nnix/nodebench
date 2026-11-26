#ifndef NB_RENDER_COLOR_H
#define NB_RENDER_COLOR_H

#include <stdint.h>

uint16_t rgb555_pack(int r, int g, int b);
void rgb555_unpack(uint16_t color, int* r, int* g, int* b);

#endif
