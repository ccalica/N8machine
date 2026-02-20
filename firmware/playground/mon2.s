; mon2.s - Memory monitor
;
; Builds on mon1 I/O and line parsing, adds hex routines and commands.
; Commands:
;   d ADDR [LEN]     - hex dump (default 16 bytes)
;   w ADDR HH HH...  - write bytes to memory
;   f ADDR LEN BYTE  - fill memory range
;   g ADDR           - jump to address
;   help / ?         - list commands
;
; Escape cancels current line input.

.export   _main
.export   getc, putc, puts, newline
.export   readline, parse, dispatch
.export   parse_hex, print_hex_byte, print_hex_word
.export   do_dump, do_write, do_fill, do_go, do_help
.export   main_loop

; --- TTY device registers ---
TTY_OUT_CTRL = $C100
TTY_OUT_DATA = $C101
TTY_IN_CTRL  = $C102
TTY_IN_DATA  = $C103

; --- Zero page ---
zp_ptr    = $E0             ; +$E1 string pointer for puts
zp_ptr2   = $E2             ; +$E3 args pointer / parse position
zp_addr   = $E4             ; +$E5 working address
zp_val    = $E6             ; +$E7 working value/count
hex_lo    = $F0             ; parse_hex result lo
hex_hi    = $F1             ; parse_hex result hi
zp_tmp    = $F2             ; scratch
line_len  = $F3             ; readline length

; --- RAM buffers ---
LINE_BUF  = $0300           ; 80-byte input buffer
CMD_BUF   = $0350           ; 16-byte command word

; --- Constants ---
CR        = $0D
LF        = $0A
BS        = $08
DEL       = $7F
ESC       = $1B
SPACE     = $20
BUF_SIZE  = 79

; =====================================================================
.segment  "CODE"

_main:
        LDA #<str_banner
        LDX #>str_banner
        JSR puts

main_loop:
        LDA #<str_prompt
        LDX #>str_prompt
        JSR puts

        JSR readline

        ; Empty line -> re-prompt
        LDA line_len
        BEQ main_loop

        JSR parse

        ; Empty command (all spaces) -> re-prompt
        LDA CMD_BUF
        BEQ main_loop

        JSR dispatch
        JMP main_loop

; =====================================================================
; Command dispatch
; =====================================================================
dispatch:
        LDA CMD_BUF

        CMP #'d'
        BNE :+
        JMP do_dump
:       CMP #'w'
        BNE :+
        JMP do_write
:       CMP #'f'
        BNE :+
        JMP do_fill
:       CMP #'g'
        BNE :+
        JMP do_go
:       CMP #'?'
        BNE :+
        JMP do_help
:       CMP #'h'
        BNE :+
        JMP do_help
:
        ; Unknown command
        LDA #<str_unknown
        LDX #>str_unknown
        JMP puts

; =====================================================================
; Command: d ADDR [LEN]  - hex dump (default 16 bytes)
; =====================================================================
do_dump:
        JSR parse_hex
        BCC @got_addr
        JMP cmd_error
@got_addr:
        LDA hex_lo
        STA zp_addr
        LDA hex_hi
        STA zp_addr+1

        ; Try to parse optional LEN
        JSR parse_hex
        BCC @got_len
        ; Default: 16 bytes
        LDA #$10
        STA zp_val
        LDA #$00
        STA zp_val+1
        JMP @dump
@got_len:
        LDA hex_lo
        STA zp_val
        LDA hex_hi
        STA zp_val+1

@dump:
        ; Check if done (zp_val == 0)
@line:  LDA zp_val
        ORA zp_val+1
        BNE @notdone
        RTS
@notdone:

        ; Print address
        LDA zp_addr
        LDX zp_addr+1
        JSR print_hex_word
        LDA #':'
        JSR putc
        LDA #SPACE
        JSR putc

        ; Bytes this line = min(16, remaining)
        LDA zp_val+1
        BNE @full               ; hi > 0, at least 256
        LDA zp_val
        CMP #$10
        BCS @full
        STA zp_tmp              ; partial line
        JMP @hex
@full:  LDA #$10
        STA zp_tmp

        ; Print hex bytes
@hex:   LDY #$00
@hloop: LDA (zp_addr),Y
        JSR print_hex_byte
        LDA #SPACE
        JSR putc
        INY
        CPY zp_tmp
        BNE @hloop

        ; Pad short lines for ASCII alignment
        CPY #$10
        BCS @ascii
@pad:   LDA #SPACE
        JSR putc
        JSR putc
        JSR putc
        INY
        CPY #$10
        BNE @pad

        ; Print ASCII sidebar
@ascii: LDA #SPACE
        JSR putc
        LDY #$00
