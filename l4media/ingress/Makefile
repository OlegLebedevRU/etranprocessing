CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -pedantic -std=c11 -D_GNU_SOURCE
LDFLAGS ?= -lhiredis

TARGET = l4media-ingress
SRCS = src/l4media_ingress.c
OBJS = $(SRCS:.c=.o)

TEST_TARGET = test_ingress_unit
TEST_SRCS = tests/test_ingress_unit.c

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_TARGET) test_feedback
	./$(TEST_TARGET)
	./test_feedback

$(TEST_TARGET): $(TEST_SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test_feedback: tests/test_feedback.c src/l4media_ingress.c src/media_lifecycle.h src/media_redis.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_TARGET) test_feedback

.PHONY: all clean test
