# N8Machine Storage Device

Design notes for N8Machine storage hardware.

Draws heavy inspiration from C64. Closely maps to stdlibc file operations. Each device
uses a 4-bit channel ID through which IO is handled. Channel 0x0F is the Control channel
for the device. A write to DISK_CHAN switches all register views to that channel's state.

## Hardware Slot Assignment

Slot 4 (Addresses $D880-$D89F)

| Address | Register    | R/W | Description                          |
|---------|-------------|-----|--------------------------------------|
| $D880   | DISK_CHAN   | RW  | Channel select / status              |
| $D881   | DISK_DATA   | RW  | Data IO / command error code         |
| $D882   | DISK_ERROR  | R   | I/O error code. 0x00 = no error      |
| $D883   | DISK_CTRL   | W   | Control (edge-triggered)             |
| $D884   | DISK_STATUS | R   | Channel status                       |

## DISK_CHAN

Selects the active channel. All other registers reflect the selected channel's state.

**Write**: Lo nibble sets channel ID (0x00-0x0E = data channels, 0x0F = control).
Writing clears DISK_ERROR.

**Read**: Returns channel info.

| Bits    | Meaning                              |
|---------|--------------------------------------|
| Bits 0-3| Current channel ID                   |
| Bit 5   | Active (1 = channel is open)         |

Firmware can check if a channel is valid by selecting it and reading back:
`LDA DISK_CHAN / AND #$20 / BEQ not_open`.

## DISK_ERROR

Reports I/O errors from the last data operation on the current channel. Latches
until explicitly cleared by writing DISK_CHAN or DISK_CTRL bit 0 (parser reset).

- **Bit 7**: Command error flag (control channel command failed; read DISK_DATA
  for the command error code)
- **Bits 0-6**: I/O error code

Bit 7 separates the two error paths:
- **I/O errors** (bits 0-6): detected during DISK_DATA reads/writes on data channels.
  Firmware checks DISK_ERROR after data operations.
- **Command errors** (bit 7): detected after sending a command on the control channel.
  When bit 7 is set, DISK_DATA returns the command error code byte.

### I/O Error Codes (bits 0-6 of DISK_ERROR)

| Code  | Meaning                                    |
|-------|--------------------------------------------|
| $00   | No error                                   |
| $01   | Channel not open                           |
| $02   | Disk full                                  |
| $03   | Read past EOF                              |
| $04   | Write protected / permission denied        |
| $05   | Device not ready                           |
| $06   | Command buffer overflow (>256 bytes)       |

## DISK_CTRL

Write-only, edge-triggered (write the bit, action happens, bits auto-clear).

| Bit   | Function                                                          |
|-------|-------------------------------------------------------------------|
| Bit 0 | Reset command parser (abort partial command, clear DISK_ERROR)    |
| Bit 1 | Reset channel (close file, release resources, clean state)        |
| Bit 7 | Reset device (close all channels, clear all errors, power-on state) |

Bit 7 intentionally far from 0-1 to avoid accidental device reset.
Device reset closes all open channels, clears DISK_ERROR, clears the command
parser, and discards any pending writes without flushing.

DISK_CTRL bit 1 on an invalid or already-closed channel sets DISK_ERROR $01.

## DISK_STATUS

Valid only when DISK_CHAN selects an active channel.

| Bits    | Meaning                                    |
|---------|--------------------------------------------|
| Bits 0-3| Bytes available to read (lookahead window)  |
| Bit 5   | EOF                                        |
| Bit 6   | Clear to Send (always set for now)         |
| Bit 7   | Busy (0 = ready, 1 = waiting for data)     |

### Read buffer and status lifecycle

The device maintains a 256-byte read-ahead buffer per channel. DISK_STATUS
bits 0-3 report how many bytes are available, saturating at 15. The buffer
refills as firmware reads bytes from DISK_DATA.

Three states govern the firmware read loop:

