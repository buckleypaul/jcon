/*
 * jcon.c — implementation of the lightweight streaming JSON emitter.
 *
 * All state lives in a single file-scope `g_writer`. Emission primitives and
 * child-separator/key logic are factored so the typed wrappers at the bottom
 * of the file are each only a few lines.
 *
 * Invariants:
 *   - `depth` counts currently-open containers. After jcon_start(), depth == 1
 *     (the implicit root object). The root occupies level index 0.
 *   - `is_object[level]`  1 if the container at `level` is an object, 0 if array.
 *   - `has_child[level]`  1 once any child has been emitted at `level`; used for
 *     comma placement.
 *   - If `status != JCON_OK`, every emit primitive becomes a no-op, but
 *     structural bookkeeping (depth, bit stacks) still updates so that matched
 *     start/end pairs stay balanced.
 */

#include "jcon.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/*
 * Numeric scratch buffer size. Must hold the widest snprintf output the
 * library can produce: int64_t needs 20 digits + sign + NUL = 22, and `%g`
 * on a double tops out at roughly the same in practice. 32 gives headroom
 * without being wasteful on tight targets.
 */
enum { JCON_NUMERIC_BUF = 32 };
_Static_assert(JCON_NUMERIC_BUF >= 22, "numeric buffer must fit int64 + sign + NUL");

struct jcon_writer {
    jcon_putc_fn  putc;
    void         *ctx;
    bool          minify;
    bool          active;
    jcon_status_t status;
    uint16_t      depth;
    uint8_t       is_object[(JCON_MAX_DEPTH + 7) / 8];
    uint8_t       has_child[(JCON_MAX_DEPTH + 7) / 8];
};

static struct jcon_writer g_writer;

/* -------------------------------------------------------------------------- */
/* Bit-stack helpers                                                          */
/* -------------------------------------------------------------------------- */

static inline bool bit_get(const uint8_t *bits, size_t i) {
    return (bits[i >> 3] >> (i & 7u)) & 1u;
}

static inline void bit_set(uint8_t *bits, size_t i, bool v) {
    uint8_t mask = (uint8_t)(1u << (i & 7u));
    if (v) bits[i >> 3] |= mask;
    else   bits[i >> 3] &= (uint8_t)~mask;
}

/* -------------------------------------------------------------------------- */
/* Emit primitives                                                            */
/* -------------------------------------------------------------------------- */

static void emit_char(char c) {
    if (g_writer.status != JCON_OK) return;
    if (g_writer.putc(g_writer.ctx, c) != 0) {
        g_writer.status = JCON_ERR_IO;
    }
}

static void emit_str(const char *s) {
    if (g_writer.status != JCON_OK) return;
    while (*s) {
        if (g_writer.putc(g_writer.ctx, *s++) != 0) {
            g_writer.status = JCON_ERR_IO;
            return;
        }
    }
}

static void emit_newline_and_indent(size_t level) {
    if (g_writer.minify) return;
    emit_char('\n');
    for (size_t i = 0; i < level; i++) {
        emit_str(JCON_INDENT);
    }
}

/*
 * Emit the comma (if needed) and whitespace before writing a new child at the
 * current container's level. Updates `has_child` for the current level.
 */
static void emit_separator_for_child(void) {
    size_t level = (size_t)g_writer.depth - 1u;
    bool first = !bit_get(g_writer.has_child, level);
    if (!first) emit_char(',');
    bit_set(g_writer.has_child, level, true);
    emit_newline_and_indent((size_t)g_writer.depth);
}

/*
 * If the current container is an object, emit `"name": ` (or `"name":` when
 * minified). If it's an array, `name` is silently ignored.
 */
static void emit_key_if_object(const char *name) {
    size_t level = (size_t)g_writer.depth - 1u;
    if (!bit_get(g_writer.is_object, level)) return;
    assert(name != NULL && "jcon: name required inside object");
    if (name == NULL) {
        g_writer.status = JCON_ERR_USAGE;
        return;
    }
    emit_char('"');
    emit_str(name);
    emit_char('"');
    emit_char(':');
    if (!g_writer.minify) emit_char(' ');
}

/*
 * Common prologue for every value/container emission: assert active, handle
 * the separator, and emit the key if needed.
 */
static void prep_child(const char *name) {
    assert(g_writer.active && "jcon: called before jcon_start()");
    if (!g_writer.active) {
        g_writer.status = JCON_ERR_USAGE;
        return;
    }
    emit_separator_for_child();
    emit_key_if_object(name);
}

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* -------------------------------------------------------------------------- */

static void start_root(bool minify, jcon_putc_fn putc, void *ctx, bool root_is_object) {
    assert(putc != NULL);
    memset(&g_writer, 0, sizeof g_writer);
    g_writer.putc   = putc;
    g_writer.ctx    = ctx;
    g_writer.minify = minify;
    g_writer.active = true;
    g_writer.status = JCON_OK;
    g_writer.depth  = 1;
    bit_set(g_writer.is_object, 0, root_is_object);
    emit_char(root_is_object ? '{' : '[');
}

void jcon_start(bool minify, jcon_putc_fn putc, void *ctx) {
    start_root(minify, putc, ctx, true);
}

void jcon_start_array(bool minify, jcon_putc_fn putc, void *ctx) {
    start_root(minify, putc, ctx, false);
}

