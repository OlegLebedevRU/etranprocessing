#ifndef L4DESK_FFMPEG_CMDLINE_H
#define L4DESK_FFMPEG_CMDLINE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int fps;
    const char* bitrate;
    const char* maxrate;
    const char* bufsize;
} FFmpegProfileParams;

bool ffmpeg_get_profile_params(const char* profile, FFmpegProfileParams* out_params);

bool ffmpeg_build_desktop_cmdline(const wchar_t* ffmpeg_binary,
                                  const char* stream_instance_id,
                                  const char* profile,
                                  int x, int y, int width, int height,
                                  wchar_t* out_cmdline, size_t max_chars);

bool ffmpeg_build_camera_cmdline(const wchar_t* ffmpeg_binary,
                                 const char* stream_instance_id,
                                 const char* profile,
                                 const char* device_path,
                                 const char* friendly_name,
                                 wchar_t* out_cmdline, size_t max_chars);

#endif /* L4DESK_FFMPEG_CMDLINE_H */
