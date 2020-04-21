SRC ?= ./src
INCLUDE ?= ./include
BUILD ?= ./build
TESTS ?= ./tests

C = $(CC) $(CFLAGS)
CC := gcc
CFLAGS := -I$(INCLUDE) -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror

ifdef DEBUG
	CFLAGS += -O0 -g3 -fsanitize=address,leak,undefined
else
	CFLAGS += -O2 -DNDEBUG
endif

OBJECTS := logging.o util.o protocol.o
OBJECT_FILES := $(foreach obj, $(OBJECTS), $(BUILD)/$(obj))

.PHONY: all paths

all: $(BUILD)/mftp $(BUILD)/mftpserve

$(BUILD)/mftp: paths $(SRC)/mftp.c $(OBJECT_FILES)
	$(C) $(OBJECT_FILES) $(SRC)/mftp.c -o $@

$(BUILD)/mftpserve: paths $(SRC)/mftpserve.c $(OBJECT_FILES)
	$(C) $(OBJECT_FILES) $(SRC)/mftpserve.c -o $@

$(BUILD)/%.o: $(SRC)/%.c
	$(C) $^ -c -o $@

tests: paths

test-%: $(TESTS)/test_%.c
	$(C) $(OBJECT_FILES) $^ -o $(BUILD)/$@

paths:
	mkdir -p build
