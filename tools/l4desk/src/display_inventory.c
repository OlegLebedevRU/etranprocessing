#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "display_inventory.h"
#include "desktop_state.h"
#include "log.h"
#include <windows.h>
#include <objbase.h>
#include <strmif.h>
#include <olectl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "user32.lib")

static const GUID kCLSID_SystemDeviceEnum = 
    { 0x62be5d10, 0x60eb, 0x11d0, { 0xbd, 0x3b, 0x00, 0xa0, 0xc9, 0x11, 0xce, 0x86 } };
static const GUID kIID_ICreateDevEnum = 
    { 0x29840822, 0x5b84, 0x11d0, { 0xbd, 0x3b, 0x00, 0xa0, 0xc9, 0x11, 0xce, 0x86 } };
static const GUID kCLSID_VideoInputDeviceCategory = 
    { 0x860bb310, 0x5d01, 0x11d0, { 0xbd, 0x3b, 0x00, 0xa0, 0xc9, 0x11, 0xce, 0x86 } };
static const GUID kIID_IPropertyBag = 
    { 0x55272a00, 0x42cb, 0x11ce, { 0x81, 0x35, 0x00, 0xaa, 0x00, 0x4b, 0xb8, 0x51 } };

uint32_t fnv1a_32_buf(const void* buf, size_t len) {
    if (!buf || len == 0) return 0;
    uint32_t hash = 2166136261u;
    const uint8_t* p = (const uint8_t*)buf;
    for (size_t i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }
    return hash;
}

uint32_t fnv1a_32_str(const char* str) {
    if (!str) return 0;
    return fnv1a_32_buf(str, strlen(str));
}

typedef struct {
    DisplayInfo* displays;
    int max_displays;
    int count;
    DWORD session_id;
} MonitorContext;

static BOOL CALLBACK monitor_enum_proc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    (void)hdcMonitor;
    (void)lprcMonitor;
    MonitorContext* ctx = (MonitorContext*)dwData;
    if (ctx->count >= ctx->max_displays) return TRUE;

    MONITORINFOEXW mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMonitor, (LPMONITORINFO)&mi)) {
        return TRUE;
    }

    DisplayInfo* disp = &ctx->displays[ctx->count];
    ZeroMemory(disp, sizeof(DisplayInfo));

    disp->x = mi.rcMonitor.left;
    disp->y = mi.rcMonitor.top;
    disp->width = mi.rcMonitor.right - mi.rcMonitor.left;
    disp->height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    disp->primary = ((mi.dwFlags & MONITORINFOF_PRIMARY) != 0);
    disp->session_id = ctx->session_id;

    WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, disp->name, sizeof(disp->name), NULL, NULL);

    DISPLAY_DEVICEW dd;
    ZeroMemory(&dd, sizeof(dd));
    dd.cb = sizeof(dd);

    char dev_id_utf8[256] = { 0 };
    /* Try with EDD_GET_DEVICE_INTERFACE_NAME (1) */
    if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 1) && dd.DeviceID[0] != L'\0') {
        WideCharToMultiByte(CP_UTF8, 0, dd.DeviceID, -1, dev_id_utf8, sizeof(dev_id_utf8), NULL, NULL);
    } else if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0) && dd.DeviceID[0] != L'\0') {
        WideCharToMultiByte(CP_UTF8, 0, dd.DeviceID, -1, dev_id_utf8, sizeof(dev_id_utf8), NULL, NULL);
    }

    uint32_t hash = 0;
    if (dev_id_utf8[0] != '\0') {
        hash = fnv1a_32_str(dev_id_utf8);
    } else {
        char dev_str_utf8[256] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, dd.DeviceString, -1, dev_str_utf8, sizeof(dev_str_utf8), NULL, NULL);
        char fallback[512];
        snprintf(fallback, sizeof(fallback), "%s|%s|%d|%d|%d|%d",
                 disp->name, dev_str_utf8, disp->x, disp->y, disp->width, disp->height);
        hash = fnv1a_32_str(fallback);
    }

    snprintf(disp->desktop_id, sizeof(disp->desktop_id), "disp:%08x", hash);
    strcpy_s(disp->policy, sizeof(disp->policy), "input");

    ctx->count++;
    return TRUE;
}

