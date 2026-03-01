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

; --- Room IDs ---
ROOM_ALLEY       = 0
ROOM_BAR         = 1
ROOM_MARKET      = 2
ROOM_MICROSOFTS  = 3
ROOM_CLINIC      = 4
ROOM_HOTEL_LOBBY = 5
ROOM_ROOM203     = 6
ROOM_HOTEL_ROOF  = 7
ROOM_METRO_PLAT  = 8
ROOM_METRO_CAR   = 9
ROOM_CORP_PLAZA  = 10
ROOM_TOWER_LOBBY = 11
ROOM_SECURITY    = 12
ROOM_ELEVATOR    = 13
ROOM_PARKING     = 14
NO_EXIT          = $FF

NUM_ROOMS = 15

; --- Item IDs ---
ITEM_CREDCHIP    = 0
ITEM_FLASHLIGHT  = 1
ITEM_STIMPACK    = 2
ITEM_FAKEID      = 3
ITEM_ICEBREAKER  = 4
ITEM_CYBERDECK   = 5
ITEM_JACKCABLE   = 6
ITEM_UPLINK      = 7
ITEM_KEYCARD     = 8
ITEM_CROWBAR     = 9
ITEM_MAGAZINE    = 10

NUM_ITEMS = 11

LOC_INVENTORY = $FE
LOC_GONE      = $FF
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

str_prompt:     .byte "> ", 0
str_unknown:    .byte "What?", 0
str_no_exit:    .byte "You can't go that way.", 0
str_exits_hdr:  .byte "Exits: ", 0
str_quit_msg:   .byte "The neon fades to black.", 0

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

str_dir_n:  .byte "north", 0
str_dir_s:  .byte "south", 0
str_dir_e:  .byte "east", 0
str_dir_w:  .byte "west", 0
str_dir_u:  .byte "up", 0
str_dir_d:  .byte "down", 0

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
        .byte <rn_alley, <rn_bar, <rn_market, <rn_microsofts, <rn_clinic
        .byte <rn_hotel_lobby, <rn_room203, <rn_hotel_roof
        .byte <rn_metro_plat, <rn_metro_car
        .byte <rn_corp_plaza, <rn_tower_lobby, <rn_security
        .byte <rn_elevator, <rn_parking

room_name_hi:
        .byte >rn_alley, >rn_bar, >rn_market, >rn_microsofts, >rn_clinic
        .byte >rn_hotel_lobby, >rn_room203, >rn_hotel_roof
        .byte >rn_metro_plat, >rn_metro_car
        .byte >rn_corp_plaza, >rn_tower_lobby, >rn_security
        .byte >rn_elevator, >rn_parking

; =====================================================================
; Room description pointer tables
; =====================================================================

room_desc_lo:
        .byte <rd_alley, <rd_bar, <rd_market, <rd_microsofts, <rd_clinic
        .byte <rd_hotel_lobby, <rd_room203, <rd_hotel_roof
        .byte <rd_metro_plat, <rd_metro_car
        .byte <rd_corp_plaza, <rd_tower_lobby, <rd_security
        .byte <rd_elevator, <rd_parking

room_desc_hi:
        .byte >rd_alley, >rd_bar, >rd_market, >rd_microsofts, >rd_clinic
        .byte >rd_hotel_lobby, >rd_room203, >rd_hotel_roof
        .byte >rd_metro_plat, >rd_metro_car
        .byte >rd_corp_plaza, >rd_tower_lobby, >rd_security
        .byte >rd_elevator, >rd_parking

; =====================================================================
; Room exit tables
; =====================================================================

room_exit_n:
        .byte ROOM_BAR           ; 0  Alley -> Bar
        .byte NO_EXIT            ; 1  Bar
        .byte NO_EXIT            ; 2  Market
        .byte ROOM_MARKET        ; 3  Microsofts -> Market
        .byte ROOM_MARKET        ; 4  Clinic -> Market
        .byte ROOM_ROOM203       ; 5  Hotel Lobby -> Room 203
        .byte NO_EXIT            ; 6  Room 203
        .byte NO_EXIT            ; 7  Hotel Roof
        .byte NO_EXIT            ; 8  Metro Platform
        .byte NO_EXIT            ; 9  Metro Car
        .byte ROOM_TOWER_LOBBY   ; 10 Corp Plaza -> Tower Lobby
        .byte ROOM_SECURITY      ; 11 Tower Lobby -> Security (locked)
        .byte NO_EXIT            ; 12 Security
        .byte NO_EXIT            ; 13 Elevator
        .byte NO_EXIT            ; 14 Parking

