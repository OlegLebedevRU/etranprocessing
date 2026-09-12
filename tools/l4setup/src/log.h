#pragma once
#include <windows.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void log_init(const wchar_t* dest_dir);
void log_close(void);

void log_info(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_err(const char* fmt, ...);

/**
 * Mask PIN occurrences in text (e.g. "pin=123456", "--pin 123456", "-p 123456").
 * Replaces digits with "******".
 */
void log_mask_pin(const char* input, char* output, size_t output_size);

#ifdef __cplusplus
}
#endif
