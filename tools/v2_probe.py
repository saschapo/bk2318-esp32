# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""Checks for firmware bk2318_v2.

usage: v2_probe.py self    no paper: HELLO, ECHO, idle timeout, TOOBIG, empty job
                           (with the printer off or unplugged it must say PRINTER_OFF)
       v2_probe.py strip   one numbered strip: glyphs incl. the new …, Ъ, 4 RULE styles,
                           WIDE, a sample receipt; then a second job right
                           away (check=busy) and EOT 04 (does XON come when printing ends?)
"""
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "host"))
from bk2318 import Printer, PrinterError  # noqa: E402

T0 = time.monotonic()


def log(s):
    print(f"{time.monotonic() - T0:7.2f}  {s}", flush=True)


def expect_err(p, cmd, code):
    try:
        p.command(cmd)
        log(f"FAIL {cmd}: OK, expected {code}")
    except PrinterError as e:
        log(("ok   " if str(e).startswith(code) else "FAIL ") + f"{cmd} -> ERR {e}")


def self_test(p):
    log("HELLO " + p.command("HELLO"))
    for text, want in [("Съёмка «FX3»…", 'СЪЕМКА "FX3"…'), ("☐ 0125 штатив", "☐ 0125 ШТАТИВ"),
                       ("йод", "ИОД"), ("ok👍️", "ok?"), ("№5 — x4", "N5 - x4")]:
        got = p.echo(text) if "̆" not in text else p.command("ECHO " + text)
        log(("ok   " if got == want else "FAIL ") + f"ECHO {text!r} -> {got!r}")
    expect_err(p, "PRINT outside", "NOJOB")
    p.command("JOB BEGIN")
    expect_err(p, "JOB BEGIN", "INJOB")
    log("waiting 6 s for the idle timeout")
    time.sleep(6)
    p.poll_events()  # the callback logs JOB_ABORTED
    expect_err(p, "PRINT after timeout", "NOJOB")
    p.command("JOB BEGIN")
    try:
        for _ in range(300):
            p.command("PRINT " + "X" * 24)
        log("FAIL no TOOBIG")
    except PrinterError as e:
        log(("ok   " if str(e).startswith("TOOBIG") else "FAIL ") + f"big job -> ERR {e}")
    p.command("JOB BEGIN")
    try:
        log("empty job: " + p.command("JOB END", timeout=10))
    except PrinterError as e:
        log(f"empty job -> ERR {e}")


def strip(p):
    with p.job():
        p.print("01 ✓✗Ъ☐…→─ ☐ ☐")
        p.print("02 Съёмка ъ Ё")
        for n, style in enumerate(["solid", "dots", "dash", "dashdot"], 3):
            p.print(f"0{n} RULE {style.upper()}")
            p.rule(style)
        p.print("07 WIDE:")
        p.wide(True); p.print("Klip Noise"); p.wide(False)
        p.print("RENTAL")
        p.print("18.10.26-20.10.26 3 DAYS")
        p.print("── BAG 2001 ────────────")
        p.print("☐ 0001 SONY FX30")
        p.print("☐      USB-C - USB-A x4")
        p.print("☐ 0311 ГОЛОВА MANFRO… x2")
        p.print("── LOOSE ───────────────")
        p.print("☐ 0002 АКВАБОКС ДЛЯ SONY")
        p.print("       TRV940")
        p.rule("solid")
        p.print("PRINTED 20.10.26 19:06")
    log("job 1: " + str(p.last_job))
    with p.job():
        p.print("08 SECOND JOB, BUSY PATH")
        p.feed(3)
    log("job 2: " + str(p.last_job) + "  (want check=busy)")
    p.command("RAW 04")
    log("EOT queued; events until XON or 120 s:")
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        if "XON" in p.poll_events():  # the callback logs each event
            break
        time.sleep(0.05)


if __name__ == "__main__":
    with Printer(on_event=lambda ev: log("EVT " + ev)) as p:
        {"self": self_test, "strip": strip}[sys.argv[1]](p)
