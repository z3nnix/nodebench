#ifndef NODEBENCH_AUDIO_AUDIO_H
#define NODEBENCH_AUDIO_AUDIO_H

#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdint.h>

#include "resource/resource_id.h"

#define NB_AUDIO_SAMPLE_RATE 22050
#define NB_AUDIO_MAX_VOICES 16
#define NB_AUDIO_MAX_NAME 64

typedef struct {
	uint16_t sample_rate;
	uint8_t channels;
	int16_t* samples;
	int frame_count;
} audio_stream_t;

typedef struct {
	int16_t* samples;
	int frame_count;
	int channels;
	uint16_t sample_rate;
	double position;
	float volume;
	int active;
	int loop;
	audio_id id;
} audio_voice_t;

typedef struct {
	SDL_AudioDeviceID device;
	SDL_AudioSpec spec;
	audio_voice_t voices[NB_AUDIO_MAX_VOICES];
	int ready;
} audio_system_t;

typedef struct resource_store_t resource_store_t;

int wav_load(const char* path, audio_stream_t* stream);
int wav_load_mem(const void* data, size_t size, audio_stream_t* stream);
void audio_stream_free(audio_stream_t* stream);

int audio_init(audio_system_t* audio);
void audio_shutdown(audio_system_t* audio);
int audio_play(audio_system_t* audio, resource_store_t* store, audio_id id, float volume);
int audio_play_ex(audio_system_t* audio, resource_store_t* store, audio_id id, float volume, int loop);
int audio_play_stream(audio_system_t* audio, const audio_stream_t* stream, float volume, int loop);
void audio_stop_voice(audio_system_t* audio, int voice);
int audio_stop(audio_system_t* audio, resource_store_t* store, const char* name);
int audio_set_volume(audio_system_t* audio, resource_store_t* store, const char* name, float volume);
void audio_stop_all(audio_system_t* audio);

#endif
