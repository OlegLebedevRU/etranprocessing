#pragma once
#include <windows.h>
#include <stdbool.h>

/**
 * Get the unique and stable hardware fingerprint of the current machine.
 * Combines MachineGuid (HKLM\SOFTWARE\Microsoft\Cryptography) and System Volume Serial Number.
 */
bool hw_get_fingerprint(char* out_fp, size_t out_size);

/**
 * Delete any terminal certificates (*@leo4.ru, *@forpay.ru) from LocalMachine\MY store.
 * Used during automatic reset when a cloned disk is detected.
 */
bool hw_clean_terminal_certificates(void);
