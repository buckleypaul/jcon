# jcon

Streaming JSON emitter for bare-metal C. Single static writer, zero heap,
character-at-a-time output via a caller-supplied `putc`.

## Is this for you?

**Probably yes** if you want to emit JSON from an MCU (telemetry, config
dumps, log payloads), your output sink (UART, ring buffer, file) can accept
one byte at a time, and you're willing to trade features for size.

**Probably no** if you need to parse JSON, emit from multiple writers
concurrently, or want the library to sanitize untrusted string data.

## Design choices

- **Zero heap.** State is one file-scope static struct. Size scales with
  `JCON_MAX_DEPTH`.
- **Sink-agnostic.** You supply `int putc(char)`. The library never owns a
  buffer.
- **No string escaping.** Strings are emitted verbatim between `"..."`;
  caller is responsible for ensuring the contents are valid inside a JSON
  string body. If you need escaping, do it before calling. This keeps the
  core small and avoids pulling in a UTF-8/escape ruleset you might not want.
- **Sticky errors.** The first `putc` failure latches; subsequent emits
  no-op. Check once at `jcon_end()` — no per-call branching in caller code.
- **Root is always an object.** No root arrays.
- **Not thread-safe, not reentrant.** One global writer; don't call `jcon_*`
  from inside `putc`.
- **Typed emitters with optional `_Generic` dispatch.** Call
  `jcon_add_int` / `jcon_add_string` / etc. explicitly, or use
  `jcon_add(name, value)` to select the right one at compile time.
- **Misuse asserts in debug, latches in release.** Mismatched start/end,
  depth overflow, and NULL keys trip `assert()` under debug; with `NDEBUG`
  they silently latch `JCON_ERR_USAGE`.

## Requirements

- C11 or later (`_Generic`).
- `snprintf`, `assert`, and the standard fixed-width int headers.
- No other dependencies.

## Compile-time knobs

| Macro | Default | Effect |
|---|---|---|
| `JCON_MAX_DEPTH` | `16` | Max nesting depth. |
| `JCON_INDENT` | `"  "` | Indent unit for pretty-print mode. |
| `JCON_ENABLE_FLOAT` | *off* | Enables `jcon_add_float` / `jcon_add_double`. |
| `NDEBUG` | standard | Turns misuse asserts into silent `JCON_ERR_USAGE`. |

## Shape of the API

```c
jcon_start(false, uart_putc);
jcon_add("id", 42);
jcon_array_start("log");
    jcon_add(NULL, "boot");
jcon_array_end();
jcon_end();
```

→

```json
{
  "id": 42,
  "log": [
    "boot"
  ]
}
```

Full API in [`include/jcon.h`](include/jcon.h); usage patterns and edge cases
in [`tests/test_jcon.c`](tests/test_jcon.c).

## Build

```sh
make          # run debug, release (NDEBUG), and float test configurations
make lib      # build/jcon.o for consumer linking
```

To integrate: compile `src/jcon.c` with your project and put `include/` on
the header search path.

## License

MIT. See `LICENSE`.
