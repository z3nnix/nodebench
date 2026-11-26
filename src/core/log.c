#include <stdio.h>
#include <string.h>

#include "core/log.h"

const char* log_level_name(nb_log_level_t level) {
	if (level == NB_LOG_DEBUG) { return "DEBUG"; }
	if (level == NB_LOG_INFO) { return "INFO"; }
	if (level == NB_LOG_WARN) { return "WARN"; }
	if (level == NB_LOG_ERROR) { return "ERROR"; }
	return "UNKNOWN";
}

int log_init(log_t* log, const char* path, nb_log_level_t level) {
	if (!log) { return 0; }

	memset(log, 0, sizeof(*log));
	log->level = level;

	if (path && *path) {
		log->file = fopen(path, "w");
		if (!log->file) { return 0; }
	}

	return 1;
}

void log_shutdown(log_t* log) {
	if (!log) { return; }
	if (log->file) { fclose(log->file); }
	log->file = NULL;
}

void log_write(log_t* log, nb_log_level_t level, const char* format, ...) {
	if (!format) { return; }

	va_list args;
	va_start(args, format);
	log_vwrite(log, level, format, args);
	va_end(args);
}

void log_vwrite(log_t* log, nb_log_level_t level, const char* format, va_list args) {
	if (!log || !format) { return; }
	if (level < NB_LOG_DEBUG || level >= NB_LOG_LEVEL_COUNT) { return; }
	if (level < log->level) { return; }

	vsnprintf(log->last_message, sizeof(log->last_message), format, args);

	fprintf(stderr, "[%s] %s\n", log_level_name(level), log->last_message);

	if (log->file) {
		fprintf(log->file, "[%s] %s\n", log_level_name(level), log->last_message);
		fflush(log->file);
	}

	log->message_count++;
}
