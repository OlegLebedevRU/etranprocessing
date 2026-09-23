#include <windows.h>
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4201)
#endif
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "l4capture/dxgi_capture.h"
#include "l4capture/clock.h"
#include "l4capture/limits.h"
#include "l4capture/safety_gate.h"
#include "l4capture/telemetry.h"

#include <stdlib.h>
#include <string.h>

/* Function pointer prototypes for dynamic binding */
typedef HRESULT (WINAPI *PFN_D3D11_CREATE_DEVICE)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**
);
typedef HRESULT (WINAPI *PFN_CREATE_DXGI_FACTORY1)(REFIID, void**);

/* Internal context structure */
typedef struct {
    HMODULE h_d3d11;
    HMODULE h_dxgi;
    PFN_D3D11_CREATE_DEVICE pfn_create_device;
    PFN_CREATE_DXGI_FACTORY1 pfn_create_factory1;

    ID3D11Device *device;
    ID3D11DeviceContext *context;
    IDXGIOutput1 *output1;
    IDXGIOutputDuplication *duplication;
    ID3D11Texture2D *staging_texture;

    DXGI_OUTDUPL_DESC dupl_desc;
    l4c_capture_config_t config;
    l4c_rect_t physical_rect;
    uint32_t raw_width;
    uint32_t raw_height;
    uint32_t width;
    uint32_t height;
    int32_t stride;
    size_t buffer_size;
    uint64_t geometry_generation;

    bool has_acquired_frame;
    bool is_mapped;
    D3D11_MAPPED_SUBRESOURCE mapped;

    uint8_t *rotation_buf;
    size_t rotation_buf_size;

    uint8_t *cursor_shape_buf;
    UINT cursor_shape_buf_size;
    DXGI_OUTDUPL_POINTER_SHAPE_INFO cursor_shape_info;
    bool cursor_shape_valid;
    int32_t cursor_x;
    int32_t cursor_y;
    bool cursor_visible;
} dxgi_ctx_t;

/* Test hooks */
static int s_test_injected_error = 0;
static int s_test_reinit_count = 0;

void l4c_dxgi_test_inject_access_lost(int mode) {
    s_test_injected_error = mode;
    s_test_reinit_count = 0;
}

int l4c_dxgi_test_get_reinit_count(void) {
    return s_test_reinit_count;
}

/* Helper to load libraries strictly from System32 */
static HMODULE load_system_dll(const wchar_t *dll_name) {
    HMODULE h = LoadLibraryExW(dll_name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) {
        WCHAR path[MAX_PATH];
        UINT len = GetSystemDirectoryW(path, MAX_PATH);
        if (len > 0 && len < MAX_PATH - 32) {
            wcscat_s(path, MAX_PATH, L"\\");
            wcscat_s(path, MAX_PATH, dll_name);
            h = LoadLibraryW(path);
        }
    }
    return h;
}

/* Rotation dimension calculation */
void l4c_dxgi_calc_rotation_dims(uint32_t in_w, uint32_t in_h, DXGI_MODE_ROTATION rot,
                                 uint32_t *out_w, uint32_t *out_h) {
    if (!out_w || !out_h) return;
    if (rot == DXGI_MODE_ROTATION_ROTATE90 || rot == DXGI_MODE_ROTATION_ROTATE270) {
        *out_w = in_h;
        *out_h = in_w;
    } else {
        *out_w = in_w;
        *out_h = in_h;
    }
}

