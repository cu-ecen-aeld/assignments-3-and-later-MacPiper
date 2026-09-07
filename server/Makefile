CC = $(CROSS_COMPILE)gcc
CC_OPTS = -g -Wall
TARGET = aesdsocket
OBJ = aesdsocket.o
SRC = aesdsocket.c

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CC_OPTS) $(OBJ) -o $(TARGET)

%.o: %.c
	$(CC) $(CC_OPTS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)

.PHONY: all clean
