#pragma once

#include <cstdint>
#include <cstddef>

// ---- Callback interface ----
// GDB stub uses these to access the emulator. Zero coupling to N8machine internals.

typedef struct {
    uint8_t  (*read_reg8)(int reg_id);       // reg: 0=A,1=X,2=Y,3=S,4=P
    uint16_t (*read_reg16)(int reg_id);      // reg: 5=PC
    void     (*write_reg8)(int reg_id, uint8_t val);
    void     (*write_reg16)(int reg_id, uint16_t val);
    uint8_t  (*read_mem)(uint16_t addr);
    void     (*write_mem)(uint16_t addr, uint8_t val);
    int      (*step_instruction)(void);      // returns stop signal (5=SIGTRAP, 4=SIGILL)
    void     (*set_breakpoint)(uint16_t addr);
    void     (*clear_breakpoint)(uint16_t addr);
    uint16_t (*get_pc)(void);
    int      (*get_stop_reason)(void);       // returns last signal number
    void     (*reset)(void);
} gdb_stub_callbacks_t;

typedef struct {
    int  port;
    bool enabled;
} gdb_stub_config_t;

typedef enum {
    GDB_POLL_NONE,
    GDB_POLL_HALTED,
    GDB_POLL_RESUMED,
    GDB_POLL_STEPPED,
    GDB_POLL_DETACHED,
    GDB_POLL_KILL
} gdb_poll_result_t;

#if !ENABLE_GDB_STUB

// Compile-time no-op stubs when GDB support is disabled
static inline void gdb_stub_init(const gdb_stub_callbacks_t*, const gdb_stub_config_t*) {}
static inline void gdb_stub_shutdown(void) {}
static inline gdb_poll_result_t gdb_stub_poll(void) { return GDB_POLL_NONE; }
static inline bool gdb_stub_is_connected(void) { return false; }
static inline bool gdb_stub_is_halted(void) { return false; }
static inline void gdb_stub_notify_stop(int) {}

#else

// ---- Lifecycle ----
void gdb_stub_init(const gdb_stub_callbacks_t* cb, const gdb_stub_config_t* cfg);
void gdb_stub_shutdown(void);
gdb_poll_result_t gdb_stub_poll(void);
bool gdb_stub_is_connected(void);
bool gdb_stub_is_halted(void);
void gdb_stub_notify_stop(int signal);

#endif // ENABLE_GDB_STUB

// ---- Testing API ----
#ifdef GDB_STUB_TESTING

#include <string>

void gdb_stub_feed_byte(uint8_t byte);
std::string gdb_stub_process_packet(const char* payload);
std::string gdb_stub_get_response(void);
bool gdb_stub_noack_mode(void);
void gdb_stub_reset_state(void);
int  gdb_stub_last_signal(void);
bool gdb_stub_interrupt_requested(void);
void gdb_stub_set_callbacks(const gdb_stub_callbacks_t* cb);

#endif // GDB_STUB_TESTING
