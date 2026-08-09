# Flash-cost bench: BCD+exponent vs float+dtostrf on an ATtiny85.
#
#   make test    host correctness tests for the BCD implementation (no AVR needed)
#   make sizes   the flash table (needs avr-gcc + avr-size)
#   make         both

CC      ?= cc
AVRCC   ?= avr-gcc
AVRSIZE ?= avr-size
MCU     ?= attiny85
AVRFLAGS = -Os -mmcu=$(MCU)
CFLAGS   = -O1 -Wall -Wextra -std=c99

all: test sizes

test: test_num
	./test_num

test_num: test_num.c num.c
	$(CC) $(CFLAGS) -DSTAGE=4 -DHOST_TEST -o $@ test_num.c

# Each stage adds one capability, so the deltas attribute cost to a feature.
# Stage 0 is the empty harness -- subtract it to get net figures.
sizes:
	@printf 'stage  what                 BCD   float\n'
	@for s in 0 1 2 3 4; do \
	  case $$s in \
	    0) w='baseline (harness)  ' ;; \
	    1) w='+ display formatter ' ;; \
	    2) w='+ add / subtract    ' ;; \
	    3) w='+ multiply          ' ;; \
	    4) w='+ divide            ' ;; \
	  esac; \
	  $(AVRCC) $(AVRFLAGS) -DSTAGE=$$s -o .n$$s.elf num.c 2>/dev/null; \
	  $(AVRCC) $(AVRFLAGS) -DSTAGE=$$s -o .f$$s.elf flt.c 2>/dev/null; \
	  printf '  %s  %s %5s %7s\n' "$$s" "$$w" \
	    "$$($(AVRSIZE) .n$$s.elf | awk 'NR==2{print $$1+$$2}')" \
	    "$$($(AVRSIZE) .f$$s.elf | awk 'NR==2{print $$1+$$2}')"; \
	done
	@rm -f .n*.elf .f*.elf

clean:
	rm -f test_num .n*.elf .f*.elf

.PHONY: all test sizes clean
