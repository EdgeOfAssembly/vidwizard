# Formal gate: CBMC on parse_time.c (depends on parent `test` via root Makefile).

CBMC ?= $(HOME)/.local/bin/cbmc
ROOT := ..

.PHONY: verify

verify:
	$(CBMC) $(ROOT)/src/parse_time.c parse_time_harness.c \
	  -I$(ROOT)/include \
	  --bounds-check --pointer-check --div-by-zero-check \
	  --conversion-check --signed-overflow-check --unsigned-overflow-check \
	  --unwind 128 --unwinding-assertions