static void enumerate_directshow_cameras(CameraInfo* cameras, int max_cameras, int* out_count) {
    *out_count = 0;
    HRESULT hr = CoInitialize(NULL);
    bool co_inited = SUCCEEDED(hr);

    ICreateDevEnum* pDevEnum = NULL;
    hr = CoCreateInstance(&kCLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
                          &kIID_ICreateDevEnum, (void**)&pDevEnum);
    if (SUCCEEDED(hr) && pDevEnum) {
        IEnumMoniker* pEnum = NULL;
        hr = pDevEnum->lpVtbl->CreateClassEnumerator(pDevEnum, &kCLSID_VideoInputDeviceCategory, &pEnum, 0);
        if (hr == S_OK && pEnum) {
            IMoniker* pMoniker = NULL;
            while (pEnum->lpVtbl->Next(pEnum, 1, &pMoniker, NULL) == S_OK && *out_count < max_cameras) {
                IPropertyBag* pPropBag = NULL;
                hr = pMoniker->lpVtbl->BindToStorage(pMoniker, 0, 0, &kIID_IPropertyBag, (void**)&pPropBag);
                if (SUCCEEDED(hr) && pPropBag) {
                    CameraInfo* cam = &cameras[*out_count];
                    ZeroMemory(cam, sizeof(CameraInfo));
                    cam->available = true;

                    VARIANT varName;
                    VariantInit(&varName);
                    hr = pPropBag->lpVtbl->Read(pPropBag, L"FriendlyName", &varName, NULL);
                    if (SUCCEEDED(hr) && varName.vt == VT_BSTR && varName.bstrVal) {
                        WideCharToMultiByte(CP_UTF8, 0, varName.bstrVal, -1, cam->name, sizeof(cam->name), NULL, NULL);
                    } else {
                        strcpy_s(cam->name, sizeof(cam->name), "USB Video Device");
                    }
                    VariantClear(&varName);

                    VARIANT varPath;
                    VariantInit(&varPath);
                    hr = pPropBag->lpVtbl->Read(pPropBag, L"DevicePath", &varPath, NULL);
                    if (SUCCEEDED(hr) && varPath.vt == VT_BSTR && varPath.bstrVal) {
                        WideCharToMultiByte(CP_UTF8, 0, varPath.bstrVal, -1, cam->device_path, sizeof(cam->device_path), NULL, NULL);
                    }
                    VariantClear(&varPath);

                    uint32_t hash = 0;
                    if (cam->device_path[0] != '\0') {
                        hash = fnv1a_32_str(cam->device_path);
                    } else {
                        hash = fnv1a_32_str(cam->name);
                    }
                    snprintf(cam->camera_id, sizeof(cam->camera_id), "cam:%08x", hash);

                    pPropBag->lpVtbl->Release(pPropBag);
                    (*out_count)++;
                }
                pMoniker->lpVtbl->Release(pMoniker);
            }
            pEnum->lpVtbl->Release(pEnum);
        }
        pDevEnum->lpVtbl->Release(pDevEnum);
    }

    if (co_inited) {
        CoUninitialize();
    }
}

static bool token_in_list(const char* list, const char* token) {
    if (!list || !token) return false;
    if (strcmp(list, "*") == 0) return true;
    size_t tlen = strlen(token);
    const char* p = list;
    while ((p = strstr(p, token)) != NULL) {
        bool left_ok = (p == list || *(p - 1) == ',' || *(p - 1) == ';' || isspace((unsigned char)*(p - 1)));
        const char* end = p + tlen;
        bool right_ok = (*end == '\0' || *end == ',' || *end == ';' || isspace((unsigned char)*end));
        if (left_ok && right_ok) {
            return true;
        }
        p = end;
    }
    return false;
}

