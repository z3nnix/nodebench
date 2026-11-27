#include <stdio.h>
#include <string.h>

#include "audio/audio.h"
#include "resource/resource.h"

static int test_write_wav(const char* path) {
	FILE* file = fopen(path, "wb");
	if (!file) {
		return 0;
	}

	const unsigned char bytes[] = {
		'R', 'I', 'F', 'F', 76, 0, 0, 0, 'W', 'A', 'V', 'E',
		'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x22, 0x56, 0, 0,
		0x44, 0xac, 0x00, 0x00, 2, 0, 16, 0,
		'd', 'a', 't', 'a', 40, 0, 0, 0,
		0, 0, 0x00, 0x20, 0, 0, 0x40, 0x60,
		0, 0, 0x80, 0xa0, 0, 0, 0xff, 0x7f,
		0, 0, 0x00, 0x80, 0, 0, 0x00, 0x20,
		0, 0, 0x40, 0x60, 0, 0, 0x80, 0xa0,
		0, 0, 0xff, 0x7f, 0, 0, 0x00, 0x80
	};

	int ok = fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
	fclose(file);

	return ok;
}

int main(void) {
	const char* path = "build/test_audio.wav";
	if (!test_write_wav(path)) {
		return 1;
	}

	audio_stream_t stream;
	if (!wav_load(path, &stream)) {
		return 1;
	}

	if (stream.sample_rate != 22050 || stream.channels != 1 || stream.frame_count != 20 || !stream.samples) {
		audio_stream_free(&stream);
		return 1;
	}

	resource_store_t store;
	resource_store_init(&store, 1048576);
	audio_id id = resource_add_audio(&store, &stream, "hit");
	audio_stream_free(&stream);
	if (id == NB_RESOURCE_INVALID_ID || resource_find_audio(&store, "hit") != id || !resource_get_audio(&store, id)) {
		resource_store_shutdown(&store);
		return 1;
	}

	audio_system_t audio;
	if (!audio_init(&audio)) {
		resource_store_shutdown(&store);
		printf("audio test ok (device unavailable)\n");
		return 0;
	}

	if (!audio_play(&audio, &store, id, 1.0f) || !audio_play_ex(&audio, &store, id, 0.5f, 1)) {
		audio_shutdown(&audio);
		resource_store_shutdown(&store);
		return 1;
	}

	if (!audio_set_volume(&audio, &store, "hit", 0.25f) || !audio_stop(&audio, &store, "hit") || audio_stop(&audio, &store, "hit")) {
		audio_shutdown(&audio);
		resource_store_shutdown(&store);
		return 1;
	}

	audio_stop_all(&audio);
	audio_shutdown(&audio);
	resource_store_shutdown(&store);
	remove(path);
	printf("audio test ok\n");

	return 0;
}