@aloop: LDA (zp_addr),Y
        CMP #$20
        BCC @dot
        CMP #$7F
        BCS @dot
        JSR putc
        JMP @anext
@dot:   LDA #'.'
        JSR putc
@anext: INY
        CPY zp_tmp
        BNE @aloop

        JSR newline

        ; Advance address by zp_tmp
        CLC
        LDA zp_addr
        ADC zp_tmp
        STA zp_addr
        LDA zp_addr+1
        ADC #$00
        STA zp_addr+1

        ; Subtract zp_tmp from remaining count
        SEC
        LDA zp_val
        SBC zp_tmp
        STA zp_val
        LDA zp_val+1
        SBC #$00
        STA zp_val+1

        JMP @line

; =====================================================================
; Command: w ADDR HH HH...  - write bytes to memory
; =====================================================================
do_write:
        JSR parse_hex
        BCC @got_addr
        JMP cmd_error
@got_addr:
        LDA hex_lo
        STA zp_addr
        LDA hex_hi
        STA zp_addr+1

@loop:  JSR parse_hex
        BCS @done               ; no more bytes
        LDA hex_lo
        LDY #$00
        STA (zp_addr),Y
        ; Increment address
        INC zp_addr
        BNE @loop
        INC zp_addr+1
        JMP @loop

@done:  RTS

; =====================================================================
; Command: f ADDR LEN BYTE  - fill memory range
; =====================================================================
do_fill:
        JSR parse_hex
        BCC @got_addr
        JMP cmd_error
@got_addr:
        LDA hex_lo
        STA zp_addr
        LDA hex_hi
        STA zp_addr+1

        JSR parse_hex
        BCC @got_len
        JMP cmd_error
@got_len:
        LDA hex_lo
        STA zp_val
        LDA hex_hi
        STA zp_val+1

        JSR parse_hex
        BCC @got_byte
        JMP cmd_error
@got_byte:
        LDA hex_lo
        STA zp_tmp              ; fill byte

@loop:  LDA zp_val
        ORA zp_val+1
        BEQ @done
        LDA zp_tmp
        LDY #$00
        STA (zp_addr),Y
        ; Increment address
        INC zp_addr
        BNE :+
        INC zp_addr+1
:       ; Decrement count
        SEC
        LDA zp_val
        SBC #$01
        STA zp_val
        LDA zp_val+1
        SBC #$00
        STA zp_val+1
        JMP @loop

@done:  RTS

; =====================================================================
; Command: g ADDR  - jump to address
; =====================================================================
do_go:
        JSR parse_hex
        BCC @got_addr
        JMP cmd_error
@got_addr:
        LDA hex_lo
        STA zp_addr
        LDA hex_hi
        STA zp_addr+1
        JMP (zp_addr)

; =====================================================================
; Command: help / ?
; =====================================================================
do_help:
        LDA #<str_help
        LDX #>str_help
        JMP puts

; =====================================================================
; Common error handler
; =====================================================================
cmd_error:
        LDA #<str_err
        LDX #>str_err
        JMP puts

; =====================================================================
; Hex routines
; =====================================================================

; parse_hex - Parse hex number from args at (zp_ptr2)
; Skips leading spaces and optional '$' prefix.
; Result in hex_lo/hex_hi. Advances zp_ptr2 past number.
; Carry clear = success, carry set = no valid digits.
parse_hex:
        LDA #$00
        STA hex_lo
        STA hex_hi

        ; Skip leading spaces
        LDY #$00
@skip:  LDA (zp_ptr2),Y
        CMP #SPACE
        BNE @prefix
        INY
        BNE @skip

        ; Check for '$' prefix
@prefix:
        CMP #'$'
        BNE @digits
        INY

@digits:
        LDX #$00               ; digit count
@dloop: LDA (zp_ptr2),Y
        JSR hex_digit_val
        BCS @done

        ; Shift result left 4 bits: hex_hi:hex_lo <<= 4
        ASL hex_lo
        ROL hex_hi
        ASL hex_lo
        ROL hex_hi
        ASL hex_lo
        ROL hex_hi
        ASL hex_lo
        ROL hex_hi

        ; OR in new digit value
        ORA hex_lo
        STA hex_lo

        INX
        INY
        BNE @dloop

@done:
        ; Advance zp_ptr2 by Y
        TYA
        CLC
        ADC zp_ptr2
        STA zp_ptr2
        LDA #$00
        ADC zp_ptr2+1
        STA zp_ptr2+1

        ; Return carry based on digit count
        CPX #$00
        BEQ @fail
        CLC
        RTS
@fail:  SEC
        RTS

