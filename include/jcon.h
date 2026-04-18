/*
 * jcon.h — Lightweight streaming JSON emitter for bare-metal C
 *
 * Single-writer, zero-heap, character-at-a-time emission via a caller-supplied
 * putc callback. Designed for MCU targets where flash and RAM are tight and
 * the output sink (UART, ring buffer, etc.) is owned by the caller.
 *
 * USAGE (minimal):
 *
 *     static int uart_putc(char c) { return uart_write(&c, 1) == 1 ? 0 : -1; }
 *
 *     jcon_start(false, uart_putc);         // opens the root object: "{"
 *     jcon_add("id", 42);                   // generic dispatch via _Generic
 *     jcon_add("ok", true);
 *     jcon_array_start("log");
 *         jcon_add(NULL, "boot");           // name NULL inside an array
 *         jcon_add(NULL, "ready");
 *     jcon_array_end();
 *     jcon_status_t st = jcon_end();        // closes the root and returns sticky status
 *
 * IMPORTANT BEHAVIOURS
 *   - Root container is always an object. jcon_start() emits '{', jcon_end() emits '}'.
 *   - Container/value `name` is used when the parent is an object, ignored when the
 *     parent is an array. Pass NULL inside arrays; non-NULL inside objects.
 *   - No string escaping is performed. jcon_add_string() emits its argument verbatim
 *     between double quotes. The caller is responsible for ensuring the string
 *     contents are valid inside a JSON string body (no unescaped ", \, or control
 *     characters).
 *   - Errors are sticky. The first putc failure latches JCON_ERR_IO; subsequent
 *     emit calls become no-ops so the caller does not need to check per call.
 *     In debug builds, misuse (mismatched end, depth overflow, NULL name inside an
 *     object, etc.) trips assert(). In release builds misuse latches JCON_ERR_USAGE.
 *   - Not thread-safe. One global writer instance.
 *   - Not reentrant. Do not call jcon_* from a putc callback.
 *
 * COMPILE-TIME CONFIGURATION (define before including this header or via -D):
 *   JCON_MAX_DEPTH     max nesting depth (default 16)
 *   JCON_INDENT        indent unit for non-minified output (default "  ")
 *   JCON_ENABLE_FLOAT  if defined, enables jcon_add_float / jcon_add_double and
 *                      adds them to the generic dispatch
 */

#ifndef JCON_H
#define JCON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>

#ifndef JCON_MAX_DEPTH
#define JCON_MAX_DEPTH 16
#endif

#ifndef JCON_INDENT
#define JCON_INDENT "  "
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

/*
 * Caller-supplied character sink. Return 0 on success, any nonzero value on
 * error. On first error the library latches JCON_ERR_IO and stops emitting.
 */
typedef int (*jcon_putc_fn)(char c);

typedef enum {
    JCON_OK        = 0,
    JCON_ERR_IO    = 1,   /* putc returned nonzero at some point */
    JCON_ERR_USAGE = 2    /* misuse caught in release build (debug asserts) */
} jcon_status_t;

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* -------------------------------------------------------------------------- */

/*
 * Reset internal state, install the putc callback, and emit the root object's
 * opening '{'. Abandons any in-progress document. Clears sticky status.
 *
 *   minify  true  → no whitespace between tokens; no trailing newline
 *           false → pretty-print with JCON_INDENT per level and newlines
 */
void jcon_start(bool minify, jcon_putc_fn putc);

/*
 * Emit the root object's closing '}' (plus a trailing '\n' when not minified)
 * and return the final sticky status. After this call the writer is inactive
 * until the next jcon_start().
 */
jcon_status_t jcon_end(void);

/*
 * Peek the sticky status without closing the document. Useful when a caller
 * wants to bail out early on I/O failure.
 */
jcon_status_t jcon_status(void);

/* -------------------------------------------------------------------------- */
/* Containers                                                                 */
/* -------------------------------------------------------------------------- */

/*
 * Open a nested object. `name` is the key if the parent container is an
 * object (must be non-NULL); `name` is ignored when the parent is an array
 * (pass NULL by convention).
 */
void jcon_object_start(const char *name);
void jcon_object_end(void);

void jcon_array_start(const char *name);
void jcon_array_end(void);

