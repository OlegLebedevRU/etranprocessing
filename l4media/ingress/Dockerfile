# Stage 1: Build binary
FROM alpine:3.19@sha256:b58899f069c47216f6002a6850143dc6fae0d35eb8b0df9300bbe6327b9c2171 AS builder

RUN apk add --no-cache gcc musl-dev make hiredis-dev jansson-dev

WORKDIR /build
COPY Makefile ./
COPY src/ ./src/
COPY tests/ ./tests/
COPY spec/ ./spec/
COPY openapi-camera-v1.json ./

RUN make test CFLAGS="-O2 -Wall -Wextra -Werror -pedantic -std=c11 -D_GNU_SOURCE"
RUN make CFLAGS="-O2 -Wall -Wextra -Werror -pedantic -std=c11 -D_GNU_SOURCE" LDFLAGS="-lhiredis -ljansson"

# Stage 2: Minimal runtime
FROM alpine:3.19@sha256:b58899f069c47216f6002a6850143dc6fae0d35eb8b0df9300bbe6327b9c2171

RUN apk add --no-cache curl ca-certificates hiredis jansson

WORKDIR /app
ARG SOURCE_REVISION
ARG SOURCE_ARCHIVE_SHA256
LABEL org.opencontainers.image.revision=$SOURCE_REVISION \
      io.l4desk.source-archive-sha256=$SOURCE_ARCHIVE_SHA256
COPY --from=builder /build/l4media-ingress /app/l4media-ingress

# Default routes configuration location
RUN mkdir -p /etc/l4media
COPY routes.conf /etc/l4media/routes.conf
COPY openapi.json /etc/l4media/openapi.json
COPY openapi-v2.json /etc/l4media/openapi-v2.json
COPY openapi-camera-v1.json /etc/l4media/openapi-camera-v1.json
COPY spec/hevc.schema.json /etc/l4media/hevc.schema.json
COPY spec/camera-v1/hevc.schema.json /etc/l4media/camera-v1.schema.json

EXPOSE 9000 9100

ENTRYPOINT ["/app/l4media-ingress"]
