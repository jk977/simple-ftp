PORT ?= 49999

SRC ?= ./src
INCLUDE ?= ./include
BUILD ?= ./build

CC := gcc
CFLAGS := -I$(INCLUDE) -std=c99 -DCFG_PORT=$(PORT) -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror

C = $(CC) $(CFLAGS)

ifdef DEBUG
	CFLAGS += -g3 -fsanitize=address,leak,undefined
else
	CFLAGS += -DNDEBUG
endif

OBJECTS := logging.o io.o util.o commands.o
OBJECT_FILES := $(foreach obj, $(OBJECTS), $(BUILD)/$(obj))

.PHONY: all paths clean

all: mftp mftpserve

mftp: paths $(SRC)/mftp.c $(OBJECT_FILES)
	$(C) $(OBJECT_FILES) $(SRC)/mftp.c -o $(BUILD)/$@

mftpserve: paths $(SRC)/mftpserve.c $(OBJECT_FILES)
	$(C) $(OBJECT_FILES) $(SRC)/mftpserve.c -o $(BUILD)/$@

$(BUILD)/%.o: $(SRC)/%.c
	$(C) $^ -c -o $@

paths:
	mkdir -p build

clean:
	rm -f build/* tags
