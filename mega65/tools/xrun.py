#!/usr/bin/env python3
"""Run Xemu until a program says it is done.

usage: xrun.py TIMEOUT SERIALFILE MARKER -- xemu-args...

Adds -hyperserialfile SERIALFILE to the command line, then stops Xemu (SIGTERM,
so that -dumpmem / -screenshot are still written) as soon as MARKER appears in
that file, or after TIMEOUT seconds. Exit status: 0 marker seen, 1 timeout.
"""
import os
import signal
import subprocess
import sys
import time

timeout, serial, marker = float(sys.argv[1]), sys.argv[2], sys.argv[3]
cmd = sys.argv[sys.argv.index("--") + 1:] + ["-hyperserialfile", serial]

if os.path.exists(serial):
    os.remove(serial)
start = time.time()
seen = False
with open(os.devnull, "w") as null:
    proc = subprocess.Popen(cmd, stdout=null, stderr=null)
    while proc.poll() is None and time.time() - start < timeout:
        try:
            with open(serial, "r", errors="replace") as f:
                if marker in f.read():
                    seen = True
                    break
        except FileNotFoundError:
            pass
        time.sleep(0.2)
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(30)
        except subprocess.TimeoutExpired:
            proc.kill()
if seen:
    what = "marker seen"
elif time.time() - start < timeout:
    what = "EMULATOR EXITED (no marker)"
else:
    what = "TIMEOUT"
print(f"xrun: {what} after {time.time() - start:.1f}s")
sys.exit(0 if seen else 1)
