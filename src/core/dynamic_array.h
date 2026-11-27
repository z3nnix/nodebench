#ifndef NB_CORE_DYNAMIC_ARRAY_H
#define NB_CORE_DYNAMIC_ARRAY_H

#include <stddef.h>

#define NB_ARRAY_INITIAL_CAPACITY 8
#define NB_ARRAY_MAX_CAPACITY 65536

void* nb_array_grow(void* data, int* capacity, int needed, size_t element_size);
void nb_array_free(void* data);

#endif
