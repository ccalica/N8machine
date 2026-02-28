# N8 Firmware — Memory Map

See also: [docs/memory_map/README.md](../docs/memory_map/README.md) (system-level),
[docs/memory_map/hardware.md](../docs/memory_map/hardware.md) (device registers).

## Address Space (64 KB)

```
$FFFF ┌───────────────────────────────┐
      │ Vectors (NMI/RESET/IRQ)       │  6 bytes
$FFFA ├───────────────────────────────┤
      │                               │
      │ ROM (8 KB)                    │
      │ STARTUP, CODE, RODATA, ONCE   │
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

## Linker Segments (n8.cfg)

ROM is loaded at `$E000` (8 KB). DATA is loaded in ROM but runs in RAM
(copied by `copydata` at boot).

| Segment   | Load | Run | Contents                                    |
|-----------|------|-----|---------------------------------------------|
| `STARTUP` | ROM  | ROM | Boot code (`_init`), constructor tables      |
| `ONCE`    | ROM  | ROM | One-time initialization (currently unused)   |
| `CODE`    | ROM  | ROM | Main program, TTY driver, interrupt handlers |
| `RODATA`  | ROM  | ROM | Read-only data (string literals)             |
| `DATA`    | ROM  | RAM | Initialized data (copied to RAM at boot)     |
| `BSS`     | —    | RAM | Zero-initialized at boot (`zerobss`)         |
| `VECTORS` | ROM  | ROM | NMI/RESET/IRQ vectors at `$FFFA`             |

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
