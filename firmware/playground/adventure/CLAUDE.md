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
- Inline syntax: `n8gdb kbd_inject 'look[enter]'`
- Named keys in brackets: `[enter]`, `[backspace]`, `[esc]`, `[tab]`, `[up]`, `[down]`, `[left]`, `[right]`, `[f1]`-`[f12]`
- Hex keycodes: `[0x41]`
- Bare text chars sent as ASCII keycodes ($20-$7E). No auto-SHIFT.
- Max 32 keys per injection (half of 64-entry hardware buffer).

### Console output
- `n8gdb console_text` — renders frame buffer as text (80x25 grid)
- `n8gdb console_video /tmp/screenshot.png` — saves pixel screenshot

### Workflow pattern
Each n8gdb CLI call opens/closes a TCP connection. New connections auto-halt the CPU.
The working pattern for scripted testing:
1. Inject keys (CPU halted, keys queue in 64-entry keyboard buffer)
2. `n8gdb resume` — resumes CPU (fire-and-forget), processes queued keys
3. `n8gdb console_text` — check results
Max 32 keys per kbd_inject call (enforced client-side).

### REPL mode
For interactive debugging with persistent breakpoints:
```bash
n8gdb repl --sym adventure.sym
```
