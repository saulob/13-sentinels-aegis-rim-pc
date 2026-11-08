#!/usr/bin/env python3
"""Writes a DCPX3 block patch: per (archive, entry) the CRC32 of the original and of the patched file and the 16-byte runs
that differ. An entry may be listed for several archives and, per archive, once per language copy.

    from make_dcpx import write_patch
    write_patch(out_path, [(archive_name, entry_name, original_bytes, patched_bytes), ...])
"""
import struct
import zlib
from pathlib import Path

MAGIC = b'DCPX3' + bytes(3)


def runs_of(orig, new):
    assert len(orig) == len(new)
    return [(i, new[i:i + 16]) for i in range(0, len(orig), 16) if orig[i:i + 16] != new[i:i + 16]]


def write_patch(out, targets):
    buf = bytearray(MAGIC + struct.pack('<I', len(targets)))
    for archive, name, orig, new in targets:
        runs = runs_of(orig, new)
        assert runs, (archive, name)
        for text in (archive, name):
            data = text.encode()
            buf += struct.pack('<H', len(data)) + data
        buf += struct.pack('<IIII', len(orig), zlib.crc32(orig), zlib.crc32(new), len(runs))
        for offset, data in runs:
            buf += struct.pack('<I', offset) + data
    Path(out).write_bytes(buf)
    return len(buf)
