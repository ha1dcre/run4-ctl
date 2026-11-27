# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Eugen Schott
CC ?= cc
CXX ?= c++
PYTHON ?= python3
BUILD ?= build$(if $(filter 1,$(SANITIZE)),/sanitized,)
SDK_VERSION := $(shell $(PYTHON) package.py --version)
# In the repository use the canonical MIT codec; archives carry that same codec.
CODEC_DIR := $(if $(wildcard ../src/r4p.c),../src,src)
CPPFLAGS += -Iinclude -I$(CODEC_DIR)
CFLAGS += -std=c99 -O1 -g -Wall -Wextra -Werror -pedantic
CXXFLAGS += -std=c++11 -O1 -g -Wall -Wextra -Werror -pedantic
ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=address,undefined -fno-sanitize-recover=all
CXXFLAGS += -fsanitize=address,undefined -fno-sanitize-recover=all
LDFLAGS += -fsanitize=address,undefined
endif
OBJ = $(BUILD)/run4ctl.o $(BUILD)/run4ctl_transport.o $(BUILD)/r4p.o
.PHONY: all test example archive package-check clean
all: test example
$(BUILD):
	mkdir -p $@
$(BUILD)/run4ctl.o: src/run4ctl.c include/run4ctl.h $(CODEC_DIR)/r4p.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/run4ctl_transport.o: src/run4ctl_transport.c include/run4ctl_transport.h include/run4ctl.h $(CODEC_DIR)/r4p.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/r4p.o: $(CODEC_DIR)/r4p.c $(CODEC_DIR)/r4p.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/test: tests/test.c $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $@
$(BUILD)/cpp_smoke: tests/cpp_smoke.cpp $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ $(LDFLAGS) -o $@
test: $(BUILD)/test $(BUILD)/cpp_smoke
	$(BUILD)/test
	$(BUILD)/cpp_smoke
$(BUILD)/minimal: examples/minimal.c $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $@
example: $(BUILD)/minimal
	$(BUILD)/minimal
	$(BUILD)/minimal --display-feedback
archive:
	$(PYTHON) package.py --output $(BUILD)/run4ctl-sdk-$(SDK_VERSION).zip
package-check:
	$(PYTHON) tests/package_test.py
clean:
	rm -rf $(BUILD)
