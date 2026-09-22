/*
 * OpenH264 C API spike — verifies header compile-ability in pure C mode
 * and exercises the public create/destroy symbols.
 *
 * This file is compiled standalone (not linked into l4capture.exe) to
 * validate that wels/codec_api.h produces a clean C translation unit.
 * The actual static linking with openh264.lib will be done in L4C-03.
 */
#include <stdio.h>
#include <stddef.h>
#define HAVE_STDINT_H
#include "wels/codec_api.h"

static int test_version(void) {
    OpenH264Version ver = WelsGetCodecVersion();
    printf("  OpenH264 version: %d.%d.%d\n", ver.uMajor, ver.uMinor, ver.uRevision);
    if (ver.uMajor == 0 && ver.uMinor == 0 && ver.uRevision == 0) {
        printf("  WARN: version resolved to 0.0.0 (lib not linked, header-only check)\n");
    }
    return 0;
}

static int test_api_symbols(void) {
    /* Verify that the C API function declarations are visible and well-typed. */
    int (*pCreate)(ISVCEncoder **) = WelsCreateSVCEncoder;
    void (*pDestroy)(ISVCEncoder *) = WelsDestroySVCEncoder;
    long (*pCreateDec)(ISVCDecoder **) = WelsCreateDecoder;
    void (*pDestroyDec)(ISVCDecoder *) = WelsDestroyDecoder;
    if (!pCreate || !pDestroy || !pCreateDec || !pDestroyDec) {
        printf("  FAIL: NULL function pointer detected\n");
        return 1;
    }
    printf("  WelsCreateSVCEncoder:   %p\n", (void *)(uintptr_t)(size_t)(void *)pCreate);
    printf("  WelsDestroySVCEncoder:  %p\n", (void *)(uintptr_t)(size_t)(void *)pDestroy);
    printf("  WelsCreateDecoder:      %p\n", (void *)(uintptr_t)(size_t)(void *)pCreateDec);
    printf("  WelsDestroyDecoder:     %p\n", (void *)(uintptr_t)(size_t)(void *)pDestroyDec);
    return 0;
}

static int test_struct_sizes(void) {
    /* Verify that key structures have non-trivial sizes. */
    size_t sz_enc = sizeof(SEncParamExt);
    size_t sz_bs = sizeof(SFrameBSInfo);
    size_t sz_pic = sizeof(SSourcePicture);
    printf("  sizeof(SEncParamExt):   %zu\n", sz_enc);
    printf("  sizeof(SFrameBSInfo):   %zu\n", sz_bs);
    printf("  sizeof(SSourcePicture): %zu\n", sz_pic);
    if (sz_enc < 64 || sz_bs < 32 || sz_pic < 32) {
        printf("  FAIL: struct sizes unexpectedly small\n");
        return 1;
    }
    return 0;
}

int main(void) {
    int failures = 0;
    printf("[openh264_spike] C API header compile check\n");
    failures += test_version();
    failures += test_api_symbols();
    failures += test_struct_sizes();
    printf("[openh264_spike] %s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
