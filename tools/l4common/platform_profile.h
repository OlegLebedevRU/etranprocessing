#pragma once
#include <windows.h>
#include <stdbool.h>
#define L4_PLATFORM_PROFILE_SIZE 64u
typedef struct {DWORD major,minor,build;WORD native_arch;BYTE product_type;} L4PlatformFacts;
/* Read-only OS facts. No environment/registry/manifest-sensitive fallback.
 * Native machine architecture is independent of the selected suite architecture. */
bool l4_platform_read(L4PlatformFacts* facts);
/* Codec for trusted producer facts and isolated fixtures, not runtime authority.
 * A profile identifies a platform; it never asserts compatibility or admission. */
bool l4_platform_format(const L4PlatformFacts* facts,char profile[L4_PLATFORM_PROFILE_SIZE]);
/* Production selector: exact NT build/native machine/client class, not an RPC
 * parameter or downloaded alias. A matching signed transition is still required. */
bool l4_platform_current(char profile[L4_PLATFORM_PROFILE_SIZE]);
