#!/usr/bin/env python3
"""Decode a private GPU draw snapshot into a textured native-preview triangle."""
import argparse
from array import array
import json
import math
from pathlib import Path
import struct
import sys


def words(path, expected=None):
    data = path.read_bytes()
    if len(data) % 4 or expected is not None and len(data) != expected*4:
        raise ValueError(f'Unexpected word buffer size: {path.name}')
    result = array('I')
    result.frombytes(data)
    if sys.byteorder != 'little':result.byteswap()
    return result


def address(fmt, x, y, tables):
    base, row, table, bits, mask = fmt
    px, py, byte_mask, nibble = bits & 15, (bits >> 4) & 15, (bits >> 8) & 3, (bits >> 10) & 1
    if not 0 < px <= 7 or not 0 < py <= 7 or bits >> 11 > 31:
        raise ValueError('Unsupported format descriptor.')
    index = table + ((y & ((1 << py)-1)) << px) + (x & ((1 << px)-1))
    if index >= len(tables):raise ValueError('Swizzle table index outside capture.')
    offset = tables[index]
    byte = base + ((x >> px) + (y >> py)*row)*8192 + (offset >> nibble)
    return (byte & 0x3FFFFF) >> 2, (bits >> 11)+(byte & byte_mask)*8+(offset & nibble)*4, mask


def expand16(value, setup):
    alpha = (setup[20] >> 8) & 255 if value & 0x8000 else setup[20] & 255
    if not value & 0x8000 and setup[0] & 32768 and not value & 0x7FFF:alpha = 0
    return ((value & 31) << 3) | (((value >> 5) & 31) << 11) | (((value >> 10) & 31) << 19) | (alpha << 24)


def convert(raw, setup, palette):
    psm = setup[14]
    if psm in (0, 0x30):return raw
    if psm in (1, 0x31):
        alpha = setup[20] & 255
        if setup[0] & 32768 and not raw & 0xFFFFFF:alpha = 0
        return (raw & 0xFFFFFF) | (alpha << 24)
    if psm in (2, 10, 0x32, 0x3A):return expand16(raw, setup)
    if psm in (0x13, 0x1B):index = raw & 255
    elif psm in (0x14, 0x24, 0x2C):index = (((setup[17] & 15)*16) + (raw & 15)) & 511
    else:raise ValueError(f'Unsupported texture PSM {psm:02X}.')
    if index >= len(palette):raise ValueError('Palette index outside capture.')
    colour = palette[index]
    if setup[16] in (0, 1):return colour
    if setup[16] in (2, 10):return expand16(colour, setup)
    raise ValueError('Unsupported palette format.')


def texture_rgba(setup, vram, palette, tables):
    tw, th = setup[15] & 255, setup[15] >> 8
    if tw > 10 or th > 10:raise ValueError('Texture exceeds capture limits.')
    width, height = 1 << tw, 1 << th
    result = bytearray(width*height*4)
    for y in range(height):
        for x in range(width):
            word, shift, mask = address(setup[25:30], x, y, tables)
            if word >= len(vram):raise ValueError('VRAM address outside capture.')
            colour = convert((vram[word] >> shift) & mask, setup, palette)
            struct.pack_into('<I', result, (y*width+x)*4, colour)
    return width, height, result


def float_bits(bits):
    value = struct.unpack('<f', struct.pack('<I', bits))[0]
    if not math.isfinite(value):raise ValueError('Non-finite texture coordinate.')
    return value


def prepare(folder):
    capture = json.loads((folder/'draw.json').read_text())
    if capture['version'] != 1 or capture['scale'] != 1:
        raise ValueError('Expected a version 1 draw at internal scale 1.')
    setup = words(folder/'setup.bin', 40)
    vram, palette = words(folder/'vram.bin', 1 << 20), words(folder/'clut.bin', 512)
    tables = words(folder/'tables.bin')
    if capture['attr'] & 7 not in (3, 4, 5) or len(capture['vertices']) != 3:
        raise ValueError('The material preview currently accepts one assembled triangle.')
    if not setup[0] & 2:raise ValueError('Choose a textured primitive for this preview.')
    if (setup[7] >> 24) & 3 > 1 or (setup[7] >> 26) & 3 > 1:
        raise ValueError('Region texture wrapping is not implemented by the native preview.')
    width, height, texture = texture_rgba(setup, vram, palette, tables)
    reference = folder/'texture-reference.bin'
    if reference.exists() and reference.read_bytes() != texture:
        raise ValueError('CPU texture decoder differs from captured GPU reference.')
    (folder/'texture.rgba').write_bytes(texture)
    flat = capture['vertices'][-1]['rgba']
    vertices = bytearray()
    for vertex in capture['vertices']:
        x = (vertex['x'] - capture['xyoffset'][0])/16
        y = (vertex['y'] - capture['xyoffset'][1])/16
        colour = vertex['rgba'] if setup[0] & 1 else flat
        if setup[0] & 16:
            stq = (vertex['uv'][0]/(16*width), vertex['uv'][1]/(16*height), 1.0)
        else:stq = tuple(float_bits(w) for w in vertex['stq_bits'])
        vertices += struct.pack('<9f', x, y, *(c/255 for c in colour), *stq)
    output = folder/'triangle.nfm'
    output.write_bytes(struct.pack('<4s6I', b'NFM2', 3, width, height, setup[0], setup[7], setup[8]) + vertices + texture)
    summary = dict(version=1, width=width, height=height, psm=setup[14], palette_psm=setup[16],
                   flags=setup[0], modes=setup[7], alpha_reference=setup[8], mesh=output.name,
                   limitations=['Standalone triangle; no in-game framebuffer or depth handoff.',
                                'Native preview does not implement destination alpha, fog, blending or depth tests.',
                                'Base texture level only; no mip selection.'])
    (folder/'material.json').write_text(json.dumps(summary, indent=2)+'\n')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    print(prepare(args.capture))


if __name__ == '__main__':main()
