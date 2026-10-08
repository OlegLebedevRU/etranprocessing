#pragma once
#include <windows.h>
#include <stdbool.h>
/* Separate strict fresh grammar, before legacy state/log/elevation discovery.
 * Explicit install is the only path that mutates the four suite services. */
bool setup_install_entry(int argc,wchar_t** argv,DWORD* result);
