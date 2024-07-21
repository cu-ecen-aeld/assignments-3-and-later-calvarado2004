# finder-app/Makefile
TARGET = writer
SRC = writer.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
    $(CC) -o $@ $^

%.o: %.c
    $(CC) -c $< -o $@

.PHONY: clean
clean:
    rm -f $(TARGET) $(OBJ)