| bytes_avail | Busy | EOF | Meaning                          |
|-------------|------|-----|----------------------------------|
| > 0         | 0    | 0   | Data ready, more to come         |
| > 0         | 0    | 1   | Last bytes — EOF after these     |
| 0           | 1    | 0   | Waiting for device to fill buffer|
| 0           | 0    | 1   | End of file, no more data        |

EOF is set when the device has no more data to provide. It rises when
bytes_avail reaches 0 at the end of the stream. While bytes remain in the
buffer, EOF is not set even if the device knows no more data is coming.

When bytes_avail is 0 and EOF is 0, Busy is 1 — the device is fetching
more data. Firmware should poll DISK_STATUS until Busy clears.

Firmware read loop:
```
read_loop:
    LDA DISK_STATUS
    TAX                     ; save full status
    AND #$0F                ; bytes available
    BNE read_bytes          ; got data, read it
    TXA
    AND #$20                ; check EOF (bit 5)
    BNE read_done           ; EOF, we're done
    JMP read_loop           ; Busy — wait for data
read_bytes:
    TAY                     ; byte count
@loop:
    LDA DISK_DATA           ; read one byte
    ; ... process byte ...
    DEY
    BNE @loop
    JMP read_loop           ; check for more
read_done:
```

CTS is a placeholder for future slow media support.

## Command Execution Model

Commands are parsed and executed synchronously on the null byte write. After
writing `$00` to DISK_DATA on the control channel, DISK_ERROR and DISK_STATUS
are immediately valid. No need to poll Busy (currently always synchronous).

The command parser buffers up to 256 bytes. If firmware writes more than 256
bytes before a null terminator, DISK_ERROR is set to `$06` (buffer overflow,
I/O error, no bit 7) and the parser resets. Extra null bytes after a command
are ignored (no re-execution).

The parser is opcode-aware: some commands (e.g., SK) have fixed-length binary
payloads where $00 bytes are valid data. The parser uses the opcode to
determine payload framing rather than scanning for null within the payload.

### Command Error Codes (read from DISK_DATA when DISK_ERROR bit 7 set)

| Code  | Meaning                                    |
|-------|--------------------------------------------|
| $01   | File not found                             |
| $02   | File exists / already open                 |
| $03   | Directory not found / invalid path         |
| $04   | Channel not open                           |
| $05   | No free channels                           |
| $06   | Disk full                                  |
| $08   | Permission denied                          |
| $09   | Invalid command syntax                     |
| $0A   | Invalid argument                           |
| $0B   | Name too long                              |
| $0C   | Not a directory (CD/RD on a file)          |
| $0D   | Directory not empty (RMDIR)                |
| $0E   | Device not ready                           |

## Control Channel Protocol

```
1. Write 0x0F to DISK_CHAN         (select control channel, clears DISK_ERROR)
2. Write cmd string to DISK_DATA   (byte at a time)
3. Write 0x00 to DISK_DATA         (null-terminate; command executes immediately)
4. Read DISK_ERROR                  (bit 7 set = command failed)
5a. If bit 7 set: read DISK_DATA for command error code
5b. If bit 7 clear: read return byte(s) from DISK_DATA (e.g., channel_id)
```

## Commands

| CMD    | Opcode | Description        |
|--------|--------|--------------------|
| OPEN   | OP     | Open file          |
| CLOSE  | CL     | Close channel      |
| SEEK   | SK     | Seek file cursor   |
| LIST   | LS     | List directory     |
| MOVE   | MV     | Rename / move      |
| REMOVE | RM     | Delete file        |
| CHDIR  | CD     | Change directory   |
| PWDIR  | PD     | Print working dir  |
| MKDIR  | MD     | Create directory   |
| RMDIR  | RD     | Remove directory   |

### Command Descriptions (rough implementation order)

**LIST** — `"LS,<dir|file>"`
Returns channel_id via DISK_DATA. Reads on that channel return entries in format:
`D|F\t<name>\t<lo><hi>\n` where `<lo><hi>` is file size as LE uint16 (binary).
EOF signals end of listing. Response channel auto-closes when last byte is
read (or on command parser reset via DISK_CTRL bit 0).