/* Rotate 32-bit BGRA raster */
void l4c_dxgi_rotate_bgra(const uint8_t *src, uint32_t src_w, uint32_t src_h, int32_t src_stride,
                          uint8_t *dst, int32_t dst_stride, DXGI_MODE_ROTATION rotation) {
    if (!src || !dst || src_w == 0 || src_h == 0) return;

    if (rotation == DXGI_MODE_ROTATION_ROTATE90) {
        /* Clockwise 90 deg: dst_w = src_h, dst_h = src_w */
        for (uint32_t dy = 0; dy < src_w; ++dy) {
            uint32_t *dst_row = (uint32_t*)(dst + (size_t)dy * (size_t)dst_stride);
            for (uint32_t dx = 0; dx < src_h; ++dx) {
                uint32_t sx = dy;
                uint32_t sy = src_h - 1 - dx;
                const uint32_t *src_pixel = (const uint32_t*)(src + (size_t)sy * (size_t)src_stride) + sx;
                dst_row[dx] = *src_pixel;
            }
        }
    } else if (rotation == DXGI_MODE_ROTATION_ROTATE180) {
        /* 180 deg: dst_w = src_w, dst_h = src_h */
        for (uint32_t dy = 0; dy < src_h; ++dy) {
            uint32_t *dst_row = (uint32_t*)(dst + (size_t)dy * (size_t)dst_stride);
            uint32_t sy = src_h - 1 - dy;
            const uint32_t *src_row = (const uint32_t*)(src + (size_t)sy * (size_t)src_stride);
            for (uint32_t dx = 0; dx < src_w; ++dx) {
                uint32_t sx = src_w - 1 - dx;
                dst_row[dx] = src_row[sx];
            }
        }
    } else if (rotation == DXGI_MODE_ROTATION_ROTATE270) {
        /* Clockwise 270 deg (Counter-clockwise 90 deg): dst_w = src_h, dst_h = src_w */
        for (uint32_t dy = 0; dy < src_w; ++dy) {
            uint32_t *dst_row = (uint32_t*)(dst + (size_t)dy * (size_t)dst_stride);
            for (uint32_t dx = 0; dx < src_h; ++dx) {
                uint32_t sx = src_w - 1 - dy;
                uint32_t sy = dx;
                const uint32_t *src_pixel = (const uint32_t*)(src + (size_t)sy * (size_t)src_stride) + sx;
                dst_row[dx] = *src_pixel;
            }
        }
    } else {
        /* Identity / Unspecified */
        size_t row_bytes = (size_t)src_w * 4;
        for (uint32_t y = 0; y < src_h; ++y) {
            memcpy(dst + (size_t)y * (size_t)dst_stride, src + (size_t)y * (size_t)src_stride, row_bytes);
        }
    }
}

/* Cursor overlay for DXGI shapes */
void l4c_dxgi_draw_cursor(uint8_t *frame_data, uint32_t frame_w, uint32_t frame_h, int32_t frame_stride,
                          const DXGI_OUTDUPL_POINTER_SHAPE_INFO *shape, const uint8_t *shape_data,
                          int32_t pos_x, int32_t pos_y) {
    if (!frame_data || !shape || !shape_data || frame_w == 0 || frame_h == 0) return;

    if (shape->Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
        for (UINT cy = 0; cy < shape->Height; ++cy) {
            int fy = pos_y + (int)cy;
            if (fy < 0 || (uint32_t)fy >= frame_h) continue;
            const uint8_t *src_row = shape_data + (size_t)cy * shape->Pitch;
            uint8_t *dst_row = frame_data + (size_t)fy * (size_t)frame_stride;

            for (UINT cx = 0; cx < shape->Width; ++cx) {
                int fx = pos_x + (int)cx;
                if (fx < 0 || (uint32_t)fx >= frame_w) continue;
                const uint8_t *sp = src_row + (size_t)cx * 4;
                uint8_t *dp = dst_row + (size_t)fx * 4;
                uint8_t a = sp[3];
                if (a == 255) {
                    dp[0] = sp[0];
                    dp[1] = sp[1];
                    dp[2] = sp[2];
                    dp[3] = 0xFF;
                } else if (a > 0) {
                    dp[0] = (uint8_t)(((UINT)sp[0] * a + (UINT)dp[0] * (255 - a)) / 255);
                    dp[1] = (uint8_t)(((UINT)sp[1] * a + (UINT)dp[1] * (255 - a)) / 255);
                    dp[2] = (uint8_t)(((UINT)sp[2] * a + (UINT)dp[2] * (255 - a)) / 255);
                    dp[3] = 0xFF;
                }
            }
        }
    } else if (shape->Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME) {
        /* In monochrome shape, Height is double the actual height: top AND mask, bottom XOR mask */
        UINT h_actual = shape->Height / 2;
        const uint8_t *and_mask = shape_data;
        const uint8_t *xor_mask = shape_data + ((size_t)h_actual * shape->Pitch);

        for (UINT cy = 0; cy < h_actual; ++cy) {
            int fy = pos_y + (int)cy;
            if (fy < 0 || (uint32_t)fy >= frame_h) continue;
            const uint8_t *and_row = and_mask + (size_t)cy * shape->Pitch;
            const uint8_t *xor_row = xor_mask + (size_t)cy * shape->Pitch;
            uint8_t *dst_row = frame_data + (size_t)fy * (size_t)frame_stride;

            for (UINT cx = 0; cx < shape->Width; ++cx) {
                int fx = pos_x + (int)cx;
                if (fx < 0 || (uint32_t)fx >= frame_w) continue;
                uint8_t and_bit = (and_row[cx / 8] >> (7 - (cx % 8))) & 1;
                uint8_t xor_bit = (xor_row[cx / 8] >> (7 - (cx % 8))) & 1;
                uint32_t *pixel = (uint32_t*)(dst_row + (size_t)fx * 4);

                if (!and_bit && !xor_bit) {
                    *pixel = 0xFF000000; /* Black */
                } else if (!and_bit && xor_bit) {
                    *pixel = 0xFFFFFFFF; /* White */
                } else if (and_bit && xor_bit) {
                    *pixel ^= 0x00FFFFFF; /* Invert RGB */
                }
            }
        }
    } else if (shape->Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR) {
        for (UINT cy = 0; cy < shape->Height; ++cy) {
            int fy = pos_y + (int)cy;
            if (fy < 0 || (uint32_t)fy >= frame_h) continue;
            const uint8_t *src_row = shape_data + (size_t)cy * shape->Pitch;
            uint8_t *dst_row = frame_data + (size_t)fy * (size_t)frame_stride;

            for (UINT cx = 0; cx < shape->Width; ++cx) {
                int fx = pos_x + (int)cx;
                if (fx < 0 || (uint32_t)fx >= frame_w) continue;
                const uint8_t *sp = src_row + (size_t)cx * 4;
                uint8_t *dp = dst_row + (size_t)fx * 4;
                if (sp[3] == 0) {
                    dp[0] = sp[0];
                    dp[1] = sp[1];
                    dp[2] = sp[2];
                    dp[3] = 0xFF;
                } else {
                    dp[0] ^= sp[0];
                    dp[1] ^= sp[1];
                    dp[2] ^= sp[2];
                }
            }
        }
    }
}