; hex_digit_val - Convert ASCII hex char to value 0-15
; Input: A = character
; Output: A = value, carry clear. Or carry set if not hex.
hex_digit_val:
        CMP #'0'
        BCC @bad
        CMP #'9'+1
        BCC @digit
        CMP #'A'
        BCC @bad
        CMP #'F'+1
        BCC @upper
        CMP #'a'
        BCC @bad
        CMP #'f'+1
        BCC @lower
@bad:   SEC
        RTS
@digit: SEC
        SBC #'0'
        CLC
        RTS
@upper: SEC
        SBC #('A'-10)
        CLC
        RTS
@lower: SEC
        SBC #('a'-10)
        CLC
        RTS

; print_hex_byte - Print byte in A as two hex digits
print_hex_byte:
        PHA
        LSR
        LSR
        LSR
        LSR
        JSR print_nib
        PLA
        AND #$0F
        JMP print_nib

; print_hex_word - Print 16-bit value; X=hi, A=lo
print_hex_word:
        PHA
        TXA
        JSR print_hex_byte
        PLA
        JMP print_hex_byte

; print_nib - Print low nibble of A as hex digit
print_nib:
        CMP #$0A
        BCC @dec
        CLC
        ADC #('A'-10)
        JMP putc
@dec:   CLC
        ADC #'0'
        JMP putc

; =====================================================================
; TTY I/O routines
; =====================================================================

putc:
        PHA
@wait:  LDA TTY_OUT_CTRL
        AND #$01
        BNE @wait
        PLA
        STA TTY_OUT_DATA
        RTS

getc:
        LDA TTY_IN_CTRL
        AND #$01
        BEQ getc
        LDA TTY_IN_DATA
        RTS

puts:
        STA zp_ptr
        STX zp_ptr+1
        LDY #$00
        LDA (zp_ptr),Y
        BEQ @done
@loop:  JSR putc
        INY
        LDA (zp_ptr),Y
        BNE @loop
@done:  RTS

newline:
        LDA #CR
        JSR putc
        LDA #LF
        JMP putc

; =====================================================================
; readline - with escape-to-cancel
; =====================================================================
readline:
        LDA #$00
        STA line_len
@loop:
        JSR getc

        CMP #CR
        BEQ @done
        CMP #LF
        BEQ @done

        CMP #ESC
        BEQ @cancel

        CMP #BS
        BEQ @bs
        CMP #DEL
        BEQ @bs

        ; Buffer full -> ignore
        LDX line_len
        CPX #BUF_SIZE
        BCS @loop

        ; Store and echo
        STA LINE_BUF,X
        INC line_len
        JSR putc
        JMP @loop

@bs:
        LDX line_len
        BEQ @loop
        DEC line_len
        LDA #BS
        JSR putc
        LDA #SPACE
        JSR putc
        LDA #BS
        JSR putc
        JMP @loop

@cancel:
        LDA #'\'
        JSR putc
        JSR newline
        LDA #$00
        STA line_len
        STA LINE_BUF
        RTS

@done:
        LDX line_len
        LDA #$00
        STA LINE_BUF,X
        JSR newline
        RTS

; =====================================================================
; parse - split LINE_BUF into CMD_BUF + set zp_ptr2 to args
; =====================================================================
parse:
        LDX #$00
        LDY #$00
@cmd:   LDA LINE_BUF,X
        BEQ @end_cmd
        CMP #SPACE
        BEQ @end_cmd
        CPY #15
        BCS @skip
        STA CMD_BUF,Y
        INY
@skip:  INX
        JMP @cmd

@end_cmd:
        LDA #$00
        STA CMD_BUF,Y

        LDA LINE_BUF,X
        BEQ @no_args

@skip_sp:
        LDA LINE_BUF,X
        CMP #SPACE
        BNE @has_args
        INX
        JMP @skip_sp

@has_args:
        CLC
        TXA
        ADC #<LINE_BUF
        STA zp_ptr2
        LDA #>LINE_BUF
        ADC #$00
        STA zp_ptr2+1
        RTS

@no_args:
        LDA #<str_empty
        STA zp_ptr2
        LDA #>str_empty
        STA zp_ptr2+1
        RTS

; =====================================================================
; String data
; =====================================================================

str_banner:
        .byte "N8 Mon2 - Memory Monitor", CR, LF, 0
str_prompt:
        .byte "> ", 0
str_unknown:
        .byte "Unknown command. Type ? for help.", CR, LF, 0
str_err:
        .byte "Error: bad arguments", CR, LF, 0
str_help:
        .byte "Commands:", CR, LF
        .byte "  d ADDR [LEN]     Hex dump (default 16)", CR, LF
        .byte "  w ADDR HH HH..  Write bytes", CR, LF
        .byte "  f ADDR LEN BYTE  Fill memory", CR, LF
        .byte "  g ADDR           Jump to address", CR, LF
        .byte "  ? / help         This help", CR, LF, 0
str_empty:
        .byte 0