**OPEN** — `"OP,<mode>,<file>"`
Mode is a single character, followed by comma, then null-terminated filename:
- `R` — read (fail if file does not exist)
- `W` — write (create if missing, truncate on open)
- `A` — append (create if missing, all writes go to end of file)

Returns channel_id via DISK_DATA. Read/write on data channel with that chan_id.
SEEK is not valid on append-mode channels (command error $0A).
Opening a file that is already open returns command error $02.
SEEK past EOF pads the file with zeros (POSIX behavior).

**CLOSE** — `"CL,<chan_id>"`
Fixed-length binary payload: 1 byte channel ID (0x00-0x0E), then null terminator.
The parser knows CL expects exactly 1 payload byte after the comma.
Close the file, free channel_id for reuse.
Closing an already-closed or invalid channel returns command error $04.

**SEEK** — `"SK,<type><lo><hi>"`
Fixed-length binary payload: type byte + LE uint16 offset, then null terminator.
The parser knows SK expects exactly 3 payload bytes before the null. Binary bytes
may contain $00 safely — the parser counts bytes, not scans for null within payload.
If null arrives before all 3 bytes, command error $09 (invalid syntax).

Type byte:
- `A` — absolute (from start)
- `+` — relative forward
- `-` — relative backward

Returns $00 via DISK_DATA on success, command error code on failure.
Not valid on append-mode channels (command error $0A).

**PWDIR** — `"PD"`
Returns channel_id via DISK_DATA. Reads on that channel return full path as string.
Response channel auto-closes when last byte is read.

**CHDIR** — `"CD,<path>"`
Returns error via DISK_DATA (command error) or success ($00).

**MKDIR** — `"MD,<path>"`
Returns error via DISK_DATA (command error) or success ($00).

**RMDIR** — `"RD,<path>"`
Returns error via DISK_DATA (command error) or success ($00).

**REMOVE** — `"RM,<path>"`
Returns error via DISK_DATA (command error) or success ($00).

**MOVE** — `"MV,<src>,<dst>"`
Rename or move. If dst is an existing directory, moves src into it.
Otherwise renames src to dst. Returns error via DISK_DATA (command error) or success ($00).

## Edge Cases

- **Partial command + CTRL reset**: DISK_CTRL bit 0 clears all buffered bytes,
  DISK_ERROR, and auto-closes any response channels. Firmware can immediately
  begin a new command.
- **Double null**: Extra null bytes after a command are ignored. No re-execution.
- **Read on closed channel**: Sets DISK_ERROR $01 (channel not open).
- **Write on read-only channel**: Sets DISK_ERROR $04.
- **Read on write-only channel**: Sets DISK_ERROR $04.
- **Command buffer overflow**: Writing >256 bytes before null sets DISK_ERROR
  $06 (I/O error, no bit 7) and resets the parser.
- **Move to self**: `"MV,file,file"` returns command error $0A (invalid argument).
- **Seek past EOF**: File is extended with zero bytes (POSIX sparse file behavior).
- **Double open**: Opening an already-open file returns command error $02.
- **Response channels (LIST, PWDIR)**: Auto-close when last byte is read, or
  on command parser reset (DISK_CTRL bit 0). No explicit CLOSE needed.
  After auto-close, DISK_CHAN bit 5 (Active) reads 0 for that channel.
- **DISK_CTRL bit 1 on closed channel**: Sets DISK_ERROR $01 (channel not open).
- **DISK_STATUS on inactive channel**: Undefined. Firmware should check
  DISK_CHAN bit 5 (Active) before reading DISK_STATUS.

## Filename Conventions

Valid filename characters: ASCII letters (`A-Z`, `a-z`), digits (`0-9`),
underscore (`_`), hyphen (`-`), period (`.`). Maximum filename length: 63 bytes.

Filenames must not contain: NUL (`$00`), tab (`$09`), newline (`$0A`),
space (`$20`), slash (`/`), or any control/extended characters.

Case sensitivity is preserved but matching is case-insensitive.

## Path Conventions

- `/` as delimiter
- Leading `/` = absolute path
- Otherwise relative to current working directory
