
; -----------TTY_OUT_DATA-----------------------------------------------------
; 

.export   _tty_putc,send_wait

.include  "zeropage.inc"
.include  "devices.inc"


; -----------------------------------------------------------------
; tty_putc

_tty_putc:   PHA               ; A contains ASCII char push to stack
send_wait:  LDA TTY_OUT_CTRL
            AND #$01          ; Test for 1 bit
            BNE send_wait     ; Wait for bit 0 to clear
            PLA               ; Pop char from stack
            STA TTY_OUT_DATA
            rts

