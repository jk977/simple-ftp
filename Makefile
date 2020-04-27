PORT ?= 49999

SRC ?= ./src
INCLUDE ?= ./include
BUILD ?= ./build

C = $(CC) $(CFLAGS)
CC := gcc
CFLAGS := -I$(INCLUDE) -std=c99 -DCFG_PORT=$(PORT) -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror

ifdef DEBUG
	CFLAGS += -g3 -fsanitize=address,leak,undefined
else
	CFLAGS += -DNDEBUG
endif

OBJECTS := logging.o io.o util.o commands.o
OBJECT_FILES := $(foreach obj, $(OBJECTS), $(BUILD)/$(obj))

.PHONY: all paths clean tags

all: $(BUILD)/mftp $(BUILD)/mftpserve

$(BUILD)/mftp: paths $(SRC)/mftp.c $(OBJECT_FILES)
	$(C) $(OBJECT_FILES) $(SRC)/mftp.c -o $@

$(BUILD)/mftpserve: paths $(SRC)/mftpserve.c $(OBJECT_FILES)
	$(C) $(OBJECT_FILES) $(SRC)/mftpserve.c -o $@

$(BUILD)/%.o: $(SRC)/%.c
	$(C) $^ -c -o $@

paths:
	mkdir -p build

clean:
	rm -f build/* tags

tags:
	ctags -R
