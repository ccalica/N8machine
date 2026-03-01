; adventure.s - Sprawl Adventure engine
;
; Frame buffer + video registers + keyboard I/O.
; FB I/O routines adapted from test_monitor.s.
; Readline adapted from mon1.s for keyboard+FB.
; Parser adapted from mon1.s with verb dispatch table.

.export   _main
.export   cursor_on, cursor_off, put_char, new_line, scroll_up, clear_screen
.export   kbd_wait, kbd_read, print_str, print_wrap, readline_fb
.export   parse, dispatch

; --- World data imports ---
.import   str_banner, str_prompt, str_unknown
.import   str_help_text, str_no_exit, str_exits_hdr
.import   str_dir_n, str_dir_s, str_dir_e, str_dir_w, str_dir_u, str_dir_d
.import   str_quit_msg
.import   room_name_lo, room_name_hi
.import   room_desc_lo, room_desc_hi
.import   room_exit_n, room_exit_s, room_exit_e, room_exit_w
.import   room_exit_u, room_exit_d
.import   NUM_ROOMS
.import   item_name_lo, item_name_hi
.import   item_desc_lo, item_desc_hi
.import   item_init_loc, item_flags
; NUM_ITEMS defined locally (must match world.s)
NUM_ITEMS = 4
.import   str_you_see, str_taken, str_dropped, str_carrying, str_nothing
.import   str_not_here, str_cant_take, str_no_have, str_examine_what
.import   str_take_what, str_drop_what

; --- Hardware registers ---
KBD_DATA   = $D860
KBD_STATUS = $D861
KBD_ACK    = $D861

VID_OPER   = $D844
VID_CURSOR = $D845
VID_CURCOL = $D846
VID_CURROW = $D847

FB_BASE    = $C000

; Video operation codes
VIDOP_SCROLL_UP = $01

; Cursor: flash + block + rate 15
CURSOR_STYLE = $F6

; Keys
KEY_ENTER = $0D
KEY_BS    = $08

; Line buffer
BUF_SIZE  = 79                  ; max chars (leave room for null)

; Room exit: no exit marker
NO_EXIT   = $FF

; Item locations
LOC_INVENTORY = $FE             ; item is in player's inventory
LOC_GONE      = $FF             ; item is removed from game

; Item flags
ITEMF_TAKEABLE = $01            ; bit 0: can be picked up
ITEMF_HIDDEN   = $02            ; bit 1: hidden (not listed)

; Screen dimensions
SCR_COLS  = 80
SCR_ROWS  = 25

; --- Zero page variables ---
.segment "ZEROPAGE"
zp_col:   .res 1               ; current column (0-79)
zp_row:   .res 1               ; current row (0-24)
zp_fb:    .res 2               ; frame buffer pointer
zp_str:   .res 2               ; string pointer
zp_tmp:   .res 1               ; temp for Y save
line_len: .res 1               ; current input line length
cur_room: .res 1               ; current room ID
zp_ptr2:  .res 2               ; noun pointer (set by parse)
zp_tmp2:  .res 1               ; second temp
zp_item:  .res 1               ; resolved item ID ($FF = none)

; --- BSS (RAM) ---
.segment "BSS"
LINE_BUF:      .res 80         ; input line buffer
VERB_BUF:      .res 16         ; parsed verb
NOUN_BUF:      .res 40         ; parsed noun (rest of line)
item_location: .res 16         ; current location of each item

; =====================================================================
.segment "CODE"

_main:
        JSR clear_screen

        ; Print banner
        LDA #<str_banner
        STA zp_str
        LDA #>str_banner
        STA zp_str+1
        JSR print_str
        JSR new_line

        ; Init item locations from ROM table
        JSR init_items

        ; Set starting room
        LDA #$00
        STA cur_room

        ; Auto-look at start
        JSR do_look

main_loop:
        ; Print prompt
        LDA #<str_prompt
        STA zp_str
        LDA #>str_prompt
        STA zp_str+1
        JSR print_str
        JSR cursor_on

        ; Read input line
        JSR readline_fb

        ; Empty line -> re-prompt
        LDA line_len
        BEQ main_loop

        ; Parse verb + noun
        JSR parse

        ; Dispatch verb
        JSR dispatch

        JMP main_loop

