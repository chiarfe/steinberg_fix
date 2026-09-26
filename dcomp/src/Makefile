# Makefile — Cross-compile dcomp.dll with MinGW-w64 (x86_64 and x86)
#
# Usage (from this directory):
#   make -f Makefile
#
# Prerequisites:
#   x86_64-w64-mingw32-gcc
#   i686-w64-mingw32-gcc
#

CC64    = x86_64-w64-mingw32-gcc
CC32    = i686-w64-mingw32-gcc
WINDRES = x86_64-w64-mingw32-windres

# ----- Compilation flags -----------------------------------------------------
CFLAGS  = \
    -DWINE_NATIVE_BUILD \
    -D_WIN32_WINNT=0x0A00 \
    -DWINVER=0x0A00 \
    -Wall -Wno-unused-parameter \
    -O2

# ----- Build directories -----------------------------------------------------
BUILD_DIR     = build
BUILD_DIR_X64 = $(BUILD_DIR)/x64
BUILD_DIR_X86 = $(BUILD_DIR)/x86

# ----- Include paths ---------------------------------------------------------
# .                      — wine_compat.h, dcomp_private.h (local)
INCLUDES = \
    -I.

INCLUDES_X64 = $(INCLUDES)
INCLUDES_X86 = $(INCLUDES)

# ----- Sources & objects -----------------------------------------------------
SOURCES   = device.c surface.c target.c visual.c
OBJECTS64 = $(addprefix $(BUILD_DIR_X64)/, $(SOURCES:.c=.o))
OBJECTS32 = $(addprefix $(BUILD_DIR_X86)/, $(SOURCES:.c=.o))

# ----- Link libraries --------------------------------------------------------
# MinGW ships d3dcompiler_47 as -ld3dcompiler_47; adjust if your installation
# provides it under a different name (e.g. -ld3dcompiler).
LIBS = -ld2d1 -ldwmapi -luser32 -lgdi32 -ld3dcompiler -ld3d11 -ldxgi -lole32

# ----- Targets ---------------------------------------------------------------
.PHONY: all install clean

all: $(BUILD_DIR_X64)/dcomp.dll $(BUILD_DIR_X86)/dcomp.dll

$(BUILD_DIR_X64)/dcomp.dll: $(OBJECTS64) dcomp.def | $(BUILD_DIR_X64)
	$(CC64) -shared -o $@ $(OBJECTS64) dcomp.def $(LIBS) -Wl,--allow-multiple-definition

$(BUILD_DIR_X86)/dcomp.dll: $(OBJECTS32) dcomp.def | $(BUILD_DIR_X86)
	$(CC32) -shared -o $@ $(OBJECTS32) dcomp.def $(LIBS) -Wl,--allow-multiple-definition

$(BUILD_DIR_X64)/%.o: %.c | $(BUILD_DIR_X64)
	$(CC64) $(CFLAGS) $(INCLUDES_X64) -c $< -o $@

$(BUILD_DIR_X86)/%.o: %.c | $(BUILD_DIR_X86)
	$(CC32) $(CFLAGS) $(INCLUDES_X86) -c $< -o $@

$(BUILD_DIR_X64) $(BUILD_DIR_X86):
	mkdir -p $@

install: all
	install -Dm755 $(BUILD_DIR_X64)/dcomp.dll $(BUILD_DIR_X64)/../../steinbergfix_binaries/x86_64-windows/dcomp.dll
	install -Dm755 $(BUILD_DIR_X86)/dcomp.dll $(BUILD_DIR_X86)/../../steinbergfix_binaries/i386-windows/dcomp.dll

clean:
	rm -rf $(BUILD_DIR)
