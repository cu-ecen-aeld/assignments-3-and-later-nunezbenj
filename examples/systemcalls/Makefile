# Define the compiler and compiler flags
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra

# Define the target executable and source files
TARGET = systemcalls_test 
SRCS = systemcalls.c test_systemcalls.c

# Default target (optimized build)
all: CFLAGS += -O2
all: $(TARGET)

# Debug target
debug: CFLAGS += -g
debug: $(TARGET)

# Rule to build the target executable
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

# Rule to clean up the build files
clean:
	rm -f $(TARGET)

# Rule to run the program
run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run

