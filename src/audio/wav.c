#include "audio/audio.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t wav_read_u16(const uint8_t* bytes) {
	return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t wav_read_u32(const uint8_t* bytes) {
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

int wav_load(const char* path, audio_stream_t* stream) {
	if (!path || !stream) {
		return 0;
	}

	memset(stream, 0, sizeof(*stream));

	FILE* file = fopen(path, "rb");
	if (!file) {
		return 0;
	}

	uint8_t header[12];
	if (fread(header, 1, sizeof(header), file) != sizeof(header) || memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
		fclose(file);

		return 0;
	}

	uint16_t audio_format = 0;
	uint16_t channels = 0;
	uint32_t sample_rate = 0;
	uint16_t bits_per_sample = 0;
	uint8_t* data = NULL;
	uint32_t data_size = 0;
	uint8_t chunk_header[8];

	while (fread(chunk_header, 1, sizeof(chunk_header), file) == sizeof(chunk_header)) {
		uint32_t chunk_size = wav_read_u32(chunk_header + 4);
		if (memcmp(chunk_header, "fmt ", 4) == 0 && chunk_size >= 16) {
			uint8_t format[16];
			if (fread(format, 1, sizeof(format), file) != sizeof(format)) {
				free(data);
				fclose(file);

				return 0;
			}

			audio_format = wav_read_u16(format);
			channels = wav_read_u16(format + 2);
			sample_rate = wav_read_u32(format + 4);
			bits_per_sample = wav_read_u16(format + 14);
			if (chunk_size > 16 && fseek(file, (long)(chunk_size - 16), SEEK_CUR) != 0) {
				free(data);
				fclose(file);

				return 0;
			}
		}

		else if (memcmp(chunk_header, "data", 4) == 0) {
			if (chunk_size == 0 || chunk_size > 8u * 1024u * 1024u) {
				free(data);
				fclose(file);

				return 0;
			}

			data = (uint8_t*)malloc(chunk_size);
			if (!data || fread(data, 1, chunk_size, file) != chunk_size) {
				free(data);
				fclose(file);

				return 0;
			}

			data_size = chunk_size;
			if (chunk_size % 2 != 0 && fgetc(file) == EOF) {
				free(data);
				fclose(file);

				return 0;
			}
		}

		else if (fseek(file, (long)(chunk_size + (chunk_size % 2)), SEEK_CUR) != 0) {
			free(data);
			fclose(file);

			return 0;
		}
	}

	fclose(file);

	if (audio_format != 1 || (channels != 1 && channels != 2) || sample_rate == 0 || !data || (bits_per_sample != 8 && bits_per_sample != 16)) {
		free(data);

		return 0;
	}

	int bytes_per_sample = bits_per_sample / 8;
	int frame_count = (int)(data_size / ((uint32_t)bytes_per_sample * channels));
	if (frame_count <= 0) {
		free(data);

		return 0;
	}

	int16_t* samples = (int16_t*)malloc((size_t)frame_count * (size_t)channels * sizeof(int16_t));
	if (!samples) {
		free(data);

		return 0;
	}

	for (int frame = 0; frame < frame_count; frame++) {
		for (int channel = 0; channel < channels; channel++) {
			size_t offset = (size_t)(frame * channels + channel) * (size_t)bytes_per_sample;
			int value = 0;
			if (bits_per_sample == 8) {
				value = ((int)data[offset] - 128) << 8;
			}

			else {
				value = (int)(int16_t)wav_read_u16(data + offset);
			}

			samples[frame * channels + channel] = (int16_t)value;
		}
	}

	free(data);
	stream->sample_rate = (uint16_t)sample_rate;
	stream->channels = (uint8_t)channels;
	stream->samples = samples;
	stream->frame_count = frame_count;

	return 1;
}

void audio_stream_free(audio_stream_t* stream) {
	if (!stream) {
		return;
	}

	free(stream->samples);
	memset(stream, 0, sizeof(*stream));
}
