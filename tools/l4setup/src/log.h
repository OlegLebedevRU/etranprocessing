#pragma once
#include <windows.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void log_init(const wchar_t* dest_dir);
void log_close(void);
void log_get_path(wchar_t* out_path, size_t out_size);

void log_info(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_err(const char* fmt, ...);

typedef void (*LogCallback)(const char* line, void* user_data);
void log_set_callback(LogCallback cb, void* user_data);

/**
 * Mask PIN occurrences in text (e.g. "pin=123456", "--pin 123456", "-p 123456").
 * Replaces digits with "******".
 */
void log_mask_pin(const char* input, char* output, size_t output_size);

#ifdef __cplusplus
}
#endif
