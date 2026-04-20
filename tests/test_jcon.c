/*
 * jcon test suite.
 *
 * Builds in three configurations via the Makefile:
 *   - debug    (asserts active, no float)
 *   - release  (NDEBUG, exercises usage-error latching)
 *   - float    (JCON_ENABLE_FLOAT, covers float/double emitters)
 *
 * No external test framework: tests write into an in-memory buffer via a
 * test-only putc, then compare against expected strings.
 */

#include "jcon.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Test-only character sink + mini assertion framework                        */
/* -------------------------------------------------------------------------- */

#define BUF_CAP 4096

struct test_sink {
    char   buf[BUF_CAP];
    size_t len;
    bool   fail_next;
};

static struct test_sink g_sink;

static int test_putc(void *ctx, char c) {
    struct test_sink *s = ctx;
    if (s->fail_next) return -1;
    if (s->len + 1 >= BUF_CAP) return -1;
    s->buf[s->len++] = c;
    return 0;
}

static void sink_reset(struct test_sink *s) {
    s->len = 0;
    s->buf[0] = '\0';
    s->fail_next = false;
}

static const char *sink_cstr(struct test_sink *s) {
    s->buf[s->len] = '\0';
    return s->buf;
}

static int g_tests_run = 0;
static int g_tests_failed = 0;
static const char *g_current_test = "?";

#define EXPECT_STR_EQ(actual, expected) do {                                   \
    const char *_a = (actual);                                                 \
    const char *_e = (expected);                                               \
    g_tests_run++;                                                             \
    if (strcmp(_a, _e) != 0) {                                                 \
        g_tests_failed++;                                                      \
        fprintf(stderr, "  FAIL %s:%d [%s]\n    expected: %s\n    actual:   %s\n", \
                __FILE__, __LINE__, g_current_test, _e, _a);                   \
    }                                                                          \
} while (0)

#define EXPECT_EQ(actual, expected) do {                                       \
    long long _a = (long long)(actual);                                        \
    long long _e = (long long)(expected);                                      \
    g_tests_run++;                                                             \
    if (_a != _e) {                                                            \
        g_tests_failed++;                                                      \
        fprintf(stderr, "  FAIL %s:%d [%s] %s (=%lld) != %s (=%lld)\n",        \
                __FILE__, __LINE__, g_current_test, #actual, _a, #expected, _e); \
    }                                                                          \
} while (0)

#define RUN(fn) do { g_current_test = #fn; sink_reset(&g_sink); fn(); } while (0)

/* -------------------------------------------------------------------------- */
/* Happy-path tests                                                           */
/* -------------------------------------------------------------------------- */

static void test_empty_object(void) {
    jcon_start(true, test_putc, &g_sink);
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink), "{}");
}

static void test_empty_object_pretty(void) {
    jcon_start(false, test_putc, &g_sink);
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink), "{}\n");
}

static void test_basic_minified(void) {
    jcon_start(true, test_putc, &g_sink);
    jcon_add_int("a", 1);
    jcon_add_int("b", 2);
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink), "{\"a\":1,\"b\":2}");
}

static void test_basic_pretty(void) {
    jcon_start(false, test_putc, &g_sink);
    jcon_add_int("a", 1);
    jcon_add_int("b", 2);
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink), "{\n  \"a\": 1,\n  \"b\": 2\n}\n");
}

static void test_all_numeric_types(void) {
    jcon_start(true, test_putc, &g_sink);
    jcon_add_int   ("i",   -1);
    jcon_add_uint  ("u",   1u);
    jcon_add_int32 ("i32", INT32_MIN);
    jcon_add_uint32("u32", UINT32_MAX);
    jcon_add_int64 ("i64", INT64_MIN);
    jcon_add_uint64("u64", UINT64_MAX);
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink),
        "{\"i\":-1,\"u\":1,"
        "\"i32\":-2147483648,\"u32\":4294967295,"
        "\"i64\":-9223372036854775808,\"u64\":18446744073709551615}");
}

static void test_scalar_types(void) {
    jcon_start(true, test_putc, &g_sink);
    jcon_add_bool  ("bt", true);
    jcon_add_bool  ("bf", false);
    jcon_add_char  ("c",  'x');
    jcon_add_string("s",  "hello");
    jcon_add_raw   ("r",  "42.5");
    jcon_add_null  ("n");
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink),
        "{\"bt\":true,\"bf\":false,\"c\":\"x\","
        "\"s\":\"hello\",\"r\":42.5,\"n\":null}");
}

static void test_array_of_ints(void) {
    jcon_start(true, test_putc, &g_sink);
    jcon_array_start("xs");
        jcon_add_int(NULL, 1);
        jcon_add_int(NULL, 2);
        jcon_add_int(NULL, 3);
    jcon_array_end();
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink), "{\"xs\":[1,2,3]}");
}

