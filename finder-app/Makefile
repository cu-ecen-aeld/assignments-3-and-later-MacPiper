CC = $(CROSS_COMPILE)gcc
CC_OPTS = -g -Wall
TARGET = writer
OBJ = writer.o
SRC = writer.c

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CC_OPTS) $(OBJ) -o $(TARGET)

%.o: %.c
	$(CC) $(CC_OPTS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)

.PHONY: all clean