static void apply_policy(const char* base_path, SystemInventory* inv) {
    char ini_path[MAX_PATH];
    snprintf(ini_path, sizeof(ini_path), "%s\\l4desk\\l4desk_policy.ini",
             (base_path && base_path[0]) ? base_path : "C:\\l4tools");

    /* Default profiles */
    strcpy_s(inv->allowed_profiles[0], sizeof(inv->allowed_profiles[0]), "default");
    strcpy_s(inv->allowed_profiles[1], sizeof(inv->allowed_profiles[1]), "low");
    inv->profile_count = 2;

    if (GetFileAttributesA(ini_path) == INVALID_FILE_ATTRIBUTES) {
        return;
    }

    /* 1. Displays policy */
    char disp_deny[512] = { 0 };
    char disp_view[512] = { 0 };
    char disp_allow[512] = { 0 };
    GetPrivateProfileStringA("displays", "deny", "", disp_deny, sizeof(disp_deny), ini_path);
    GetPrivateProfileStringA("displays", "view_only", "", disp_view, sizeof(disp_view), ini_path);
    GetPrivateProfileStringA("displays", "allow", "*", disp_allow, sizeof(disp_allow), ini_path);

    for (int i = 0; i < inv->display_count; i++) {
        DisplayInfo* d = &inv->displays[i];
        if (disp_deny[0] != '\0' && token_in_list(disp_deny, d->desktop_id)) {
            strcpy_s(d->policy, sizeof(d->policy), "denied");
        } else if (disp_view[0] != '\0' && token_in_list(disp_view, d->desktop_id)) {
            strcpy_s(d->policy, sizeof(d->policy), "view");
        } else if (token_in_list(disp_allow, d->desktop_id)) {
            strcpy_s(d->policy, sizeof(d->policy), "input");
        } else {
            strcpy_s(d->policy, sizeof(d->policy), "denied");
        }
    }

    /* 2. Cameras policy */
    char cam_deny[512] = { 0 };
    GetPrivateProfileStringA("cameras", "deny", "", cam_deny, sizeof(cam_deny), ini_path);
    if (cam_deny[0] != '\0') {
        for (int i = 0; i < inv->camera_count; i++) {
            if (token_in_list(cam_deny, inv->cameras[i].camera_id)) {
                inv->cameras[i].available = false;
            }
        }
    }

    /* 3. Profiles policy */
    char prof_buf[256] = { 0 };
    GetPrivateProfileStringA("profiles", "allow", "", prof_buf, sizeof(prof_buf), ini_path);
    if (prof_buf[0] != '\0') {
        inv->profile_count = 0;
        char* next_tok = NULL;
        char* tok = strtok_s(prof_buf, ", ;", &next_tok);
        while (tok && inv->profile_count < MAX_PROFILES) {
            strcpy_s(inv->allowed_profiles[inv->profile_count++],
                     sizeof(inv->allowed_profiles[0]), tok);
            tok = strtok_s(NULL, ", ;", &next_tok);
        }
    }
}

void inventory_init(const char* base_path) {
    (void)base_path;
}

bool inventory_refresh(const char* base_path, SystemInventory* inv) {
    if (!inv) return false;
    memset(inv, 0, sizeof(SystemInventory));

    DWORD session_id = desktop_get_current_session_id();

    MonitorContext ctx;
    ctx.displays = inv->displays;
    ctx.max_displays = MAX_DISPLAYS;
    ctx.count = 0;
    ctx.session_id = session_id;

    EnumDisplayMonitors(NULL, NULL, monitor_enum_proc, (LPARAM)&ctx);
    inv->display_count = ctx.count;

    /* Fallback if no monitors returned */
    if (inv->display_count == 0) {
        DisplayInfo* d = &inv->displays[0];
        ScreenMetrics sm;
        desktop_get_screen_metrics(&sm);
        d->x = sm.virtual_x;
        d->y = sm.virtual_y;
        d->width = sm.virtual_width;
        d->height = sm.virtual_height;
        d->primary = true;
        d->session_id = session_id;
        strcpy_s(d->name, sizeof(d->name), "\\\\.\\DISPLAY1");
        strcpy_s(d->policy, sizeof(d->policy), "input");
        uint32_t hash = fnv1a_32_str(d->name);
        snprintf(d->desktop_id, sizeof(d->desktop_id), "disp:%08x", hash);
        inv->display_count = 1;
    }

    enumerate_directshow_cameras(inv->cameras, MAX_CAMERAS, &inv->camera_count);

    apply_policy(base_path, inv);
    inv->hash = inventory_compute_hash(inv);
    return true;
}

bool inventory_find_display(const SystemInventory* inv, const char* desktop_id, DisplayInfo* out_disp) {
    if (!inv || !desktop_id) return false;

    /* 1. Exact match by desktop_id (e.g. disp:3580dc5e) */
    for (int i = 0; i < inv->display_count; i++) {
        if (_stricmp(inv->displays[i].desktop_id, desktop_id) == 0) {
            if (out_disp) *out_disp = inv->displays[i];
            return true;
        }
    }

    /* 2. Match by display device name (e.g. \\.\DISPLAY1 or DISPLAY1) */
    for (int i = 0; i < inv->display_count; i++) {
        if (_stricmp(inv->displays[i].name, desktop_id) == 0) {
            if (out_disp) *out_disp = inv->displays[i];
            return true;
        }
        const char* short_name = strrchr(inv->displays[i].name, '\\');
        if (short_name && _stricmp(short_name + 1, desktop_id) == 0) {
            if (out_disp) *out_disp = inv->displays[i];
            return true;
        }
    }

    /* 3. Match hash part without "disp:" prefix */
    for (int i = 0; i < inv->display_count; i++) {
        if (strncmp(inv->displays[i].desktop_id, "disp:", 5) == 0 &&
            _stricmp(inv->displays[i].desktop_id + 5, desktop_id) == 0) {
            if (out_disp) *out_disp = inv->displays[i];
            return true;
        }
    }

    /* 4. Fallback for generic identifiers ("disp", "desktop", "0", "primary", "default") -> primary display */
    if (_stricmp(desktop_id, "disp") == 0 ||
        _stricmp(desktop_id, "desktop") == 0 ||
        strcmp(desktop_id, "0") == 0 ||
        _stricmp(desktop_id, "primary") == 0 ||
        _stricmp(desktop_id, "default") == 0) {
        if (inv->display_count > 0) {
            int target_idx = 0;
            for (int i = 0; i < inv->display_count; i++) {
                if (inv->displays[i].primary) {
                    target_idx = i;
                    break;
                }
            }
            if (out_disp) *out_disp = inv->displays[target_idx];
            return true;
        }
    }

    return false;
}

