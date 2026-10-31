# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""B&K 2318 thermal printer through the ESP32-C6 adapter (firmware bk2318_v2).

Library:
    from bk2318 import Printer
    with Printer() as p:              # finds the adapter on /dev/cu.usbmodem*
        with p.job():                 # printed on exit, all or nothing
            p.wide(True); p.print("ВЫЕЗД 42"); p.wide(False)
            p.rule("dash")
            p.print("☐ 0125 Sony FX3\\nКофр 1001")
        p.drain()

Command line:
    uv run host/bk2318.py "Привет, 2318!"
    echo "две\\nстроки" | uv run host/bk2318.py --wide
    uv run host/bk2318.py --status

Protocol: firmware/API_v2.md. Text is UTF-8 (NFC); the adapter converts Cyrillic
to the printer's Russian set (uppercase only), prints ✓ ✗ Ъ ☐ … → ─ as its own
glyphs and turns what the printer cannot print into "?".
"""
from __future__ import annotations

import argparse
import contextlib
import glob
import sys
import time
import unicodedata

import serial

LINE_WIDTH = 24       # characters per line; 12 in wide mode
WIDE_WIDTH = 12


class PrinterError(RuntimeError):
    """The adapter answered ERR, or did not answer in time."""


class Printer:
    def __init__(self, port: str | None = None, timeout: float = 3.0, on_event=None):
        """port: e.g. /dev/cu.usbmodem101; None = find the adapter.
        on_event: called with each "EVT ..." line (without "EVT "), e.g. "PRINTER_RESET"."""
        self.timeout = timeout
        self.on_event = on_event
        self.events: list[str] = []
        self.port = port or find_port()
        self._ser = serial.Serial(self.port, 115200, timeout=0.1)
        self._buf = b""
        reply = self.command("HELLO")
        if not reply.startswith("bk2318"):
            raise PrinterError(f"{self.port}: not a bk2318 adapter ({reply!r})")
        self.version = reply

    def close(self) -> None:
        self._ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # ---- protocol ----

    def _readline(self, deadline: float) -> str | None:
        while b"\n" not in self._buf:
            if time.monotonic() > deadline:
                return None
            self._buf += self._ser.read(256)
        line, self._buf = self._buf.split(b"\n", 1)
        return line.decode("utf-8", "replace").rstrip("\r")

    def command(self, line: str, timeout: float | None = None) -> str:
        """Sends one command, returns the text after "OK" (may be empty)."""
        if "\n" in line:
            raise ValueError("one command per line")
        self._ser.write(line.encode("utf-8") + b"\n")
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while True:
            resp = self._readline(deadline)
            if resp is None:
                raise PrinterError(f"no reply to {line.split(' ')[0]}")
            if resp.startswith("EVT "):
                self._event(resp[4:])
            elif resp == "OK" or resp.startswith("OK "):
                return resp[3:]
            elif resp.startswith("ERR "):
                raise PrinterError(resp[4:])
            # anything else (boot noise) is ignored

    def _event(self, ev: str) -> None:
        self.events.append(ev)
        if self.on_event:
            self.on_event(ev)

    def poll_events(self) -> list[str]:
        """Reads events that came without a command; returns and clears the list."""
        deadline = time.monotonic() + 0.05
        while (line := self._readline(deadline)) is not None:
            if line.startswith("EVT "):
                self._event(line[4:])
        ev, self.events = self.events, []
        return ev

    # ---- jobs ----

    @contextlib.contextmanager
    def job(self):
        """Collects everything inside the block and prints it on exit, or nothing.
        Raises PrinterError("PRINTER_OFF ...") if the printer does not answer.
        The adapter drops a job after 5 s without commands: build it first."""
        self.command("JOB BEGIN")
        try:
            yield self
        except BaseException:
            with contextlib.suppress(PrinterError):
                self.command("JOB CANCEL")
            raise
        reply = self.command("JOB END", timeout=10.0)
        self.last_job = dict(kv.split("=", 1) for kv in reply.split())

    def echo(self, text: str) -> str:
        """The text exactly as it will print; one character per cell."""
        return self.command("ECHO " + _clean(text))

    # ---- printing, inside job() ----

    def print(self, text: str = "") -> None:
        """Prints text; each "\\n" starts a new line, longer lines wrap at 24."""
        for line in text.split("\n"):
            self.command("PRINT " + _clean(line))

    def rule(self, style: str = "solid") -> None:
        """Full-width line: solid, dots, dash or dashdot."""
        self.command("RULE " + style.upper())

    def feed(self, lines: int = 1) -> None:
        self.command(f"FEED {lines}")

    def wide(self, on: bool = True) -> None:
        self.command("WIDE " + ("ON" if on else "OFF"))

    def inverse(self, on: bool = True) -> None:
        self.command("INVERSE " + ("ON" if on else "OFF"))

    def align(self, side: str = "left") -> None:
        self.command("ALIGN " + side.upper())

    def status(self) -> dict[str, str]:
        return dict(kv.split("=", 1) for kv in self.command("STATUS").split())

    def drain(self, timeout: float = 300.0) -> None:
        """Returns when every byte has gone to the printer (it may still be printing)."""
        self.command("DRAIN", timeout=timeout)


def _clean(text: str) -> str:
    """NFC, so that й is one character, and no line breaks inside a command."""
    return unicodedata.normalize("NFC", text).replace("\r", " ").replace("\n", " ")


def find_port() -> str:
    """The first /dev/cu.usbmodem* that answers HELLO like a bk2318 adapter."""
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    for port in ports:
        try:
            with serial.Serial(port, 115200, timeout=0.1) as s:
                s.reset_input_buffer()
                s.write(b"HELLO\n")
                deadline = time.monotonic() + 1.0
                buf = b""
                while time.monotonic() < deadline:
                    buf += s.read(256)
                    if b"OK bk2318" in buf:
                        return port
        except (OSError, serial.SerialException):
            continue
    raise PrinterError("bk2318 adapter not found on " + (", ".join(ports) or "any /dev/cu.usbmodem*"))


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Print on the B&K 2318 through the ESP32-C6 adapter.")
    ap.add_argument("text", nargs="*", help="text to print (default: stdin); \\n starts a new line")
    ap.add_argument("--port", help="serial port, default: find /dev/cu.usbmodem*")
    ap.add_argument("--wide", action="store_true", help="double width, 12 characters per line")
    ap.add_argument("--inverse", action="store_true", help="white on black")
    ap.add_argument("--right", action="store_true", help="align right")
    ap.add_argument("--feed", type=int, default=0, metavar="N", help="feed N empty lines after the text")
    ap.add_argument("--status", action="store_true", help="show adapter status and exit")
    ap.add_argument("--echo", action="store_true", help="show the text as it would print, do not print")
    args = ap.parse_args(argv)

    try:
        with Printer(args.port) as p:
            if args.status:
                for k, v in p.status().items():
                    print(f"{k}={v}")
                return 0
            text = " ".join(args.text) if args.text else sys.stdin.read().rstrip("\n")
            text = text.replace("\\n", "\n")
            if args.echo:
                for line in text.split("\n"):
                    print(p.echo(line))
                return 0
            with p.job():
                if args.wide:
                    p.wide(True)
                if args.inverse:
                    p.inverse(True)
                if args.right:
                    p.align("right")
                p.print(text)
                if args.feed:
                    p.feed(args.feed)
            p.drain()
    except PrinterError as e:
        print(f"bk2318: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
