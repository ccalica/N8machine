# N8 Machine — Keyboard Key Codes

> Source: *N8Machine Keyboard Register Interface Spec — Phase 1* (Notion)

KBD_DATA is an 8-bit register. Bit 7 distinguishes ASCII from extended.

## ASCII Range (`$00–$7F`)

Standard 7-bit ASCII. Teensy converts USB HID scancodes to ASCII,
applying Shift and Caps Lock.

**Common control characters:**

| Code  | Key         |
|------:|-------------|
| `$01` | Ctrl+A      |
| `$03` | Ctrl+C      |
| `$08` | Backspace   |
| `$09` | Tab         |
| `$0D` | Enter (CR)  |
| `$1B` | Escape      |

## Extended Range (`$80–$FF`)

Non-ASCII keys. Bit 7 is always set.

### Navigation Keys (`$80–$8F`)

| Code  | Key        |
|------:|------------|
| `$80` | Up Arrow   |
| `$81` | Down Arrow |
| `$82` | Left Arrow |
| `$83` | Right Arrow|
| `$84` | Home       |
| `$85` | End        |
| `$86` | Page Up    |
| `$87` | Page Down  |
| `$88` | Insert     |
| `$89` | Delete     |
| `$8A` | Print Screen |
| `$8B` | Pause/Break|
| `$8C`–`$8F` | Reserved |

### Function Keys (`$90–$9B`)

| Code  | Key |
|------:|-----|
| `$90` | F1  |
| `$91` | F2  |
| `$92` | F3  |
| `$93` | F4  |
| `$94` | F5  |
| `$95` | F6  |
| `$96` | F7  |
| `$97` | F8  |
| `$98` | F9  |
| `$99` | F10 |
| `$9A` | F11 |
| `$9B` | F12 |

### Reserved (`$9C–$FF`)

Reserved for future use.

Code assignments are preliminary — may be adjusted during firmware implementation.
