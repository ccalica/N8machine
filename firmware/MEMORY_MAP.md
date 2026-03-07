# N8 Firmware — Memory Map

See also: [docs/memory_map/README.md](../docs/memory_map/README.md) (system-level),
[docs/memory_map/hardware.md](../docs/memory_map/hardware.md) (device registers).

## Address Space (64 KB)

```
$FFFF ┌───────────────────────────────┐
      │ Vectors (NMI/RESET/IRQ)       │  6 bytes
$FFFA ├───────────────────────────────┤
      │                               │
      │ Kernel Entry Jump Table       │  512 bytes
$FE00 ├───────────────────────────────┤
      │                               │
      │ Kernel Implementation (4 KB)  │
      │ STARTUP, CODE, RODATA, ONCE   │
      │                               │
$F000 ├───────────────────────────────┤
      │                               │
      │ Monitor (4 KB)                │
      │ CODE, RODATA                  │
      │                               │
$E000 ├───────────────────────────────┤
      │ Device Space ($D800-$DFFF)    │  2 KB
$D800 ├───────────────────────────────┤
      │ Dev Bank                      │  2 KB
$D000 ├───────────────────────────────┤
      │ Frame Buffer (4 KB)           │  separate backing store
$C000 ├───────────────────────────────┤
      │                               │
      │ Program RAM (47 KB)           │
      │ DATA, BSS, HEAP, cc65 stack   │
      │                               │
$0400 ├───────────────────────────────┤
      │ TBD (toolchain use)           │  512 bytes
$0200 ├───────────────────────────────┤
      │ Hardware Stack                │  256 bytes
$0100 ├───────────────────────────────┤
      │ Zero Page                     │  256 bytes
$0000 └───────────────────────────────┘
```

## ROM Layout

The 8KB ROM region ($E000-$FFFF) is split into two separately-built binaries:

| Binary       | Range           | Size | Linker Config  |
|-------------|-----------------|------|----------------|
| `n8_shell`   | `$E000-$EFFF`  | 4 KB | `shell.cfg`    |
| `n8_kernel`  | `$F000-$FFFF`  | 4 KB | `kernel.cfg`   |

### Kernel Segments (kernel.cfg)

| Segment   | Load | Run | Contents                                    |
|-----------|------|-----|---------------------------------------------|
| `STARTUP` | ROM  | ROM | Boot code (`_init`), constructor tables      |
| `ONCE`    | ROM  | ROM | One-time initialization (currently unused)   |
| `CODE`    | ROM  | ROM | TTY driver, console routines, interrupt handlers |
| `RODATA`  | ROM  | ROM | Read-only data (string literals)             |
| `DATA`    | ROM  | RAM | Initialized data (copied to RAM at boot)     |
| `BSS`     | —    | RAM | Zero-initialized at boot (`zerobss`)         |
| `KENTRY`  | ROM  | ROM | Kernel entry jump table at `$FE00`           |
| `VECTORS` | ROM  | ROM | NMI/RESET/IRQ vectors at `$FFFA`             |

### Monitor Segments (monitor.cfg)

| Segment   | Load | Run | Contents                    |
|-----------|------|-----|-----------------------------|
| `CODE`    | ROM  | ROM | Interactive echo shell       |
| `RODATA`  | ROM  | ROM | Banner string                |

### Kernel Entry Jump Table ($FE00)

The monitor calls kernel services through fixed entry points:

| Address | Function       | Description            |
|---------|---------------|------------------------|
| `$FE00` | `K_TTY_PUTC`      | Print character (A)                              |
| `$FE03` | `K_TTY_GETC`      | Get character → A                                |
| `$FE06` | `K_TTY_PEEKC`     | Peek char count → A                              |
| `$FE09` | `K_CON_GETKEY`    | Non-blocking key read → A=keycode, X=modifiers   |
| `$FE0C` | `K_CON_SETMODE`   | Set video mode (A) and control (X)               |
| `$FE0F` | `K_CON_GETSTATUS` | → A=VID_STATUS, X=col, Y=row                    |
| `$FE12` | `K_CON_PUTCHAR`   | Write char (A) to VID_DATA at cursor             |
| `$FE15` | `K_CON_NEWLINE`   | Col=0, advance row, scroll at bottom             |
| `$FE18` | `K_CON_CLEAR`     | Clear screen (VIDOP_CLEAR)                       |
| `$FE1B` | `K_CON_SCROLL`    | Scroll screen: X=horiz(signed), Y=vert(signed)   |
| `$FE1E` | `K_CON_MOVCURSOR` | Move cursor: X=horiz(signed), Y=vert(signed)     |
| `$FE21` | `K_CON_SETCURSOR` | Set cursor: X=col, Y=row                        |

## Zero Page ($0000-$00FF)

| Range       | Owner    | Symbol(s)               | Notes                          |
|-------------|----------|-------------------------|--------------------------------|
| `$00-$01`   | cc65     | `sp`                    | Software stack pointer         |
| `$02-$03`   | cc65     | `sreg`                  | Secondary register (32-bit)    |
| `$04-$07`   | cc65     | `regsave`               | Register save area             |
| `$08-$0F`   | cc65     | `ptr1`-`ptr4`           | Scratch pointers (2 bytes ea.) |
| `$10-$17`   | cc65     | `tmp1`-`tmp4`           | Scratch temporaries            |
| `$18-$1F`   | cc65     | `regbank`               | Register bank                  |
| `$20-$DF`   | cc65     | (available)             | Unused by runtime              |
| `$E0-$E1`   | firmware | `ZP_A_PTR`              | General pointer A              |
| `$E2-$E3`   | firmware | `ZP_B_PTR`              | General pointer B              |
| `$E4-$E5`   | firmware | `ZP_C_PTR`              | General pointer C              |
| `$E6-$E7`   | firmware | `ZP_D_PTR`              | General pointer D              |
| `$E8-$EF`   | —        | (free)                  |                                |
| `$F0-$F3`   | firmware | `BYTE_0`-`BYTE_3`       | Scratch bytes                  |
| `$F4-$FF`   | —        | (free)                  |                                |

## Device Slots ($D800-$DFFF)

32 bytes per slot. See `n8_memory_map.inc` / `devices.h` for constants.

| Slot | Base    | Device       |
|-----:|---------|--------------|
| 0    | `$D800` | System / IRQ |
| 1    | `$D820` | TTY          |
| 2    | `$D840` | Video Control|
| 3    | `$D860` | Keyboard     |
| 4-7  | `$D880` | Reserved     |

## Interrupt Vectors

| Address | Vector | Handler    | Function                  |
|---------|--------|------------|---------------------------|
| `$FFFA` | NMI    | `_nmi_int` | Immediate RTI (unused)    |
| `$FFFC` | RESET  | `_init`    | Boot entry point          |
| `$FFFE` | IRQ    | `_irq_int` | TTY input + BRK detection |
