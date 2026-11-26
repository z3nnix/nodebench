CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -MMD -MP -Isrc $(shell pkg-config --cflags sdl2)
LDFLAGS := $(shell pkg-config --libs sdl2) -lm
BUILD_DIR := build
SRC := $(shell find src -type f -name '*.c')
OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))
DEP := $(OBJ:.o=.d)
TARGET := $(BUILD_DIR)/nodebench

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEP)
