#include "audio/audio.h"

#include <stdlib.h>
#include <string.h>

#include "resource/resource.h"

static int audio_clamp(int value) {
	if (value > 32767) {
		return 32767;
	}

	if (value < -32768) {
		return -32768;
	}

	return value;
}

static void audio_callback(void* userdata, Uint8* stream, int length) {
	audio_system_t* audio = (audio_system_t*)userdata;
	int16_t* output = (int16_t*)stream;
	int frames = length / (int)sizeof(int16_t) / 2;
	if (!audio || !audio->ready || frames <= 0) {
		memset(stream, 0, (size_t)length);

		return;
	}

	for (int frame = 0; frame < frames; frame++) {
		int32_t left = 0;
		int32_t right = 0;

		for (int i = 0; i < NB_AUDIO_MAX_VOICES; i++) {
			audio_voice_t* voice = &audio->voices[i];
			if (!voice->active || !voice->samples || voice->frame_count <= 0) {
				continue;
			}

			if (voice->position >= (double)voice->frame_count) {
				if (!voice->loop) {
					voice->active = 0;
					continue;
				}

				voice->position = 0.0;
			}

			int source_frame = (int)voice->position;

			double ratio = (double)voice->sample_rate / (double)audio->spec.freq;
			voice->position += ratio;
			if (voice->position >= (double)voice->frame_count) {
				if (voice->loop) {
					while (voice->position >= (double)voice->frame_count) {
						voice->position -= (double)voice->frame_count;
					}
				}

				else {
					voice->active = 0;
				}
			}

			int sample_index = source_frame * voice->channels;
			int mixed = (int)(voice->samples[sample_index] * voice->volume);
			left += mixed;

			if (voice->channels > 1) {
				mixed = (int)(voice->samples[sample_index + 1] * voice->volume);
			}

			right += mixed;
		}

		output[frame * 2] = (int16_t)audio_clamp(left);
		output[frame * 2 + 1] = (int16_t)audio_clamp(right);
	}
}

int audio_init(audio_system_t* audio) {
	if (!audio) {
		return 0;
	}

	memset(audio, 0, sizeof(*audio));

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		return 0;
	}

	SDL_AudioSpec desired;
	memset(&desired, 0, sizeof(desired));
	desired.freq = NB_AUDIO_SAMPLE_RATE;
	desired.format = AUDIO_S16SYS;
	desired.channels = 2;
	desired.samples = 512;
	desired.callback = audio_callback;
	desired.userdata = audio;

	audio->device = SDL_OpenAudioDevice(NULL, 0, &desired, &audio->spec, 0);
	if (!audio->device) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO);

		return 0;
	}

	audio->ready = 1;
	SDL_PauseAudioDevice(audio->device, 0);

	return 1;
}

void audio_shutdown(audio_system_t* audio) {
	if (!audio || !audio->ready) {
		return;
	}

	audio_stop_all(audio);
	SDL_CloseAudioDevice(audio->device);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	memset(audio, 0, sizeof(*audio));
}

int audio_play(audio_system_t* audio, resource_store_t* store, audio_id id, float volume) {
	return audio_play_ex(audio, store, id, volume, 0);
}