; =====================================================================
; Parse — split LINE_BUF into VERB_BUF + NOUN_BUF
; Converts verb to lowercase.
; =====================================================================
parse:
        LDX #$00               ; index into LINE_BUF
        LDY #$00               ; index into VERB_BUF

        ; Copy verb (up to space or end)
@verb:  LDA LINE_BUF,X
        BEQ @end_verb
        CMP #$20               ; space
        BEQ @end_verb
        ; To lowercase: if A-Z ($41-$5A), OR $20
        CMP #$41
        BCC @store_v
        CMP #$5B
        BCS @store_v
        ORA #$20
@store_v:
        CPY #15                 ; VERB_BUF max 15 chars + null
        BCS @skip_v
        STA VERB_BUF,Y
        INY
@skip_v:
        INX
        JMP @verb

@end_verb:
        ; Null-terminate verb
        LDA #$00
        STA VERB_BUF,Y

        ; Skip spaces
@skip_sp:
        LDA LINE_BUF,X
        BEQ @no_noun
        CMP #$20
        BNE @has_noun
        INX
        JMP @skip_sp

@has_noun:
        ; Copy rest to NOUN_BUF, lowercase
        LDY #$00
@noun:  LDA LINE_BUF,X
        BEQ @end_noun
        ; To lowercase
        CMP #$41
        BCC @store_n
        CMP #$5B
        BCS @store_n
        ORA #$20
@store_n:
        CPY #38                 ; NOUN_BUF max
        BCS @skip_n
        STA NOUN_BUF,Y
        INY
@skip_n:
        INX
        JMP @noun

@end_noun:
        LDA #$00
        STA NOUN_BUF,Y
        RTS

@no_noun:
        LDA #$00
        STA NOUN_BUF
        RTS

; =====================================================================
; Dispatch — look up VERB_BUF in verb table, call handler
; =====================================================================
dispatch:
        LDX #$00               ; index into verb_table (by 4s)
@loop:
        ; Load verb string pointer from table
        LDA verb_table,X
        STA zp_ptr2
        LDA verb_table+1,X
        STA zp_ptr2+1

        ; End of table? (null pointer)
        ORA zp_ptr2
        BEQ @unknown

        ; Compare VERB_BUF with table entry
        STX zp_tmp2             ; save table index
        JSR strcmp
        LDX zp_tmp2
        BEQ @found              ; strcmp returned 0 = match

        ; Next entry (4 bytes: 2 name ptr + 2 handler ptr)
        INX
        INX
        INX
        INX
        JMP @loop

@found:
        ; Load handler address, push for RTS dispatch
        LDA verb_table+3,X     ; handler hi
        PHA
        LDA verb_table+2,X     ; handler lo
        PHA
        RTS                     ; "return" to handler

@unknown:
        LDA #<str_unknown
        STA zp_str
        LDA #>str_unknown
        STA zp_str+1
        JSR print_str
        JSR new_line
        RTS

; =====================================================================
; strcmp — compare VERB_BUF with string at (zp_ptr2)
; Returns: A=0 if equal, A!=0 if different.
; =====================================================================
strcmp:
        LDY #$00
@loop:  LDA VERB_BUF,Y
        CMP (zp_ptr2),Y
        BNE @diff
        ; Both null? -> match
        CMP #$00
        BEQ @match
        INY
        BNE @loop
@diff:  LDA #$01
        RTS
@match: LDA #$00
        RTS

; =====================================================================
; Verb table: { name_lo, name_hi, (handler-1)_lo, (handler-1)_hi }
; Handler address is stored as addr-1 for RTS dispatch.
; =====================================================================
.segment "RODATA"

