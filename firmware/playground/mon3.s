; mon3.s - Extended monitor
;
; Builds on mon2 (all mon2 commands included), adds:
;   copy SRC DST LEN      - memory block copy
;   cmp ADDR1 ADDR2 LEN   - memory compare (shows first difference)
;   search ADDR LEN HH..  - find byte pattern in memory
;   + A B                  - hex add (16-bit)
;   - A B                  - hex subtract (16-bit)
;   cls                    - clear text display ($C000-$C0FF)
;   ascii ADDR LEN         - display memory as printable ASCII

.export   _main
.export   getc, putc, puts, newline
.export   readline, parse, dispatch
.export   parse_hex, print_hex_byte, print_hex_word
.export   do_dump, do_write, do_fill, do_go, do_help
.export   do_copy, do_cmp, do_search
.export   do_add, do_sub, do_cls, do_ascii
.export   main_loop

; --- TTY device registers ---
TTY_OUT_CTRL = $C100
TTY_OUT_DATA = $C101
TTY_IN_CTRL  = $C102
TTY_IN_DATA  = $C103

; --- Text display ---
TXT_BASE  = $C000

; --- Zero page ---
zp_ptr    = $E0             ; +$E1 string pointer for puts
zp_ptr2   = $E2             ; +$E3 args pointer / dst addr in copy/cmp
zp_addr   = $E4             ; +$E5 working address
zp_val    = $E6             ; +$E7 working value/count
hex_lo    = $F0             ; parse_hex result lo
hex_hi    = $F1             ; parse_hex result hi
zp_tmp    = $F2             ; scratch / pat_len for search
line_len  = $F3             ; readline length

; --- RAM buffers ---
LINE_BUF  = $0300           ; 80-byte input buffer
CMD_BUF   = $0350           ; 16-byte command word
PATTERN   = $0360           ; search pattern buffer (16 bytes max)

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

        LDA line_len
        BEQ main_loop

        JSR parse

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
:       CMP #'c'
        BNE :+
        JMP dispatch_c
:       CMP #'s'
        BNE :+
        JMP do_search
:       CMP #'+'
        BNE :+
        JMP do_add
:       CMP #'-'
        BNE :+
        JMP do_sub
:       CMP #'a'
        BNE :+
        JMP do_ascii
:
        LDA #<str_unknown
        LDX #>str_unknown
        JMP puts

; Disambiguate c... commands: copy, cmp, cls
dispatch_c:
        LDA CMD_BUF+1
        CMP #'o'
        BNE :+
        JMP do_copy
:       CMP #'m'
        BNE :+
        JMP do_cmp
:       CMP #'l'
        BNE :+
        JMP do_cls
:
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

        JSR parse_hex
        BCC @got_len
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
        BNE @full
        LDA zp_val
        CMP #$10
        BCS @full
        STA zp_tmp
        JMP @hex
@full:  LDA #$10
        STA zp_tmp

@hex:   LDY #$00
@hloop: LDA (zp_addr),Y
        JSR print_hex_byte
        LDA #SPACE
        JSR putc
        INY
        CPY zp_tmp
        BNE @hloop

        ; Pad short lines
        CPY #$10
        BCS @ascii
@pad:   LDA #SPACE
        JSR putc
        JSR putc
        JSR putc
        INY
        CPY #$10
        BNE @pad

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

        ; Advance address
        CLC
        LDA zp_addr
        ADC zp_tmp
        STA zp_addr
        LDA zp_addr+1
        ADC #$00
        STA zp_addr+1

        ; Subtract from remaining
        SEC
        LDA zp_val
        SBC zp_tmp
        STA zp_val
        LDA zp_val+1
        SBC #$00
        STA zp_val+1

        JMP @line

; =====================================================================
; Command: w ADDR HH HH...  - write bytes
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
        BCS @done
        LDA hex_lo
        LDY #$00
        STA (zp_addr),Y
        INC zp_addr
        BNE @loop
        INC zp_addr+1
        JMP @loop

@done:  RTS

; =====================================================================
; Command: f ADDR LEN BYTE  - fill
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
        STA zp_tmp

@loop:  LDA zp_val
        ORA zp_val+1
        BEQ @done
        LDA zp_tmp
        LDY #$00
        STA (zp_addr),Y
        INC zp_addr
        BNE :+
        INC zp_addr+1
:       SEC
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
; Command: copy SRC DST LEN
; =====================================================================
do_copy:
        JSR parse_hex
        BCC @got_src
        JMP cmd_error
@got_src:
        LDA hex_lo
        STA zp_addr
        LDA hex_hi
        STA zp_addr+1

        JSR parse_hex
        BCC @got_dst
        JMP cmd_error
