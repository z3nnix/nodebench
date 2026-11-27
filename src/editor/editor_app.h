#ifndef NB_EDITOR_EDITOR_APP_H
#define NB_EDITOR_EDITOR_APP_H

#include "core/engine.h"

void editor_app_register(engine_t* engine);
nb_result_t editor_app_open_scene(engine_t* engine, const char* path);

#endif