room_exit_s:
        .byte NO_EXIT            ; 0  Alley
        .byte ROOM_ALLEY         ; 1  Bar -> Alley
        .byte ROOM_HOTEL_LOBBY   ; 2  Market -> Hotel Lobby
        .byte NO_EXIT            ; 3  Microsofts
        .byte NO_EXIT            ; 4  Clinic
        .byte ROOM_MARKET        ; 5  Hotel Lobby -> Market
        .byte ROOM_HOTEL_LOBBY   ; 6  Room 203 -> Hotel Lobby
        .byte NO_EXIT            ; 7  Hotel Roof
        .byte NO_EXIT            ; 8  Metro Platform
        .byte NO_EXIT            ; 9  Metro Car
        .byte NO_EXIT            ; 10 Corp Plaza
        .byte ROOM_CORP_PLAZA    ; 11 Tower Lobby -> Corp Plaza
        .byte ROOM_TOWER_LOBBY   ; 12 Security -> Tower Lobby
        .byte NO_EXIT            ; 13 Elevator
        .byte ROOM_CORP_PLAZA    ; 14 Parking -> Corp Plaza

room_exit_e:
        .byte NO_EXIT            ; 0  Alley
        .byte ROOM_MARKET        ; 1  Bar -> Market
        .byte ROOM_MICROSOFTS    ; 2  Market -> Microsofts
        .byte NO_EXIT            ; 3  Microsofts
        .byte NO_EXIT            ; 4  Clinic
        .byte ROOM_METRO_PLAT    ; 5  Hotel Lobby -> Metro Platform
        .byte NO_EXIT            ; 6  Room 203
        .byte NO_EXIT            ; 7  Hotel Roof
        .byte ROOM_METRO_CAR     ; 8  Metro Platform -> Metro Car
        .byte ROOM_CORP_PLAZA    ; 9  Metro Car -> Corp Plaza
        .byte ROOM_PARKING       ; 10 Corp Plaza -> Parking
        .byte ROOM_ELEVATOR      ; 11 Tower Lobby -> Elevator
        .byte NO_EXIT            ; 12 Security
        .byte NO_EXIT            ; 13 Elevator
        .byte NO_EXIT            ; 14 Parking

room_exit_w:
        .byte NO_EXIT            ; 0  Alley
        .byte NO_EXIT            ; 1  Bar
        .byte ROOM_BAR           ; 2  Market -> Bar
        .byte ROOM_MARKET        ; 3  Microsofts -> Market
        .byte ROOM_MARKET        ; 4  Clinic -> Market
        .byte NO_EXIT            ; 5  Hotel Lobby
        .byte NO_EXIT            ; 6  Room 203
        .byte NO_EXIT            ; 7  Hotel Roof
        .byte ROOM_HOTEL_LOBBY   ; 8  Metro Platform -> Hotel Lobby
        .byte ROOM_METRO_PLAT    ; 9  Metro Car -> Metro Platform
        .byte ROOM_METRO_CAR     ; 10 Corp Plaza -> Metro Car
        .byte NO_EXIT            ; 11 Tower Lobby
        .byte NO_EXIT            ; 12 Security
        .byte ROOM_TOWER_LOBBY   ; 13 Elevator -> Tower Lobby
        .byte ROOM_CORP_PLAZA    ; 14 Parking -> Corp Plaza

room_exit_u:
        .byte NO_EXIT            ; 0  Alley
        .byte NO_EXIT            ; 1  Bar
        .byte NO_EXIT            ; 2  Market
        .byte NO_EXIT            ; 3  Microsofts
        .byte NO_EXIT            ; 4  Clinic
        .byte NO_EXIT            ; 5  Hotel Lobby
        .byte ROOM_HOTEL_ROOF    ; 6  Room 203 -> Hotel Roof
        .byte NO_EXIT            ; 7  Hotel Roof
        .byte NO_EXIT            ; 8  Metro Platform
        .byte NO_EXIT            ; 9  Metro Car
        .byte NO_EXIT            ; 10 Corp Plaza
        .byte NO_EXIT            ; 11 Tower Lobby
        .byte NO_EXIT            ; 12 Security
        .byte NO_EXIT            ; 13 Elevator (up = locked, handled by engine)
        .byte NO_EXIT            ; 14 Parking

