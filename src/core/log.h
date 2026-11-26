#ifndef NB_CORE_LOG_H
#define NB_CORE_LOG_H

#include <stdarg.h>
#include <stdio.h>

#define NB_LOG_MESSAGE_CAPACITY 512

typedef enum {
	NB_LOG_DEBUG,
	NB_LOG_INFO,
	NB_LOG_WARN,
	NB_LOG_ERROR,
	NB_LOG_LEVEL_COUNT
} nb_log_level_t;

typedef struct {
	nb_log_level_t level;
	FILE* file;
	char last_message[NB_LOG_MESSAGE_CAPACITY];
	int message_count;
} log_t;

int log_init(log_t* log, const char* path, nb_log_level_t level);
void log_shutdown(log_t* log);
void log_write(log_t* log, nb_log_level_t level, const char* format, ...);
void log_vwrite(log_t* log, nb_log_level_t level, const char* format, va_list args);
const char* log_level_name(nb_log_level_t level);

#endif
