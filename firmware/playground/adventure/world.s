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
.export   str_go_where, str_already_have
.export   str_neural_link, str_already_linked
.export   str_fakeid_use, str_already_access
.export   str_keycard_use, str_already_elev
.export   str_guard_block, str_buy_ice
.export   str_elev_locked, str_vent_locked
.export   str_crowbar_use, str_already_vent
.export   str_ice_block, str_ice_use, str_already_ice
.export   str_medkit_use
.export   str_jack_where, str_jack_nodeck, str_jack_nocable
.export   str_jack_nolink, str_jack_in
.export   str_victory, str_victory2

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

.segment "GAMEDATA"

; =====================================================================
; System strings
; =====================================================================

str_banner:
        .byte "SPRAWL ADVENTURE", $0D
        .byte "A Neuromancer Story", $0D
        .byte $0D
        .byte "Chiba City, 2058. The sky above the port is the color of "
        .byte "television, tuned to a dead channel. You wake in an alley "
        .byte "behind Ratz's bar, rain on your face, a job in your head. "
        .byte "Wintermute wants a construct pulled from Tessier-Ashpool "
        .byte "ice. The pay is enough to buy new eyes.", $0D
        .byte $0D
        .byte "Type 'help' for commands.", 0

str_prompt:     .byte "> ", 0
str_unknown:    .byte "What?", 0
str_no_exit:    .byte "You can't go that way.", 0
str_exits_hdr:  .byte "Exits: ", 0
str_quit_msg:
        .byte "The neon fades to black. Somewhere in the matrix, "
        .byte "Wintermute waits. It always waits.", 0

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
        .byte "Rain drips from fire escapes into pools of neon "
        .byte "reflection. A dead payphone hangs off the hook, "
        .byte "its cord cut clean. The air smells of ozone and "
        .byte "frying krill. Graffiti tags the walls in "
        .byte "kanji and pidgin English. The sound of breaking "
        .byte "glass and jazz drifts from somewhere north.", 0

rd_bar:
        .byte "Ratz tends bar with his prosthetic arm, the "
        .byte "pink manipulator whining as he polishes glasses "
        .byte "that will never be clean. Razorgirls occupy the "
        .byte "back booths, silver lenses reflecting the Fuji "
        .byte "Electric logo that rotates above the bar. "
        .byte "A Braun coffee maker hisses behind the counter. "
        .byte "The night market lies east.", 0

rd_market:
        .byte "Stalls crowd the narrow street, hawking knockoff "
        .byte "tech under strings of colored bulbs. Microsofts, "
        .byte "cheap RAM, black-market biologicals. Drones buzz "
        .byte "overhead recording everything. A body clinic "
        .byte "operates below street level. Microsofts stall "
        .byte "east. Capsule hotel south.", 0

rd_microsofts:
        .byte "A narrow stall crammed with trays of software "
        .byte "chips and grey-market neuralware. The vendor is "
        .byte "a thin Vietnamese kid with military-surplus "
        .byte "optics bolted to his face. A hand-painted sign: "
        .byte "WE BUY / SELL / TRADE. He watches your hands "
        .byte "carefully.", 0

rd_clinic:
        .byte "Underground clinic, white tile stained yellow. A "
        .byte "surgical chair sits under a bank of halogen "
        .byte "lights. Bioware floats in labeled jars along the "
        .byte "walls. The doctor, a Russian ex-military medic, "
        .byte "checks readouts on a cracked monitor without "
        .byte "looking up.", 0

rd_hotel_lobby:
        .byte "The Cheap Hotel, capsule-style. An unmanned desk "
        .byte "collects payment through a slot. Hourly, daily, "
        .byte "weekly. The carpet was red once. A drunk salary- "
        .byte "man snores on a vinyl couch. Rooms north, metro "
        .byte "platform east.", 0

rd_room203:
        .byte "Your capsule. Narrow as a coffin, fold-down cot, "
        .byte "one thin blanket. A cracked mirror. Behind a "
        .byte "loose ceiling panel you keep your gear stashed "
        .byte "where the cleaning drones never look. A fire "
        .byte "escape leads up to the roof.", 0

rd_hotel_roof:
        .byte "Wind tears across the rooftop. Satellite dishes "
        .byte "and jury-rigged antennas sprout like chrome "
        .byte "mushrooms. The neon canyon of Chiba stretches in "
        .byte "every direction, holographic billboards painting "
        .byte "the clouds. You can see the port from here, "
        .byte "container ships like floating cities.", 0

