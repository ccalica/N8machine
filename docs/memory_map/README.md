# N8 Machine — Proposed Memory Map

> Source: `N8Machine.pdf` page 6

## Address Space (64 KB)

```
$FFFF ┌───────────────────────────────┐
      │ Kernel Entry                  │  512 bytes
$FE00 ├───────────────────────────────┤
      │ Implementation                │  3.5 KB
$F000 ├───────────────────────────────┤
      │ Monitor / stdlib              │  4 KB
$E000 ├───────────────────────────────┤
      │ Device Space                  │  2 KB
$D800 ├───────────────────────────────┤
      │ Dev Bank                      │  2 KB
$D000 ├───────────────────────────────┤
      │ Dev Bank                      │  4 KB
$C000 ├───────────────────────────────┤
      │ Program RAM                   │  47 KB
$0400 ├───────────────────────────────┤
      │ ????                          │  512 bytes
$0200 ├───────────────────────────────┤
      │ Hardware Stack                │  256 bytes
$0100 ├───────────────────────────────┤
      │ Zero Page                     │  256 bytes
$0000 └───────────────────────────────┘
```

## Detail

| Start   | End     | Size   | Region                                |
| -------:| -------:| ------:| ------------------------------------- |
| `$FFE0` | `$FFFF` | 32     | Vectors + Bank Switch Registers (TBD) |
| `$FE00` | `$FFDF` | 480    | Kernel Entry                          |
| `$F000` | `$FDFF` | 3.5 KB | Kernel Implementation                 |
| `$E000` | `$EFFF` | 4 KB   | Monitor / stdlib / Devbank            |
| `$D800` | `$DFFF` | 2 KB   | Device Register / Devbank (see [hardware.md](hardware.md)) |
| `$D000` | `$D7FF` | 2 KB   | Dev Bank                              |
| `$C000` | `$CFFF` | 4 KB   | Frame Buffer / Dev Bank               |
| `$0400` | `$BFFF` | 47 KB  | Program RAM                           |
| `$0200` | `$03FF` | 512    | ????                                  |
| `$0100` | `$01FF` | 256    | Hardware Stack                        |
| `$0000` | `$00FF` | 256    | Zero Page                             |