jcon_status_t jcon_end(void) {
    assert(g_writer.active && "jcon_end without jcon_start");
    assert(g_writer.depth == 1 && "jcon_end with unclosed containers");
    if (!g_writer.active || g_writer.depth != 1) {
        g_writer.status = JCON_ERR_USAGE;
    } else {
        char close = bit_get(g_writer.is_object, 0) ? '}' : ']';
        if (bit_get(g_writer.has_child, 0)) {
            emit_newline_and_indent(0);
        }
        emit_char(close);
        if (!g_writer.minify) emit_char('\n');
    }
    g_writer.active = false;
    return g_writer.status;
}

jcon_status_t jcon_status(void) {
    return g_writer.status;
}

/* -------------------------------------------------------------------------- */
/* Containers                                                                 */
/* -------------------------------------------------------------------------- */

static void container_start(const char *name, char open, bool is_object_container) {
    assert(g_writer.active);
    assert(g_writer.depth < JCON_MAX_DEPTH && "jcon: JCON_MAX_DEPTH exceeded");
    if (!g_writer.active || g_writer.depth >= JCON_MAX_DEPTH) {
        g_writer.status = JCON_ERR_USAGE;
        return;
    }
    emit_separator_for_child();
    emit_key_if_object(name);
    emit_char(open);

    size_t new_level = (size_t)g_writer.depth;
    bit_set(g_writer.is_object, new_level, is_object_container);
    bit_set(g_writer.has_child, new_level, false);
    g_writer.depth++;
}

static void container_end(char close, bool expect_object) {
    assert(g_writer.active);
    assert(g_writer.depth > 1 && "jcon: matching *_end without *_start (root uses jcon_end)");
    if (!g_writer.active || g_writer.depth <= 1) {
        g_writer.status = JCON_ERR_USAGE;
        return;
    }
    size_t level = (size_t)g_writer.depth - 1u;
    bool actual_object = bit_get(g_writer.is_object, level);
    assert(actual_object == expect_object && "jcon: object/array end mismatch");
    if (actual_object != expect_object) {
        g_writer.status = JCON_ERR_USAGE;
        return;
    }
    bool had_children = bit_get(g_writer.has_child, level);
    g_writer.depth--;
    if (had_children) emit_newline_and_indent((size_t)g_writer.depth);
    emit_char(close);
}

void jcon_object_start(const char *name) { container_start(name, '{', true);  }
void jcon_object_end(void)               { container_end('}', true);          }
void jcon_array_start(const char *name)  { container_start(name, '[', false); }
void jcon_array_end(void)                { container_end(']', false);         }

/* -------------------------------------------------------------------------- */
/* Typed value emitters                                                       */
/*                                                                            */
/* The numeric family is generated by expanding a local x-list so that every  */
/* function is guaranteed to use the same prologue and snprintf pattern. The  */
/* non-numeric wrappers (bool, char, string, raw, null, float, double) are    */
/* hand-written because their bodies differ enough that sharing a template    */
/* would only obscure them.                                                   */
/* -------------------------------------------------------------------------- */

#define JCON_DEFINE_NUMERIC_(suffix, ctype, fmt)              \
    void jcon_add_##suffix(const char *name, ctype value) {   \
        char buf[JCON_NUMERIC_BUF];                           \
        prep_child(name);                                     \
        (void)snprintf(buf, sizeof buf, fmt, value);          \
        emit_str(buf);                                        \
    }
JCON_NUMERIC_TYPES(JCON_DEFINE_NUMERIC_)
#undef JCON_DEFINE_NUMERIC_

void jcon_add_bool(const char *name, bool value) {
    prep_child(name);
    emit_str(value ? "true" : "false");
}

void jcon_add_char(const char *name, char value) {
    prep_child(name);
    emit_char('"');
    emit_char(value);
    emit_char('"');
}

void jcon_add_string(const char *name, const char *value) {
    assert(value != NULL && "jcon_add_string: NULL value (use jcon_add_null)");
    if (value == NULL) {
        g_writer.status = JCON_ERR_USAGE;
        return;
    }
    prep_child(name);
    emit_char('"');
    emit_str(value);
    emit_char('"');
}

void jcon_add_raw(const char *name, const char *value) {
    assert(value != NULL && "jcon_add_raw: NULL value");
    if (value == NULL) {
        g_writer.status = JCON_ERR_USAGE;
        return;
    }
    prep_child(name);
    emit_str(value);
}

void jcon_add_null(const char *name) {
    prep_child(name);
    emit_str("null");
}

void jcon_add_bytes_hex(const char *name, const void *bytes, size_t len) {
    static const char hex[] = "0123456789abcdef";
    assert((bytes != NULL || len == 0) && "jcon_add_bytes_hex: NULL with len > 0");
    if (bytes == NULL && len != 0) {
        g_writer.status = JCON_ERR_USAGE;
        return;
    }
    prep_child(name);
    emit_char('"');
    const uint8_t *p = bytes;
    for (size_t i = 0; i < len; i++) {
        emit_char(hex[p[i] >> 4]);
        emit_char(hex[p[i] & 0x0fu]);
    }
    emit_char('"');
}

#ifdef JCON_ENABLE_FLOAT
void jcon_add_float(const char *name, float value) {
    char buf[JCON_NUMERIC_BUF];
    prep_child(name);
    (void)snprintf(buf, sizeof buf, "%g", (double)value);
    emit_str(buf);
}

void jcon_add_double(const char *name, double value) {
    char buf[JCON_NUMERIC_BUF];
    prep_child(name);
    (void)snprintf(buf, sizeof buf, "%g", value);
    emit_str(buf);
}
#endif