verb_table:
        ; look / l
        .addr vn_look
        .word do_look - 1
        .addr vn_l
        .word do_look - 1
        ; go
        .addr vn_go
        .word do_go - 1
        ; directions
        .addr vn_n
        .word do_north - 1
        .addr vn_s
        .word do_south - 1
        .addr vn_e
        .word do_east - 1
        .addr vn_w
        .word do_west - 1
        .addr vn_u
        .word do_up - 1
        .addr vn_d
        .word do_down - 1
        .addr vn_north
        .word do_north - 1
        .addr vn_south
        .word do_south - 1
        .addr vn_east
        .word do_east - 1
        .addr vn_west
        .word do_west - 1
        .addr vn_up
        .word do_up - 1
        .addr vn_down
        .word do_down - 1
        ; help / ?
        .addr vn_help
        .word do_help - 1
        .addr vn_qmark
        .word do_help - 1
        ; take / get
        .addr vn_take
        .word do_take - 1
        .addr vn_get
        .word do_take - 1
        ; drop
        .addr vn_drop
        .word do_drop - 1
        ; inventory / i
        .addr vn_inventory
        .word do_inventory - 1
        .addr vn_i
        .word do_inventory - 1
        ; examine / x
        .addr vn_examine
        .word do_examine - 1
        .addr vn_x
        .word do_examine - 1
        ; quit
        .addr vn_quit
        .word do_quit - 1
        ; end of table
        .addr $0000
        .word $0000

; Verb name strings
vn_look:  .byte "look", 0
vn_l:     .byte "l", 0
vn_go:    .byte "go", 0
vn_n:     .byte "n", 0
vn_s:     .byte "s", 0
vn_e:     .byte "e", 0
vn_w:     .byte "w", 0
vn_u:     .byte "u", 0
vn_d:     .byte "d", 0
vn_north: .byte "north", 0
vn_south: .byte "south", 0
vn_east:  .byte "east", 0
vn_west:  .byte "west", 0
vn_up:    .byte "up", 0
vn_down:  .byte "down", 0
vn_take:      .byte "take", 0
vn_get:       .byte "get", 0
vn_drop:      .byte "drop", 0
vn_inventory: .byte "inventory", 0
vn_i:         .byte "i", 0
vn_examine:   .byte "examine", 0
vn_x:         .byte "x", 0
vn_help:      .byte "help", 0
vn_qmark:     .byte "?", 0
vn_quit:      .byte "quit", 0

; =====================================================================
; Verb handlers
; =====================================================================
.segment "CODE"

; --- do_look: print room name, description, exits ---
do_look:
        LDX cur_room

        ; Print room name
        LDA room_name_lo,X
        STA zp_str
        LDA room_name_hi,X
        STA zp_str+1
        JSR print_str
        JSR new_line

        ; Print room description (word-wrapped)
        LDA room_desc_lo,X
        STA zp_str
        LDA room_desc_hi,X
        STA zp_str+1
        JSR print_wrap
        JSR new_line

        ; Print exits
        JSR print_exits

        ; List items in room
        JSR print_room_items
        RTS

; --- print_exits: list available exits for cur_room ---
print_exits:
        LDA #<str_exits_hdr
        STA zp_str
        LDA #>str_exits_hdr
        STA zp_str+1
        JSR print_str

        LDX cur_room
        LDA #$00
        STA zp_tmp2             ; exit count

        LDA room_exit_n,X
        CMP #NO_EXIT
        BEQ @check_s
        INC zp_tmp2
        JSR print_exit_sep
        LDA #<str_dir_n
        STA zp_str
        LDA #>str_dir_n
        STA zp_str+1
        JSR print_str

@check_s:
        LDX cur_room
        LDA room_exit_s,X
        CMP #NO_EXIT
        BEQ @check_e
        INC zp_tmp2
        JSR print_exit_sep
        LDA #<str_dir_s
        STA zp_str
        LDA #>str_dir_s
        STA zp_str+1
        JSR print_str

@check_e:
        LDX cur_room
        LDA room_exit_e,X
        CMP #NO_EXIT
        BEQ @check_w
        INC zp_tmp2
        JSR print_exit_sep
        LDA #<str_dir_e
        STA zp_str
        LDA #>str_dir_e
        STA zp_str+1
        JSR print_str

