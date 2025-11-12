# Clementina MIA Project Guidelines

## Testing

**IMPORTANT**: To run tests in this project, use:

```bash
make test
```

Do NOT use `cd tests && ./build_and_run.sh` or similar commands. The `make test` command from the project root is the correct way to build and run all tests.

## Project Structure

This is a Raspberry Pi Pico 2 W firmware project for the MIA (Multifunction Interface Adapter) that provides:
- Clock generation for the 6502 CPU
- ROM emulation for bootloading
- Indexed memory interface for accessing video, USB, and system functions
- Wi-Fi video transmission

## Build System

- Uses CMake with Pico SDK
- Main build: `make` or `make build`
- Tests: `make test`
- Clean: `make clean`

## Git Workflow

When viewing git diffs or other commands that may open vi/vim, pipe to `cat` to avoid blocking:

```bash
git diff --cached | cat
git log | cat
```

This prevents interactive editors from opening and blocking execution.
