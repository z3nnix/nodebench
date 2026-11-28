#ifndef NB_CORE_MEMSTREAM_H
#define NB_CORE_MEMSTREAM_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stddef.h>
#include <stdio.h>

static FILE* nb_memstream_open(const void* data, size_t size) {
	return fmemopen((void*)data, size, "rb");
}

#endif