/* Probe DXGI Desktop Duplication support */
bool l4c_dxgi_capture_is_supported(void) {
    DWORD session_id = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &session_id) && session_id == 0) {
        return false;
    }

    HMODULE h_d3d11 = load_system_dll(L"d3d11.dll");
    HMODULE h_dxgi = load_system_dll(L"dxgi.dll");
    if (!h_d3d11 || !h_dxgi) {
        if (h_d3d11) FreeLibrary(h_d3d11);
        if (h_dxgi) FreeLibrary(h_dxgi);
        return false;
    }

    PFN_D3D11_CREATE_DEVICE pfn_create_device =
        (PFN_D3D11_CREATE_DEVICE)GetProcAddress(h_d3d11, "D3D11CreateDevice");
    PFN_CREATE_DXGI_FACTORY1 pfn_create_factory1 =
        (PFN_CREATE_DXGI_FACTORY1)GetProcAddress(h_dxgi, "CreateDXGIFactory1");
    if (!pfn_create_device || !pfn_create_factory1) {
        FreeLibrary(h_d3d11);
        FreeLibrary(h_dxgi);
        return false;
    }

    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    D3D_FEATURE_LEVEL fl;
    static const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_1
    };
    HRESULT hr = pfn_create_device(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                                   levels, (UINT)(sizeof(levels)/sizeof(levels[0])),
                                   D3D11_SDK_VERSION, &device, &fl, &context);
    if (FAILED(hr) || !device) {
        FreeLibrary(h_d3d11);
        FreeLibrary(h_dxgi);
        return false;
    }

    IDXGIDevice *dxgi_dev = NULL;
    hr = device->lpVtbl->QueryInterface(device, &IID_IDXGIDevice, (void**)&dxgi_dev);
    if (FAILED(hr) || !dxgi_dev) {
        if (context) context->lpVtbl->Release(context);
        device->lpVtbl->Release(device);
        FreeLibrary(h_d3d11);
        FreeLibrary(h_dxgi);
        return false;
    }

    IDXGIAdapter *adapter = NULL;
    hr = dxgi_dev->lpVtbl->GetAdapter(dxgi_dev, &adapter);
    dxgi_dev->lpVtbl->Release(dxgi_dev);
    if (FAILED(hr) || !adapter) {
        if (context) context->lpVtbl->Release(context);
        device->lpVtbl->Release(device);
        FreeLibrary(h_d3d11);
        FreeLibrary(h_dxgi);
        return false;
    }

    IDXGIOutput *output = NULL;
    hr = adapter->lpVtbl->EnumOutputs(adapter, 0, &output);
    adapter->lpVtbl->Release(adapter);
    if (FAILED(hr) || !output) {
        if (context) context->lpVtbl->Release(context);
        device->lpVtbl->Release(device);
        FreeLibrary(h_d3d11);
        FreeLibrary(h_dxgi);
        return false;
    }

    IDXGIOutput1 *output1 = NULL;
    hr = output->lpVtbl->QueryInterface(output, &IID_IDXGIOutput1, (void**)&output1);
    output->lpVtbl->Release(output);
    if (FAILED(hr) || !output1) {
        if (context) context->lpVtbl->Release(context);
        device->lpVtbl->Release(device);
        FreeLibrary(h_d3d11);
        FreeLibrary(h_dxgi);
        return false;
    }

    IDXGIOutputDuplication *dupl = NULL;
    hr = output1->lpVtbl->DuplicateOutput(output1, (IUnknown*)device, &dupl);
    bool supported = false;
    if (SUCCEEDED(hr) && dupl) {
        supported = true;
        dupl->lpVtbl->Release(dupl);
    }
    output1->lpVtbl->Release(output1);
    if (context) context->lpVtbl->Release(context);
    device->lpVtbl->Release(device);
    FreeLibrary(h_d3d11);
    FreeLibrary(h_dxgi);
    return supported;
}

