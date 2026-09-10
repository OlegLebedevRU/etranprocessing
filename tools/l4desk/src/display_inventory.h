#ifndef L4DESK_DISPLAY_INVENTORY_H
#define L4DESK_DISPLAY_INVENTORY_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MAX_DISPLAYS 16
#define MAX_CAMERAS 16
#define MAX_PROFILES 8

typedef struct {
    char desktop_id[32];   /* "disp:<8 hex>" */
    char name[64];         /* "\\.\DISPLAY1" */
    bool primary;
    int x;
    int y;
    int width;
    int height;
    DWORD session_id;
    char policy[16];       /* "input", "view", "denied" */
} DisplayInfo;

typedef struct {
    char camera_id[32];    /* "cam:<8 hex>" */
    char name[128];        /* FriendlyName */
    char device_path[256]; /* DirectShow DevicePath (@device:pnp:...) */
    bool available;
} CameraInfo;

typedef struct {
    DisplayInfo displays[MAX_DISPLAYS];
    int display_count;
    CameraInfo cameras[MAX_CAMERAS];
    int camera_count;
    char allowed_profiles[MAX_PROFILES][32];
    int profile_count;
    uint32_t hash;
} SystemInventory;

/* FNV-1a 32-bit utilities */
uint32_t fnv1a_32_buf(const void* buf, size_t len);
uint32_t fnv1a_32_str(const char* str);

/* Inventory management */
void inventory_init(const char* base_path);
bool inventory_refresh(const char* base_path, SystemInventory* inv);
bool inventory_find_display(const SystemInventory* inv, const char* desktop_id, DisplayInfo* out_disp);
bool inventory_find_camera(const SystemInventory* inv, const char* camera_id, CameraInfo* out_cam);
bool inventory_is_profile_allowed(const SystemInventory* inv, const char* profile);
uint32_t inventory_compute_hash(const SystemInventory* inv);

#endif /* L4DESK_DISPLAY_INVENTORY_H */