static void test_empty_containers(void) {
    jcon_start(true, test_putc, &g_sink);
    jcon_object_start("o");
    jcon_object_end();
    jcon_array_start("a");
    jcon_array_end();
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink), "{\"o\":{},\"a\":[]}");
}

static void test_nested(void) {
    jcon_start(true, test_putc, &g_sink);
    jcon_object_start("obj");
        jcon_add_int("x", 1);
        jcon_array_start("arr");
            jcon_add_int(NULL, 10);
            jcon_object_start(NULL);
                jcon_add_bool("flag", true);
            jcon_object_end();
        jcon_array_end();
    jcon_object_end();
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink),
        "{\"obj\":{\"x\":1,\"arr\":[10,{\"flag\":true}]}}");
}

static void test_nested_pretty(void) {
    jcon_start(false, test_putc, &g_sink);
    jcon_array_start("xs");
        jcon_add_int(NULL, 1);
        jcon_add_int(NULL, 2);
    jcon_array_end();
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink),
        "{\n"
        "  \"xs\": [\n"
        "    1,\n"
        "    2\n"
        "  ]\n"
        "}\n");
}

static void test_generic_dispatch(void) {
    jcon_start(true, test_putc, &g_sink);
    int                 i   = 7;
    unsigned int        u   = 8u;
    long                l   = 11L;
    unsigned long       ul  = 12uL;
    long long           ll  = 9;
    unsigned long long  ull = 10ull;
    size_t              sz  = 13;
    bool                b   = true;
    char                c   = 'y';
    const char         *s   = "z";
    jcon_add("i",   i);
    jcon_add("u",   u);
    jcon_add("l",   l);
    jcon_add("ul",  ul);
    jcon_add("ll",  ll);
    jcon_add("ull", ull);
    jcon_add("sz",  sz);
    jcon_add("b",   b);
    jcon_add("c",   c);
    jcon_add("s",   s);
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink),
        "{\"i\":7,\"u\":8,\"l\":11,\"ul\":12,\"ll\":9,\"ull\":10,"
        "\"sz\":13,\"b\":true,\"c\":\"y\",\"s\":\"z\"}");
}

static void test_array_of_mixed(void) {
    jcon_start(true, test_putc, &g_sink);
    jcon_array_start("mix");
        jcon_add_int   (NULL, 1);
        jcon_add_string(NULL, "two");
        jcon_add_bool  (NULL, true);
        jcon_add_null  (NULL);
    jcon_array_end();
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink), "{\"mix\":[1,\"two\",true,null]}");
}

static void test_bytes_hex_basic(void) {
    const uint8_t bytes[] = { 0x00, 0x01, 0xab, 0xff };
    jcon_start(true, test_putc, &g_sink);
    jcon_add_bytes_hex("mac", bytes, sizeof bytes);
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink), "{\"mac\":\"0001abff\"}");
}

static void test_bytes_hex_empty(void) {
    jcon_start(true, test_putc, &g_sink);
    jcon_add_bytes_hex("empty", NULL, 0);
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink), "{\"empty\":\"\"}");
}

/* -------------------------------------------------------------------------- */
/* Array-root tests                                                           */
/* -------------------------------------------------------------------------- */

static void test_array_root_empty(void) {
    jcon_start_array(true, test_putc, &g_sink);
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink), "[]");
}

static void test_array_root_values(void) {
    jcon_start_array(true, test_putc, &g_sink);
    jcon_add_int(NULL, 1);
    jcon_add_int(NULL, 2);
    jcon_add_int(NULL, 3);
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink), "[1,2,3]");
}

static void test_array_root_pretty(void) {
    jcon_start_array(false, test_putc, &g_sink);
    jcon_object_start(NULL);
        jcon_add_int("x", 1);
    jcon_object_end();
    jcon_object_start(NULL);
        jcon_add_int("x", 2);
    jcon_object_end();
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink),
        "[\n"
        "  {\n"
        "    \"x\": 1\n"
        "  },\n"
        "  {\n"
        "    \"x\": 2\n"
        "  }\n"
        "]\n");
}

/* -------------------------------------------------------------------------- */
/* Error path: sticky I/O error                                               */
/* -------------------------------------------------------------------------- */

static void test_io_error_sticky(void) {
    g_sink.fail_next = true;
    jcon_start(true, test_putc, &g_sink);          /* first putc fails -> JCON_ERR_IO */
    jcon_add_int("a", 1);                  /* no-op */
    jcon_add_int("b", 2);                  /* no-op */
    EXPECT_EQ(jcon_status(), JCON_ERR_IO);
    EXPECT_EQ(jcon_end(), JCON_ERR_IO);
}

static void test_start_clears_sticky(void) {
    g_sink.fail_next = true;
    jcon_start(true, test_putc, &g_sink);
    EXPECT_EQ(jcon_end(), JCON_ERR_IO);

    g_sink.fail_next = false;
    sink_reset(&g_sink);                   /* fresh buffer */
    jcon_start(true, test_putc, &g_sink);           /* should clear sticky status */
    jcon_add_int("ok", 1);
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink), "{\"ok\":1}");
}