rd_metro_plat:
        .byte "An underground platform, fluorescent tubes "
        .byte "buzzing overhead. A maglev train waits with "
        .byte "doors open, the route map showing one stop to "
        .byte "the corporate sector. Graffiti covers the "
        .byte "support columns. Hotel lobby west.", 0

rd_metro_car:
        .byte "Empty maglev car, seats worn smooth. The route "
        .byte "map glows on the wall: one stop, CORP PLAZA. "
        .byte "Advertisements cycle on the overhead screens. "
        .byte "T-A recruitment. Hosaka consumer electronics. "
        .byte "Sense/Net programming. The doors chime.", 0

rd_corp_plaza:
        .byte "A canyon of glass and polished granite. The "
        .byte "Tessier-Ashpool tower rises north, its logo "
        .byte "pulsing blue-white against the dark sky. "
        .byte "Corporate drones in matching suits flow around "
        .byte "you like water around a stone. Surveillance "
        .byte "cameras track from every ledge. Metro west, "
        .byte "parking garage east.", 0

rd_tower_lobby:
        .byte "Marble floors, holographic art installations "
        .byte "that shift as you move. Armed guards in "
        .byte "T-A security uniforms check IDs at a gate "
        .byte "north. A directory lists floors you have no "
        .byte "clearance to visit. The elevator bank is east. "
        .byte "A discarded magazine lies near a planter.", 0

rd_security:
        .byte "Banks of monitors show camera feeds from every "
        .byte "floor. Cold coffee in paper cups. The night "
        .byte "shift guard left in a hurry. One keycard sits "
        .byte "on the rack by the door, still warm from the "
        .byte "proximity reader.", 0

rd_elevator:
        .byte "Corporate elevator, brushed steel and indirect "
        .byte "lighting. A card reader glows red on the panel. "
        .byte "The floor indicator shows sub-levels and upper "
        .byte "floors, all locked out. Lobby west.", 0

rd_parking:
        .byte "Underground parking level. Black town cars sit "
        .byte "under sodium lights, waxed and waiting for "
        .byte "executives who work through the night. Oil "
        .byte "stains pattern the concrete like Rorschach "
        .byte "blots. An air vent is set into the ceiling.", 0

rd_server:
        .byte "Rows of server racks hum in refrigerated air. "
        .byte "Status LEDs blink in cascading patterns, "
        .byte "processing transactions for half of Chiba. The "
        .byte "floor vibrates with the power draw. Data center "
        .byte "north. Corner office east. Service tunnel south "
        .byte "through a maintenance door.", 0

rd_data_center:
        .byte "The mainframe core. Thick fiber-optic cables "
        .byte "feed a central processing column that rises "
        .byte "floor to ceiling, cooling fans roaring. A "
        .byte "neural jack port glows steady blue on the main "
        .byte "console, waiting for a connection. This is "
        .byte "where the ice starts.", 0

rd_corner_off:
        .byte "An executive corner office, floor-to-ceiling "
        .byte "windows overlooking the Sprawl. The desk is "
        .byte "real wood, imported. A terminal displays "
        .byte "encrypted files, T-A proprietary headers "
        .byte "scrolling past. Someone left in a hurry. The "
        .byte "chair is still warm.", 0

rd_svc_tunnel:
        .byte "A maintenance corridor that runs behind the "
        .byte "main structure. Pipes and fiber conduits line "
        .byte "the walls, color-coded and labeled in Japanese. "
        .byte "The air tastes of ozone and machine oil. A vent "
        .byte "shaft descends into darkness.", 0

rd_vent_shaft:
        .byte "A vertical shaft lined with metal rungs, cold "
        .byte "air rising from somewhere below. The walls are "
        .byte "corrugated steel, riveted and sweating "
        .byte "condensation. Service tunnel above, parking "
        .byte "level below.", 0

rd_matrix_gw:
        .byte "The matrix unfolds around you, an infinite blue "
        .byte "wireframe grid stretching to a false horizon. "
        .byte "Your chrome icon takes shape, hands becoming "
        .byte "tool-clusters. The T-A data fortress rises "
        .byte "north: black geometry, beautiful and lethal, "
        .byte "sheathed in corporate ice.", 0

rd_ice_wall:
        .byte "A wall of black ICE, Intrusion Countermeasures "
        .byte "Electronics, lethal to any unprotected deck. "
        .byte "Code spirals across its surface in patterns "
        .byte "that hurt to look at. Military-grade. The kind "
        .byte "that flatlines cowboys and leaves their meat "
        .byte "behind. The data vault lies beyond.", 0

