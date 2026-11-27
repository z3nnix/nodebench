#include "core/dynamic_array.h"

#include <stdlib.h>

void* nb_array_grow(void* data, int* capacity, int needed, size_t element_size) {
	if (!capacity || needed <= 0 || element_size == 0 || needed > NB_ARRAY_MAX_CAPACITY) {
		return NULL;
	}

	if (needed <= *capacity && data) {
		return data;
	}

	int next = *capacity > 0 ? *capacity : NB_ARRAY_INITIAL_CAPACITY;
	while (next < needed) {
		if (next > NB_ARRAY_MAX_CAPACITY / 2) {
			next = NB_ARRAY_MAX_CAPACITY;
			break;
		}

		next *= 2;
	}

	if ((size_t)next > (size_t)-1 / element_size) {
		return NULL;
	}

	void* grown = realloc(data, (size_t)next * element_size);
	if (!grown) {
		return NULL;
	}

	*capacity = next;

	return grown;
}

void nb_array_free(void* data) {
	free(data);
}
