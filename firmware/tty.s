
; -----------TTY_OUT_DATA-----------------------------------------------------
; 

.export   _tty_putc,_tty_puts

.include  "zeropage.inc"
.include  "zp.inc"
.include  "devices.inc"

.segment    "CODE"

; -----------------------------------------------------------------
; tty_putc

_tty_putc:  PHA               ; A contains ASCII char push to stack
@wait:      LDA TTY_OUT_CTRL
            AND #$01          ; Test for 1 bit
            BNE @wait         ; Wait for bit 0 to clear
            PLA               ; Pop char from stack
            STA TTY_OUT_DATA
            RTS

_tty_puts:  STA ZP_A_PTR         ; Store lower byte
            STX ZP_A_PTR+1       ; Store upper byte
            LDY #$00
            LDA (ZP_A_PTR),Y
            BEQ @done
@loop:      JSR _tty_putc
            INY
            LDA (ZP_A_PTR),Y
            BNE @loop
@done:      RTS


