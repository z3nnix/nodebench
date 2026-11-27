CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -MMD -MP -I. -Isrc $(shell pkg-config --cflags sdl2)
LDFLAGS := $(shell pkg-config --libs sdl2) -lm
BUILD_DIR := build
SRC := $(shell find src -type f -name '*.c' ! -path 'src/test/*')
VENDOR_SRC := third_party/wren/wren.c
OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC)) $(patsubst %.c,$(BUILD_DIR)/%.o,$(VENDOR_SRC))
DEP := $(OBJ:.o=.d)
TARGET := $(BUILD_DIR)/nodebench
TEST_SRC := src/test/test_frame.c src/test/test_scene.c src/test/test_wren.c src/test/test_collision.c src/test/test_audio.c
TEST_OBJ := $(BUILD_DIR)/test/test_frame.o $(BUILD_DIR)/test/test_scene.o $(BUILD_DIR)/test/test_wren.o $(BUILD_DIR)/test/test_collision.o $(BUILD_DIR)/test/test_audio.o
TEST_TARGET := $(BUILD_DIR)/test_frame
SCENE_TEST_TARGET := $(BUILD_DIR)/test_scene
WREN_TEST_TARGET := $(BUILD_DIR)/test_wren
COLLISION_TEST_TARGET := $(BUILD_DIR)/test_collision
AUDIO_TEST_TARGET := $(BUILD_DIR)/test_audio
EDITOR_FS_TEST_TARGET := $(BUILD_DIR)/test_editor_fs
EDITOR_FS_TEST_OBJ := $(BUILD_DIR)/test/test_editor_fs.o $(BUILD_DIR)/editor/editor_fs.o
CONFIG_ROOT_TEST_TARGET := $(BUILD_DIR)/test_config_root
CONFIG_ROOT_TEST_OBJ := $(BUILD_DIR)/test/test_config_root.o $(BUILD_DIR)/core/config.o
SCENE_DOC_TEST_TARGET := $(BUILD_DIR)/test_scene_doc
SCENE_DOC_TEST_OBJ := $(BUILD_DIR)/test/test_scene_doc.o
SCENE_DOC_TEST_LIB_OBJ := $(filter-out $(BUILD_DIR)/main.o,$(OBJ))
DYNAMIC_ARRAY_OBJ := $(BUILD_DIR)/core/dynamic_array.o
EDITOR_LIFECYCLE_TEST_TARGET := $(BUILD_DIR)/test_editor_lifecycle
EDITOR_LIFECYCLE_TEST_OBJ := $(BUILD_DIR)/test/test_editor_lifecycle.o
EDITOR_LIFECYCLE_TEST_LIB_OBJ := $(filter-out $(BUILD_DIR)/main.o,$(OBJ))
ASSET_DB_TEST_TARGET := $(BUILD_DIR)/test_asset_db
ASSET_DB_TEST_OBJ := $(BUILD_DIR)/test/test_asset_db.o
ASSET_DB_TEST_LIB_OBJ := $(DYNAMIC_ARRAY_OBJ) $(BUILD_DIR)/asset/asset_db.o
DYNAMIC_TEST_TARGET := $(BUILD_DIR)/test_dynamic
DYNAMIC_TEST_OBJ := $(BUILD_DIR)/test/test_dynamic.o
DYNAMIC_TEST_LIB_OBJ := $(filter-out $(BUILD_DIR)/main.o,$(OBJ))
TEST_OUTPUT := $(BUILD_DIR)/test_frame.ppm
TEST_LIB_OBJ := $(DYNAMIC_ARRAY_OBJ) $(BUILD_DIR)/core/math.o $(BUILD_DIR)/core/config.o $(BUILD_DIR)/render/color.o $(BUILD_DIR)/render/texture.o $(BUILD_DIR)/render/mesh.o $(BUILD_DIR)/render/material.o $(BUILD_DIR)/render/draw_list.o $(BUILD_DIR)/render/framebuffer.o $(BUILD_DIR)/render/renderer.o
SCENE_TEST_LIB_OBJ := $(DYNAMIC_ARRAY_OBJ) $(BUILD_DIR)/core/math.o $(BUILD_DIR)/scene/scene.o
WREN_TEST_LIB_OBJ := $(DYNAMIC_ARRAY_OBJ) $(BUILD_DIR)/core/math.o $(BUILD_DIR)/core/log.o $(BUILD_DIR)/core/platform.o $(BUILD_DIR)/scene/scene.o $(BUILD_DIR)/physics/collision.o $(BUILD_DIR)/script/script.o $(BUILD_DIR)/script/wren_vm.o $(BUILD_DIR)/third_party/wren/wren.o $(BUILD_DIR)/resource/resource.o $(BUILD_DIR)/audio/audio.o $(BUILD_DIR)/audio/wav.o
COLLISION_TEST_LIB_OBJ := $(DYNAMIC_ARRAY_OBJ) $(BUILD_DIR)/core/math.o $(BUILD_DIR)/scene/scene.o $(BUILD_DIR)/physics/collision.o
AUDIO_TEST_LIB_OBJ := $(DYNAMIC_ARRAY_OBJ) $(BUILD_DIR)/audio/audio.o $(BUILD_DIR)/audio/wav.o $(BUILD_DIR)/resource/resource.o

