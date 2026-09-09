#ifndef L4DESK_INPUT_INJECT_H
#define L4DESK_INPUT_INJECT_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

bool input_inject_move(int x, int y, DWORD* out_error);
bool input_inject_click(int x, int y, DWORD* out_error);

#endif /* L4DESK_INPUT_INJECT_H */
