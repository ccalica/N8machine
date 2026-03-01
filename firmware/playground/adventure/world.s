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
ROOM_SERVER      = 15
ROOM_DATA_CENTER = 16
ROOM_CORNER_OFF  = 17
ROOM_SVC_TUNNEL  = 18
ROOM_VENT_SHAFT  = 19
ROOM_MATRIX_GW   = 20
ROOM_ICE_WALL    = 21
ROOM_DATA_VAULT  = 22
ROOM_EXTRACTION  = 23
ROOM_HELIPAD     = 24
NO_EXIT          = $FF

NUM_ROOMS = 25

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
ITEM_ENCCHIP     = 11
ITEM_SVCMAP      = 12
ITEM_CONSTRUCT   = 13
ITEM_MEDKIT      = 14

NUM_ITEMS = 15

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
        .byte "look(l) go n/s/e/w/u/d take/get", $0D
        .byte "drop use examine(x) inventory(i)", $0D
        .byte "jack help(?) quit", 0

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
        .byte <rn_server, <rn_data_center, <rn_corner_off
        .byte <rn_svc_tunnel, <rn_vent_shaft
        .byte <rn_matrix_gw, <rn_ice_wall, <rn_data_vault
        .byte <rn_extraction, <rn_helipad

room_name_hi:
        .byte >rn_alley, >rn_bar, >rn_market, >rn_microsofts, >rn_clinic
        .byte >rn_hotel_lobby, >rn_room203, >rn_hotel_roof
        .byte >rn_metro_plat, >rn_metro_car
        .byte >rn_corp_plaza, >rn_tower_lobby, >rn_security
        .byte >rn_elevator, >rn_parking
        .byte >rn_server, >rn_data_center, >rn_corner_off
        .byte >rn_svc_tunnel, >rn_vent_shaft
        .byte >rn_matrix_gw, >rn_ice_wall, >rn_data_vault
        .byte >rn_extraction, >rn_helipad

; =====================================================================
; Room description pointer tables
; =====================================================================

room_desc_lo:
        .byte <rd_alley, <rd_bar, <rd_market, <rd_microsofts, <rd_clinic
        .byte <rd_hotel_lobby, <rd_room203, <rd_hotel_roof
        .byte <rd_metro_plat, <rd_metro_car
        .byte <rd_corp_plaza, <rd_tower_lobby, <rd_security
        .byte <rd_elevator, <rd_parking
        .byte <rd_server, <rd_data_center, <rd_corner_off
        .byte <rd_svc_tunnel, <rd_vent_shaft
        .byte <rd_matrix_gw, <rd_ice_wall, <rd_data_vault
        .byte <rd_extraction, <rd_helipad

