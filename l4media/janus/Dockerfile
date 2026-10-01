# Upstream version matches the accepted production Janus before this packaging change.
FROM debian:bookworm-slim@sha256:3783cc01769c7b2b1b83a5c5ad96c815348e28ed7da68e2e3687004faa906251 AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl build-essential autoconf automake libtool pkg-config \
    libglib2.0-dev libjansson-dev libconfig-dev libnice-dev libssl-dev \
    libsrtp2-dev libusrsctp-dev libmicrohttpd-dev libwebsockets-dev libcurl4-openssl-dev \
    libogg-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /build
RUN curl --fail --location --retry 3 \
      https://codeload.github.com/meetecho/janus-gateway/tar.gz/3c39ce8cf11c54cf6f1607030a47ac9db798389a \
      -o janus-source.tar.gz \
    && echo 'a3e211e601980f0f618e1189705a2d23d36abc2b81c178a4c5cf20072edb19e5  janus-source.tar.gz' | sha256sum -c - \
    && mkdir source && tar -xzf janus-source.tar.gz --strip-components=1 -C source
WORKDIR /build/source
RUN ./autogen.sh && ./configure --prefix=/usr/local \
      --disable-docs --disable-all-plugins --enable-plugin-streaming \
      --disable-all-transports --enable-rest --enable-websockets \
      --disable-all-handlers --disable-all-loggers --enable-data-channels \
      --enable-libsrtp2 \
    && make -j2 && make install && make configs \
    && mkdir -p /usr/local/share/l4media \
    && cp COPYING /build/janus-source.tar.gz /usr/local/share/l4media/

FROM debian:bookworm-slim@sha256:3783cc01769c7b2b1b83a5c5ad96c815348e28ed7da68e2e3687004faa906251
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libglib2.0-0 libjansson4 libconfig9 libnice10 libssl3 \
    libsrtp2-1 libusrsctp2 libmicrohttpd12 libwebsockets17 libcurl4 libogg0 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=builder /usr/local/ /usr/local/
RUN ldconfig
ARG SOURCE_REVISION
ARG SOURCE_ARCHIVE_SHA256
LABEL org.opencontainers.image.revision=$SOURCE_REVISION \
      io.l4desk.source-archive-sha256=$SOURCE_ARCHIVE_SHA256 \
      io.l4desk.janus-upstream-revision="3c39ce8cf11c54cf6f1607030a47ac9db798389a" \
      io.l4desk.janus-upstream-sha256="a3e211e601980f0f618e1189705a2d23d36abc2b81c178a4c5cf20072edb19e5" \
      org.opencontainers.image.licenses="GPL-3.0-only"
COPY private-config-entrypoint.sh /usr/local/bin/l4media-private-config
RUN chmod 755 /usr/local/bin/l4media-private-config
ENTRYPOINT ["/usr/local/bin/l4media-private-config"]
CMD ["/usr/local/bin/janus", "--configs-folder=/run/l4media-janus"]
