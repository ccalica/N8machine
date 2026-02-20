; test_tty.s - TTY output test
;
; Sends several strings to the TTY device. Step through to watch
; characters appear, or run freely and inspect TTY output.
; Good for testing: step (through I/O), bp on putc/puts, read on I/O region
;
; Expected workflow with n8gdb:
;   sym test_tty.sym
;   bp send_hello
;   run                   ; stop before first string
;   step 10               ; watch characters go out one at a time
;   read C100 4           ; inspect TTY registers
;   bp send_done
;   run                   ; let all strings send
;   bp tty_putc
;   run                   ; break on every character output

.export   _main
.export   send_hello, send_nums, send_banner, send_done
.export   tty_putc, tty_puts
.export   str_hello, str_nums, str_banner

; TTY device registers
TTY_OUT_CTRL = $C100
TTY_OUT_DATA = $C101

; Zero page pointer for puts
zp_ptr = $E0                ; E0/E1

.segment  "CODE"

_main:

; --- Send "Hello, world!" ---
send_hello:
        LDA #<str_hello
        LDX #>str_hello
        JSR tty_puts

; --- Send "0123456789" ---
send_nums:
        LDA #<str_nums
        LDX #>str_nums
        JSR tty_puts

; --- Send multi-line banner ---
send_banner:
        LDA #<str_banner
        LDX #>str_banner
        JSR tty_puts

send_done:
        NOP                  ; All strings sent
        JMP _main            ; Loop and send again

; --- Minimal TTY output routines (self-contained, no firmware link) ---

tty_putc:
        PHA
@wait:  LDA TTY_OUT_CTRL
        AND #$01
        BNE @wait            ; busy-wait for TX ready
        PLA
        STA TTY_OUT_DATA
        RTS

tty_puts:
        STA zp_ptr           ; A = lo byte of string addr
        STX zp_ptr+1         ; X = hi byte
        LDY #$00
        LDA (zp_ptr),Y
        BEQ @done
@loop:  JSR tty_putc
        INY
        LDA (zp_ptr),Y
        BNE @loop
@done:  RTS

; --- String data ---

str_hello:
        .byte "Hello, world!", 13, 10, 0

str_nums:
        .byte "0123456789", 13, 10, 0

str_banner:
        .byte "=== N8machine GDB test ===", 13, 10
        .byte "TTY output working.", 13, 10, 0