/* Vtable: destroy */
static void dxgi_destroy(struct l4c_capture_backend *self) {
    if (!self) return;
    dxgi_ctx_t *ctx = (dxgi_ctx_t *)self->impl_ctx;
    if (ctx) {
        if (ctx->is_mapped && ctx->context && ctx->staging_texture) {
            ctx->context->lpVtbl->Unmap(ctx->context, (ID3D11Resource*)ctx->staging_texture, 0);
            ctx->is_mapped = false;
        }
        if (ctx->has_acquired_frame && ctx->duplication) {
            ctx->duplication->lpVtbl->ReleaseFrame(ctx->duplication);
            ctx->has_acquired_frame = false;
        }
        if (ctx->staging_texture) {
            ctx->staging_texture->lpVtbl->Release(ctx->staging_texture);
            ctx->staging_texture = NULL;
        }
        if (ctx->duplication) {
            ctx->duplication->lpVtbl->Release(ctx->duplication);
            ctx->duplication = NULL;
        }
        if (ctx->output1) {
            ctx->output1->lpVtbl->Release(ctx->output1);
            ctx->output1 = NULL;
        }
        if (ctx->context) {
            ctx->context->lpVtbl->Release(ctx->context);
            ctx->context = NULL;
        }
        if (ctx->device) {
            ctx->device->lpVtbl->Release(ctx->device);
            ctx->device = NULL;
        }
        if (ctx->rotation_buf) {
            free(ctx->rotation_buf);
            ctx->rotation_buf = NULL;
        }
        if (ctx->cursor_shape_buf) {
            free(ctx->cursor_shape_buf);
            ctx->cursor_shape_buf = NULL;
        }
        if (ctx->h_d3d11) {
            FreeLibrary(ctx->h_d3d11);
            ctx->h_d3d11 = NULL;
        }
        if (ctx->h_dxgi) {
            FreeLibrary(ctx->h_dxgi);
            ctx->h_dxgi = NULL;
        }
        free(ctx);
        self->impl_ctx = NULL;
    }
    free(self);
}

/* Vtable: release_frame */
static void dxgi_release(struct l4c_capture_backend *self, l4c_frame_view_t *frame) {
    (void)frame;
    if (!self) return;
    dxgi_ctx_t *ctx = (dxgi_ctx_t *)self->impl_ctx;
    if (!ctx) return;
    if (ctx->is_mapped && ctx->context && ctx->staging_texture) {
        ctx->context->lpVtbl->Unmap(ctx->context, (ID3D11Resource*)ctx->staging_texture, 0);
        ctx->is_mapped = false;
    }
    if (ctx->has_acquired_frame && ctx->duplication) {
        ctx->duplication->lpVtbl->ReleaseFrame(ctx->duplication);
        ctx->has_acquired_frame = false;
    }
}