room_exit_d:
        .byte NO_EXIT            ; 0  Alley
        .byte NO_EXIT            ; 1  Bar
        .byte ROOM_CLINIC        ; 2  Market -> Clinic
        .byte NO_EXIT            ; 3  Microsofts
        .byte NO_EXIT            ; 4  Clinic
        .byte NO_EXIT            ; 5  Hotel Lobby
        .byte NO_EXIT            ; 6  Room 203
        .byte ROOM_ROOM203       ; 7  Hotel Roof -> Room 203
        .byte NO_EXIT            ; 8  Metro Platform
        .byte NO_EXIT            ; 9  Metro Car
        .byte NO_EXIT            ; 10 Corp Plaza
        .byte NO_EXIT            ; 11 Tower Lobby
        .byte NO_EXIT            ; 12 Security
        .byte ROOM_TOWER_LOBBY   ; 13 Elevator -> Tower Lobby (always works)
        .byte NO_EXIT            ; 14 Parking

; =====================================================================
; Item tables
; =====================================================================

item_name_lo:
        .byte <in_credchip, <in_flashlight, <in_stimpack, <in_fakeid
        .byte <in_icebreaker
        .byte <in_cyberdeck, <in_jackcable, <in_uplink
        .byte <in_keycard, <in_crowbar, <in_magazine

item_name_hi:
        .byte >in_credchip, >in_flashlight, >in_stimpack, >in_fakeid
        .byte >in_icebreaker
        .byte >in_cyberdeck, >in_jackcable, >in_uplink
        .byte >in_keycard, >in_crowbar, >in_magazine

item_desc_lo:
        .byte <id_credchip, <id_flashlight, <id_stimpack, <id_fakeid
        .byte <id_icebreaker
        .byte <id_cyberdeck, <id_jackcable, <id_uplink
        .byte <id_keycard, <id_crowbar, <id_magazine

item_desc_hi:
        .byte >id_credchip, >id_flashlight, >id_stimpack, >id_fakeid
        .byte >id_icebreaker
        .byte >id_cyberdeck, >id_jackcable, >id_uplink
        .byte >id_keycard, >id_crowbar, >id_magazine

item_init_loc:
        .byte ROOM_BAR           ; credchip
        .byte ROOM_ALLEY         ; flashlight
        .byte ROOM_CLINIC        ; stim pack
        .byte ROOM_MARKET        ; fake ID
        .byte LOC_GONE           ; ICE breaker (puzzle)
        .byte ROOM_ROOM203       ; cyberdeck
        .byte ROOM_ROOM203       ; jack cable
        .byte ROOM_HOTEL_ROOF    ; uplink code
        .byte ROOM_SECURITY      ; keycard
        .byte ROOM_PARKING       ; crowbar
        .byte ROOM_TOWER_LOBBY   ; magazine

item_flags:
        .byte ITEMF_TAKEABLE     ; credchip
        .byte ITEMF_TAKEABLE     ; flashlight
        .byte ITEMF_TAKEABLE     ; stim pack
        .byte ITEMF_TAKEABLE     ; fake ID
        .byte ITEMF_TAKEABLE     ; ICE breaker
        .byte ITEMF_TAKEABLE     ; cyberdeck
        .byte ITEMF_TAKEABLE     ; jack cable
        .byte ITEMF_TAKEABLE     ; uplink code
        .byte ITEMF_TAKEABLE     ; keycard
        .byte ITEMF_TAKEABLE     ; crowbar
        .byte ITEMF_TAKEABLE     ; magazine

; =====================================================================
; Room names
; =====================================================================

rn_alley:        .byte "Chiba Alley", 0
rn_bar:          .byte "Ratz's Bar", 0
rn_market:       .byte "Night Market", 0
rn_microsofts:   .byte "Microsofts Stall", 0
rn_clinic:       .byte "Body Clinic", 0
rn_hotel_lobby:  .byte "Hotel Lobby", 0
rn_room203:      .byte "Room 203", 0
rn_hotel_roof:   .byte "Hotel Roof", 0
rn_metro_plat:   .byte "Metro Platform", 0
rn_metro_car:    .byte "Metro Car", 0
rn_corp_plaza:   .byte "Corp Plaza", 0
rn_tower_lobby:  .byte "Tower Lobby", 0
rn_security:     .byte "Security Office", 0
rn_elevator:     .byte "Elevator", 0
rn_parking:      .byte "Parking Garage", 0

; =====================================================================
; Room descriptions
; =====================================================================

rd_alley:
        .byte "Neon stutters overhead, reflections smeared across wet "
        .byte "concrete. A dead payphone leans against the wall. "
        .byte "The alley narrows north toward music and breaking glass.", 0

rd_bar:
        .byte "Ratz tends bar with his prosthetic arm. Razorgirls "
        .byte "in the booths. Smoke hangs in the black light. "
        .byte "The night market sprawls east.", 0