@check_w:
        LDX cur_room
        LDA room_exit_w,X
        CMP #NO_EXIT
        BEQ @check_u
        INC zp_tmp2
        JSR print_exit_sep
        LDA #<str_dir_w
        STA zp_str
        LDA #>str_dir_w
        STA zp_str+1
        JSR print_str

@check_u:
        LDX cur_room
        LDA room_exit_u,X
        CMP #NO_EXIT
        BEQ @check_d
        INC zp_tmp2
        JSR print_exit_sep
        LDA #<str_dir_u
        STA zp_str
        LDA #>str_dir_u
        STA zp_str+1
        JSR print_str

@check_d:
        LDX cur_room
        LDA room_exit_d,X
        CMP #NO_EXIT
        BEQ @done
        INC zp_tmp2
        JSR print_exit_sep
        LDA #<str_dir_d
        STA zp_str
        LDA #>str_dir_d
        STA zp_str+1
        JSR print_str

@done:
        JSR new_line
        RTS

; Print separator between exits (comma+space after first)
print_exit_sep:
        LDA zp_tmp2
        BEQ @none
        LDA #','
        JSR put_char
        LDA #' '
        JSR put_char
@none:  RTS

; --- do_go: table-driven direction lookup from NOUN_BUF ---
do_go:
        LDA NOUN_BUF
        BNE @has_noun
        ; No noun
        LDA #<str_go_where
        STA zp_str
        LDA #>str_go_where
        STA zp_str+1
        JSR print_str
        JMP new_line

@has_noun:
        ; Scan go_dir_table: { name_ptr, handler-1 } x N, terminated by $0000
        LDX #$00
@scan:  LDA go_dir_table,X
        STA zp_ptr2
        LDA go_dir_table+1,X
        STA zp_ptr2+1
        ORA zp_ptr2
        BEQ @bad_dir            ; end of table
        STX zp_tmp2
        JSR strcmp_noun
        LDX zp_tmp2
        BEQ @matched
        INX
        INX
        INX
        INX
        JMP @scan

@matched:
        ; RTS-dispatch to handler
        LDA go_dir_table+3,X
        PHA
        LDA go_dir_table+2,X
        PHA
        RTS

@bad_dir:
        LDA #<str_no_exit
        STA zp_str
        LDA #>str_no_exit
        STA zp_str+1
        JSR print_str
        JMP new_line

; --- strcmp_noun: compare NOUN_BUF with (zp_ptr2) ---
strcmp_noun:
        LDY #$00
@loop:  LDA NOUN_BUF,Y
        CMP (zp_ptr2),Y
        BNE @diff
        CMP #$00
        BEQ @match
        INY
        BNE @loop
@diff:  LDA #$01
        RTS
@match: LDA #$00
        RTS

; --- Direction handlers ---
do_north:
        LDX cur_room
        LDA room_exit_n,X
        JMP try_move

do_south:
        LDX cur_room
        LDA room_exit_s,X
        JMP try_move

do_east:
        LDX cur_room
        LDA room_exit_e,X
        JMP try_move

do_west:
        LDX cur_room
        LDA room_exit_w,X
        JMP try_move

do_up:
        LDX cur_room
        LDA room_exit_u,X
        JMP try_move

do_down:
        LDX cur_room
        LDA room_exit_d,X
        JMP try_move

; --- try_move: A = destination room, $FF = no exit ---
try_move:
        CMP #NO_EXIT
        BEQ @blocked
        STA cur_room
        JSR new_line
        JSR do_look
        RTS
@blocked:
        LDA #<str_no_exit
        STA zp_str
        LDA #>str_no_exit
        STA zp_str+1
        JSR print_str
        JSR new_line
        RTS

; --- init_items: copy item_init_loc → item_location ---
init_items:
        LDX #NUM_ITEMS
        DEX
@loop:  LDA item_init_loc,X
        STA item_location,X
        DEX
        BPL @loop
        RTS