room_desc_hi:
        .byte >rd_alley, >rd_bar, >rd_market, >rd_microsofts, >rd_clinic
        .byte >rd_hotel_lobby, >rd_room203, >rd_hotel_roof
        .byte >rd_metro_plat, >rd_metro_car
        .byte >rd_corp_plaza, >rd_tower_lobby, >rd_security
        .byte >rd_elevator, >rd_parking
        .byte >rd_server, >rd_data_center, >rd_corner_off
        .byte >rd_svc_tunnel, >rd_vent_shaft
        .byte >rd_matrix_gw, >rd_ice_wall, >rd_data_vault
        .byte >rd_extraction, >rd_helipad

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
        .byte ROOM_DATA_CENTER   ; 15 Server Floor -> Data Center
        .byte NO_EXIT            ; 16 Data Center
        .byte NO_EXIT            ; 17 Corner Office
        .byte ROOM_SERVER        ; 18 Service Tunnel -> Server Floor
        .byte ROOM_SVC_TUNNEL    ; 19 Vent Shaft -> Service Tunnel
        .byte ROOM_ICE_WALL      ; 20 Matrix Gateway -> ICE Wall
        .byte ROOM_DATA_VAULT    ; 21 ICE Wall -> Data Vault (locked)
        .byte NO_EXIT            ; 22 Data Vault
        .byte NO_EXIT            ; 23 Extraction Point
        .byte NO_EXIT            ; 24 Helipad

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
        .byte ROOM_SVC_TUNNEL    ; 15 Server Floor -> Service Tunnel
        .byte ROOM_SERVER        ; 16 Data Center -> Server Floor
        .byte NO_EXIT            ; 17 Corner Office
        .byte ROOM_VENT_SHAFT    ; 18 Service Tunnel -> Vent Shaft
        .byte NO_EXIT            ; 19 Vent Shaft
        .byte NO_EXIT            ; 20 Matrix Gateway
        .byte ROOM_MATRIX_GW     ; 21 ICE Wall -> Matrix Gateway
        .byte ROOM_EXTRACTION    ; 22 Data Vault -> Extraction Point
        .byte ROOM_DATA_CENTER   ; 23 Extraction Point -> Data Center (jack out)
        .byte NO_EXIT            ; 24 Helipad

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
        .byte ROOM_CORNER_OFF    ; 15 Server Floor -> Corner Office
        .byte NO_EXIT            ; 16 Data Center
        .byte NO_EXIT            ; 17 Corner Office
        .byte NO_EXIT            ; 18 Service Tunnel
        .byte NO_EXIT            ; 19 Vent Shaft
        .byte NO_EXIT            ; 20 Matrix Gateway
        .byte NO_EXIT            ; 21 ICE Wall
        .byte NO_EXIT            ; 22 Data Vault
        .byte NO_EXIT            ; 23 Extraction Point
        .byte NO_EXIT            ; 24 Helipad

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
        .byte NO_EXIT            ; 15 Server Floor
        .byte NO_EXIT            ; 16 Data Center
        .byte ROOM_SERVER        ; 17 Corner Office -> Server Floor
        .byte NO_EXIT            ; 18 Service Tunnel
        .byte NO_EXIT            ; 19 Vent Shaft
        .byte NO_EXIT            ; 20 Matrix Gateway
        .byte NO_EXIT            ; 21 ICE Wall
        .byte ROOM_ICE_WALL      ; 22 Data Vault -> ICE Wall
        .byte NO_EXIT            ; 23 Extraction Point
        .byte NO_EXIT            ; 24 Helipad

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
        .byte ROOM_SERVER        ; 13 Elevator -> Server Floor (locked)
        .byte ROOM_VENT_SHAFT    ; 14 Parking -> Vent Shaft (locked)
        .byte NO_EXIT            ; 15 Server Floor
        .byte NO_EXIT            ; 16 Data Center
        .byte NO_EXIT            ; 17 Corner Office
        .byte NO_EXIT            ; 18 Service Tunnel
        .byte ROOM_SVC_TUNNEL    ; 19 Vent Shaft -> Service Tunnel
        .byte NO_EXIT            ; 20 Matrix Gateway
        .byte NO_EXIT            ; 21 ICE Wall
        .byte NO_EXIT            ; 22 Data Vault
        .byte ROOM_HELIPAD       ; 23 Extraction Point -> Helipad
        .byte NO_EXIT            ; 24 Helipad

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
        .byte ROOM_TOWER_LOBBY   ; 13 Elevator -> Tower Lobby
        .byte NO_EXIT            ; 14 Parking
        .byte ROOM_ELEVATOR      ; 15 Server Floor -> Elevator
        .byte NO_EXIT            ; 16 Data Center
        .byte NO_EXIT            ; 17 Corner Office
        .byte ROOM_VENT_SHAFT    ; 18 Service Tunnel -> Vent Shaft
        .byte ROOM_PARKING       ; 19 Vent Shaft -> Parking
        .byte NO_EXIT            ; 20 Matrix Gateway
        .byte NO_EXIT            ; 21 ICE Wall
        .byte NO_EXIT            ; 22 Data Vault
        .byte NO_EXIT            ; 23 Extraction Point
        .byte NO_EXIT            ; 24 Helipad