.PHONY: all run test assets smoke clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_TARGET): $(BUILD_DIR)/test/test_frame.o $(TEST_LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SCENE_TEST_TARGET): $(BUILD_DIR)/test/test_scene.o $(SCENE_TEST_LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(WREN_TEST_TARGET): $(BUILD_DIR)/test/test_wren.o $(WREN_TEST_LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(COLLISION_TEST_TARGET): $(BUILD_DIR)/test/test_collision.o $(COLLISION_TEST_LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(AUDIO_TEST_TARGET): $(BUILD_DIR)/test/test_audio.o $(AUDIO_TEST_LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(EDITOR_FS_TEST_TARGET): $(EDITOR_FS_TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CONFIG_ROOT_TEST_TARGET): $(CONFIG_ROOT_TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SCENE_DOC_TEST_TARGET): $(SCENE_DOC_TEST_OBJ) $(SCENE_DOC_TEST_LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(ASSET_DB_TEST_TARGET): $(ASSET_DB_TEST_OBJ) $(ASSET_DB_TEST_LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(EDITOR_LIFECYCLE_TEST_TARGET): $(EDITOR_LIFECYCLE_TEST_OBJ) $(EDITOR_LIFECYCLE_TEST_LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(DYNAMIC_TEST_TARGET): $(DYNAMIC_TEST_OBJ) $(DYNAMIC_TEST_LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/third_party/%.o: third_party/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

run: all
	./$(TARGET)

smoke: all
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 20s ./$(TARGET) --max_frames=60

test: $(TEST_TARGET) $(SCENE_TEST_TARGET) $(WREN_TEST_TARGET) $(COLLISION_TEST_TARGET) $(AUDIO_TEST_TARGET) $(EDITOR_FS_TEST_TARGET) $(CONFIG_ROOT_TEST_TARGET) $(SCENE_DOC_TEST_TARGET) $(ASSET_DB_TEST_TARGET) $(EDITOR_LIFECYCLE_TEST_TARGET) $(DYNAMIC_TEST_TARGET)
	./$(ASSET_DB_TEST_TARGET)
	./$(EDITOR_LIFECYCLE_TEST_TARGET)
	./$(SCENE_DOC_TEST_TARGET)
	./$(SCENE_TEST_TARGET)
	./$(EDITOR_FS_TEST_TARGET)
	./$(CONFIG_ROOT_TEST_TARGET)
	./$(WREN_TEST_TARGET)
	./$(COLLISION_TEST_TARGET)
	./$(AUDIO_TEST_TARGET)
	./$(DYNAMIC_TEST_TARGET)
	./$(TEST_TARGET) $(TEST_OUTPUT)
	@read -r expected < tests/golden.sha256; \
	actual=$$(sha256sum $(TEST_OUTPUT) | cut -d' ' -f1); \
	if [ "$$actual" != "$$expected" ]; then \
		printf 'hash mismatch: %s != %s\n' "$$actual" "$$expected"; \
		exit 1; \
	fi; \
	printf 'test ok: %s\n' "$$actual"

clean:
	rm -rf $(BUILD_DIR)

-include $(DEP)
-include $(TEST_OBJ:.o=.d)
-include $(EDITOR_FS_TEST_OBJ:.o=.d)
-include $(CONFIG_ROOT_TEST_OBJ:.o=.d)
-include $(SCENE_DOC_TEST_OBJ:.o=.d)
-include $(ASSET_DB_TEST_OBJ:.o=.d)
-include $(EDITOR_LIFECYCLE_TEST_OBJ:.o=.d)
-include $(DYNAMIC_TEST_OBJ:.o=.d)
