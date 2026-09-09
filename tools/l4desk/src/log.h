#ifndef L4DESK_LOG_H
#define L4DESK_LOG_H

#include <stdbool.h>

void log_init(const char* log_file_path, bool verbose, bool console_output);
void log_info(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_error(const char* fmt, ...);
void log_debug(const char* fmt, ...);
void log_close(void);

#endif /* L4DESK_LOG_H */