; --- print_room_items: list visible items in cur_room ---
print_room_items:
        ; First pass: check if any items here
        LDX #$00
        LDA #$00
        STA zp_tmp2             ; found count
@scan:  CPX #NUM_ITEMS
        BCS @scan_done
        LDA item_location,X
        CMP cur_room
        BNE @scan_next
        ; Check not hidden
        LDA item_flags,X
        AND #ITEMF_HIDDEN
        BNE @scan_next
        INC zp_tmp2
@scan_next:
        INX
        JMP @scan
@scan_done:
        LDA zp_tmp2
        BEQ @no_items

        ; Print "You see: " header
        LDA #<str_you_see
        STA zp_str
        LDA #>str_you_see
        STA zp_str+1
        JSR print_str

        ; Second pass: print item names
        LDX #$00
        LDA #$00
        STA zp_tmp2             ; printed count
@print: CPX #NUM_ITEMS
        BCS @print_done
        LDA item_location,X
        CMP cur_room
        BNE @print_next
        LDA item_flags,X
        AND #ITEMF_HIDDEN
        BNE @print_next
        ; Print separator
        STX zp_item
        JSR print_exit_sep      ; reuse comma separator
        LDX zp_item
        INC zp_tmp2
        ; Print item name
        LDA item_name_lo,X
        STA zp_str
        LDA item_name_hi,X
        STA zp_str+1
        STX zp_item
        JSR print_str
        LDX zp_item
@print_next:
        INX
        JMP @print
@print_done:
        JSR new_line
@no_items:
        RTS

; --- find_noun_item: resolve NOUN_BUF to item ID ---
; Searches items in current room and inventory.
; Sets zp_item = item ID, or $FF if not found.
; Also sets A = item ID or $FF.
find_noun_item:
        LDX #$00
@loop:  CPX #NUM_ITEMS
        BCS @not_found
        ; Check if item is in current room or inventory
        LDA item_location,X
        CMP cur_room
        BEQ @check_name
        CMP #LOC_INVENTORY
        BEQ @check_name
        INX
        JMP @loop

@check_name:
        ; Compare NOUN_BUF with item name
        STX zp_item
        LDA item_name_lo,X
        STA zp_ptr2
        LDA item_name_hi,X
        STA zp_ptr2+1
        JSR strcmp_noun
        LDX zp_item
        BEQ @found
        INX
        JMP @loop

@found: STX zp_item
        TXA
        RTS

@not_found:
        LDA #$FF
        STA zp_item
        RTS

; --- do_take ---
do_take:
        LDA NOUN_BUF
        BNE @has_noun
        LDA #<str_take_what
        STA zp_str
        LDA #>str_take_what
        STA zp_str+1
        JSR print_str
        JMP new_line

@has_noun:
        JSR find_noun_item
        CMP #$FF
        BNE @found_item

        ; Item not found
        LDA #<str_not_here
        STA zp_str
        LDA #>str_not_here
        STA zp_str+1
        JSR print_str
        JMP new_line

@found_item:
        ; Check if item is in current room (not inventory)
        LDX zp_item
        LDA item_location,X
        CMP cur_room
        BEQ @in_room
        ; Already carrying it
        LDA #<str_already_have
        STA zp_str
        LDA #>str_already_have
        STA zp_str+1
        JSR print_str
        JMP new_line

@in_room:
        ; Check if takeable
        LDA item_flags,X
        AND #ITEMF_TAKEABLE
        BNE @can_take
        LDA #<str_cant_take
        STA zp_str
        LDA #>str_cant_take
        STA zp_str+1
        JSR print_str
        JMP new_line

@can_take:
        ; Move to inventory
        LDA #LOC_INVENTORY
        STA item_location,X
        LDA #<str_taken
        STA zp_str
        LDA #>str_taken
        STA zp_str+1
        JSR print_str
        JMP new_line

; --- do_drop ---
do_drop:
        LDA NOUN_BUF
        BNE @has_noun
        LDA #<str_drop_what
        STA zp_str
        LDA #>str_drop_what
        STA zp_str+1
        JSR print_str
        JMP new_line

