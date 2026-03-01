# Adventure CLAUDE.md

## Build & Run

```bash
make
n8gdb load adventure_data 0x0500
n8gdb load adventure 0xE000
n8gdb reset
n8gdb run
```

## n8gdb Console & Keyboard

### Keyboard injection
- String mode: `n8gdb kbd_inject '"look"'` (outer single-quotes preserve inner doubles)
- Enter key: `n8gdb kbd_inject enter`
- Named keys: enter, backspace, escape, tab, up, down, left, right, f1-f12, etc.
- Single chars are sent as ASCII keycodes ($20-$7E). No auto-SHIFT.

### Console output
- `n8gdb console_text` — renders frame buffer as text (80x25 grid)
- `n8gdb console_video /tmp/screenshot.png` — saves pixel screenshot

### Workflow pattern
Each n8gdb CLI call opens/closes a TCP connection. New connections auto-halt the CPU.
The working pattern for scripted testing:
1. Inject keys (CPU halted, keys queue in 64-entry keyboard buffer)
2. `n8gdb run` — resumes CPU, processes queued keys, times out (expected)
3. `n8gdb console_text` — check results
Keep batches under 64 total keys to avoid keyboard buffer overflow.

### REPL mode
For interactive debugging with persistent breakpoints:
```bash
n8gdb repl --sym adventure.sym
```
