# Host-side unit tests — no AVR toolchain required.
#   make        build and run
#   make clean

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -O1 -g
SRC      = test_led.c \
           ../../../lib/smartiny-slave/smartiny_slave.c \
           ../firmware/core/led_core.c
BIN      = run_tests

.PHONY: test clean
test: $(BIN)
	./$(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -f $(BIN)
