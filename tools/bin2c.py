#!/usr/bin/env python3
"""bin2c.py INPUT OUTPUT.c NAME: emit a const uint8_t NAME[] with the file's bytes and
a const uint32_t NAME_size, 4 byte aligned, for embedding one image in another."""
import sys
data = open(sys.argv[1], "rb").read()
with open(sys.argv[2], "w") as f:
    f.write("#include <stdint.h>\n")
    f.write("const uint8_t %s[] __attribute__((aligned(4))) = {\n" % sys.argv[3])
    for i in range(0, len(data), 16):
        f.write("    " + ", ".join("0x%02x" % b for b in data[i:i+16]) + ",\n")
    f.write("};\nconst uint32_t %s_size = %u;\n" % (sys.argv[3], len(data)))
