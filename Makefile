###########
# variables
###########
VULKAN_MEMORY_ALLOCATOR_INCLUDE_PATH := /mirrors/VulkanMemoryAllocator/include

CC := clang
CXX := clang++
CCXXFLAGS := -Wall -Wextra -Wpedantic -g -Wno-unused-parameter -Wno-dangling-else $(if $(SANITISE),-fsanitize=$(SANITISE)) -Wno-logical-op-parentheses -fdiagnostics-show-template-tree  -fdiagnostics-color=always -Wno-parentheses -I$(VULKAN_MEMORY_ALLOCATOR_INCLUDE_PATH)
CFLAGS := $(CCXXFLAGS)
CCF = $(CC) $(CFLAGS)
CXXFLAGS = -std=c++17 $(CCXXFLAGS) $(TRANSLATION-UNIT-SPECIFIC-FLAGS)
CXXF = $(CXX) $(CXXFLAGS)
# clang sanitisers (address, thread, memory, undefined, dataflow, cfi, safe-stack)
# https://clang.llvm.org/docs/UsersManual.html
SANITISE := address
PKG_CONFIG_PKGS := vulkan wayland-client glfw3 glm
CLIENT_OBJECTS := client.o client-networking.o networking.o wayland-protocol.o vulkan-enum-name-maps.o common.o vulkan.o stb-image-impl.o tinyobjloader-impl.o vulkan-memory-allocator-impl.o concurrency.o
SERVER_OBJECTS := server.o networking.o concurrency.o
SHADER_NAMES := plain ground
SHADER_OBJECTS := $(foreach SHADER_NAME,$(SHADER_NAMES),shaders/$(SHADER_NAME).vert.spv shaders/$(SHADER_NAME).frag.spv)
SHADERS_STAMP_FILE := shaders/built.stamp

##############
# dependencies
##############
all-print:
	@printf '===== BUILDING EVERYTHING =====\n'
client: $(CLIENT_OBJECTS) $(SHADERS_STAMP_FILE)
	$(CXX) $(CXXFLAGS) $$(pkg-config --cflags $(PKG_CONFIG_PKGS)) $(CLIENT_OBJECTS) $(LIBRARY_PATHS) $$(pkg-config --libs $(PKG_CONFIG_PKGS)) -o client
server: $(SERVER_OBJECTS)
	$(CXX) $(CXXFLAGS) $(SERVER_OBJECTS) -o server
-include *.d
wayland-protocol.o:
	wayland-scanner private-code < $$(pkg-config --variable=pkgdatadir wayland-protocols)/stable/xdg-shell/xdg-shell.xml > wayland-protocol.c
	$(CCF) -c -MD wayland-protocol.c
wayland-protocol.h:
	wayland-scanner client-header < $$(pkg-config --variable=pkgdatadir wayland-protocols)/stable/xdg-shell/xdg-shell.xml > wayland-protocol.h
client.o: wayland-protocol.h
%.o: %.cpp
	$(CXXF) -c -MD $<
$(SHADERS_STAMP_FILE): $(SHADER_OBJECTS)
	touch $(SHADERS_STAMP_FILE)
%.vert.spv: %.vert
	glslc $< -o $@
%.frag.spv: %.frag
	glslc $< -o $@

#########################################################
# phony commands, meant to be invoked directly from shell
#########################################################
all: all-print client server $(SHADERS_STAMP_FILE)
shaders: $(SHADERS_STAMP_FILE)
run: client
	./client
debug: SANITISE=
debug: client
#	LD_LIBRARY_PATH=lunarg-sdk/1.2.198.1/x86_64/lib
	gdb ./client
run-server: server
	./server
debug-server: SANITISE=
debug-server: server
	gdb ./server
memcheck: SANITISE=
memcheck: all
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./main
clean-shaders:
	rm -rf shaders/built.stamp $(SHADER_OBJECTS)
clean: clean-shaders
	rm -rf main *.d *.o wayland-protocol.*
linecount:
	wc -l Makefile *.cpp *.hpp
.PHONY: all all-print run debug clean clean-shaders debug-server run-server shaders memcheck linecount
.DEFAULT_GOAL := all