; =====================================================================
; Item tables
; =====================================================================

item_name_lo:
        .byte <in_credchip, <in_flashlight, <in_stimpack, <in_fakeid
        .byte <in_icebreaker
        .byte <in_cyberdeck, <in_jackcable, <in_uplink
        .byte <in_keycard, <in_crowbar, <in_magazine
        .byte <in_encchip, <in_svcmap
        .byte <in_construct, <in_medkit

item_name_hi:
        .byte >in_credchip, >in_flashlight, >in_stimpack, >in_fakeid
        .byte >in_icebreaker
        .byte >in_cyberdeck, >in_jackcable, >in_uplink
        .byte >in_keycard, >in_crowbar, >in_magazine
        .byte >in_encchip, >in_svcmap
        .byte >in_construct, >in_medkit

item_desc_lo:
        .byte <id_credchip, <id_flashlight, <id_stimpack, <id_fakeid
        .byte <id_icebreaker
        .byte <id_cyberdeck, <id_jackcable, <id_uplink
        .byte <id_keycard, <id_crowbar, <id_magazine
        .byte <id_encchip, <id_svcmap
        .byte <id_construct, <id_medkit

item_desc_hi:
        .byte >id_credchip, >id_flashlight, >id_stimpack, >id_fakeid
        .byte >id_icebreaker
        .byte >id_cyberdeck, >id_jackcable, >id_uplink
        .byte >id_keycard, >id_crowbar, >id_magazine
        .byte >id_encchip, >id_svcmap
        .byte >id_construct, >id_medkit

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
        .byte ROOM_CORNER_OFF    ; encrypted chip
        .byte ROOM_SVC_TUNNEL    ; service map
        .byte ROOM_DATA_VAULT    ; data construct
        .byte ROOM_EXTRACTION    ; medkit

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
        .byte ITEMF_TAKEABLE     ; encrypted chip
        .byte ITEMF_TAKEABLE     ; service map
        .byte ITEMF_TAKEABLE     ; data construct
        .byte ITEMF_TAKEABLE     ; medkit

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
rn_server:       .byte "Server Floor", 0
rn_data_center:  .byte "Data Center", 0
rn_corner_off:   .byte "Corner Office", 0
rn_svc_tunnel:   .byte "Service Tunnel", 0
rn_vent_shaft:   .byte "Ventilation Shaft", 0
rn_matrix_gw:    .byte "Matrix Gateway", 0
rn_ice_wall:     .byte "ICE Wall", 0
rn_data_vault:   .byte "Data Vault", 0
rn_extraction:   .byte "Extraction Point", 0
rn_helipad:      .byte "Rooftop Helipad", 0

; =====================================================================
; Room descriptions
; =====================================================================

rd_alley:
        .byte "Neon stutters on wet concrete. Dead payphone. "
        .byte "Music and glass north.", 0

rd_bar:
        .byte "Ratz tends bar, prosthetic arm. Razorgirls in "
        .byte "the booths. Market east.", 0

rd_market:
        .byte "Stalls hawk knockoff tech. Drones overhead. "
        .byte "Clinic down. Microsofts east. Hotel south.", 0

rd_microsofts:
        .byte "Software chips, grey-market neuralware. Vendor "
        .byte "watches. WE BUY / SELL / TRADE.", 0

rd_clinic:
        .byte "Surgical chair. Bioware in jars. Doctor checks "
        .byte "readouts on a cracked monitor.", 0

rd_hotel_lobby:
        .byte "Capsule hotel. Unmanned desk. Hourly rates. "
        .byte "Rooms north. Metro east.", 0

rd_room203:
        .byte "Narrow capsule, fold-down cot. Gear stashed "
        .byte "behind a loose ceiling panel. Escape up.", 0