/* Vtable: acquire_frame */
static l4c_status_t dxgi_acquire(struct l4c_capture_backend *self, l4c_frame_view_t *out_frame, uint32_t timeout_ms) {
    if (!self || !out_frame) return L4C_ERR_INVALID_ARG;
    dxgi_ctx_t *ctx = (dxgi_ctx_t *)self->impl_ctx;
    if (!ctx || !ctx->duplication || !ctx->staging_texture || !ctx->context) {
        return L4C_ERR_INVALID_ARG;
    }

    /* Test injection check */
    if (s_test_injected_error == 1) {
        /* Mode 1: DXGI_ERROR_ACCESS_LOST with locked/dead session -> fail-closed <= 500ms */
        return L4C_ERR_SESSION_UNAVAILABLE;
    }
    if (s_test_injected_error == 2) {
        /* Mode 2: DXGI_ERROR_ACCESS_LOST with active session -> 3 retries fail -> GDI fallback */
        s_test_reinit_count = 3;
        return L4C_ERR_DEVICE_LOST;
    }

    DXGI_OUTDUPL_FRAME_INFO frame_info;
    IDXGIResource *resource = NULL;
    HRESULT hr = ctx->duplication->lpVtbl->AcquireNextFrame(ctx->duplication, timeout_ms, &frame_info, &resource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return L4C_ERR_NO_FRAME;
    }

    if (hr == DXGI_ERROR_ACCESS_LOST) {
        /* Step 1: Immediate check of interactive user session */
        HDESK desk = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
        if (!desk) {
            /* Session is unavailable (Lock screen, UAC, RDP disconnect) -> FAIL-CLOSED <= 500ms */
            return L4C_ERR_SESSION_UNAVAILABLE;
        }
        CloseDesktop(desk);

        /* Step 2: Session is available -> attempt up to 3 reinitializations */
        static const uint32_t retry_delays_ms[3] = { 100, 300, 1000 };
        bool reinit_ok = false;
        for (int attempt = 0; attempt < 3; ++attempt) {
            Sleep(retry_delays_ms[attempt]);
            desk = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
            if (!desk) {
                return L4C_ERR_SESSION_UNAVAILABLE;
            }
            CloseDesktop(desk);

            /* Recreate duplication and staging texture */
            if (ctx->staging_texture) {
                ctx->staging_texture->lpVtbl->Release(ctx->staging_texture);
                ctx->staging_texture = NULL;
            }
            if (ctx->duplication) {
                ctx->duplication->lpVtbl->Release(ctx->duplication);
                ctx->duplication = NULL;
            }

            hr = ctx->output1->lpVtbl->DuplicateOutput(ctx->output1, (IUnknown*)ctx->device, &ctx->duplication);
            if (SUCCEEDED(hr) && ctx->duplication) {
                ctx->duplication->lpVtbl->GetDesc(ctx->duplication, &ctx->dupl_desc);
                D3D11_TEXTURE2D_DESC sdesc;
                memset(&sdesc, 0, sizeof(sdesc));
                sdesc.Width = ctx->raw_width;
                sdesc.Height = ctx->raw_height;
                sdesc.MipLevels = 1;
                sdesc.ArraySize = 1;
                sdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                sdesc.SampleDesc.Count = 1;
                sdesc.Usage = D3D11_USAGE_STAGING;
                sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                hr = ctx->device->lpVtbl->CreateTexture2D(ctx->device, &sdesc, NULL, &ctx->staging_texture);
                if (SUCCEEDED(hr) && ctx->staging_texture) {
                    reinit_ok = true;
                    break;
                }
            }
        }

        if (!reinit_ok) {
            /* 3 retries failed -> trigger GDI fallback */
            return L4C_ERR_DEVICE_LOST;
        }

        /* Re-acquire after successful reinit */
        hr = ctx->duplication->lpVtbl->AcquireNextFrame(ctx->duplication, timeout_ms, &frame_info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return L4C_ERR_NO_FRAME;
        }
        if (FAILED(hr) || !resource) {
            return L4C_ERR_DEVICE_LOST;
        }
    } else if (FAILED(hr) || !resource) {
        return L4C_ERR_DEVICE_LOST;
    }

    ctx->has_acquired_frame = true;

    ID3D11Texture2D *acquired_tex = NULL;
    hr = resource->lpVtbl->QueryInterface(resource, &IID_ID3D11Texture2D, (void**)&acquired_tex);
    resource->lpVtbl->Release(resource);
    if (FAILED(hr) || !acquired_tex) {
        ctx->duplication->lpVtbl->ReleaseFrame(ctx->duplication);
        ctx->has_acquired_frame = false;
        return L4C_ERR_FATAL;
    }

    ctx->context->lpVtbl->CopyResource(ctx->context, (ID3D11Resource*)ctx->staging_texture, (ID3D11Resource*)acquired_tex);
    acquired_tex->lpVtbl->Release(acquired_tex);

    hr = ctx->context->lpVtbl->Map(ctx->context, (ID3D11Resource*)ctx->staging_texture, 0, D3D11_MAP_READ, 0, &ctx->mapped);
    if (FAILED(hr)) {
        ctx->duplication->lpVtbl->ReleaseFrame(ctx->duplication);
        ctx->has_acquired_frame = false;
        return L4C_ERR_FATAL;
    }
    ctx->is_mapped = true;

    /* Update pointer position and shape if provided */
    if (frame_info.LastMouseUpdateTime.QuadPart != 0) {
        ctx->cursor_visible = frame_info.PointerPosition.Visible ? true : false;
        ctx->cursor_x = frame_info.PointerPosition.Position.x;
        ctx->cursor_y = frame_info.PointerPosition.Position.y;
    }

    if (frame_info.PointerShapeBufferSize > 0) {
        if (frame_info.PointerShapeBufferSize > ctx->cursor_shape_buf_size) {
            uint8_t *new_buf = (uint8_t*)realloc(ctx->cursor_shape_buf, frame_info.PointerShapeBufferSize);
            if (new_buf) {
                ctx->cursor_shape_buf = new_buf;
                ctx->cursor_shape_buf_size = frame_info.PointerShapeBufferSize;
            }
        }
        if (ctx->cursor_shape_buf && frame_info.PointerShapeBufferSize <= ctx->cursor_shape_buf_size) {
            UINT req_size = 0;
            hr = ctx->duplication->lpVtbl->GetFramePointerShape(ctx->duplication,
                                                                frame_info.PointerShapeBufferSize,
                                                                ctx->cursor_shape_buf,
                                                                &req_size,
                                                                &ctx->cursor_shape_info);
            if (SUCCEEDED(hr)) {
                ctx->cursor_shape_valid = true;
            }
        }
    }

    /* Draw cursor if enabled and visible */
    if (ctx->config.capture_cursor && ctx->cursor_visible && ctx->cursor_shape_valid) {
        l4c_dxgi_draw_cursor((uint8_t*)ctx->mapped.pData, ctx->raw_width, ctx->raw_height,
                             (int32_t)ctx->mapped.RowPitch, &ctx->cursor_shape_info,
                             ctx->cursor_shape_buf, ctx->cursor_x, ctx->cursor_y);
    }

    /* Handle rotation */
    if (ctx->dupl_desc.Rotation != DXGI_MODE_ROTATION_IDENTITY && ctx->rotation_buf) {
        l4c_dxgi_rotate_bgra((const uint8_t*)ctx->mapped.pData, ctx->raw_width, ctx->raw_height,
                             (int32_t)ctx->mapped.RowPitch, ctx->rotation_buf, ctx->stride,
                             ctx->dupl_desc.Rotation);
        out_frame->data = ctx->rotation_buf;
        out_frame->stride = ctx->stride;
    } else {
        out_frame->data = (const uint8_t*)ctx->mapped.pData;
        out_frame->stride = (int32_t)ctx->mapped.RowPitch;
    }

    out_frame->width = ctx->width;
    out_frame->height = ctx->height;
    out_frame->buffer_size = ctx->buffer_size;
    out_frame->physical_rect = ctx->physical_rect;
    out_frame->pts_ms = l4c_now_monotonic_ms();
    out_frame->geometry_generation = ctx->geometry_generation;

    return L4C_OK;
}

