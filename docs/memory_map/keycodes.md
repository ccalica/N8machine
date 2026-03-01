# N8 Machine — Keyboard Key Codes

KBD_DATA is an 8-bit register. Key codes are divided into three ranges:
control/nav (`$00-$1F`), printable ASCII (`$20-$7E`), and function keys (`$80-$8B`).

## No Key (`$00`)

| Code  | Meaning |
|------:|---------|
| `$00` | No key (null) |

## Control & Navigation Keys (`$01-$1F`)

| Code  | Key          |
|------:|--------------|
| `$01` | Up Arrow     |
| `$02` | Down Arrow   |
| `$03` | Left Arrow   |
| `$04` | Right Arrow  |
| `$05` | Home         |
| `$06` | End          |
| `$07` | (reserved)   |
| `$08` | Backspace    |
| `$09` | Tab          |
| `$0A` | Page Up      |
| `$0B` | Page Down    |
| `$0C` | (reserved)   |
| `$0D` | Enter (CR)   |
| `$0E` | Insert       |
| `$0F` | Delete       |
| `$10` | Print Screen |
| `$11` | Pause/Break  |
| `$12`–`$1A` | (reserved) |
| `$1B` | Escape       |
| `$1C`–`$1F` | (reserved) |

Codes `$08`, `$09`, `$0D`, and `$1B` align with their ASCII control character values.

## Printable ASCII (`$20-$7E`)

Standard ASCII. Shift and Caps Lock are applied by the host before injection.

## Reserved (`$7F`)

| Code  | Meaning |
|------:|---------|
| `$7F` | (reserved) |

## Function Keys (`$80-$8B`)

| Code  | Key |
|------:|-----|
| `$80` | F1  |
| `$81` | F2  |
| `$82` | F3  |
| `$83` | F4  |
| `$84` | F5  |
| `$85` | F6  |
| `$86` | F7  |
| `$87` | F8  |
| `$88` | F9  |
| `$89` | F10 |
| `$8A` | F11 |
| `$8B` | F12 |

## Reserved (`$8C-$FF`)

Reserved for future use.

## Modifier Keys

Modifier keys (Shift, Ctrl, Alt, Caps Lock) do not have dedicated key codes.
They are reported as modifier bits in the KBD_STATUS register alongside the
front key's code in KBD_DATA.

Ctrl+letter combinations send the letter's ASCII code (e.g., `$01` for Ctrl+A
is **not** used — instead KBD_DATA = `$41` with CTRL bit set in KBD_STATUS).

See `keyboard.md` for KBD_STATUS modifier bit definitions.