rd_data_vault:
        .byte "Geometric data objects orbit a central core "
        .byte "like electrons around a nucleus. Financial "
        .byte "records, research data, personnel files. And "
        .byte "there, glowing like a captured star, the "
        .byte "construct. A digitized human personality, "
        .byte "trapped in T-A ice for years.", 0

rd_extraction:
        .byte "A jack-out node, the boundary between matrix "
        .byte "and meatspace. Ghost images of the real world "
        .byte "bleed through the wireframe. Your meat body "
        .byte "waits in the data center, slumped in a chair. "
        .byte "Helipad access up. Data center south.", 0

rd_helipad:
        .byte "The tower roof, wind screaming across the "
        .byte "landing pad. A black Hughes-Boeing helicopter "
        .byte "idles on the pad, rotors turning. The pilot "
        .byte "flashes a thumbs-up through armored glass. The "
        .byte "city burns neon below, the Sprawl stretching "
        .byte "to the horizon in every direction.", 0

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
        .byte "A thin plastic wafer loaded with untraceable "
        .byte "New Yen. Street currency, pre-paid and "
        .byte "anonymous. Enough to buy what you need if you "
        .byte "know where to shop.", 0

id_flashlight:
        .byte "A tactical LED flashlight, matte black, lens "
        .byte "cracked from impact. Still throws a solid beam. "
        .byte "Military surplus, probably lifted from a dead "
        .byte "soldier's kit.", 0

id_stimpack:
        .byte "A military-grade endorphin booster in a spring-"
        .byte "loaded syrette. One hit numbs pain for hours. "
        .byte "The label reads EMERGENCY USE ONLY in three "
        .byte "languages.", 0

id_fakeid:
        .byte "A holographic ID card, expert forgery. 'Col. "
        .byte "Willis Armitage, Tessier-Ashpool Biomedical "
        .byte "Division.' The holo shifts convincingly when "
        .byte "you tilt it. Good enough to fool a guard.", 0

id_icebreaker:
        .byte "A black ROM chip, military ICE penetration "
        .byte "software. The kind of thing that gets you "
        .byte "killed if the wrong people find it on you. No "
        .byte "manufacturer markings. Pure contraband.", 0

id_cyberdeck:
        .byte "An Ono-Sendai Cyberspace VII. Battered, custom "
        .byte "firmware, the case held together with electrical "
        .byte "tape. Modified trodes and a cranked signal "
        .byte "amplifier. It smells faintly of solder.", 0

id_jackcable:
        .byte "A fiber-optic patch cable with a neural jack on "
        .byte "one end and a standard data port on the other. "
        .byte "The connector is gold-plated, milspec. Bridge "
        .byte "between meat and machine.", 0

id_uplink:
        .byte "A yellow sticky note with hex digits scrawled "
        .byte "in ballpoint. An uplink authorization code for "
        .byte "the building's satellite array. Someone left "
        .byte "it on the antenna housing.", 0

id_keycard:
        .byte "A Tessier-Ashpool security keycard, Level 3 "
        .byte "clearance. Matte grey with the T-A logo "
        .byte "embossed in silver. The magnetic strip still "
        .byte "reads clean.", 0

id_crowbar:
        .byte "A steel crowbar, rust-spotted but solid. "
        .byte "Heavy enough to pry open anything short of "
        .byte "a bank vault. The kind of tool that doubles "
        .byte "as a weapon in the wrong neighborhood.", 0

id_magazine:
        .byte "A dog-eared copy of Node magazine. One article "
        .byte "is circled in red: 'Tessier-Ashpool Tower: "
        .byte "service tunnels run parallel to the main "
        .byte "elevator shaft. Architectural oversight or "
        .byte "deliberate backdoor?'", 0

id_encchip:
        .byte "A T-A encrypted data chip, corporate markings "
        .byte "lasered into the casing. Contents unknown but "
        .byte "valuable enough to lock behind three layers of "
        .byte "physical security.", 0

id_svcmap:
        .byte "A laminated building schematic, probably left "
        .byte "by a maintenance crew. Service routes marked in "
        .byte "red ink, access panels circled. Shows a path "
        .byte "from the parking level to the server floor.", 0

id_construct:
        .byte "A digitized personality construct, a human mind "
        .byte "encoded in crystalline memory. It pulses with "
        .byte "faint light, warm to the touch. Someone lived "
        .byte "in here once. Priceless to the right buyer. "
        .byte "Priceless to Wintermute.", 0

id_medkit:
        .byte "A military field medkit, olive drab. Contains "
        .byte "synth-skin patches, endorphin boosters, and an "
        .byte "auto-suture unit. Everything you need to keep "
        .byte "moving when your body wants to quit.", 0

; =====================================================================
; Puzzle / system messages
; =====================================================================

