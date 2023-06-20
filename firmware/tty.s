
; -----------TTY_OUT_DATA-----------------------------------------------------
; 

.export   _tty_putc,_tty_puts,_tty_peekc,_tty_getc
.export   tty_recv
.export   rb_base,rb_len,rb_start,rb_end
.export   in_bounds

.include  "zeropage.inc"
.include  "zp.inc"
.include  "devices.inc"

.segment    "DATA"
rb_base:   .byte 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
            .byte 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
rb_len:     .byte 32
rb_start:      .byte 0             ; index where next char placed
rb_end:     .byte 0             ; index where next char pull
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

; ----------------------------------------------------------
; Return number of chars available
_tty_peekc: LDA rb_end
            SBC rb_start
            BMI @minus
            RTS
@minus:     ADC rb_len
            RTS

_tty_getc:  LDY rb_start
            CPY rb_end
            BEQ @nodata
            LDX rb_base,Y
            INY
            CPY rb_len
            BNE @cont
            LDY #$00
@cont:      STY rb_start
            TXA
            RTS
@nodata:    LDA #$00
            RTS

tty_recv:   TAX                 ; push char to X
            LDY rb_end          ; load ring buffer data end index
            CPY rb_len
            BMI in_bounds
            LDY #$00            ; overflow to 0
in_bounds:  SEC                 ; set carry
            TYA
            SBC rb_start        ; don't run over rb_start ptr
            BEQ @write_rb       ; common case
            CMP #$FF            ; -1
            BEQ @done           ; discard new char
            CMP #$E1            ; -31?
            BEQ @done
@write_rb:  TXA
            STA rb_base,Y
            INY
            CPY rb_len
            BMI @inb2
            LDY #$00
@inb2:      STY rb_end

@done:      RTS            