@has_noun:
        ; Find item in inventory
        LDX #$00
@find:  CPX #NUM_ITEMS
        BCS @not_carrying
        LDA item_location,X
        CMP #LOC_INVENTORY
        BNE @find_next
        ; Check name
        STX zp_item
        LDA item_name_lo,X
        STA zp_ptr2
        LDA item_name_hi,X
        STA zp_ptr2+1
        JSR strcmp_noun
        LDX zp_item
        BEQ @found
@find_next:
        INX
        JMP @find

@found:
        ; Drop to current room
        LDA cur_room
        STA item_location,X
        LDA #<str_dropped
        STA zp_str
        LDA #>str_dropped
        STA zp_str+1
        JSR print_str
        JMP new_line

@not_carrying:
        LDA #<str_no_have
        STA zp_str
        LDA #>str_no_have
        STA zp_str+1
        JSR print_str
        JMP new_line

; --- do_inventory ---
do_inventory:
        LDA #<str_carrying
        STA zp_str
        LDA #>str_carrying
        STA zp_str+1
        JSR print_str

        LDX #$00
        LDA #$00
        STA zp_tmp2             ; count
@loop:  CPX #NUM_ITEMS
        BCS @done
        LDA item_location,X
        CMP #LOC_INVENTORY
        BNE @next
        ; Print separator
        STX zp_item
        JSR print_exit_sep
        LDX zp_item
        INC zp_tmp2
        ; Print item name
        LDA item_name_lo,X
        STA zp_str
        LDA item_name_hi,X
        STA zp_str+1
        STX zp_item
        JSR print_str
        LDX zp_item
@next:  INX
        JMP @loop

@done:
        LDA zp_tmp2
        BNE @has_items
        LDA #<str_nothing
        STA zp_str
        LDA #>str_nothing
        STA zp_str+1
        JSR print_str
@has_items:
        JMP new_line

; --- do_examine ---
do_examine:
        LDA NOUN_BUF
        BNE @has_noun
        LDA #<str_examine_what
        STA zp_str
        LDA #>str_examine_what
        STA zp_str+1
        JSR print_str
        JMP new_line

@has_noun:
        JSR find_noun_item
        CMP #$FF
        BNE @found
        LDA #<str_not_here
        STA zp_str
        LDA #>str_not_here
        STA zp_str+1
        JSR print_str
        JMP new_line

@found:
        LDX zp_item
        LDA item_desc_lo,X
        STA zp_str
        LDA item_desc_hi,X
        STA zp_str+1
        JSR print_wrap
        JMP new_line

; --- do_help ---
do_help:
        LDA #<str_help_text
        STA zp_str
        LDA #>str_help_text
        STA zp_str+1
        JSR print_str
        JSR new_line
        RTS

; --- do_quit ---
do_quit:
        LDA #<str_quit_msg
        STA zp_str
        LDA #>str_quit_msg
        STA zp_str+1
        JSR print_str
        JSR new_line
@halt:  JMP @halt

; =====================================================================
; print_wrap — word-wrapped output from (zp_str)
; Wraps at column SCR_COLS, breaks on spaces.
; =====================================================================
print_wrap:
        LDY #$00               ; source index
@loop:
        LDA (zp_str),Y
        BEQ @done

        ; Embedded newline ($0D)?
        CMP #$0D
        BEQ @nl

        ; If we're at column 0 and char is space, skip leading space
        LDA zp_col
        BNE @no_skip
        LDA (zp_str),Y
        CMP #$20
        BNE @no_skip
        INY
        BNE @loop
        BEQ @done

@no_skip:
        ; Check if we need to wrap
        LDA zp_col
        CMP #SCR_COLS
        BCC @fits

        ; Wrap: newline, then print this char
        STY zp_tmp
        JSR new_line
        LDY zp_tmp

