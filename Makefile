CXX ?= c++
AR ?= ar
CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic
SOURCES := src/grid.cpp src/rs.cpp src/payload.cpp src/image.cpp src/common.cpp
BUILD_DIR ?= build
# LeakSanitizer is unavailable in the reported Apple/Xcode ASan runtime.
# Linux keeps leak checking enabled. Override explicitly when supported:
#   make sanitize SANITIZER_DETECT_LEAKS=1   (or =0 to disable leak checks)
SANITIZER_DETECT_LEAKS ?= $(if $(filter Darwin,$(shell uname -s)),0,1)
OBJECTS := $(SOURCES:src/%.cpp=$(BUILD_DIR)/%.o)
-include $(OBJECTS:.o=.d)
.DEFAULT_GOAL := all
.PHONY: all clean test test-core test-gate test-writing test-reference sanitize
all: $(BUILD_DIR)/libspecqr_reader.a $(BUILD_DIR)/specqr-reader $(BUILD_DIR)/consumer
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
$(BUILD_DIR)/%.o: src/%.cpp include/specqr/reader.hpp src/internal.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD_DIR)/libspecqr_reader.a: $(OBJECTS)
	$(AR) rcs $@ $^
$(BUILD_DIR)/specqr-reader: tools/reader_cli.cpp $(BUILD_DIR)/libspecqr_reader.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/libspecqr_reader.a $(LDFLAGS) -o $@
$(BUILD_DIR)/consumer: examples/consumer.cpp $(BUILD_DIR)/libspecqr_reader.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/libspecqr_reader.a $(LDFLAGS) -o $@
$(BUILD_DIR)/api_tests: tests/api_tests.cpp $(BUILD_DIR)/libspecqr_reader.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/libspecqr_reader.a $(LDFLAGS) -o $@
$(BUILD_DIR)/payload_rs_tests: tests/payload_rs_tests.cpp tests/rs_vectors.inc $(BUILD_DIR)/libspecqr_reader.a
	$(CXX) $(CPPFLAGS) -Isrc $(CXXFLAGS) $< $(BUILD_DIR)/libspecqr_reader.a $(LDFLAGS) -o $@
$(BUILD_DIR)/fuzz_smoke: tests/fuzz_smoke.cpp $(BUILD_DIR)/libspecqr_reader.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/libspecqr_reader.a $(LDFLAGS) -o $@
$(BUILD_DIR)/image_boundary_tests: tests/image_boundary_tests.cpp $(BUILD_DIR)/libspecqr_reader.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/libspecqr_reader.a $(LDFLAGS) -o $@
$(BUILD_DIR)/fnc1_public_tests: tests/fnc1_public_tests.cpp tests/fnc1_vectors.inc $(BUILD_DIR)/libspecqr_reader.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD_DIR)/libspecqr_reader.a $(LDFLAGS) -o $@
$(BUILD_DIR)/grid_padding_tests: tests/grid_padding_tests.cpp $(BUILD_DIR)/libspecqr_reader.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG $< $(BUILD_DIR)/libspecqr_reader.a $(LDFLAGS) -o $@
test-core: all $(BUILD_DIR)/api_tests $(BUILD_DIR)/payload_rs_tests $(BUILD_DIR)/fuzz_smoke $(BUILD_DIR)/image_boundary_tests $(BUILD_DIR)/grid_padding_tests $(BUILD_DIR)/fnc1_public_tests
	$(BUILD_DIR)/api_tests
	$(BUILD_DIR)/payload_rs_tests
	$(BUILD_DIR)/fuzz_smoke 20000
	$(BUILD_DIR)/image_boundary_tests
	$(BUILD_DIR)/grid_padding_tests fixtures/grids/segno-v01-H-m0.grid
	$(BUILD_DIR)/fnc1_public_tests
test-gate:
	python3 -B tests/evaluator_gate_tests.py --output $(BUILD_DIR)/evaluator-gate.json
test-writing:
	python3 -B tests/test_writing.py
	python3 -B tools/verify_writing.py
test: test-core test-gate test-writing
test-reference: test
	@test -f fixtures/manifest.json || { echo "Full reference corpus is separate; fixtures/manifest.json is required for test-reference."; exit 2; }
	python3 tools/evaluate.py --binary $(BUILD_DIR)/specqr-reader --fail-on-mismatch
sanitize:
	ASAN_OPTIONS="detect_leaks=$(SANITIZER_DETECT_LEAKS):halt_on_error=1" UBSAN_OPTIONS=halt_on_error=1 $(MAKE) BUILD_DIR=build-make-sanitize CXXFLAGS="-O1 -g -std=c++17 -Wall -Wextra -Wpedantic -fno-omit-frame-pointer -fsanitize=address,undefined" LDFLAGS="-fsanitize=address,undefined" test-core
clean:
	rm -rf $(BUILD_DIR)
