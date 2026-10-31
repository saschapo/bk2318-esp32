# Probe scripts

Scripts we used to find out how the printer behaves. Each one printed a numbered strip; the results are in `../docs/` and the logs and photos in `../evidence/`. Scripts with inline metadata run with `uv run tools/<name>.py`; the rest need `pyserial`.

| Script | Firmware | What it does |
|---|---|---|
| `console.py` | v0 | send console commands, print the replies |
| `koi7_probe.py`, `koi7_probe.cmds` | v0 | Russian set `4C` in G0 and in G1 with SO/SI |
| `xoff_fill.py` | v0 | try to fill the buffer with no-op commands to get XOFF without paper |
| `drc_receipt.py` | v1 | load 14 DRC glyphs, print a mock receipt, check they survive a reset |
| `pdi_probe.py` | v1 | PDI graphics: DOT, LINE, bars, overlay, bold, wide + normal, Code 128 |
| `v2_probe.py` | v2 | `self`: protocol checks without paper; `strip`: glyphs, rules, jobs, EOT |
| `tape_probe.py` | v2 | receipt-editor techniques: ROM font, inverse, bold, alignment by cursor, tiles, barcodes |
| `lowercase_probe.py` | v2 | lowercase Cyrillic as DRC glyphs 0x37–0x50 |
| `webserial_test.html` | v2 | talk to the adapter from Chrome through Web Serial |
| `annotate_socket.py` | — | number the pins on the socket photo |

Most scripts have a `dry` or `list` mode that shows the bytes without a port.
