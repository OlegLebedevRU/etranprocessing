#include "ffmpeg_cmdline.h"
#include <stdio.h>
#include <string.h>

bool ffmpeg_get_profile_params(const char* profile, FFmpegProfileParams* out_params) {
    if (!out_params) return false;

    if (profile && _stricmp(profile, "low") == 0) {
        out_params->fps = 15;
        out_params->bitrate = "800k";
        out_params->maxrate = "1000k";
        out_params->bufsize = "1600k";
        return true;
    }

    /* Default profile */
    out_params->fps = 25;
    out_params->bitrate = "2000k";
    out_params->maxrate = "2500k";
    out_params->bufsize = "4000k";
    return true;
}

bool ffmpeg_build_desktop_cmdline(const wchar_t* ffmpeg_binary,
                                  const char* stream_instance_id,
                                  const char* profile,
                                  int x, int y, int width, int height,
                                  wchar_t* out_cmdline, size_t max_chars) {
    if (!ffmpeg_binary || !stream_instance_id || !out_cmdline || max_chars == 0) {
        return false;
    }

    FFmpegProfileParams params;
    ffmpeg_get_profile_params(profile, &params);

    /* Ensure even dimensions for yuv420p */
    if (width > 0) width = (width / 2) * 2;
    if (height > 0) height = (height / 2) * 2;
    if (width <= 0) width = 1920;
    if (height <= 0) height = 1080;

    int gop = params.fps * 2;

    int written = swprintf_s(
        out_cmdline, max_chars,
        L"\"%ls\" -progress pipe:1 "
        L"-f gdigrab -framerate %d -offset_x %d -offset_y %d -video_size %dx%d -i desktop -draw_mouse 1 "
        L"-c:v libx264 -preset ultrafast -tune zerolatency "
        L"-b:v %hs -maxrate %hs -bufsize %hs -g %d -pix_fmt yuv420p -r %d "
        L"-metadata comment=l4desk:%hs "
        L"-f rtp rtp://127.0.0.1:5004?rtcpport=5005",
        ffmpeg_binary,
        params.fps, x, y, width, height,
        params.bitrate, params.maxrate, params.bufsize, gop, params.fps,
        stream_instance_id
    );

    return (written > 0 && (size_t)written < max_chars);
}

bool ffmpeg_build_camera_cmdline(const wchar_t* ffmpeg_binary,
                                 const char* stream_instance_id,
                                 const char* profile,
                                 const char* device_path,
                                 const char* friendly_name,
                                 wchar_t* out_cmdline, size_t max_chars) {
    if (!ffmpeg_binary || !stream_instance_id || !out_cmdline || max_chars == 0) {
        return false;
    }

    FFmpegProfileParams params;
    ffmpeg_get_profile_params(profile, &params);

    int cam_w = 1280;
    int cam_h = 720;
    if (profile && _stricmp(profile, "low") == 0) {
        cam_w = 640;
        cam_h = 480;
    }

    int gop = params.fps * 2;

    wchar_t video_dev_arg[512] = { 0 };
    if (device_path && device_path[0] != '\0') {
        if (device_path[0] == '@') {
            swprintf_s(video_dev_arg, sizeof(video_dev_arg) / sizeof(wchar_t),
                       L"video=%hs", device_path);
        } else {
            swprintf_s(video_dev_arg, sizeof(video_dev_arg) / sizeof(wchar_t),
                       L"video=@%hs", device_path);
        }
    } else if (friendly_name && friendly_name[0] != '\0') {
        swprintf_s(video_dev_arg, sizeof(video_dev_arg) / sizeof(wchar_t),
                   L"video=\"%hs\"", friendly_name);
    } else {
        swprintf_s(video_dev_arg, sizeof(video_dev_arg) / sizeof(wchar_t),
                   L"video=none");
    }

    int written = swprintf_s(
        out_cmdline, max_chars,
        L"\"%ls\" -progress pipe:1 "
        L"-f dshow -rtbufsize 64M -framerate %d -video_size %dx%d -i %ls "
        L"-c:v libx264 -preset ultrafast -tune zerolatency "
        L"-b:v %hs -maxrate %hs -bufsize %hs -g %d -pix_fmt yuv420p -r %d "
        L"-metadata comment=l4desk:%hs "
        L"-f rtp rtp://127.0.0.1:5004?rtcpport=5005",
        ffmpeg_binary,
        params.fps, cam_w, cam_h, video_dev_arg,
        params.bitrate, params.maxrate, params.bufsize, gop, params.fps,
        stream_instance_id
    );

    return (written > 0 && (size_t)written < max_chars);
}