/* Vtable: init */
static l4c_status_t dxgi_init(struct l4c_capture_backend *self, const l4c_capture_config_t *config) {
    if (!self || !config) return L4C_ERR_INVALID_ARG;
    dxgi_ctx_t *ctx = (dxgi_ctx_t *)self->impl_ctx;
    if (!ctx) return L4C_ERR_INVALID_ARG;

    ctx->config = *config;
    ctx->geometry_generation = 1;

    /* Create DXGI factory to find output */
    IDXGIFactory1 *factory = NULL;
    HRESULT hr = ctx->pfn_create_factory1(&IID_IDXGIFactory1, (void**)&factory);
    if (FAILED(hr) || !factory) return L4C_ERR_FATAL;

    IDXGIAdapter1 *best_adapter = NULL;
    IDXGIOutput *best_output = NULL;
    DXGI_OUTPUT_DESC best_desc;
    memset(&best_desc, 0, sizeof(best_desc));
    int total_attached_outputs = 0;
    bool is_target_zero = (config->target_rect.left == 0 && config->target_rect.top == 0 &&
                           config->target_rect.right == 0 && config->target_rect.bottom == 0);

    for (UINT a = 0; ; ++a) {
        IDXGIAdapter1 *ad = NULL;
        if (factory->lpVtbl->EnumAdapters1(factory, a, &ad) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT o = 0; ; ++o) {
            IDXGIOutput *out = NULL;
            if (ad->lpVtbl->EnumOutputs(ad, o, &out) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC odesc;
            if (SUCCEEDED(out->lpVtbl->GetDesc(out, &odesc)) && odesc.AttachedToDesktop) {
                total_attached_outputs++;
                if (is_target_zero) {
                    if (!best_output) {
                        best_output = out;
                        best_adapter = ad;
                        best_desc = odesc;
                        out->lpVtbl->AddRef(out);
                        ad->lpVtbl->AddRef(ad);
                    }
                } else {
                    if (config->target_rect.left >= odesc.DesktopCoordinates.left &&
                        config->target_rect.top >= odesc.DesktopCoordinates.top &&
                        config->target_rect.right <= odesc.DesktopCoordinates.right &&
                        config->target_rect.bottom <= odesc.DesktopCoordinates.bottom &&
                        config->target_rect.right > config->target_rect.left &&
                        config->target_rect.bottom > config->target_rect.top) {
                        if (!best_output) {
                            best_output = out;
                            best_adapter = ad;
                            best_desc = odesc;
                            out->lpVtbl->AddRef(out);
                            ad->lpVtbl->AddRef(ad);
                        }
                    }
                }
            }
            out->lpVtbl->Release(out);
        }
        ad->lpVtbl->Release(ad);
    }
    factory->lpVtbl->Release(factory);

    /* Section 4.3: Virtual desktop across multiple monitors must be rejected */
    if (is_target_zero && total_attached_outputs > 1) {
        if (best_output) best_output->lpVtbl->Release(best_output);
        if (best_adapter) best_adapter->lpVtbl->Release(best_adapter);
        return L4C_ERR_INVALID_ARG;
    }

    if (!best_output || !best_adapter) {
        if (best_output) best_output->lpVtbl->Release(best_output);
        if (best_adapter) best_adapter->lpVtbl->Release(best_adapter);
        return L4C_ERR_INVALID_ARG;
    }

    hr = best_output->lpVtbl->QueryInterface(best_output, &IID_IDXGIOutput1, (void**)&ctx->output1);
    best_output->lpVtbl->Release(best_output);
    if (FAILED(hr) || !ctx->output1) {
        best_adapter->lpVtbl->Release(best_adapter);
        return L4C_ERR_DEVICE_LOST;
    }

    /* Create D3D11 device on matching adapter */
    D3D_FEATURE_LEVEL fl;
    static const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_1
    };
    hr = ctx->pfn_create_device((IDXGIAdapter*)best_adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL,
                                D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                (UINT)(sizeof(levels)/sizeof(levels[0])),
                                D3D11_SDK_VERSION, &ctx->device, &fl, &ctx->context);
    best_adapter->lpVtbl->Release(best_adapter);
    if (FAILED(hr) || !ctx->device || !ctx->context) {
        return L4C_ERR_DEVICE_LOST;
    }

    /* Create Output Duplication */
    hr = ctx->output1->lpVtbl->DuplicateOutput(ctx->output1, (IUnknown*)ctx->device, &ctx->duplication);
    if (FAILED(hr) || !ctx->duplication) {
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE || hr == DXGI_ERROR_ACCESS_LOST) {
            return L4C_ERR_SESSION_UNAVAILABLE;
        }
        return L4C_ERR_DEVICE_LOST;
    }

    ctx->duplication->lpVtbl->GetDesc(ctx->duplication, &ctx->dupl_desc);
    ctx->raw_width = ctx->dupl_desc.ModeDesc.Width;
    ctx->raw_height = ctx->dupl_desc.ModeDesc.Height;

    /* Dimension calculation with rotation */
    l4c_dxgi_calc_rotation_dims(ctx->raw_width, ctx->raw_height, ctx->dupl_desc.Rotation,
                                &ctx->width, &ctx->height);

    l4c_raster_layout_t layout;
    l4c_status_t s = l4c_bgra_layout(ctx->width, ctx->height, &layout);
    if (s != L4C_OK) return s;
    ctx->stride = (int32_t)layout.stride;
    ctx->buffer_size = layout.bytes;

    if (is_target_zero) {
        ctx->physical_rect.left = best_desc.DesktopCoordinates.left;
        ctx->physical_rect.top = best_desc.DesktopCoordinates.top;
        ctx->physical_rect.right = best_desc.DesktopCoordinates.left + (LONG)ctx->width;
        ctx->physical_rect.bottom = best_desc.DesktopCoordinates.top + (LONG)ctx->height;
    } else {
        ctx->physical_rect = config->target_rect;
    }

    /* Zero-allocation invariant: Create staging texture once in init */
    D3D11_TEXTURE2D_DESC sdesc;
    memset(&sdesc, 0, sizeof(sdesc));
    sdesc.Width = ctx->raw_width;
    sdesc.Height = ctx->raw_height;
    sdesc.MipLevels = 1;
    sdesc.ArraySize = 1;
    sdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sdesc.SampleDesc.Count = 1;
    sdesc.Usage = D3D11_USAGE_STAGING;
    sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sdesc.BindFlags = 0;
    sdesc.MiscFlags = 0;
    hr = ctx->device->lpVtbl->CreateTexture2D(ctx->device, &sdesc, NULL, &ctx->staging_texture);
    if (FAILED(hr) || !ctx->staging_texture) {
        return L4C_ERR_FATAL;
    }

    /* If rotation != IDENTITY, allocate dedicated rotation buffer */
    if (ctx->dupl_desc.Rotation != DXGI_MODE_ROTATION_IDENTITY) {
        ctx->rotation_buf = (uint8_t*)malloc(ctx->buffer_size);
        if (!ctx->rotation_buf) return L4C_ERR_OUT_OF_MEMORY;
        ctx->rotation_buf_size = ctx->buffer_size;
    }

    /* Cursor buffer */
    ctx->cursor_shape_buf = (uint8_t*)malloc(4096);
    ctx->cursor_shape_buf_size = 4096;

    return L4C_OK;
}

