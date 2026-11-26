CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -MMD -MP -Isrc $(shell pkg-config --cflags sdl2)
LDFLAGS := $(shell pkg-config --libs sdl2) -lm
BUILD_DIR := build
SRC := $(shell find src -type f -name '*.c' ! -path 'src/test/*')
OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))
DEP := $(OBJ:.o=.d)
TARGET := $(BUILD_DIR)/nodebench
TEST_SRC := src/test/test_frame.c
TEST_OBJ := $(BUILD_DIR)/test/test_frame.o
TEST_TARGET := $(BUILD_DIR)/test_frame
TEST_OUTPUT := $(BUILD_DIR)/test_frame.ppm
TEST_LIB_OBJ := $(BUILD_DIR)/core/math.o $(BUILD_DIR)/core/config.o $(BUILD_DIR)/render/color.o $(BUILD_DIR)/render/texture.o $(BUILD_DIR)/render/mesh.o $(BUILD_DIR)/render/material.o $(BUILD_DIR)/render/draw_list.o $(BUILD_DIR)/render/framebuffer.o $(BUILD_DIR)/render/renderer.o

.PHONY: all run test clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_TARGET): $(TEST_OBJ) $(TEST_LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

run: all
	./$(TARGET)

test: $(TEST_TARGET)
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
