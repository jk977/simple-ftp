SRC ?= ./src
INCLUDE ?= ./include
BUILD ?= ./build
TESTS ?= ./tests

C = $(CC) $(CFLAGS)
CC := gcc
CFLAGS := -I$(INCLUDE) -std=c99 -Wall -Wextra -Wpedantic -Werror

ifdef DEBUG
	CFLAGS += -O0 -g3 -fsanitize=address,leak,undefined
else
	CFLAGS += -O2 -DNDEBUG
endif

OBJECTS := mftp.o mftpserve.o
OBJECT_FILES := $(foreach obj, $(OBJECTS), $(BUILD)/$(obj))

.PHONY: all paths

all: mftp mftpserve 
	$(C) $(OBJECT_FILES) $(SRC)/assignment8.c -o $(BUILD)/assignment8

$(BUILD)/mftp.o: paths $(SRC)/mftp.c
	$(C) $(SRC)/mftp.c -o $@

$(BUILD)/mftpserve.o: paths $(SRC)/mftpserve.c $(OBJECT_FILES)
	$(C) $(OBJECT_FILES) $(SRC)/mftpserve.c -o $@

tests: paths

test-%: $(TESTS)/test_%.c
	$(C) $(OBJECT_FILES) $^ -o $(BUILD)/$@

paths:
	mkdir -p build
