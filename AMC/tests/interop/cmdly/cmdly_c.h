#pragma once

#include <stddef.h> /* guards the frontend -resource-dir fix */

#include <stdint.h>

// Public C ABI facade over cmdly (a C++ line editor). Cmdly's real API is
// C++-only (classes, std::shared_ptr, templates, STL); this facade exposes a
// stable C boundary so a foreign language can drive it through ABIX.
//
// Sizes are fixed-width (uint64_t) on purpose: a cross-language ABI should not
// depend on a platform `size_t`.

#if defined(_WIN32)
#  define CMDLY_C_API __declspec(dllexport)
#else
#  define CMDLY_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Opaque owner handle, created by this module and released through the destroy
// entry below; the consumer never sees the C++ `Terminal`.
typedef struct cmdly_terminal cmdly_terminal;

CMDLY_C_API cmdly_terminal *cmdly_terminal_create(const char *script);
CMDLY_C_API int32_t cmdly_terminal_destroy(cmdly_terminal *terminal);

CMDLY_C_API int32_t cmdly_completion_insert(cmdly_terminal *terminal, const char *word);

// A key callback: invoked as the editor consumes each key, with a caller-owned
// context. Returning non-zero does not stop editing.
typedef int32_t (*cmdly_key_handler)(void *context, uint32_t key_code);

// Register a callback for a key. key_code == 0 registers for every key.
CMDLY_C_API int32_t cmdly_on_key(cmdly_terminal *terminal, uint32_t key_code, cmdly_key_handler handler, void *context);

CMDLY_C_API int32_t cmdly_history_insert(cmdly_terminal *terminal, const char *line);
CMDLY_C_API uint64_t cmdly_history_length(const cmdly_terminal *terminal);

// Read one line from the scripted input. Writes a NUL-terminated string into
// `out` (capacity bytes) and returns its length, or -1 when it does not fit.
CMDLY_C_API int32_t cmdly_read_line(cmdly_terminal *terminal, const char *prompt, char *out, uint64_t capacity);

// Borrowed text the editor wrote back, valid until the next call.
CMDLY_C_API const char *cmdly_last_output(const cmdly_terminal *terminal);

#ifdef __cplusplus
}
#endif
