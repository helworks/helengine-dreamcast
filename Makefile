# Minimal KOS bootstrap for the native Dreamcast renderer.
# Task 3 adds the reproducible Docker/manifest wrappers around this interface.

TARGET := helengine_dc

ifndef BUILD_ROOT
$(error BUILD_ROOT is required; pass an external output directory such as BUILD_ROOT=/out)
endif

KOS_BASE ?= /opt/toolchains/dc/kos

ELF := $(BUILD_ROOT)/$(TARGET).elf
MAP := $(BUILD_ROOT)/$(TARGET).map
OBJ_ROOT := $(BUILD_ROOT)/obj

SOURCES := \
    src/main.cpp \
    src/platform/dreamcast/DreamcastBootHost.cpp \
    src/platform/dreamcast/DreamcastVideo.cpp \
    src/platform/dreamcast/DreamcastRasterRenderer.cpp \
    src/platform/dreamcast/DreamcastGeometryPipeline.cpp \
    src/platform/dreamcast/DreamcastCubeMesh.cpp
OBJECTS := $(patsubst src/%.cpp,$(OBJ_ROOT)/%.o,$(SOURCES))

CPPFLAGS += -Isrc
CXXFLAGS += -std=c++17 -Wall -Wextra -Werror -g
HOST_CXX ?= c++

.PHONY: all host-test clean

all: $(ELF)

include $(KOS_BASE)/Makefile.rules

$(ELF): $(OBJECTS)
	@mkdir -p "$(dir $@)"
	kos-c++ -g -Wl,-Map,$(MAP) -o $@ $(OBJECTS)

$(OBJ_ROOT)/%.o: src/%.cpp
	@mkdir -p "$(dir $@)"
	kos-c++ $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

host-test:
	@mkdir -p "$(BUILD_ROOT)"
	$(HOST_CXX) -std=c++17 -Wall -Wextra -Werror -Isrc \
		tests/native/DreamcastGeometryPipelineTests.cpp \
		src/platform/dreamcast/DreamcastGeometryPipeline.cpp \
		src/platform/dreamcast/DreamcastCubeMesh.cpp \
		-o $(BUILD_ROOT)/dreamcast-geometry-tests
	$(HOST_CXX) -std=c++17 -Wall -Wextra -Werror -Isrc \
		tests/native/DreamcastRasterSeamTests.cpp \
		-o $(BUILD_ROOT)/dreamcast-raster-seam-tests
	$(BUILD_ROOT)/dreamcast-geometry-tests
	$(BUILD_ROOT)/dreamcast-raster-seam-tests

clean:
	-rm -rf "$(OBJ_ROOT)" "$(ELF)" "$(MAP)"
