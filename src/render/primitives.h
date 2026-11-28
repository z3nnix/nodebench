#ifndef NB_RENDER_PRIMITIVES_H
#define NB_RENDER_PRIMITIVES_H

#include "render/mesh.h"

#define NB_MESH_PRIMITIVE_MAX 5

const char* mesh_primitive_name(int index);
const char* mesh_primitive_label(int index);
int mesh_primitive_count(void);
int mesh_create_primitive(const char* name, mesh_t* mesh);

#endif