bool inventory_find_camera(const SystemInventory* inv, const char* camera_id, CameraInfo* out_cam) {
    if (!inv || !camera_id) return false;

    /* 1. Exact match by camera_id (e.g. cam:7933044a) */
    for (int i = 0; i < inv->camera_count; i++) {
        if (_stricmp(inv->cameras[i].camera_id, camera_id) == 0) {
            if (out_cam) *out_cam = inv->cameras[i];
            return true;
        }
    }

    /* 2. Match by camera name */
    for (int i = 0; i < inv->camera_count; i++) {
        if (_stricmp(inv->cameras[i].name, camera_id) == 0) {
            if (out_cam) *out_cam = inv->cameras[i];
            return true;
        }
    }

    /* 3. Match hash part without "cam:" prefix */
    for (int i = 0; i < inv->camera_count; i++) {
        if (strncmp(inv->cameras[i].camera_id, "cam:", 4) == 0 &&
            _stricmp(inv->cameras[i].camera_id + 4, camera_id) == 0) {
            if (out_cam) *out_cam = inv->cameras[i];
            return true;
        }
    }

    /* 4. Fallback for generic identifiers ("cam", "camera", "usb-camera", "0", "default") -> first available */
    if (_stricmp(camera_id, "cam") == 0 ||
        _stricmp(camera_id, "camera") == 0 ||
        _stricmp(camera_id, "usb-camera") == 0 ||
        strcmp(camera_id, "0") == 0 ||
        _stricmp(camera_id, "default") == 0) {
        if (inv->camera_count > 0) {
            int target_idx = 0;
            for (int i = 0; i < inv->camera_count; i++) {
                if (inv->cameras[i].available) {
                    target_idx = i;
                    break;
                }
            }
            if (out_cam) *out_cam = inv->cameras[target_idx];
            return true;
        }
    }

    return false;
}

bool inventory_is_profile_allowed(const SystemInventory* inv, const char* profile) {
    if (!inv) return false;
    if (!profile || profile[0] == '\0' || _stricmp(profile, "default") == 0) {
        for (int i = 0; i < inv->profile_count; i++) {
            if (_stricmp(inv->allowed_profiles[i], "default") == 0) return true;
        }
        return (inv->profile_count == 0);
    }
    for (int i = 0; i < inv->profile_count; i++) {
        if (_stricmp(inv->allowed_profiles[i], profile) == 0) {
            return true;
        }
    }
    return false;
}

uint32_t inventory_compute_hash(const SystemInventory* inv) {
    if (!inv) return 0;
    uint32_t hash = 2166136261u;
    for (int i = 0; i < inv->display_count; i++) {
        const DisplayInfo* d = &inv->displays[i];
        hash = fnv1a_32_buf(d->desktop_id, strlen(d->desktop_id)) ^ hash;
        hash *= 16777619u;
        hash = fnv1a_32_buf(d->name, strlen(d->name)) ^ hash;
        hash *= 16777619u;
        hash = fnv1a_32_buf(&d->x, sizeof(d->x)) ^ hash;
        hash *= 16777619u;
        hash = fnv1a_32_buf(&d->y, sizeof(d->y)) ^ hash;
        hash *= 16777619u;
        hash = fnv1a_32_buf(&d->width, sizeof(d->width)) ^ hash;
        hash *= 16777619u;
        hash = fnv1a_32_buf(&d->height, sizeof(d->height)) ^ hash;
        hash *= 16777619u;
        hash = fnv1a_32_buf(d->policy, strlen(d->policy)) ^ hash;
        hash *= 16777619u;
    }
    for (int i = 0; i < inv->camera_count; i++) {
        const CameraInfo* c = &inv->cameras[i];
        hash = fnv1a_32_buf(c->camera_id, strlen(c->camera_id)) ^ hash;
        hash *= 16777619u;
        hash = fnv1a_32_buf(c->name, strlen(c->name)) ^ hash;
        hash *= 16777619u;
        hash = fnv1a_32_buf(&c->available, sizeof(c->available)) ^ hash;
        hash *= 16777619u;
    }
    return hash;
}