/* -------------------------------------------------------------------------- */
/* Usage-error tests: only meaningful when assertions are compiled out        */
/* (NDEBUG), because assert() would abort the process otherwise.              */
/* -------------------------------------------------------------------------- */

#ifdef NDEBUG
static void test_depth_overflow(void) {
    jcon_start(true, test_putc, &g_sink);
    /* Root occupies one depth slot already; we can open (MAX-1) more. */
    for (int i = 0; i < JCON_MAX_DEPTH - 1; i++) {
        jcon_object_start("x");
    }
    EXPECT_EQ(jcon_status(), JCON_OK);
    jcon_object_start("overflow");         /* should latch usage error */
    EXPECT_EQ(jcon_status(), JCON_ERR_USAGE);
    /* Unwind only what actually opened; the overflow attempt did not push. */
    for (int i = 0; i < JCON_MAX_DEPTH - 1; i++) {
        jcon_object_end();
    }
    EXPECT_EQ(jcon_end(), JCON_ERR_USAGE);
}

static void test_container_type_mismatch(void) {
    jcon_start(true, test_putc, &g_sink);
    jcon_object_start("o");
    jcon_array_end();                      /* wrong end type */
    EXPECT_EQ(jcon_status(), JCON_ERR_USAGE);
}

static void test_end_at_root(void) {
    jcon_start(true, test_putc, &g_sink);
    jcon_object_end();                     /* cannot close root via object_end */
    EXPECT_EQ(jcon_status(), JCON_ERR_USAGE);
}

static void test_null_string_value(void) {
    jcon_start(true, test_putc, &g_sink);
    jcon_add_string("s", NULL);            /* NULL value: use jcon_add_null */
    EXPECT_EQ(jcon_status(), JCON_ERR_USAGE);
}

static void test_null_raw_value(void) {
    jcon_start(true, test_putc, &g_sink);
    jcon_add_raw("r", NULL);
    EXPECT_EQ(jcon_status(), JCON_ERR_USAGE);
}

static void test_null_bytes_hex_with_len(void) {
    jcon_start(true, test_putc, &g_sink);
    jcon_add_bytes_hex("b", NULL, 4);      /* NULL + nonzero len: usage error */
    EXPECT_EQ(jcon_status(), JCON_ERR_USAGE);
}
#endif /* NDEBUG */

/* -------------------------------------------------------------------------- */
/* Float tests (only when JCON_ENABLE_FLOAT is defined)                       */
/* -------------------------------------------------------------------------- */

#ifdef JCON_ENABLE_FLOAT
static void test_float_basic(void) {
    jcon_start(true, test_putc, &g_sink);
    jcon_add_double("d", 3.14);
    jcon_add_float ("f", 1.5f);
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink), "{\"d\":3.14,\"f\":1.5}");
}

static void test_float_generic_dispatch(void) {
    jcon_start(true, test_putc, &g_sink);
    double d = 2.5;
    float  f = 0.25f;
    jcon_add("d", d);
    jcon_add("f", f);
    EXPECT_EQ(jcon_end(), JCON_OK);
    EXPECT_STR_EQ(sink_cstr(&g_sink), "{\"d\":2.5,\"f\":0.25}");
}
#endif

/* -------------------------------------------------------------------------- */
/* Runner                                                                     */
/* -------------------------------------------------------------------------- */

int main(void) {
    RUN(test_empty_object);
    RUN(test_empty_object_pretty);
    RUN(test_basic_minified);
    RUN(test_basic_pretty);
    RUN(test_all_numeric_types);
    RUN(test_scalar_types);
    RUN(test_array_of_ints);
    RUN(test_empty_containers);
    RUN(test_nested);
    RUN(test_nested_pretty);
    RUN(test_generic_dispatch);
    RUN(test_array_of_mixed);
    RUN(test_bytes_hex_basic);
    RUN(test_bytes_hex_empty);

    RUN(test_array_root_empty);
    RUN(test_array_root_values);
    RUN(test_array_root_pretty);

    RUN(test_io_error_sticky);
    RUN(test_start_clears_sticky);

#ifdef NDEBUG
    RUN(test_depth_overflow);
    RUN(test_container_type_mismatch);
    RUN(test_end_at_root);
    RUN(test_null_string_value);
    RUN(test_null_raw_value);
    RUN(test_null_bytes_hex_with_len);
#endif

#ifdef JCON_ENABLE_FLOAT
    RUN(test_float_basic);
    RUN(test_float_generic_dispatch);
#endif

    printf("\n%d/%d assertions passed", g_tests_run - g_tests_failed, g_tests_run);
#ifdef NDEBUG
    printf(" [NDEBUG]");
#endif
#ifdef JCON_ENABLE_FLOAT
    printf(" [FLOAT]");
#endif
    printf("\n");
    return g_tests_failed == 0 ? 0 : 1;
}
