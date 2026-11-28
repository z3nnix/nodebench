#ifndef NB_CORE_FILE_IO_H
#define NB_CORE_FILE_IO_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static int nb_file_read_all(const char* path, unsigned char** out, size_t* size) {
	if (!path || !out || !size) {
		return 0;
	}

	FILE* file = fopen(path, "rb");
	if (!file) {
		return 0;
	}

	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return 0;
	}

	long length = ftell(file);
	if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return 0;
	}

	unsigned char* data = (unsigned char*)malloc((size_t)length + 1);
	if (!data) {
		fclose(file);
		return 0;
	}

	size_t read = fread(data, 1, (size_t)length, file);
	fclose(file);
	data[read] = '\0';
	*out = data;
	*size = read;

	return 1;
}

#endif