rd_hotel_roof:
        .byte "Wind. Satellite dishes and jury-rigged antennas. "
        .byte "Neon canyon skyline.", 0

rd_metro_plat:
        .byte "Underground platform. Maglev waits east, doors "
        .byte "open. Hotel west.", 0

rd_metro_car:
        .byte "Empty maglev. Route map: one stop. CORP PLAZA.", 0

rd_corp_plaza:
        .byte "Glass canyon. T-A tower north, logo pulsing. "
        .byte "Metro west. Parking east.", 0

rd_tower_lobby:
        .byte "Marble and holograms. Guards check IDs at the "
        .byte "gate north. Elevators east.", 0

rd_security:
        .byte "Monitor banks, camera feeds. Cold coffee. One "
        .byte "keycard left on the rack.", 0

rd_elevator:
        .byte "Corporate elevator. Card reader glows red. "
        .byte "Lobby west.", 0

rd_parking:
        .byte "Black town cars under sodium light. Oil stains. "
        .byte "Echoing concrete.", 0

rd_server:
        .byte "Server racks hum in cold air. LEDs blink. "
        .byte "Data center north. Office east. Tunnel south.", 0

rd_data_center:
        .byte "Mainframe core. Cables feed a processing column. "
        .byte "Neural jack port glows blue.", 0

rd_corner_off:
        .byte "Executive suite. Windows over the Sprawl. "
        .byte "Terminal shows encrypted files.", 0

rd_svc_tunnel:
        .byte "Maintenance corridor. Pipes and conduits. "
        .byte "Ozone smell. Vent shaft down.", 0

rd_vent_shaft:
        .byte "Vertical shaft, metal rungs. Cold air rises. "
        .byte "Tunnel above.", 0

rd_matrix_gw:
        .byte "Blue wireframe grid. Your chrome icon forms. "
        .byte "T-A fortress north: black geometry.", 0

rd_ice_wall:
        .byte "Black ICE. Lethal to the unprotected. Code "
        .byte "spirals. Data vault beyond.", 0

rd_data_vault:
        .byte "Geometric data objects orbit a central core. "
        .byte "The construct glows like a star.", 0

rd_extraction:
        .byte "Jack-out node. Ghost images of meatspace. "
        .byte "Helipad up. Data center east.", 0

rd_helipad:
        .byte "Tower roof. Black helicopter idles on the pad. "
        .byte "The city burns neon below.", 0

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
in_encchip:     .byte "encrypted chip", 0
in_svcmap:      .byte "service map", 0
in_construct:   .byte "data construct", 0
in_medkit:      .byte "medkit", 0

; =====================================================================
; Item descriptions
; =====================================================================

id_credchip:
        .byte "Plastic wafer. Untraceable New Yen.", 0

id_flashlight:
        .byte "Tactical LED. Cracked lens.", 0

id_stimpack:
        .byte "Endorphin booster. Military grade.", 0

id_fakeid:
        .byte "Holo ID. 'Armitage, Col. W.' T-A subsidiary.", 0

id_icebreaker:
        .byte "Black ROM. Military ICE penetration.", 0

id_cyberdeck:
        .byte "Ono-Sendai VII. Battered, custom firmware.", 0

id_jackcable:
        .byte "Fiber-optic patch cable. Neural to data.", 0

id_uplink:
        .byte "Sticky note. Hex digits. Uplink code.", 0

id_keycard:
        .byte "T-A security keycard. Level 3.", 0

id_crowbar:
        .byte "Steel crowbar. Rust-spotted, solid.", 0

id_magazine:
        .byte "Node mag. 'T-A: tunnels parallel the "
        .byte "elevator shaft.'", 0

id_encchip:
        .byte "T-A encrypted data chip. Contents unknown.", 0

id_svcmap:
        .byte "Building schematic. Service routes in red.", 0

id_construct:
        .byte "Digitized personality construct. Priceless.", 0

id_medkit:
        .byte "Military medkit. Synth-skin, endorphin.", 0