@fits:
        ; Look ahead: if near end of line, check for word break
        LDA zp_col
        CMP #60                 ; start checking at col 60
        BCC @just_print

        ; If current char is space and close to end, wrap here
        LDA (zp_str),Y
        CMP #$20
        BNE @just_print

        ; Check if next word would fit
        STY zp_tmp
        JSR word_len_ahead      ; returns A = length of next word
        CLC
        ADC zp_col
        CMP #SCR_COLS
        BCC @just_print         ; word fits, print the space

        ; Word won't fit — wrap
        LDY zp_tmp
        INY                     ; skip the space
        STY zp_tmp
        JSR new_line
        LDY zp_tmp
        JMP @loop

@just_print:
        LDA (zp_str),Y
        STY zp_tmp
        JSR put_char
        LDY zp_tmp
        INY
        BNE @loop
        BEQ @done

@nl:
        STY zp_tmp
        JSR new_line
        LDY zp_tmp
        INY
        BNE @loop

@done:  RTS

; --- word_len_ahead: count chars until next space or null ---
; Y = current position (on the space). Count starts from Y+1.
; Returns A = word length.
word_len_ahead:
        LDY zp_tmp
        INY                     ; skip current space
        LDA #$00
        STA zp_tmp2             ; length counter
@wl:    LDA (zp_str),Y
        BEQ @wl_done
        CMP #$20
        BEQ @wl_done
        CMP #$0D
        BEQ @wl_done
        INC zp_tmp2
        INY
        BNE @wl
@wl_done:
        LDA zp_tmp2
        RTS

; =====================================================================
; Readline — keyboard poll + frame buffer echo
; Fills LINE_BUF, sets line_len. Null-terminates.
; =====================================================================
readline_fb:
        LDA #$00
        STA line_len
@loop:
        JSR kbd_wait
        JSR kbd_read            ; A = keycode

        ; Enter -> done
        CMP #KEY_ENTER
        BEQ @done

        ; Backspace
        CMP #KEY_BS
        BEQ @bs

        ; Printable ASCII ($20-$7E)?
        CMP #$20
        BCC @loop
        CMP #$7F
        BCS @loop

        ; Buffer full?
        LDX line_len
        CPX #BUF_SIZE
        BCS @loop

        ; Store in buffer
        STA LINE_BUF,X
        INC line_len

        ; Echo to screen
        PHA
        JSR cursor_off
        PLA
        JSR put_char
        JSR cursor_on
        JMP @loop

@bs:
        LDX line_len
        BEQ @loop               ; nothing to erase
        DEC line_len
        JSR cursor_off
        DEC zp_col
        LDA #$20                ; erase with space
        JSR put_char_at_cur
        DEC zp_col
        JSR cursor_on
        JMP @loop

@done:
        ; Null-terminate
        LDX line_len
        LDA #$00
        STA LINE_BUF,X
        ; Move to next line
        JSR cursor_off
        JSR new_line
        JSR cursor_on
        RTS

; =====================================================================
; Frame buffer I/O (from test_monitor.s)
; =====================================================================

; --- Print null-terminated string at (zp_str) ---
; Handles $0D as newline within strings.
print_str:
        LDY #$00
@loop:  LDA (zp_str),Y
        BEQ @done
        CMP #$0D
        BEQ @nl
        STY zp_tmp
        JSR put_char
        LDY zp_tmp
        INY
        BNE @loop
@done:  RTS
@nl:    STY zp_tmp
        JSR new_line
        LDY zp_tmp
        INY
        BNE @loop

; --- Put character A at cursor position, advance cursor ---
put_char:
        PHA
        JSR calc_fb_addr
        PLA
        LDY #$00
        STA (zp_fb),Y

        INC zp_col
        LDA zp_col
        CMP #80
        BCC @update
        LDA #$00
        STA zp_col
        JSR advance_row
@update:
        LDA zp_col
        STA VID_CURCOL
        LDA zp_row
        STA VID_CURROW
        RTS

; --- Put char A at cursor without advancing row (for backspace erase) ---
put_char_at_cur:
        PHA
        JSR calc_fb_addr
        PLA
        LDY #$00
        STA (zp_fb),Y
        INC zp_col
        LDA zp_col
        STA VID_CURCOL
        RTS