str_go_where:
        .byte "Go where? Specify a direction.", 0

str_already_have:
        .byte "You already have that.", 0

str_neural_link:
        .byte "Quick sting behind the ear as the doctor's "
        .byte "hands move with practiced speed. The world "
        .byte "flickers. Done. Neural interface installed, a "
        .byte "cold coin of metal flush against your skull. "
        .byte "The matrix is one cable away now.", 0

str_already_linked:
        .byte "You touch the metal disc behind your ear. "
        .byte "Your neural link is already installed.", 0

str_fakeid_use:
        .byte "You flash the T-A holographic ID. The guard "
        .byte "squints, checks it against his screen, nods "
        .byte "curtly. 'Go ahead, Colonel.' Security north "
        .byte "is cleared.", 0

str_already_access:
        .byte "The guard recognizes you. 'Already cleared, "
        .byte "Colonel.' He waves you through.", 0

str_keycard_use:
        .byte "The keycard slides into the reader with a soft "
        .byte "click. Green light. The elevator hums to life, "
        .byte "floor indicators cycling as it descends to "
        .byte "your level.", 0

str_already_elev:
        .byte "The elevator panel already glows green. "
        .byte "It's waiting for you.", 0

str_guard_block:
        .byte "A T-A security guard blocks the way north, "
        .byte "one hand on a stun baton. 'Authorized "
        .byte "personnel only. Show ID or leave.'", 0

str_buy_ice:
        .byte "The credchip slides across the counter. The "
        .byte "Vietnamese kid palms it without looking, "
        .byte "reaches under the counter and produces a "
        .byte "featureless black ROM chip. 'Military grade. "
        .byte "Don't ask where I got it. Don't come back.'", 0

str_elev_locked:
        .byte "The elevator panel is dark. A card reader "
        .byte "slot glows red, waiting for authorization.", 0

str_vent_locked:
        .byte "A heavy steel grate covers the vent shaft, "
        .byte "bolted to the concrete. Cold air whispers "
        .byte "through the slats from below.", 0

str_crowbar_use:
        .byte "You wedge the crowbar under the grate and "
        .byte "lever hard. Bolts snap one by one, pinging "
        .byte "off the walls. The grate swings open. Cold "
        .byte "air rushes up from the shaft below.", 0

str_already_vent:
        .byte "The vent grate hangs open, bolts sheared "
        .byte "clean.", 0

str_ice_block:
        .byte "A wall of black ICE blocks the northern "
        .byte "path, code spiraling across its surface in "
        .byte "patterns designed to kill. Military grade. "
        .byte "Touch it without protection and you flatline.", 0

str_ice_use:
        .byte "The ICE breaker ROM slots into your deck. "
        .byte "Code spears into the black wall. For three "
        .byte "seconds nothing happens. Then the ICE "
        .byte "fragments into static and dissolves. The "
        .byte "path to the data vault lies open.", 0

str_already_ice:
        .byte "The ICE is already down, nothing but fading "
        .byte "static where the wall stood.", 0

str_medkit_use:
        .byte "You crack the syrette and jam it against "
        .byte "your thigh. The endorphin rush hits like a "
        .byte "freight train. Pain recedes to a distant "
        .byte "memory. You feel ready for anything.", 0

str_jack_where:
        .byte "There's nothing to jack into here. You need "
        .byte "a neural jack port to enter the matrix.", 0

str_jack_nodeck:
        .byte "You need a cyberdeck to jack into the matrix. "
        .byte "Without one, the port is just a hole in the "
        .byte "wall.", 0

str_jack_nocable:
        .byte "You need a jack cable to bridge the neural "
        .byte "interface to the data port.", 0

str_jack_nolink:
        .byte "You need a neural interface first. The body "
        .byte "clinic in Chiba could install one.", 0

str_jack_in:
        .byte "The cable clicks home. The world dissolves "
        .byte "into blue geometry, the office falling away "
        .byte "like a discarded skin. Cyberspace unfolds "
        .byte "around you, infinite and electric. You're in.", 0

str_victory:
        .byte "The helicopter lifts off the tower roof and "
        .byte "banks hard over the Sprawl. Below, Chiba City "
        .byte "burns neon against the dark. The construct "
        .byte "pulses warm in your jacket. Somewhere in the "
        .byte "matrix, Wintermute acknowledges delivery. The "
        .byte "pay hits your account before you cross the bay.", 0

str_victory2:
        .byte $0D
        .byte "*** YOU WIN ***", $0D
        .byte $0D
        .byte "The sky above the port was the color of "
        .byte "television, tuned to a dead channel.", 0
