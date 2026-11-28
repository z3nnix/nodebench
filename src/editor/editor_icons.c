#include "editor/editor_icons.h"

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

#define NB_EDITOR_ICON_PATH_MAX 512

static const char* icon_files[NB_EDITOR_ICON_COUNT] = {
	"Node3D.svg",
	"Camera3D.svg",
	"MeshInstance3D.svg",
	"DirectionalLight3D.svg",
	"CollisionShape3D.svg",
	"Script.svg",
	"AudioStreamPlayer3D.svg",
	"PackedScene.svg",
	"Folder.svg",
	"File.svg",
	"TextFile.svg",
	"Object.svg",
	"AudioStream.svg",
	"Mesh.svg",
	"Texture2D.svg",
	"Plus.svg",
	"Import.svg",
	"Search.svg",
	"Settings.svg",
	"Play.svg",
	"Edit.svg",
	"Trash.svg",
	"Tag.svg",
	"ChevronDown.svg"
};

static int editor_icon_file_exists(const char* path) {
	FILE* file = fopen(path, "rb");
	if (!file) {
		return 0;
	}
	fclose(file);
	return 1;
}

static int editor_icon_path(char* out, size_t size, const char* root, const char* name) {
	size_t root_len = root ? strlen(root) : 0;
	while (root_len > 0 && root[root_len - 1] == '/') {
		root_len--;
	}
	if (root_len > 0) {
		snprintf(out, size, "%.*s/assets/icons/%s", (int)root_len, root, name);
		if (editor_icon_file_exists(out)) { return 1; }
	}
	char* base = SDL_GetBasePath();
	if (base) {
		snprintf(out, size, "%s../assets/icons/%s", base, name);
		SDL_free(base);
		if (editor_icon_file_exists(out)) { return 1; }
	}
	snprintf(out, size, "assets/icons/%s", name);
	return editor_icon_file_exists(out);
}

int editor_icons_load(void* renderer, const char* project_root, void** textures, int count, int size) {
	SDL_Renderer* sdl_renderer = (SDL_Renderer*)renderer;
	NSVGrasterizer* rasterizer = NULL;

	if (!sdl_renderer || !textures || count != NB_EDITOR_ICON_COUNT || size <= 0 || size > 48) {
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
		char path[NB_EDITOR_ICON_PATH_MAX];
		unsigned char pixels[48 * 48 * 4];
		NSVGimage* image = NULL;
		SDL_Texture* texture = NULL;
		float max_dim;
		float scale;
		float tx;
		float ty;

		memset(pixels, 0, sizeof(pixels));
		if (!editor_icon_path(path, sizeof(path), project_root, icon_files[i])) {
			goto fail;
		}
		image = nsvgParseFromFile(path, "px", 96.0f);
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
