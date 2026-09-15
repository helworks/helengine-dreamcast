# KOS build for the Dreamcast renderer and the generated Core compatibility gate.

TARGET := helengine_dc

MODE ?= Diagnostic

ifeq ($(filter Diagnostic Authored,$(MODE)),)
$(error MODE must be Diagnostic or Authored)
endif

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
DEPS := $(OBJECTS:.o=.d)

ifeq ($(MODE),Authored)
ifndef GENERATED_CORE_ROOT
$(error GENERATED_CORE_ROOT is required in Authored mode)
endif
ifndef CONTENT_ROOT
$(error CONTENT_ROOT is required in Authored mode)
endif
ifndef CATALOG_ROOT
$(error CATALOG_ROOT is required in Authored mode)
endif
ifeq ($(wildcard $(GENERATED_CORE_ROOT)/Core.hpp),)
$(error GENERATED_CORE_ROOT does not contain Core.hpp: $(GENERATED_CORE_ROOT))
endif
ifeq ($(wildcard $(CATALOG_ROOT)),)
$(error CATALOG_ROOT does not exist: $(CATALOG_ROOT))
endif
ifeq ($(wildcard $(CONTENT_ROOT)),)
$(error CONTENT_ROOT does not exist: $(CONTENT_ROOT))
endif
CPPFLAGS += -DHELENGINE_DREAMCAST_HAS_GENERATED_CORE \
    -I$(GENERATED_CORE_ROOT) -I$(GENERATED_CORE_ROOT)/runtime \
    -I$(CATALOG_ROOT)
GENERATED_OBJECT := $(OBJ_ROOT)/generated/generated_unity.o
OBJECTS += $(GENERATED_OBJECT)
DEPS += $(GENERATED_OBJECT:.o=.d)
GENERATED_UNITY_SOURCE := $(GENERATED_CORE_ROOT)/generated_unity.cpp
GENERATED_CPP_SOURCES := $(shell find "$(GENERATED_CORE_ROOT)" -type f -name '*.cpp' ! -name 'helengine_core_unity.cpp' | sort)
GENERATED_UNITY_INCLUDES := $(shell tr -d '\r' < "$(GENERATED_UNITY_SOURCE)" | sed -n 's/^#include "\(.*\.cpp\)".*/\1/p')
GENERATED_EXTRA_SOURCES := $(filter-out $(GENERATED_UNITY_SOURCE) $(addprefix $(GENERATED_CORE_ROOT)/,$(GENERATED_UNITY_INCLUDES)),$(GENERATED_CPP_SOURCES))
GENERATED_EXTRA_OBJECTS := $(patsubst $(GENERATED_CORE_ROOT)/%.cpp,$(OBJ_ROOT)/generated-extra/%.o,$(GENERATED_EXTRA_SOURCES))
OBJECTS += $(GENERATED_EXTRA_OBJECTS)
DEPS += $(GENERATED_EXTRA_OBJECTS:.o=.d)
GENERATED_SOURCE_LIST := $(BUILD_ROOT)/generated-source-list.txt
GENERATED_UNITY_INCLUDE_LIST := $(BUILD_ROOT)/generated-unity-includes.txt
GENERATED_EXTRA_RELATIVE_LIST := $(BUILD_ROOT)/generated-extra-relative-list.txt
ALL_GENERATED_SOURCES := $(GENERATED_UNITY_SOURCE) $(GENERATED_EXTRA_SOURCES)
GENERATED_WARNING_FLAGS := \
    -Wno-error=unused-function \
    -Wno-error=unused-local-typedefs \
    -Wno-error=unused-parameter \
    -Wno-error=delete-non-virtual-dtor \
    -Wno-error=reorder \
    -Wno-error=sign-compare
all: $(GENERATED_SOURCE_LIST)
AUTHORED_SOURCES := \
    src/platform/dreamcast/DreamcastContentPath.cpp \
    src/platform/dreamcast/DreamcastContentStreamSource.cpp \
    src/platform/dreamcast/DreamcastInputBackend.cpp \
    src/platform/dreamcast/DreamcastRenderManager2D.cpp \
    src/platform/dreamcast/DreamcastRenderManager3D.cpp \
    src/platform/dreamcast/DreamcastRuntimeSceneCatalog.cpp \
    src/platform/dreamcast/DreamcastRuntimeHost.cpp
