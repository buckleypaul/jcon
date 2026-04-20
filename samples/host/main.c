/*
 * jcon host sample — writes to stdout via fputc.
 *
 * Demonstrates: pretty object root, minified array root, nested containers,
 * and jcon_add_bytes_hex. Run with `make sample` from the repo root.
 */

#include "jcon.h"

#include <stdio.h>

static int stdout_putc(void *ctx, char c) {
    (void)ctx;
    return fputc(c, stdout) == EOF ? -1 : 0;
}

int main(void) {
    const uint8_t mac[6] = { 0xde, 0xad, 0xbe, 0xef, 0x12, 0x34 };

    /* Pretty object root. */
    jcon_start(false, stdout_putc, NULL);
    jcon_add_string("board", "host-sample");
    jcon_add_int("build", 42);
    jcon_add_bytes_hex("mac", mac, sizeof mac);
    jcon_array_start("log");
        jcon_add_string(NULL, "boot");
        jcon_add_string(NULL, "ready");
    jcon_array_end();
    jcon_status_t st = jcon_end();
    if (st != JCON_OK) { fprintf(stderr, "jcon error (object root): %d\n", st); return 1; }

    /* Minified array root. */
    jcon_start_array(true, stdout_putc, NULL);
    for (int i = 0; i < 3; i++) {
        jcon_add_int(NULL, i);
    }
    st = jcon_end();
    fputc('\n', stdout);
    if (st != JCON_OK) { fprintf(stderr, "jcon error (array root): %d\n", st); return 1; }

    return 0;
}
