#ifndef NODEBENCH_SCRIPT_SCRIPT_H
#define NODEBENCH_SCRIPT_SCRIPT_H

#include <stddef.h>

char* script_load_file(const char* path);
char* script_load_mem(const void* data, size_t size);

#endif
