#!/usr/bin/env python3
"""Convert logoTex.png to a C header with embedded RGBA32 pixel data."""
import sys
from PIL import Image

if len(sys.argv) != 3:
    print("Usage: build_logo.py <input.png> <output.h>")
    sys.exit(1)

in_path = sys.argv[1]
out_path = sys.argv[2]

img = Image.open(in_path).convert("RGBA")
w, h = img.size
pixels = img.tobytes()

with open(out_path, "w") as f:
    f.write("#pragma once\n")
    f.write("#define LOGO_WIDTH %d\n" % w)
    f.write("#define LOGO_HEIGHT %d\n" % h)
    f.write("static const uint32_t logo_width = %d;\n" % w)
    f.write("static const uint32_t logo_height = %d;\n" % h)
    f.write("static const uint8_t logo_rgba[] = {\n")
    for i in range(0, len(pixels), 16):
        chunk = pixels[i:i + 16]
        line = ", ".join("0x%02X" % b for b in chunk)
        f.write("    " + line + ",\n")
    f.write("};\n")

print("logo_bin.h: %dx%d, %d bytes" % (w, h, len(pixels)))
