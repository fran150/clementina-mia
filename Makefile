# Simplified Makefile
.PHONY: all build release diag flash flash-diag clean

all: build

release:
	cmake -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

build:
	cmake -B build
	cmake --build build -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

# Diagnostic firmware: adds the bus event trace (console `trace`) to core 1's
# action loop. Builds in build-diag/ so it never mixes with the normal build.
# See docs/diagnostics.md.
diag:
	cmake -B build-diag -DCMAKE_BUILD_TYPE=Release -DMIA_DIAG=ON
	cmake --build build-diag -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

flash-diag: diag
	picotool load build-diag/mia.uf2 -f

flash: build
	picotool load build/mia.uf2 -f

clean:
	rm -rf build/ build-diag/