static int audio_play_stream_internal(audio_system_t* audio, const audio_stream_t* stream, float volume, int loop, audio_id id) {
	if (!audio || !audio->ready || !stream || !stream->samples || stream->frame_count <= 0 || (stream->channels != 1 && stream->channels != 2)) {
		return -1;
	}

	size_t bytes = (size_t)stream->frame_count * (size_t)stream->channels * sizeof(int16_t);
	int16_t* copy = (int16_t*)malloc(bytes);
	if (!copy) {
		return -1;
	}

	memcpy(copy, stream->samples, bytes);

	if (volume < 0.0f) {
		volume = 0.0f;
	}

	else if (volume > 1.0f) {
		volume = 1.0f;
	}

	SDL_LockAudioDevice(audio->device);
	int slot = -1;
	for (int i = 0; i < NB_AUDIO_MAX_VOICES; i++) {
		if (!audio->voices[i].active) {
			slot = i;
			break;
		}
	}

	if (slot < 0) {
		slot = 0;
		free(audio->voices[slot].samples);
	}

	audio->voices[slot].samples = copy;
	audio->voices[slot].frame_count = stream->frame_count;
	audio->voices[slot].channels = stream->channels;
	audio->voices[slot].sample_rate = stream->sample_rate;
	audio->voices[slot].position = 0.0;
	audio->voices[slot].volume = volume;
	audio->voices[slot].active = 1;
	audio->voices[slot].loop = loop ? 1 : 0;
	audio->voices[slot].id = id;
	SDL_UnlockAudioDevice(audio->device);

	return slot;
}

int audio_play_ex(audio_system_t* audio, resource_store_t* store, audio_id id, float volume, int loop) {
	if (!audio || !audio->ready || !store) {
		return 0;
	}

	audio_stream_t* stream = resource_get_audio(store, id);
	if (!stream) {
		return 0;
	}

	return audio_play_stream_internal(audio, stream, volume, loop, id) >= 0 ? 1 : 0;
}

int audio_play_stream(audio_system_t* audio, const audio_stream_t* stream, float volume, int loop) {
	return audio_play_stream_internal(audio, stream, volume, loop, NB_RESOURCE_INVALID_ID);
}

void audio_stop_voice(audio_system_t* audio, int voice) {
	if (!audio || !audio->ready || voice < 0 || voice >= NB_AUDIO_MAX_VOICES) {
		return;
	}

	SDL_LockAudioDevice(audio->device);
	audio_voice_t* slot = &audio->voices[voice];
	slot->active = 0;
	free(slot->samples);
	slot->samples = NULL;
	SDL_UnlockAudioDevice(audio->device);
}

int audio_stop(audio_system_t* audio, resource_store_t* store, const char* name) {
	if (!audio || !audio->ready || !store || !name) {
		return 0;
	}

	audio_id id = resource_find_audio(store, name);
	if (id == NB_RESOURCE_INVALID_ID) {
		return 0;
	}

	int stopped = 0;
	SDL_LockAudioDevice(audio->device);
	for (int i = 0; i < NB_AUDIO_MAX_VOICES; i++) {
		audio_voice_t* voice = &audio->voices[i];
		if (!voice->active || voice->id != id) {
			continue;
		}

		voice->active = 0;
		free(voice->samples);
		voice->samples = NULL;
		stopped = 1;
	}
	SDL_UnlockAudioDevice(audio->device);

	return stopped;
}

int audio_set_volume(audio_system_t* audio, resource_store_t* store, const char* name, float volume) {
	if (!audio || !audio->ready || !store || !name) {
		return 0;
	}

	audio_id id = resource_find_audio(store, name);
	if (id == NB_RESOURCE_INVALID_ID) {
		return 0;
	}

	if (volume < 0.0f) {
		volume = 0.0f;
	}

	else if (volume > 1.0f) {
		volume = 1.0f;
	}

	int updated = 0;
	SDL_LockAudioDevice(audio->device);
	for (int i = 0; i < NB_AUDIO_MAX_VOICES; i++) {
		audio_voice_t* voice = &audio->voices[i];
		if (!voice->active || voice->id != id) {
			continue;
		}

		voice->volume = volume;
		updated = 1;
	}
	SDL_UnlockAudioDevice(audio->device);

	return updated;
}

void audio_stop_all(audio_system_t* audio) {
	if (!audio || !audio->ready) {
		return;
	}

	SDL_LockAudioDevice(audio->device);
	for (int i = 0; i < NB_AUDIO_MAX_VOICES; i++) {
		audio->voices[i].active = 0;
		free(audio->voices[i].samples);
		audio->voices[i].samples = NULL;
	}
	SDL_UnlockAudioDevice(audio->device);
}
