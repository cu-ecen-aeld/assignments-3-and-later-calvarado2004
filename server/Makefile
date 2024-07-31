CC := gcc
CFLAGS := -Wall -Werror

TARGET := aesdsocket

.PHONY: all clean

all: $(TARGET)

$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) -o $(TARGET) aesdsocket.c

clean:
	rm -f $(TARGET)

