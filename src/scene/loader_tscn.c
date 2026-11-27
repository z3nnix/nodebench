#include "scene/loader_tscn.h"

#include "scene/tscn.h"

nb_result_t scene_load_tscn(const char* path, engine_t* engine, scene_t* scene) {
	if (!path || !engine || !scene) {
		return NB_RESULT_ERROR_INVALID_ARGUMENT;
	}

	tscn_file_t file;
	nb_result_t result = tscn_file_load(&file, path, engine);
	if (result != NB_RESULT_OK) {
		return result;
	}

	result = tscn_file_build_scene(&file, engine, scene);
	tscn_file_free(&file);

	if (result != NB_RESULT_OK) {
		scene_shutdown(scene);
	}

	return result;
}
