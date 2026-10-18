"""Build a lit liquid room with a sprite for repeatable mist/depth render checks.
Usage: python3 tests/sector_mist_scene.py base.wad output.wad
The fixture reuses base-game artwork and the existing room generator.
"""
import struct
import sys
from pathlib import Path
from flashlight_scene import create as create_room


def create(base_path, output_path):
    create_room(base_path, output_path)
    output = Path(output_path)
    data = output.read_bytes()
    count, directory_offset = struct.unpack_from('<ii', data, 4)
    entries = []
    for i in range(count):
        offset, size, name = struct.unpack_from('<ii8s', data, directory_offset + i * 16)
        lump = data[offset:offset + size]
        if name.rstrip(b'\0') == b'SECTORS':
            # Liquid floor, ordinary ceiling, and vanilla damaging special 7.
            lump = lump[:4] + b'NUKAGE1\0' + lump[12:20] + struct.pack('<hhh', 160, 7, 0)
        if name.rstrip(b'\0') == b'THINGS':
            # Non-attacking tall floor lamp exercises sprite depth intersections.
            lump += struct.pack('<hhhhh', 32, 80, 270, 2028, 7)
        entries.append((name, lump))
    payload = bytearray()
    directory = bytearray()
    for name, lump in entries:
        directory += struct.pack('<ii8s', 12 + len(payload), len(lump), name)
        payload += lump
    output.write_bytes(b'PWAD' + struct.pack('<ii', count, 12 + len(payload)) + payload + directory)


if __name__ == '__main__':
    create(*sys.argv[1:])
