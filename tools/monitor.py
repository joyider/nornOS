#!/usr/bin/env python3
"""Serial monitor for nornOS boards: resets the board, prints what it says, forwards
keyboard input. Usage: monitor.py PORT BAUD [SECONDS]. With SECONDS it exits after that
time, which makes it usable from scripts."""
import sys, time, select, serial

port, baud = sys.argv[1], int(sys.argv[2])
seconds = float(sys.argv[3]) if len(sys.argv) > 3 else None

with serial.Serial(port, baud, timeout=0.05) as ser:
    # CP2102 boards: pulse RTS (EN) low with DTR (IO0) high to reset into the application.
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
    t0 = time.time()
    forward = sys.stdin.isatty()        # keyboard input only from a real terminal
    try:
        while seconds is None or time.time() - t0 < seconds:
            data = ser.read(256)
            if data:
                sys.stdout.write(data.decode("utf-8", errors="replace"))
                sys.stdout.flush()
            if forward and select.select([sys.stdin], [], [], 0)[0]:
                line = sys.stdin.readline()
                if not line:
                    forward = False
                else:
                    ser.write(line.encode())
    except KeyboardInterrupt:
        pass
