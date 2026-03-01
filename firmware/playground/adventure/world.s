; world.s - Sprawl Adventure game data
;
; All strings, room tables, item tables, and game content.
; Engine code is in adventure.s.

.export   str_banner, str_prompt, str_unknown
.export   str_help_text, str_no_exit, str_exits_hdr
.export   str_dir_n, str_dir_s, str_dir_e, str_dir_w, str_dir_u, str_dir_d
.export   str_quit_msg
.export   room_name_lo, room_name_hi
.export   room_desc_lo, room_desc_hi
.export   room_exit_n, room_exit_s, room_exit_e, room_exit_w
.export   room_exit_u, room_exit_d
.export   NUM_ROOMS
.export   item_name_lo, item_name_hi
.export   item_desc_lo, item_desc_hi
.export   item_init_loc, item_flags
.export   NUM_ITEMS
.export   str_you_see, str_taken, str_dropped, str_carrying, str_nothing
.export   str_not_here, str_cant_take, str_no_have, str_examine_what
.export   str_take_what, str_drop_what
.export   str_use_what, str_cant_use

; Room IDs
ROOM_ALLEY      = 0
ROOM_BAR        = 1
ROOM_MARKET     = 2
ROOM_MICROSOFTS = 3
ROOM_CLINIC     = 4
NO_EXIT         = $FF

NUM_ROOMS = 5

; Item IDs
ITEM_CREDCHIP   = 0
ITEM_FLASHLIGHT = 1
ITEM_STIMPACK   = 2
ITEM_FAKEID     = 3
ITEM_ICEBREAKER = 4

NUM_ITEMS = 5

; Item locations
LOC_INVENTORY = $FE
LOC_GONE      = $FF

; Item flags
ITEMF_TAKEABLE = $01

.segment "RODATA"

; =====================================================================
; System strings
; =====================================================================

str_banner:
        .byte "SPRAWL ADVENTURE", $0D
        .byte "Chiba City, 2058", $0D
        .byte $0D
        .byte "Type 'help' for commands.", 0

str_prompt:
        .byte "> ", 0

str_unknown:
        .byte "What?", 0

str_no_exit:
        .byte "You can't go that way.", 0

str_exits_hdr:
        .byte "Exits: ", 0

str_quit_msg:
        .byte "The neon fades to black.", 0

str_help_text:
        .byte "Commands:", $0D
        .byte "  look (l)        - Look around", $0D
        .byte "  go <dir>        - Move (n/s/e/w/u/d)", $0D
        .byte "  n/s/e/w/u/d     - Move shortcut", $0D
        .byte "  take/get <item> - Pick up item", $0D
        .byte "  drop <item>     - Drop item", $0D
        .byte "  use <item>      - Use item", $0D
        .byte "  examine/x <item>- Examine item", $0D
        .byte "  inventory (i)   - Show inventory", $0D
        .byte "  help (?)        - This message", $0D
        .byte "  quit            - End game", 0

; Direction names for exit listing
str_dir_n:  .byte "north", 0
str_dir_s:  .byte "south", 0
str_dir_e:  .byte "east", 0
str_dir_w:  .byte "west", 0
str_dir_u:  .byte "up", 0
str_dir_d:  .byte "down", 0

; Item interaction strings
str_you_see:      .byte "You see: ", 0
str_taken:        .byte "Taken.", 0
str_dropped:      .byte "Dropped.", 0
str_carrying:     .byte "Carrying: ", 0
str_nothing:      .byte "nothing", 0
str_not_here:     .byte "You don't see that here.", 0
str_cant_take:    .byte "You can't take that.", 0
str_no_have:      .byte "You're not carrying that.", 0
str_examine_what: .byte "Examine what?", 0
str_take_what:    .byte "Take what?", 0
str_drop_what:    .byte "Drop what?", 0
str_use_what:     .byte "Use what?", 0
str_cant_use:     .byte "That doesn't work here.", 0

; =====================================================================
; Room name pointer tables
; =====================================================================

room_name_lo:
        .byte <rn_alley
        .byte <rn_bar
        .byte <rn_market
        .byte <rn_microsofts
        .byte <rn_clinic

room_name_hi:
        .byte >rn_alley
        .byte >rn_bar
        .byte >rn_market
        .byte >rn_microsofts
        .byte >rn_clinic

; =====================================================================
; Room description pointer tables
; =====================================================================

room_desc_lo:
        .byte <rd_alley
        .byte <rd_bar
        .byte <rd_market
        .byte <rd_microsofts
        .byte <rd_clinic

room_desc_hi:
        .byte >rd_alley
        .byte >rd_bar
        .byte >rd_market
        .byte >rd_microsofts
        .byte >rd_clinic

; =====================================================================
; Room exit tables (destination room ID, or $FF = no exit)
; =====================================================================

room_exit_n:
        .byte ROOM_BAR          ; Alley -> Bar (north)
        .byte NO_EXIT           ; Bar
        .byte NO_EXIT           ; Market
        .byte ROOM_MARKET       ; Microsofts -> Market (north)
        .byte ROOM_MARKET       ; Clinic -> Market (north)

room_exit_s:
        .byte NO_EXIT           ; Alley
        .byte ROOM_ALLEY        ; Bar -> Alley (south)
        .byte NO_EXIT           ; Market
        .byte NO_EXIT           ; Microsofts
        .byte NO_EXIT           ; Clinic

