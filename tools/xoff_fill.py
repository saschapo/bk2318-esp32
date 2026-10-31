"""Fill the printer buffer with no-op ESC L (1B 4C) to provoke XOFF without paper.

usage: xoff_fill.py PORT [TOTAL_BYTES] [LISTEN_AFTER_S]
Sends 100-byte chunks via /hex, paced to 1200 baud, stops sending at the
first XOFF and then listens for XON. Everything is echoed with a timestamp.
"""
import sys, time
import serial

port = sys.argv[1]
total = int(sys.argv[2]) if len(sys.argv) > 2 else 8000
listen_after = float(sys.argv[3]) if len(sys.argv) > 3 else 60
CHUNK = 100   # bytes; a /hex line must fit the 256-byte USB CDC RX buffer
CHUNK_TIME = CHUNK / 120.0   # 1200 8N1 = 120 bytes/s

s = serial.Serial(port, 115200, timeout=0.1)
time.sleep(0.3)
s.reset_input_buffer()
t0 = time.time()
buf = ""

def pump(seconds):
    """Read and echo for `seconds`; return True if XOFF was seen."""
    global buf
    seen = False
    end = time.time() + seconds
    while time.time() < end:
        data = s.read(4096).decode(errors="replace")
        if not data:
            continue
        buf += data
        while "\n" in buf:
            line, buf = buf.split("\n", 1)
            print(f"{time.time() - t0:7.1f}s  {line}", flush=True)
            if "XOFF" in line:
                seen = True
    return seen

def cmd(c, wait=0.5):
    print(f"{time.time() - t0:7.1f}s >>> {c[:40]}{'...' if len(c) > 40 else ''}", flush=True)
    s.write((c + "\n").encode())
    return pump(wait)

cmd("/status")
sent = 0
xoff = False
while sent < total and not xoff:
    xoff = cmd("/hex " + "1B4C" * (CHUNK // 2), 0.1)
    sent += CHUNK
    xoff = pump(CHUNK_TIME) or xoff
    if sent % 1000 == 0:
        cmd("/status", 0.3)
print(f"queued {sent} bytes, XOFF seen: {xoff}; listening {listen_after:.0f} s", flush=True)
pump(listen_after)
cmd("/status", 0.5)
s.close()
