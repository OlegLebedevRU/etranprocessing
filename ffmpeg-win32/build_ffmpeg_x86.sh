#!/bin/bash
set -xe
mkdir -p /build/artifacts

echo "=== 1. Building x264 static (x86 Win32) ==="
rm -rf /build/x264
git clone --depth=1 https://code.videolan.org/videolan/x264.git /build/x264
cd /build/x264
./configure \
  --prefix=/opt/ffbuild \
  --host=i686-w64-mingw32 \
  --cross-prefix=i686-w64-mingw32- \
  --enable-static \
  --disable-cli \
  --enable-pic
make -j2
make install

echo "=== 2. Building FFmpeg static (x86 Win32) ==="
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
i686-w64-mingw32-strip ffmpeg.exe

echo "=== 3. Testing with Wine ==="
wine ffmpeg.exe -version || true

echo "=== 4. Copying artifact ==="
cp ffmpeg.exe /build/artifacts/ffmpeg-x86.exe
echo "=== BUILD COMPLETE ==="
ls -lh /build/artifacts/ffmpeg-x86.exe
