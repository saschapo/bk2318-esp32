# bk2318.py: print from a computer through the adapter

One file: Python ≥ 3.9 and `pyserial`, declared as inline script metadata, so `uv run` installs it. Works with firmware v2 (`../firmware/API_v2.md`): everything prints inside a job. Port discovery looks at `/dev/cu.usbmodem*` (macOS); on Linux or Windows pass `--port`.

## Command line

```bash
uv run host/bk2318.py "Привет, 2318!"
```

```bash
printf 'ВЫЕЗД 42\n1001 Кофр Sony FX3\n' | uv run host/bk2318.py
```

```bash
uv run host/bk2318.py --wide "ВЫЕЗД 42"
```

```bash
uv run host/bk2318.py --status
```

- Options: `--wide`, `--inverse`, `--right`, `--feed N`, `--echo` (show what would print, print nothing), `--status`, `--port PORT`.
- Without `--port` it uses the first `/dev/cu.usbmodem*` that answers `HELLO` like a bk2318 adapter.
- It exits when everything has been sent; the printer may still be printing.

## Library

```python
from bk2318 import Printer, PrinterError

with Printer() as p:                  # port=..., timeout=3.0, on_event=callback
    with p.job():                     # printed on exit, all or nothing
        p.wide(True); p.print("ВЫЕЗД 42"); p.wide(False)
        p.rule("dash")                # solid, dots, dash, dashdot
        p.print("☐ 0125 Sony FX3\nКофр 1001")
        p.feed(3)
    print(p.last_job)                 # {'lines': ..., 'bytes': ..., 'eta': ..., 'check': ...}
    p.drain()                         # wait until everything is sent
```

- `print(text)`: each `\n` starts a new line; longer than 24 characters wraps in the printer, by character.
- `rule(style)`, `feed(n)`, `wide(on)`, `inverse(on)`, `align("left"|"right")`: inside `job()`.
- `echo(text)` returns the text exactly as it will print; `status()` returns a dict; `drain()` waits for the adapter's queue.
- Adapter errors and timeouts raise `PrinterError`, including `PRINTER_OFF` when the printer does not answer at `JOB END`. Build the job first: the adapter drops it after 5 s without commands.
- Events (`PRINTER_RESET`, `XOFF`, …) go to `on_event` or `poll_events()`.

## Limits that matter for layout

| What | Value |
|---|---|
| Width | **24 characters**; 12 in wide mode |
| Cyrillic | **uppercase only**; Ё → Е, Ъ is an own glyph. Latin in both cases |
| Own glyphs | ✓ ✗ Ъ ☐ … → ─ |
| Other characters | «» — № are replaced by ASCII; emoji and other alphabets → `?` |
| Speed | 0.8 lines/s: a 30-line receipt takes about 40 s |
| Size | up to 6 KB per job |
| End of receipt | the last line stays under the head, 2 lines below the tear edge: `feed(3)` or the Paper Feed button |
| Inverse | hard to read for small text |
| Paper | thermal till roll 57–58 mm, up to 40 mm across |
