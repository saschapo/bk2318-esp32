# Evidence: photos, scans and logs

Everything here is from our printer, SN 1941478 (PROM VP1915), and our SP3232 module. Logs are the raw console output of the scripts in `../tools/`. The docs link to these files where they cite a measurement.

## printer_SN1941478/

| File | What it shows |
|---|---|
| `setup_printout.jpg`, `setup_and_selftest_printout.jpg` | set-up printout (7-bit, 1200 baud, XON/XOFF) and print test with the PROM version |
| `BK-SI_socket_front.jpg`, `BK-SI_socket_pins_annotated.jpg` | the B&K-SI socket, pins numbered (external view) |
| `bench_setup.jpg` | the adapter: nanoESP32-C6, SP3232 module, resistor legs in the socket |
| `first_print_HELLO_2318.jpg`, `hello_2026-10-19.log` | first print |
| `probe1_lines_eol_ascii.jpg`, `probe1_2026-10-19.log` | CR / LF / CRLF, wrap after 24, full ASCII |
| `probe2_24chars_tail.jpg`, `probe2_2026-10-19.log` | lines of exactly 24 characters, paper tail |
| `esc_probe_wide_inverse_align.jpg`, `esc_probe_2026-10-19.log` | `ESC K/L`, `ESC H/I`, `ESC & > / <` |
| `garbled_hex_run_7bit_strip.jpg` | bytes ≥ 0x80 lose bit 7 in 7-bit mode (`B4` → `4`, `C1` → `A`) |
| `inverse_blank_bars_density5.jpg`, `inverse_blank_lines_2026-10-19.log`, `../bringup_logs/inverse_d5_2026-10-19.log` | head check: inverse bars on old paper |
| `new_paper_57mm_density5.jpg`, `new_paper_test_2026-10-19.log` | the same on a new 57 mm roll |
| `nsr_xoff_xon_2026-10-19.log`, `power_cycle_listen_2026-10-19.log` | XOFF/XON as `93`/`91` after NSR and at power-on |
| `esc4c_x10_2026-10-19.log`, `xoff_fill_2026-10-19.log` | 8000 bytes of no-op commands: no XOFF |
| `koi7_cyrillic_probe.jpg`, `koi7_probe_2026-10-19.log` | Russian set `4C` in G0 and in G1 with SO/SI |
| `v1_receipt_cyrillic.jpg`, `v1_receipt_2026-10-19.log` | a receipt through firmware v1 |
| `drc_mock_receipt.jpg`, `drc_receipt_2026-10-19.log` | first own glyphs (DRC) and LLINE |
| `drc_persistence_power_nsr.jpg` | DRC glyphs survive 15 s without batteries and an NSR |
| `pdi_probe_2026-10-20.jpg`, `pdi_probe_2026-10-20.log` | PDI graphics probe: DOT, LINE, bars, overlay, bold, Code 128, mock receipt |
| `tape_probe_2026-10-26_scan600.jpeg`, `tape_probe_2026-10-26.log` | 600 dpi scan: ROM font, inverse, bold, alignment by cursor, tiles, bars, barcodes |
| `lowercase_probe_2026-10-27_scan.png`, `lowercase_probe_2026-10-27.log` | lowercase Cyrillic as DRC glyphs |
| `zi0054_dip_N1_N2_2026-10-20.jpg` | ZI 0054 component side: EPROM VP1915, DIP switches N1 and N2 (factory) |
| `backup_nicd_leak_board_2026-10-20.jpg`, `backup_nicd_leak_closeup_2026-10-20.jpg` | both backup NiCd cells under the module have leaked |

## sp3232_module_V0017/

`top.jpg`, `bottom.jpg`: the SP3232 module; `loop_2026-10-19.log`: TXD–RXD loopback test.