/* -------------------------------------------------------------------------- */
/* Typed value emitters                                                       */
/*                                                                            */
/* The JCON_TYPES x-macro list below is the single source of truth for the    */
/* typed wrappers. The implementation file instantiates it again to define    */
/* the functions, so adding a new type is a one-line change here plus one     */
/* format specifier in jcon.c.                                                */
/*                                                                            */
/* Each entry is X(suffix, c_type, printf_fmt):                               */
/*   suffix     — used to form the function name jcon_add_<suffix>            */
/*   c_type     — the C parameter type                                        */
/*   printf_fmt — snprintf format for numeric types; empty for special cases  */
/*                (bool, char, string, raw — handled by dedicated code paths) */
/* -------------------------------------------------------------------------- */

#define JCON_NUMERIC_TYPES(X)                            \
    X(int,    int,              "%d")                    \
    X(uint,   unsigned int,     "%u")                    \
    X(int32,  int32_t,          "%" PRId32)              \
    X(uint32, uint32_t,         "%" PRIu32)              \
    X(int64,  int64_t,          "%" PRId64)              \
    X(uint64, uint64_t,         "%" PRIu64)

#define JCON_NONNUMERIC_TYPES(X)                         \
    X(bool,   bool,             "")                      \
    X(char,   char,             "")                      \
    X(string, const char *,     "")                      \
    X(raw,    const char *,     "")

#define JCON_TYPES(X) JCON_NUMERIC_TYPES(X) JCON_NONNUMERIC_TYPES(X)

#define JCON_DECL_FN_(suffix, ctype, fmt) \
    void jcon_add_##suffix(const char *name, ctype value);
JCON_TYPES(JCON_DECL_FN_)
#undef JCON_DECL_FN_

/* Not in JCON_TYPES — no value argument. */
void jcon_add_null(const char *name);

#ifdef JCON_ENABLE_FLOAT
void jcon_add_float (const char *name, float value);
void jcon_add_double(const char *name, double value);
#endif

/* -------------------------------------------------------------------------- */
/* Generic dispatch                                                           */
/*                                                                            */
/* jcon_add(name, value) picks the right jcon_add_<suffix> at compile time    */
/* based on the type of `value`, using C11 _Generic.                          */
/*                                                                            */
/* Notes on type aliasing:                                                    */
/*   - int32_t is usually a typedef for int (and uint32_t for unsigned int).  */
/*     Listing both in a _Generic controlling expression is a "same type"     */
/*     error, so we only list the fundamental types here. Passing an int32_t  */
/*     variable resolves to the `int` branch on such platforms, which is      */
/*     fine because jcon_add_int formats with %d (correct for 32-bit int).    */
/*   - On platforms where int32_t is not int (rare), call jcon_add_int32      */
/*     directly instead of using the generic macro.                           */
/*   - `long` is handled conditionally below because it may alias either      */
/*     `int` or `long long` depending on the platform.                        */
/*   - jcon_add_raw and jcon_add_null are deliberately excluded from the      */
/*     generic macro — call them by full name.                                */
/*   - C11/C99 <stdbool.h> defines `true`/`false` as int constants (1/0),     */
/*     so `jcon_add("ok", true)` dispatches to jcon_add_int and emits `1`,    */
/*     not `true`. Pass a `bool` variable, cast (`(bool)true`), or call       */
/*     jcon_add_bool directly. C23 fixes this by typing the literals as bool. */
/* -------------------------------------------------------------------------- */

#if LONG_MAX == INT_MAX
#  define JCON_GENERIC_LONG_  /* long == int, already covered */
#elif LONG_MAX == LLONG_MAX
#  define JCON_GENERIC_LONG_  /* long == long long, covered by the int64 branch */
#else
#  define JCON_GENERIC_LONG_                  \
          long:               jcon_add_int64, \
          unsigned long:      jcon_add_uint64,
#endif

#ifdef JCON_ENABLE_FLOAT
#  define JCON_GENERIC_FLOAT_            \
          float:  jcon_add_float,        \
          double: jcon_add_double,
#else
#  define JCON_GENERIC_FLOAT_
#endif

#define jcon_add(name, value) _Generic((value),    \
        int:                jcon_add_int,          \
        unsigned int:       jcon_add_uint,         \
        long long:          jcon_add_int64,        \
        unsigned long long: jcon_add_uint64,       \
        JCON_GENERIC_LONG_                         \
        JCON_GENERIC_FLOAT_                        \
        bool:               jcon_add_bool,         \
        char:               jcon_add_char,         \
        const char *:       jcon_add_string,       \
        char *:             jcon_add_string        \
    )(name, value)

#ifdef __cplusplus
}
#endif

#endif /* JCON_H */
