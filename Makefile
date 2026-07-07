CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
SRC_DIR = src
TEST_DIR = tests
BIN = hwmon

SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/cpu_stat.c $(SRC_DIR)/mem_stat.c $(SRC_DIR)/gpu_stat.c $(SRC_DIR)/thermal.c $(SRC_DIR)/display.c
OBJS = $(SRCS:.c=.o)

TEST_SRCS = $(TEST_DIR)/test_cpu_stat.c $(TEST_DIR)/test_mem_stat.c
TEST_BINS = $(TEST_SRCS:.c=)

.PHONY: all clean test valgrind

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $(BIN) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: tests/test_cpu_stat tests/test_mem_stat
	./tests/test_cpu_stat
	./tests/test_mem_stat

tests/test_cpu_stat: tests/test_cpu_stat.c src/cpu_stat.c
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_cpu_stat.c src/cpu_stat.c

tests/test_mem_stat: tests/test_mem_stat.c src/mem_stat.c
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_mem_stat.c src/mem_stat.c

valgrind: $(BIN)
	@echo "Note: hwmon runs in an infinite loop - stop it with Ctrl+C after a few seconds."
	valgrind --leak-check=full --show-leak-kinds=all ./$(BIN)

clean:
	rm -f $(OBJS) $(BIN) $(TEST_BINS)