room_exit_e:
        .byte NO_EXIT           ; Alley
        .byte ROOM_MARKET       ; Bar -> Market (east)
        .byte ROOM_MICROSOFTS   ; Market -> Microsofts (east)
        .byte NO_EXIT           ; Microsofts
        .byte NO_EXIT           ; Clinic

room_exit_w:
        .byte NO_EXIT           ; Alley
        .byte NO_EXIT           ; Bar
        .byte ROOM_BAR          ; Market -> Bar (west)
        .byte ROOM_MARKET       ; Microsofts -> Market (west)
        .byte ROOM_MARKET       ; Clinic -> Market (west)

room_exit_u:
        .byte NO_EXIT           ; Alley
        .byte NO_EXIT           ; Bar
        .byte NO_EXIT           ; Market
        .byte NO_EXIT           ; Microsofts
        .byte NO_EXIT           ; Clinic

room_exit_d:
        .byte NO_EXIT           ; Alley
        .byte NO_EXIT           ; Bar
        .byte ROOM_CLINIC       ; Market -> Clinic (down)
        .byte NO_EXIT           ; Microsofts
        .byte NO_EXIT           ; Clinic

; =====================================================================
; Item tables
; =====================================================================

item_name_lo:
        .byte <in_credchip
        .byte <in_flashlight
        .byte <in_stimpack
        .byte <in_fakeid
        .byte <in_icebreaker

item_name_hi:
        .byte >in_credchip
        .byte >in_flashlight
        .byte >in_stimpack
        .byte >in_fakeid
        .byte >in_icebreaker

item_desc_lo:
        .byte <id_credchip
        .byte <id_flashlight
        .byte <id_stimpack
        .byte <id_fakeid
        .byte <id_icebreaker

item_desc_hi:
        .byte >id_credchip
        .byte >id_flashlight
        .byte >id_stimpack
        .byte >id_fakeid
        .byte >id_icebreaker

; Initial locations
item_init_loc:
        .byte ROOM_BAR          ; credchip - on the bar
        .byte ROOM_ALLEY        ; flashlight - in the alley
        .byte ROOM_CLINIC       ; stim pack - at the clinic
        .byte ROOM_MARKET       ; fake ID - at the market
        .byte LOC_GONE          ; ICE breaker - obtained via puzzle

; Flags (bit 0 = takeable)
item_flags:
        .byte ITEMF_TAKEABLE    ; credchip
        .byte ITEMF_TAKEABLE    ; flashlight
        .byte ITEMF_TAKEABLE    ; stim pack
        .byte ITEMF_TAKEABLE    ; fake ID
        .byte ITEMF_TAKEABLE    ; ICE breaker

; =====================================================================
; Room names
; =====================================================================

rn_alley:
        .byte "Chiba Alley", 0
rn_bar:
        .byte "Ratz's Bar", 0
rn_market:
        .byte "Night Market", 0
rn_microsofts:
        .byte "Microsofts Stall", 0
rn_clinic:
        .byte "Body Clinic", 0

; =====================================================================
; Room descriptions
; =====================================================================

rd_alley:
        .byte "Neon stutters overhead, reflections smeared across wet "
        .byte "concrete. A dead payphone leans against the wall. "
        .byte "The alley narrows north toward the sound of music "
        .byte "and breaking glass.", 0

rd_bar:
        .byte "Ratz tends bar with his prosthetic arm, the "
        .byte "servomotors clicking with each pour. A few "
        .byte "razorgirls occupy the booths. Cigarette smoke "
        .byte "hangs in the black light. The night market "
        .byte "sprawls east.", 0

rd_market:
        .byte "Open-air stalls stretch in every direction, hawking "
        .byte "knockoff tech and black market software. Overhead "
        .byte "drones scan the crowd. A narrow stairway leads "
        .byte "down to a basement clinic. Microsofts to the east.", 0

rd_microsofts:
        .byte "A cramped stall overflowing with software chips, "
        .byte "interface cables, and grey-market neuralware. The "
        .byte "vendor watches you through mirrored lenses. A sign "
        .byte "reads: WE BUY / SELL / TRADE.", 0

rd_clinic:
        .byte "Fluorescent light buzzes over a reclining surgical "
        .byte "chair. The walls are lined with jars of bioware. "
        .byte "A woman in a labcoat checks readouts on a cracked "
        .byte "monitor. She looks up expectantly.", 0

; =====================================================================
; Item names
; =====================================================================

in_credchip:
        .byte "credchip", 0
in_flashlight:
        .byte "flashlight", 0
in_stimpack:
        .byte "stim pack", 0
in_fakeid:
        .byte "fake id", 0
in_icebreaker:
        .byte "ice breaker", 0

; =====================================================================
; Item descriptions
; =====================================================================

id_credchip:
        .byte "A thin plastic wafer loaded with untraceable "
        .byte "New Yen. Should cover a few black market purchases.", 0

id_flashlight:
        .byte "A small tactical LED. The lens is cracked but "
        .byte "it still throws a tight beam.", 0

id_stimpack:
        .byte "Military-grade endorphin booster. The label says "
        .byte "Maas Biolabs. One use.", 0

id_fakeid:
        .byte "Holographic corporate ID. The name reads "
        .byte "'Armitage, Col. W.' Tessier-Ashpool subsidiary.", 0

id_icebreaker:
        .byte "Black ROM chip etched with military firmware. "
        .byte "Designed to penetrate corporate intrusion "
        .byte "countermeasures.", 0
