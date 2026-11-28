#include "editor/editor_icons.h"

#include "editor_assets.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define NANOSVG_IMPLEMENTATION
#include "third_party/nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "third_party/nanosvg/nanosvgrast.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

int editor_icons_load(void* renderer, const char* project_root, void** textures, int count, int size) {
	SDL_Renderer* sdl_renderer = (SDL_Renderer*)renderer;
	NSVGrasterizer* rasterizer = NULL;

	(void)project_root;
	if (!sdl_renderer || !textures || count != NB_EDITOR_ICON_COUNT || count != NB_EDITOR_ICON_ASSET_COUNT || size <= 0 || size > 48) {
		return 0;
	}
	for (int i = 0; i < count; i++) {
		textures[i] = NULL;
	}
	rasterizer = nsvgCreateRasterizer();
	if (!rasterizer) {
		return 0;
	}
	for (int i = 0; i < count; i++) {
		char icon_text[1024];
		unsigned char pixels[48 * 48 * 4];
		NSVGimage* image = NULL;
		SDL_Texture* texture = NULL;
		float max_dim;
		float scale;
		float tx;
		float ty;

		memset(pixels, 0, sizeof(pixels));
		if (strlen(nb_editor_icon_data[i]) >= sizeof(icon_text)) {
			goto fail;
		}
		snprintf(icon_text, sizeof(icon_text), "%s", nb_editor_icon_data[i]);
		image = nsvgParse(icon_text, "px", 96.0f);
		if (!image) {
			goto fail;
		}
		if (image->width <= 0.0f || image->height <= 0.0f) {
			nsvgDelete(image);
			goto fail;
		}
		max_dim = image->width > image->height ? image->width : image->height;
		scale = (float)size / max_dim;
		tx = ((float)size - image->width * scale) * 0.5f;
		ty = ((float)size - image->height * scale) * 0.5f;
		nsvgRasterize(rasterizer, image, tx, ty, scale, pixels, size, size, size * 4);
		nsvgDelete(image);
		texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, size, size);
		if (!texture) {
			goto fail;
		}
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
		if (SDL_UpdateTexture(texture, NULL, pixels, size * 4) != 0) {
			SDL_DestroyTexture(texture);
			goto fail;
		}
		textures[i] = texture;
	}
	nsvgDeleteRasterizer(rasterizer);
	return 1;

fail:
	editor_icons_free(textures, count);
	nsvgDeleteRasterizer(rasterizer);
	return 0;
}

void editor_icons_free(void** textures, int count) {
	if (!textures) {
		return;
	}
	for (int i = 0; i < count; i++) {
		if (textures[i]) {
			SDL_DestroyTexture((SDL_Texture*)textures[i]);
			textures[i] = NULL;
		}
	}
}