@got_dst:
        ; Store dst in zp_ptr2 (safe - parsing is done after next call)
        LDA hex_lo
        PHA
        LDA hex_hi
        PHA

        JSR parse_hex
        BCC @got_len
        PLA
        PLA
        JMP cmd_error
@got_len:
        LDA hex_lo
        STA zp_val
        LDA hex_hi
        STA zp_val+1

        ; Restore dst to zp_ptr2
        PLA
        STA zp_ptr2+1
        PLA
        STA zp_ptr2

@loop:  LDA zp_val
        ORA zp_val+1
        BEQ @done
        LDY #$00
        LDA (zp_addr),Y
        STA (zp_ptr2),Y
        ; Inc src
        INC zp_addr
        BNE :+
        INC zp_addr+1
:       ; Inc dst
        INC zp_ptr2
        BNE :+
        INC zp_ptr2+1
:       ; Dec count
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
; Command: cmp ADDR1 ADDR2 LEN
; Shows first difference, or "Equal" if identical.
; =====================================================================
do_cmp:
        JSR parse_hex
        BCC @got_a1
        JMP cmd_error
@got_a1:
        LDA hex_lo
        STA zp_addr
        LDA hex_hi
        STA zp_addr+1

        JSR parse_hex
        BCC @got_a2
        JMP cmd_error
@got_a2:
        LDA hex_lo
        PHA
        LDA hex_hi
        PHA

        JSR parse_hex
        BCC @got_len
        PLA
        PLA
        JMP cmd_error
@got_len:
        LDA hex_lo
        STA zp_val
        LDA hex_hi
        STA zp_val+1

        PLA
        STA zp_ptr2+1
        PLA
        STA zp_ptr2

@loop:  LDA zp_val
        ORA zp_val+1
        BEQ @equal
        LDY #$00
        LDA (zp_addr),Y
        CMP (zp_ptr2),Y
        BNE @diff
        ; Inc both, dec count
        INC zp_addr
        BNE :+
        INC zp_addr+1
:       INC zp_ptr2
        BNE :+
        INC zp_ptr2+1
:       SEC
        LDA zp_val
        SBC #$01
        STA zp_val
        LDA zp_val+1
        SBC #$00
        STA zp_val+1
        JMP @loop

@diff:
        ; Print "Diff at ADDR: XX vs XX"
        PHA                     ; save byte from addr1
        LDA #<str_diff
        LDX #>str_diff
        JSR puts
        ; Print address
        LDA zp_addr
        LDX zp_addr+1
        JSR print_hex_word
        LDA #':'
        JSR putc
        LDA #SPACE
        JSR putc
        ; Print byte from addr1
        PLA
        JSR print_hex_byte
        ; Print " vs "
        LDA #<str_vs
        LDX #>str_vs
        JSR puts
        ; Print byte from addr2
        LDY #$00
        LDA (zp_ptr2),Y
        JSR print_hex_byte
        JMP newline

@equal:
        LDA #<str_equal
        LDX #>str_equal
        JMP puts

; =====================================================================
; Command: search ADDR LEN HH...
; Prints addresses where pattern is found.
; =====================================================================
do_search:
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

        ; Parse pattern bytes into PATTERN buffer
        LDX #$00
@ppat:  JSR parse_hex
        BCS @pdone
        LDA hex_lo
        STA PATTERN,X
        INX
        CPX #$10                ; max 16 byte pattern
        BCC @ppat
@pdone: STX zp_tmp              ; zp_tmp = pattern length
        CPX #$00
        BNE @scan
        JMP cmd_error           ; no pattern

        ; Scan memory
@scan:  LDA zp_val
        ORA zp_val+1
        BEQ @sdone

        ; Compare pattern at current address
        LDY #$00
@cmplp: LDA (zp_addr),Y
        CMP PATTERN,Y
        BNE @next
        INY
        CPY zp_tmp
        BNE @cmplp

        ; Match found - print address
        LDA zp_addr
        LDX zp_addr+1
        JSR print_hex_word
        JSR newline

@next:  INC zp_addr
        BNE :+
        INC zp_addr+1
:       SEC
        LDA zp_val
        SBC #$01
        STA zp_val
        LDA zp_val+1
        SBC #$00
        STA zp_val+1
        JMP @scan

@sdone: RTS

; =====================================================================
; Command: + A B  - hex add
; =====================================================================
do_add:
        JSR parse_hex
        BCC @got_a
        JMP cmd_error