rd_market:
        .byte "Open-air stalls hawking knockoff tech and black market "
        .byte "software. Drones scan overhead. Stairs down to a clinic. "
        .byte "Microsofts east. Cheap hotel south.", 0

rd_microsofts:
        .byte "Cramped stall overflowing with software chips and "
        .byte "grey-market neuralware. The vendor watches through "
        .byte "mirrored lenses. WE BUY / SELL / TRADE.", 0

rd_clinic:
        .byte "Fluorescent buzz over a surgical chair. Jars of "
        .byte "bioware on the walls. A woman in a labcoat checks "
        .byte "readouts on a cracked monitor.", 0

rd_hotel_lobby:
        .byte "Capsule hotel lobby. Unmanned desk. Hourly rates. "
        .byte "Stairs north to rooms. Metro station east.", 0

rd_room203:
        .byte "Narrow capsule with a fold-down cot. Someone stashed "
        .byte "gear behind a loose ceiling panel. Fire escape up.", 0

rd_hotel_roof:
        .byte "Wind whips across the roof. Satellite dishes and "
        .byte "jury-rigged antennas cluster around a rusted vent. "
        .byte "The skyline: neon canyon.", 0

rd_metro_plat:
        .byte "Underground platform, stuttering fluorescents. A maglev "
        .byte "car waits eastbound, doors open. Hotel west.", 0

rd_metro_car:
        .byte "Empty maglev interior. Route map shows one stop: "
        .byte "CORP PLAZA. 'Doors closing.'", 0

rd_corp_plaza:
        .byte "Glass and steel canyon. The Tessier-Ashpool tower "
        .byte "rises north, its logo pulsing against the clouds. "
        .byte "Metro west. Parking garage east.", 0

rd_tower_lobby:
        .byte "Marble floors, holographic directory. Armed guards "
        .byte "check IDs at the security gate north. Elevators "
        .byte "east. The plaza is south.", 0

rd_security:
        .byte "Banks of monitors show camera feeds. A guard's "
        .byte "coffee grows cold. A keycard rack hangs on the wall, "
        .byte "one card remaining.", 0

rd_elevator:
        .byte "Corporate elevator. Brushed steel walls. A card "
        .byte "reader glows red beside the control panel. The "
        .byte "lobby is west.", 0

rd_parking:
        .byte "Underground parking level. Rows of black town cars "
        .byte "under buzzing sodium lights. Oil stains and "
        .byte "echoing concrete.", 0

; =====================================================================
; Item names
; =====================================================================

in_credchip:    .byte "credchip", 0
in_flashlight:  .byte "flashlight", 0
in_stimpack:    .byte "stim pack", 0
in_fakeid:      .byte "fake id", 0
in_icebreaker:  .byte "ice breaker", 0
in_cyberdeck:   .byte "cyberdeck", 0
in_jackcable:   .byte "jack cable", 0
in_uplink:      .byte "uplink code", 0
in_keycard:     .byte "keycard", 0
in_crowbar:     .byte "crowbar", 0
in_magazine:    .byte "magazine", 0

; =====================================================================
; Item descriptions
; =====================================================================

id_credchip:
        .byte "Thin plastic wafer loaded with untraceable New Yen.", 0

id_flashlight:
        .byte "Small tactical LED. Cracked lens, tight beam.", 0

id_stimpack:
        .byte "Maas Biolabs endorphin booster. Military grade.", 0

id_fakeid:
        .byte "Holographic corporate ID. 'Armitage, Col. W.' "
        .byte "Tessier-Ashpool subsidiary.", 0

id_icebreaker:
        .byte "Black ROM chip. Military-grade ICE penetration "
        .byte "firmware.", 0

id_cyberdeck:
        .byte "Ono-Sendai Cyberspace VII. Battered case, custom "
        .byte "firmware. Standard neural jack port.", 0

id_jackcable:
        .byte "Fiber-optic patch cable. Neural interface one end, "
        .byte "data port the other.", 0

id_uplink:
        .byte "Crumpled sticky note with hex digits. Corporate "
        .byte "uplink access code.", 0

id_keycard:
        .byte "Tessier-Ashpool security keycard. Level 3 access.", 0

id_crowbar:
        .byte "Heavy steel crowbar. Rust-spotted but solid.", 0

id_magazine:
        .byte "Dog-eared copy of Node magazine. An article is "
        .byte "circled: 'T-A Tower: Service tunnels run parallel "
        .byte "to the elevator shaft.'", 0
