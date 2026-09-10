#!/bin/bash
set -xe
mkdir -p /build/artifacts

echo "=== 1. Building x264 static (x64) ==="
rm -rf /build/x264
git clone --depth=1 https://code.videolan.org/videolan/x264.git /build/x264
cd /build/x264
./configure \
  --prefix=/opt/ffbuild \
  --host=x86_64-w64-mingw32 \
  --cross-prefix=x86_64-w64-mingw32- \
  --enable-static \
  --disable-cli \
  --enable-pic
make -j2
make install

echo "=== 2. Building FFmpeg static (x64) ==="
rm -rf /build/ffmpeg
git clone --depth=1 --branch=release/9.0 https://github.com/FFmpeg/FFmpeg.git /build/ffmpeg
cd /build/ffmpeg
./configure \
  --prefix=/opt/ffbuild/prefix \
  --pkg-config-flags="--static" \
  ${FFBUILD_TARGET_FLAGS} \
  --enable-gpl \
  --enable-version3 \
  --disable-shared \
  --enable-static \
  --disable-debug \
  --disable-doc \
  --disable-ffplay \
  --disable-ffprobe \
  --enable-network \
  --enable-protocol=udp,rtp \
  --enable-muxer=rtp \
  --enable-libx264 \
  --enable-encoder=libx264,rawvideo \
  --enable-indev=gdigrab,dshow \
  --enable-filter=scale,format,fps \
  --extra-cflags="${CFLAGS}" \
  --extra-ldflags="${LDFLAGS}" \
  --cc="${CC}" --cxx="${CXX}" --ar="${AR}" --ranlib="${RANLIB}" --nm="${NM}"

make -j2
x86_64-w64-mingw32-strip ffmpeg.exe

echo "=== 3. Testing with Wine ==="
wine64 ffmpeg.exe -version 2>/dev/null || wine ffmpeg.exe -version 2>/dev/null || true

echo "=== 4. Copying artifact ==="
cp ffmpeg.exe /build/artifacts/ffmpeg-x64.exe
echo "=== BUILD COMPLETE ==="
ls -lh /build/artifacts/ffmpeg-x64.exe
