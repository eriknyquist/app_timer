OUTPUT_DIR := build
OBJ_DIR := $(OUTPUT_DIR)/obj

ifeq ($(OS),Windows_NT)
# Windows-specific vars; if you did a custom install of MinGW, or if you are using some
# other compiler, then you might need to change some of these values
    BIN_DIR := C:/MinGW/bin
    GCC := $(BIN_DIR)/gcc
    LD := $(BIN_DIR)/ld
    MKDIR := mkdir -p
    RMDIR := rm -rf
else
    ifeq ($(shell uname -s),Linux)
# Linux-specific vars
        GCC := gcc
        LD := ld
        MKDIR := mkdir -p
        RMDIR := rm -rf
    endif
endif

SRC_DIRS := ../.. ./hw_model
VPATH := $(SRC_DIRS) .

COMMON_SRC_FILES := $(foreach DIR,$(SRC_DIRS),$(wildcard $(DIR)/*.c))
TEST_SRC_FILES := $(COMMON_SRC_FILES) test_main.c
EXAMPLE_SRC_FILES := $(COMMON_SRC_FILES) example_main.c
TEST_OBJ_FILES := $(patsubst %.c,%.o,$(addprefix $(OBJ_DIR)/,$(notdir $(TEST_SRC_FILES))))
EXAMPLE_OBJ_FILES := $(patsubst %.c,%.o,$(addprefix $(OBJ_DIR)/,$(notdir $(EXAMPLE_SRC_FILES))))

INCLUDES := $(addprefix -I, $(SRC_DIRS))
FLAGS := -std=c99 -Wall -O2 -DAPP_TIMER_RUNNING_COUNT_UINT64 -DAPP_TIMER_COUNT_UINT32
CFLAGS := $(FLAGS) $(INCLUDES)

EXAMPLE_PROG := $(OUTPUT_DIR)/example_main
TEST_PROG := $(OUTPUT_DIR)/test_main

.PHONY: clean output_dir

default: example

all: example test

example: $(EXAMPLE_PROG)

test: $(TEST_PROG)

$(EXAMPLE_PROG): output_dir $(EXAMPLE_OBJ_FILES)
	$(GCC) $(LFLAGS) $(EXAMPLE_OBJ_FILES) -o $@

$(TEST_PROG): output_dir $(TEST_OBJ_FILES)
	$(GCC) $(LFLAGS) $(TEST_OBJ_FILES) -o $@

$(OBJ_DIR)/%.o: %.c
	$(GCC) $(CFLAGS) -c -o $@ $<

output_dir:
	@$(MKDIR) $(OUTPUT_DIR)
	@$(MKDIR) $(OBJ_DIR)

clean:
	@$(RMDIR) $(OUTPUT_DIR)
	@echo "Outputs removed"
