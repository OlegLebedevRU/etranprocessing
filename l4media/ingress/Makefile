CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -pedantic -std=c11 -D_GNU_SOURCE
LDFLAGS ?=

TARGET = l4media-ingress
SRCS = src/l4media_ingress.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
