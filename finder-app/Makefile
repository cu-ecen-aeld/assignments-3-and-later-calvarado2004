CC = gcc
CROSS_COMPILE =
TARGET = writer
SRC = writer.c
OBJ = $(SRC:.c=.o)

$(TARGET): $(OBJ)
	$(CROSS_COMPILE)$(CC) -o $@ $^

%.o: %.c
	$(CROSS_COMPILE)$(CC) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJ)

