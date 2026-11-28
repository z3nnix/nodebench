#ifndef NODEBENCH_RESOURCE_LOADER_TRES_H
#define NODEBENCH_RESOURCE_LOADER_TRES_H

#include <stddef.h>

#include "core/engine.h"
#include "resource/resource_id.h"

material_id material_load_tres(engine_t* engine, const char* path);
material_id material_load_tres_mem(engine_t* engine, const void* data, size_t size);

#endif
