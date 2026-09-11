# Stage 1: Build binary
FROM alpine:3.19 AS builder

RUN apk add --no-cache gcc musl-dev make

WORKDIR /build
COPY Makefile ./
COPY src/ ./src/
COPY tests/ ./tests/

RUN make test CFLAGS="-O2 -Wall -Wextra -pedantic -std=c11 -D_GNU_SOURCE"
RUN make CFLAGS="-O2 -Wall -Wextra -pedantic -std=c11 -D_GNU_SOURCE -static"

# Stage 2: Minimal runtime
FROM alpine:3.19

RUN apk add --no-cache curl ca-certificates

WORKDIR /app
COPY --from=builder /build/l4media-ingress /app/l4media-ingress

# Default routes configuration location
RUN mkdir -p /etc/l4media
COPY routes.conf /etc/l4media/routes.conf

EXPOSE 9000 9100

ENTRYPOINT ["/app/l4media-ingress"]