@got_a:
        LDA hex_lo
        STA zp_addr
        LDA hex_hi
        STA zp_addr+1

        JSR parse_hex
        BCC @got_b
        JMP cmd_error
@got_b:
        CLC
        LDA zp_addr
        ADC hex_lo
        TAX                     ; lo result in X temporarily
        LDA zp_addr+1
        ADC hex_hi
        TAY                     ; hi result in Y temporarily

        ; Print result: Y(hi):X(lo)
        TYA
        JSR print_hex_byte
        TXA
        JSR print_hex_byte
        JMP newline

; =====================================================================
; Command: - A B  - hex subtract
; =====================================================================
do_sub:
        JSR parse_hex
        BCC @got_a
        JMP cmd_error
@got_a:
        LDA hex_lo
        STA zp_addr
        LDA hex_hi
        STA zp_addr+1

        JSR parse_hex
        BCC @got_b
        JMP cmd_error
@got_b:
        SEC
        LDA zp_addr
        SBC hex_lo
        TAX
        LDA zp_addr+1
        SBC hex_hi
        TAY

        TYA
        JSR print_hex_byte
        TXA
        JSR print_hex_byte
        JMP newline

; =====================================================================
; Command: cls  - clear text display
; =====================================================================
do_cls:
        LDA #$00
        LDX #$00
@loop:  STA TXT_BASE,X
        INX
        BNE @loop
        RTS

; =====================================================================
; Command: ascii ADDR LEN  - display as printable ASCII
; =====================================================================
do_ascii:
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

@loop:  LDA zp_val
        ORA zp_val+1
        BEQ @done
        LDY #$00
        LDA (zp_addr),Y
        CMP #$20
        BCC @dot
        CMP #$7F
        BCS @dot
        JSR putc
        JMP @next
@dot:   LDA #'.'
        JSR putc
@next:  INC zp_addr
        BNE :+
        INC zp_addr+1
:       SEC
        LDA zp_val
        SBC #$01
        STA zp_val
        LDA zp_val+1
        SBC #$00
        STA zp_val+1
        JMP @loop

@done:  JMP newline

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

parse_hex:
        LDA #$00
        STA hex_lo
        STA hex_hi

        LDY #$00
@skip:  LDA (zp_ptr2),Y
        CMP #SPACE
        BNE @prefix
        INY
        BNE @skip

@prefix:
        CMP #'$'
        BNE @digits
        INY

@digits:
        LDX #$00
@dloop: LDA (zp_ptr2),Y
        JSR hex_digit_val
        BCS @done

        ASL hex_lo
        ROL hex_hi
        ASL hex_lo
        ROL hex_hi
        ASL hex_lo
        ROL hex_hi
        ASL hex_lo
        ROL hex_hi

        ORA hex_lo
        STA hex_lo

        INX
        INY
        BNE @dloop

@done:
        TYA
        CLC
        ADC zp_ptr2
        STA zp_ptr2
        LDA #$00
        ADC zp_ptr2+1
        STA zp_ptr2+1

        CPX #$00
        BEQ @fail
        CLC
        RTS
@fail:  SEC
        RTS

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

print_hex_word:
        PHA
        TXA
        JSR print_hex_byte
        PLA
        JMP print_hex_byte

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

        LDX line_len
        CPX #BUF_SIZE
        BCS @loop

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
; parse
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
        .byte "N8 Mon3 - Extended Monitor", CR, LF, 0
str_prompt:
        .byte "> ", 0
str_unknown:
        .byte "Unknown command. Type ? for help.", CR, LF, 0
str_err:
        .byte "Error: bad arguments", CR, LF, 0
str_diff:
        .byte "Diff at ", 0
str_vs:
        .byte " vs ", 0
str_equal:
        .byte "Equal", CR, LF, 0
str_help:
        .byte "Commands:", CR, LF
        .byte "  d ADDR [LEN]      Hex dump (default 16)", CR, LF
        .byte "  w ADDR HH HH..   Write bytes", CR, LF
        .byte "  f ADDR LEN BYTE   Fill memory", CR, LF
        .byte "  g ADDR            Jump to address", CR, LF
        .byte "  copy SRC DST LEN  Copy memory block", CR, LF
        .byte "  cmp A1 A2 LEN     Compare memory", CR, LF
        .byte "  search A L HH..   Find byte pattern", CR, LF
        .byte "  + A B             Hex add", CR, LF
        .byte "  - A B             Hex subtract", CR, LF
        .byte "  cls               Clear text display", CR, LF
        .byte "  ascii ADDR LEN    Show as ASCII", CR, LF
        .byte "  ? / help          This help", CR, LF, 0
str_empty:
        .byte 0
