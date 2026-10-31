"""Send commands to the bk2318_v0 console and print what comes back.

usage: console.py PORT [WAIT_S] CMD [CMD ...]
"""
import sys, time
import serial

port, rest = sys.argv[1], sys.argv[2:]
wait = 1.5
if rest and rest[0].replace('.', '', 1).isdigit():
    wait, rest = float(rest[0]), rest[1:]
s = serial.Serial(port, 115200, timeout=0.1)
time.sleep(0.3)
s.reset_input_buffer()
for cmd in rest:
    print(f">>> {cmd}")
    s.write((cmd + "\n").encode())
    end = time.time() + wait
    while time.time() < end:
        data = s.read(4096)
        if data:
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()
s.close()
