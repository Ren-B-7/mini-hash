INSTALL_NAME := mash
TARGET_NAME := mini-hash
CC := gcc
BUILD_DIR := build
BIN_DIR := bin

SRCS := $(wildcard src/*.c)
HDRS := $(wildcard src/include/*.h)
OBJS := $(SRCS:src/%.c=$(BUILD_DIR)/%.o)
TARGET := $(BIN_DIR)/$(TARGET_NAME)
HDRS_SRCS_MERGED := $(SRCS) $(HDRS)

UNAME_S := $(shell uname -s 2>/dev/null || echo unknown)

INSTALL_DIR ?= $(HOME)/.local/bin
SHARE_DIR ?= $(HOME)/.local/share/$(TARGET_NAME)

# --- Build Profile Selection ---
# Usage: make PROFILE=release
# Profiles: dev, release, fast, tiny, minimal
PROFILE ?= dev
HARDENED ?= 0

IS_GCC := $(shell $(CC) -v 2>&1 | grep -q "gcc" && echo 1 || echo 0)

# POSIX/Linux extensions (lstat, realpath, AF_ALG, etc.) needed under
# -std=c99 -pedantic-errors
POSIX_FLAGS := -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE

# Strict compilation flags
CFLAGS = -std=c99 $(POSIX_FLAGS) -pedantic -pedantic-errors \
         -Wall -Wextra -Wformat=2 -Wformat-security -Wnull-dereference \
         -Isrc -Isrc/include $(CJSON_INC) $(DARWIN_FLAGS)

# GCC-specific warnings
ifeq ($(IS_GCC),1)
    GCC_FLAGS = -Wstack-protector -Wtrampolines -Walloca -Wvla \
                -Warray-bounds=2 -Wimplicit-fallthrough=3 -Wshift-overflow=2 \
                -Wcast-qual -Wcast-align=strict -Wconversion -Wsign-conversion \
                -Wlogical-op -Wduplicated-cond -Wduplicated-branches -Wrestrict \
                -Wnested-externs -Winline -Wundef -Wstrict-prototypes \
                -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls \
                -Wshadow -Wwrite-strings -Wfloat-equal -Wpointer-arith \
                -Wbad-function-cast -Wold-style-definition
    CFLAGS += $(GCC_FLAGS)
endif

HARDENING_C = -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE \
			  -fstack-clash-protection -fcf-protection
HARDENING_L = -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack \
			  -Wl,-z,separate-code -pie

ifeq ($(HARDENED),1)
    SELECTED_HARDENING_C = $(HARDENING_C)
    SELECTED_HARDENING_L = $(HARDENING_L)
else
    SELECTED_HARDENING_C =
    SELECTED_HARDENING_L =
endif

MINIMAL_FLAGS =
ifeq ($(PROFILE),release)
    OPTFLAGS = -Os -march=x86-64 -flto
    STRIP_BINARY = 1
    PORTABLE = 1
else ifeq ($(PROFILE),fast)
    OPTFLAGS = -O3 -march=native -flto
    STRIP_BINARY = 1
    PORTABLE = 0
else ifeq ($(PROFILE),tiny)
    OPTFLAGS = -Os -flto
    STRIP_BINARY = 1
    PORTABLE = 1
else ifeq ($(PROFILE),minimal)
    OPTFLAGS = -Os -march=x86-64 -flto
    STRIP_BINARY = 1
    PORTABLE = 1
    MINIMAL_FLAGS = -DMINIMAL_BUILD
else
    # dev (default)
    OPTFLAGS = -O3 -march=native -flto
    STRIP_BINARY = 0
    PORTABLE = 0
endif

CLANG_TIDY_CHECKS := -checks=-bugprone-easily-swappable-parameters,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling
CLANG_TIDY_FLAGS := -std=c11 -Wall -Wextra -Isrc/include

PGO_FLAGS =
PGO_GEN_FLAGS = -fprofile-generate
PGO_USE_FLAGS = -fprofile-use

ALL_CFLAGS = $(CFLAGS) $(SELECTED_HARDENING_C) $(OPTFLAGS) $(PGO_FLAGS) $(MINIMAL_FLAGS) $(DATA_FLAGS) -pthread
LD_FLAGS = $(SELECTED_HARDENING_L) $(PGO_FLAGS)

.PHONY: all run clean format format-ci lint directories lint-c lint-makefile format-c format-makefile format-c-ci format-makefile-ci install uninstall

all: directories $(TARGET)

check-tools:
	@command -v clang-format > /dev/null 2>&1 || \
	    { echo "ERROR: clang-format not found. Install LLVM and ensure it is on PATH."; exit 1; }
	@command -v clang-tidy > /dev/null 2>&1 || \
	    { echo "ERROR: clang-tidy not found. Install LLVM and ensure it is on PATH."; exit 1; }
	@command -v mbake > /dev/null 2>&1 || \
	    { echo "ERROR: mbake not found. Install mbake and ensure it is on PATH."; exit 1; }

directories:
	@mkdir -p $(BIN_DIR) $(BUILD_DIR)

$(TARGET): $(OBJS)
	@echo "Linking $@ ..."
	$(CC) $(ALL_CFLAGS) $(OBJS) -o $@ $(LD_FLAGS)

$(BUILD_DIR)/%.o: src/%.c
	@echo "Compiling $< (Profile: $(PROFILE), Platform: $(PLATFORM))..."
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/include/%.c
	@echo "Compiling $< (Profile: $(PROFILE), Platform: $(PLATFORM))..."
	$(CC) $(ALL_CFLAGS) -c $< -o $@

run: all
	./$(TARGET)

clean:
	@echo "Cleaning up build artifacts..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)

format: format-c format-makefile
format-ci: format-c-ci format-makefile-ci

format-c:
	@echo "Formatting C source files"
	clang-format -style=file:./.clang-format -i $(SRCS_ALL) $(HDRS) $(JSON_ASSETS)

format-c-ci:
	@echo "Checking C source file formats"
	clang-format --dry-run -style=file:./.clang-format -Werror $(SRCS_ALL) $(HDRS)

format-makefile:
	@echo "Formatting Makefile"
	mbake format --config ./.bake.toml Makefile

format-makefile-ci:
	@echo "Checking Makefile format"
	mbake format --config ./.bake.toml --check Makefile

lint: lint-c lint-makefile

lint-c:
	@echo "Running clang-tidy analysis"
	clang-tidy -checks=-*,bugprone-*,clang-analyzer-*,performance-* \
	$(HDRS_SRCS_MERGED) -- $(CFLAGS)

lint-makefile:
	@echo "Running Makefile analysis"
	mbake validate --config ./.bake.toml Makefile

install:
	install -m 755 $(TARGET) $(INSTALL_DIR)/$(INSTALL_NAME)

uninstall:
	rm -f $(INSTALL_DIR)/$(INSTALL_NAME)
