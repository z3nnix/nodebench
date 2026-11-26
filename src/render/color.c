#include "render/color.h"

uint16_t rgb555_pack(int r, int g, int b) {
	if (r < 0) { r = 0; }
	if (r > 255) { r = 255; }
	if (g < 0) { g = 0; }
	if (g > 255) { g = 255; }
	if (b < 0) { b = 0; }
	if (b > 255) { b = 255; }

	return (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

void rgb555_unpack(uint16_t color, int* r, int* g, int* b) {
	int r5 = (color >> 10) & 31;
	int g5 = (color >> 5) & 31;
	int b5 = color & 31;

	if (r) { *r = (r5 << 3) | (r5 >> 2); }
	if (g) { *g = (g5 << 3) | (g5 >> 2); }
	if (b) { *b = (b5 << 3) | (b5 >> 2); }
}
