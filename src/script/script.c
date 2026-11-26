#include <stdio.h>
#include <stdlib.h>

#include "script/script.h"

char* script_load_file(const char* path) {
	if (!path) {
		return NULL;
	}

	FILE* file = fopen(path, "rb");
	if (!file) {
		return NULL;
	}

	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return NULL;
	}

	long length = ftell(file);
	if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return NULL;
	}

	char* buffer = (char*)malloc((size_t)length + 1);
	if (!buffer) {
		fclose(file);
		return NULL;
	}

	size_t read = fread(buffer, 1, (size_t)length, file);
	fclose(file);
	buffer[read] = '\0';

	return buffer;
}
