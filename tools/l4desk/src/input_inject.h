#ifndef L4DESK_INPUT_INJECT_H
#define L4DESK_INPUT_INJECT_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

/* Coordinate mapping from normalized [0.0, 1.0] frame coords to virtual desktop [0..65535] */
void input_map_coordinates_custom(double nx, double ny,
                                 int rect_x, int rect_y, int rect_w, int rect_h,
                                 int virt_x, int virt_y, int virt_w, int virt_h,
                                 int* out_norm_x, int* out_norm_y);

void input_map_coordinates(double nx, double ny,
                           int rect_x, int rect_y, int rect_w, int rect_h,
                           int* out_norm_x, int* out_norm_y);

/* Mouse injection */
bool input_inject_move_norm(double nx, double ny,
                            int rect_x, int rect_y, int rect_w, int rect_h,
                            DWORD* out_error);

bool input_inject_click_norm(double nx, double ny, const char* button,
                             int rect_x, int rect_y, int rect_w, int rect_h,
                             DWORD* out_error);

/* Legacy raw coordinate helpers */
bool input_inject_move(int x, int y, DWORD* out_error);
bool input_inject_click(int x, int y, DWORD* out_error);

/* Keyboard injection & whitelist */
bool input_is_vk_allowed(int vk);
bool input_inject_key(const char* kind, int vk, const char* text, DWORD* out_error);

/* Release all injected/active keys and mouse buttons */
void input_release_all(void);

#endif /* L4DESK_INPUT_INJECT_H */
