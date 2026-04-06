; -------------------------------------------------------
; tty.s -- TTY device driver (serial I/O + receive buffer)
; -------------------------------------------------------

.export   _tty_putc, _tty_puts, _tty_peekc, _tty_getc
.export   tty_recv
.export   rb_base, rb_len, rb_start, rb_end

.include  "zeropage.inc"
.include  "zp.inc"
.include  "devices.inc"

.segment    "DATA"
rb_base:    .res 32             ; 32-byte ring buffer
rb_len:     .byte 32            ; buffer capacity
rb_start:   .byte 0             ; read index (consumer pops here)
rb_end:     .byte 0             ; write index (producer inserts here)

.segment    "CODE"

; -----------------------------------------------------------------
; tty_putc -- Send one character to TTY output
;   In: A = character
; -----------------------------------------------------------------
_tty_putc:  PHA
@wait:      LDA N8_TTY_OUT_STATUS
            AND #$01            ; bit 0 = busy
            BNE @wait
            PLA
            STA N8_TTY_OUT_DATA
            RTS

; -----------------------------------------------------------------
; tty_puts -- Send null-terminated string to TTY output
;   In: A = string low, X = string high
; -----------------------------------------------------------------
_tty_puts:  STA ZP_A_PTR
            STX ZP_A_PTR+1
            LDY #$00
            LDA (ZP_A_PTR),Y
            BEQ @done
@loop:      JSR _tty_putc
            INY
            LDA (ZP_A_PTR),Y
            BNE @loop
@done:      RTS

; -----------------------------------------------------------------
; tty_peekc -- Return number of characters available in buffer
;   Out: A = count
; -----------------------------------------------------------------
_tty_peekc: SEC
            LDA rb_end
            SBC rb_start
            BPL @done
            CLC
            ADC rb_len
@done:      RTS

; -----------------------------------------------------------------
; tty_getc -- Read one character from receive buffer (blocking)
;   Out: A = character ($00 if empty)
; -----------------------------------------------------------------
_tty_getc:  LDY rb_start
            CPY rb_end
            BEQ @nodata
            LDA rb_base,Y
            PHA
            INY
            CPY rb_len
            BCC @store
            LDY #$00
@store:     STY rb_start
            PLA
            RTS
@nodata:    LDA #$00
            RTS

; -----------------------------------------------------------------
; tty_recv -- Insert character into receive ring buffer (ISR safe)
;   In: A = character
;   Full check: if (rb_end + 1) % rb_len == rb_start, discard.
; -----------------------------------------------------------------
tty_recv:   TAX                 ; save char in X
            LDY rb_end          ; Y = current write position
            ; Compute next_end = (rb_end + 1) % rb_len
            TYA
            CLC
            ADC #1
            CMP rb_len
            BCC @check_full
            LDA #$00            ; wrap to 0
@check_full:
            CMP rb_start        ; next_end == rb_start?
            BEQ @done           ; full — discard
            PHA                 ; save next_end
            TXA                 ; restore char
            STA rb_base,Y       ; write at rb_end
            PLA
            STA rb_end          ; rb_end = next_end
@done:      RTS