static const l4c_capture_backend_vtable_t s_dxgi_vtable = {
    dxgi_init,
    dxgi_acquire,
    dxgi_release,
    dxgi_destroy
};

/* Factory function */
l4c_status_t l4c_dxgi_capture_create(l4c_capture_backend_t **out_backend) {
    if (!out_backend) return L4C_ERR_INVALID_ARG;
    *out_backend = NULL;

    HMODULE h_d3d11 = load_system_dll(L"d3d11.dll");
    HMODULE h_dxgi = load_system_dll(L"dxgi.dll");
    if (!h_d3d11 || !h_dxgi) {
        if (h_d3d11) FreeLibrary(h_d3d11);
        if (h_dxgi) FreeLibrary(h_dxgi);
        return L4C_ERR_DEVICE_LOST;
    }

    PFN_D3D11_CREATE_DEVICE pfn_create_device =
        (PFN_D3D11_CREATE_DEVICE)GetProcAddress(h_d3d11, "D3D11CreateDevice");
    PFN_CREATE_DXGI_FACTORY1 pfn_create_factory1 =
        (PFN_CREATE_DXGI_FACTORY1)GetProcAddress(h_dxgi, "CreateDXGIFactory1");
    if (!pfn_create_device || !pfn_create_factory1) {
        FreeLibrary(h_d3d11);
        FreeLibrary(h_dxgi);
        return L4C_ERR_DEVICE_LOST;
    }

    dxgi_ctx_t *ctx = (dxgi_ctx_t*)malloc(sizeof(dxgi_ctx_t));
    if (!ctx) {
        FreeLibrary(h_d3d11);
        FreeLibrary(h_dxgi);
        return L4C_ERR_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(dxgi_ctx_t));
    ctx->h_d3d11 = h_d3d11;
    ctx->h_dxgi = h_dxgi;
    ctx->pfn_create_device = pfn_create_device;
    ctx->pfn_create_factory1 = pfn_create_factory1;

    l4c_capture_backend_t *backend = (l4c_capture_backend_t*)malloc(sizeof(l4c_capture_backend_t));
    if (!backend) {
        FreeLibrary(h_d3d11);
        FreeLibrary(h_dxgi);
        free(ctx);
        return L4C_ERR_OUT_OF_MEMORY;
    }
    backend->vtable = &s_dxgi_vtable;
    backend->impl_ctx = ctx;

    *out_backend = backend;
    return L4C_OK;
}
