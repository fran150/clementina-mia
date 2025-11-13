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

## Documentation Guidelines

**IMPORTANT**: Do NOT create "change summary" or "removal" documentation files without explicit user request.

### When Making Changes

1. **Update existing documentation** - Modify relevant sections in existing docs rather than creating new files
2. **Ask before creating new docs** - Only create new documentation files if the user explicitly requests them
3. **Keep docs focused** - Documentation should serve end users, not track internal changes
4. **Use git for history** - Commit messages and git history track what changed and why

### What NOT to Create

- Change summary documents (e.g., "copy_byte_removal.md", "set_copy_commands_removal.md")
- Internal design decision documents (unless explicitly requested)
- Migration guides for removed features
- Step-by-step change logs

### What TO Update

- Existing user-facing documentation (programming guides, API references)
- Code comments explaining design decisions
- README files if user-facing changes occur

### Example

**Bad:**
```
User: "Remove the COPY_BYTE command"
Agent: Creates "copy_byte_removal.md" documenting the change
```

**Good:**
```
User: "Remove the COPY_BYTE command"
Agent: 
1. Removes the command from code
2. Updates mia_programming_guide.md to remove references
3. Updates mia_indexed_interface_reference.md
4. Provides brief summary in chat
```

### When to Ask

If you're unsure whether to create a new document, ask the user first:
- "Should I create a document explaining this change?"
- "Would you like me to document this design decision?"
