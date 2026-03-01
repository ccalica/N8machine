; world.s - Sprawl Adventure game data
;
; All strings, room tables, item tables, and game content.
; Engine code is in adventure.s.

.export   str_banner, str_prompt, str_unknown

.segment "RODATA"

; --- System strings ---
str_banner:
        .byte "SPRAWL ADVENTURE", $0D
        .byte "Chiba City, 2058", $0D
        .byte $0D
        .byte "Type 'help' for commands.", 0

str_prompt:
        .byte "> ", 0

str_unknown:
        .byte "What?", 0
