CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -pedantic -std=c11 -D_GNU_SOURCE
LDFLAGS ?= -lhiredis -ljansson

TARGET = l4media-ingress
SRCS = src/l4media_ingress.c
OBJS = $(SRCS:.c=.o)
HEADERS = $(wildcard src/*.h)

TEST_TARGET = test_ingress_unit
TEST_SRCS = tests/test_ingress_unit.c

all: contract-check $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJS): $(HEADERS)

test: contract-check $(TEST_TARGET) test_feedback test_media_deadline test_media_v2 test_media_camera test_media_v3
	./$(TEST_TARGET)
	./test_feedback
	./test_media_deadline
	./test_media_v2
	./test_media_camera
	./test_media_v3

$(TEST_TARGET): $(TEST_SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_feedback: tests/test_feedback.c src/l4media_ingress.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_media_deadline: tests/test_media_deadline.c src/l4media_ingress.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_media_v2: tests/test_media_v2.c tests/test_media_common.h src/l4media_ingress.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_media_camera: tests/test_media_camera.c tests/test_media_common.h src/l4media_ingress.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_media_v3: tests/test_media_v3.c tests/test_media_common.h src/l4media_ingress.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_TARGET) test_feedback test_media_deadline test_media_v2 test_media_camera test_media_v3

.PHONY: all clean test contract-check

contract-check:
	@sha256sum -c spec/SHA256SUMS > /dev/null
	@sha256sum -c spec/camera-v1/SHA256SUMS > /dev/null
	@sha256sum -c spec/media-v3/SHA256SUMS > /dev/null