; --- Calculate FB address: zp_fb = FB_BASE + row*80 + col ---
calc_fb_addr:
        ; row << 4 (row * 16)
        LDA #$00
        STA zp_fb+1
        LDA zp_row
        ASL A
        ROL zp_fb+1
        ASL A
        ROL zp_fb+1
        ASL A
        ROL zp_fb+1
        ASL A
        ROL zp_fb+1
        STA zp_fb               ; zp_fb = row*16

        ; Save row*16 on stack
        LDA zp_fb+1
        PHA
        LDA zp_fb
        PHA

        ; row*64 = row*16 << 2
        ASL zp_fb
        ROL zp_fb+1
        ASL zp_fb
        ROL zp_fb+1
        ; zp_fb = row*64

        ; row*80 = row*64 + row*16
        PLA                     ; lo(row*16)
        CLC
        ADC zp_fb
        STA zp_fb
        PLA                     ; hi(row*16)
        ADC zp_fb+1
        STA zp_fb+1

        ; + col
        CLC
        LDA zp_fb
        ADC zp_col
        STA zp_fb
        LDA zp_fb+1
        ADC #$00
        STA zp_fb+1

        ; + FB_BASE ($C000)
        CLC
        LDA zp_fb+1
        ADC #>FB_BASE
        STA zp_fb+1
        RTS

; --- New line ---
new_line:
        LDA #$00
        STA zp_col
        JSR advance_row
        LDA zp_col
        STA VID_CURCOL
        LDA zp_row
        STA VID_CURROW
        RTS

; --- Advance row, scroll if at bottom ---
advance_row:
        INC zp_row
        LDA zp_row
        CMP #25
        BCC @done
        JSR scroll_up
        LDA #24
        STA zp_row
@done:  RTS

; --- Scroll up via hardware ---
scroll_up:
        LDA #VIDOP_SCROLL_UP
        STA VID_OPER
        RTS

; --- Clear screen ---
clear_screen:
        LDA #$00
        STA zp_col
        STA zp_row

        LDA #<FB_BASE
        STA zp_fb
        LDA #>FB_BASE
        STA zp_fb+1

        LDA #$20
        LDX #$08                ; 8 pages (2048 bytes, covers 80*25=2000)
        LDY #$00
@page:  STA (zp_fb),Y
        INY
        BNE @page
        INC zp_fb+1
        DEX
        BNE @page

        LDA #$00
        STA VID_CURCOL
        STA VID_CURROW
        RTS

; --- Cursor on (flashing block) ---
cursor_on:
        LDA zp_col
        STA VID_CURCOL
        LDA zp_row
        STA VID_CURROW
        LDA #CURSOR_STYLE
        STA VID_CURSOR
        RTS

; --- Cursor off ---
cursor_off:
        LDA #$00
        STA VID_CURSOR
        RTS

; --- Wait for key ---
kbd_wait:
        LDA KBD_STATUS
        AND #$01
        BEQ kbd_wait
        RTS

; --- Read and ack key ---
kbd_read:
        LDA KBD_DATA
        PHA
        LDA #$01
        STA KBD_ACK
        PLA
        RTS

; =====================================================================
; Local string data
; =====================================================================
.segment "RODATA"

str_go_where:
        .byte "Go where?", 0

str_already_have:
        .byte "You already have that.", 0

; Direction lookup table for "go" command: { name_ptr, (handler-1) }
go_dir_table:
        .addr vn_n
        .word do_north - 1
        .addr vn_north
        .word do_north - 1
        .addr vn_s
        .word do_south - 1
        .addr vn_south
        .word do_south - 1
        .addr vn_e
        .word do_east - 1
        .addr vn_east
        .word do_east - 1
        .addr vn_w
        .word do_west - 1
        .addr vn_west
        .word do_west - 1
        .addr vn_u
        .word do_up - 1
        .addr vn_up
        .word do_up - 1
        .addr vn_d
        .word do_down - 1
        .addr vn_down
        .word do_down - 1
        .addr $0000
        .word $0000
