#!/usr/bin/env python3
"""Run an ESP32 image in Espressif's QEMU and print its serial output.

Usage: qemu_run.py IMAGE.bin [SECONDS]
Builds a 4 MB flash image with IMAGE at 0x1000 (where the ROM loader looks), starts
qemu-system-xtensa with the serial port written to a file (stdio is not flushed through a
pipe), follows that file, and stops when a RESULT line appears or after SECONDS (default
30). Exit code 0 if a RESULT line was seen. QEMU_ESP points at the QEMU installation."""
import os, subprocess, sys, tempfile, time

image = sys.argv[1]
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
qemu = os.path.join(os.environ.get("QEMU_ESP", os.path.expanduser("~/dev/tools/qemu-esp")),
                    "bin", "qemu-system-xtensa")

with tempfile.TemporaryDirectory() as tmp:
    flash = os.path.join(tmp, "flash.bin")
    serial = os.path.join(tmp, "serial.txt")
    data = bytearray(b"\xff" * (4 * 1024 * 1024))
    img = open(image, "rb").read()
    data[0x1000:0x1000 + len(img)] = img
    open(flash, "wb").write(data)
    open(serial, "wb").close()
    proc = subprocess.Popen([qemu, "-M", "esp32", "-display", "none", "-monitor", "none",
                             "-serial", "file:" + serial,
                             "-drive", "file=%s,if=mtd,format=raw" % flash],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    seen_result = False
    pos = 0
    t0 = time.time()
    try:
        while time.time() - t0 < seconds and proc.poll() is None:
            with open(serial, "rb") as f:
                f.seek(pos)
                chunk = f.read()
            if chunk:
                pos += len(chunk)
                text = chunk.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()
                if "RESULT " in text:
                    seen_result = True
                    time.sleep(0.2)
                    break
            else:
                time.sleep(0.05)
    finally:
        proc.kill()
        proc.wait()
sys.exit(0 if seen_result else 1)
