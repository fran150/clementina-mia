# Simplified Makefile
.PHONY: all build flash clean

all: build

release:
	cmake -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

build:
	cmake -B build
	cmake --build build -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

flash: build
	picotool load build/mia.uf2 -f

clean:
	rm -rf build/