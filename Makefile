# aprs-core -- plain make, C99, no dependencies besides libm.
#   make            static + shared library into $(BUILD)
#   make test       build and run the C tests (decoder, encoders, passcode, filter, vectors)
#   make sanitize   the C tests under AddressSanitizer + UndefinedBehaviorSanitizer
#   make ref        regenerate tests/vectors from the references (tools/README.md)
#   make BUILD=/abs/dir shared   out-of-tree build (used by wave-owl)
BUILD ?= build
CC ?= cc
CFLAGS ?= -O2
CORE_FLAGS = -std=c99 -Wall -Wextra -Werror -pedantic -fPIC -Iinclude -D_POSIX_C_SOURCE=200809L
TEST_FLAGS = -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude -D_POSIX_C_SOURCE=200809L
LDLIBS = -lm

UNAME := $(shell uname)
ifeq ($(UNAME),Darwin)
  SHLIB = libaprs.dylib
  SHFLAGS = -dynamiclib -install_name @rpath/libaprs.dylib
else
  SHLIB = libaprs.so
  SHFLAGS = -shared -Wl,-soname,libaprs.so
endif

HDRS = include/aprs.h src/aprs_internal.h
OBJ = $(BUILD)/aprs_decode.o $(BUILD)/aprs_encode.o $(BUILD)/aprs_filter.o

all: static shared
static: $(BUILD)/libaprs.a
shared: $(BUILD)/$(SHLIB)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(HDRS) | $(BUILD)
	$(CC) $(CORE_FLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/libaprs.a: $(OBJ)
	ar rcs $@ $^

$(BUILD)/$(SHLIB): $(OBJ)
	$(CC) $(SHFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/test_aprs: tests/test_aprs.c $(BUILD)/libaprs.a
	$(CC) $(TEST_FLAGS) $(CFLAGS) $< $(BUILD)/libaprs.a -o $@ $(LDLIBS)

test: $(BUILD)/test_aprs
	$(BUILD)/test_aprs tests/vectors/decode.tsv

SAN_FLAGS = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all

sanitize:
	$(MAKE) --no-print-directory BUILD=$(BUILD)/san CFLAGS="$(SAN_FLAGS)" \
		LDLIBS="$(LDLIBS) $(SAN_FLAGS)" test

ref:
	python3 tools/gen_vectors.py

clean:
	rm -rf $(BUILD)

.PHONY: all static shared test sanitize ref clean
