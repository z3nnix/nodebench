#include <stdlib.h>
#include <string.h>

#include "core/file_io.h"
#include "script/script.h"

char* script_load_file(const char* path) {
	unsigned char* data = NULL;
	size_t size = 0;
	if (!nb_file_read_all(path, &data, &size)) {
		return NULL;
	}

	char* source = script_load_mem(data, size);
	free(data);

	return source;
}

char* script_load_mem(const void* data, size_t size) {
	if (!data) {
		return NULL;
	}

	char* buffer = (char*)malloc(size + 1);
	if (!buffer) {
		return NULL;
	}

	memcpy(buffer, data, size);
	buffer[size] = '\0';

	return buffer;
}