AUTHORED_OBJECTS := $(patsubst src/%.cpp,$(OBJ_ROOT)/%.o,$(AUTHORED_SOURCES))
OBJECTS += $(AUTHORED_OBJECTS)
DEPS += $(AUTHORED_OBJECTS:.o=.d)
ifeq ($(RUNTIME_INIT_PROBE),1)
CPPFLAGS += -DHELENGINE_DREAMCAST_RUNTIME_INIT_PROBE=1
CATALOG_SOURCE := $(CATALOG_ROOT)/dc_runtime_scene_catalog.cpp
CATALOG_HEADER := $(CATALOG_ROOT)/dc_runtime_scene_catalog.hpp
CATALOG_OBJECT := $(OBJ_ROOT)/catalog/dc_runtime_scene_catalog.o
OBJECTS += $(CATALOG_OBJECT)
DEPS += $(CATALOG_OBJECT:.o=.d)
endif
endif

CPPFLAGS += -Isrc
CXXFLAGS += -std=c++20 -Wall -Wextra -Werror -g
HOST_CXX ?= c++

.PHONY: all host-test clean
.DELETE_ON_ERROR:

all: $(ELF) $(MAP)

include $(KOS_BASE)/Makefile.rules

$(ELF) $(MAP) &: $(OBJECTS)
	@mkdir -p "$(dir $(ELF))"
	kos-c++ -g -Wl,-Map,$(MAP) -o $(ELF) $(OBJECTS)

$(OBJ_ROOT)/%.o: src/%.cpp
	@mkdir -p "$(dir $@)"
	kos-c++ $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c $< -o $@

ifeq ($(MODE),Authored)
$(GENERATED_OBJECT): $(GENERATED_CORE_ROOT)/generated_unity.cpp
	@mkdir -p "$(dir $@)"
	kos-c++ $(CXXFLAGS) $(GENERATED_WARNING_FLAGS) $(CPPFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(OBJ_ROOT)/generated-extra/%.o: $(GENERATED_CORE_ROOT)/%.cpp
	@mkdir -p "$(dir $@)"
	kos-c++ $(CXXFLAGS) $(GENERATED_WARNING_FLAGS) $(CPPFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

ifeq ($(RUNTIME_INIT_PROBE),1)
$(CATALOG_OBJECT): $(CATALOG_SOURCE) $(CATALOG_HEADER)
	@mkdir -p "$(dir $@)"
	kos-c++ $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"
endif

$(GENERATED_SOURCE_LIST): $(ALL_GENERATED_SOURCES)
	@mkdir -p "$(dir $@)"
	printf '%s\n' $(ALL_GENERATED_SOURCES) > "$@"
	@test "$$(wc -l < "$@")" -eq "$(words $(ALL_GENERATED_SOURCES))"
	@test "$$(sort -u "$@" | wc -l)" -eq "$(words $(ALL_GENERATED_SOURCES))"
	tr -d '\r' < "$(GENERATED_UNITY_SOURCE)" | sed -n 's/^#include "\(.*\.cpp\)".*/\1/p' | sort -u > "$(GENERATED_UNITY_INCLUDE_LIST)"
	printf '%s\n' $(GENERATED_EXTRA_SOURCES) | sed 's#^$(GENERATED_CORE_ROOT)/##' | sort -u > "$(GENERATED_EXTRA_RELATIVE_LIST)"
	@test -z "$$(comm -12 "$(GENERATED_UNITY_INCLUDE_LIST)" "$(GENERATED_EXTRA_RELATIVE_LIST)")"
endif

-include $(DEPS)

host-test:
	@mkdir -p "$(BUILD_ROOT)"
	$(HOST_CXX) -std=c++17 -Wall -Wextra -Werror -Isrc \
		tests/native/DreamcastGeometryPipelineTests.cpp \
		src/platform/dreamcast/DreamcastGeometryPipeline.cpp \
		src/platform/dreamcast/DreamcastCubeMesh.cpp \
		-o $(BUILD_ROOT)/dreamcast-geometry-tests
	$(HOST_CXX) -std=c++17 -Wall -Wextra -Werror -Isrc \
		tests/native/DreamcastRasterSeamTests.cpp \
		src/platform/dreamcast/DreamcastGeometryPipeline.cpp \
		src/platform/dreamcast/DreamcastCubeMesh.cpp \
		-o $(BUILD_ROOT)/dreamcast-raster-seam-tests
	$(HOST_CXX) -std=c++17 -Wall -Wextra -Werror -Isrc \
		tests/native/DreamcastContentPathTests.cpp \
		src/platform/dreamcast/DreamcastContentPath.cpp \
		-o $(BUILD_ROOT)/dreamcast-content-path-tests
	$(BUILD_ROOT)/dreamcast-geometry-tests
	$(BUILD_ROOT)/dreamcast-raster-seam-tests
	$(BUILD_ROOT)/dreamcast-content-path-tests

clean:
	-rm -rf "$(OBJ_ROOT)" "$(ELF)" "$(MAP)"
