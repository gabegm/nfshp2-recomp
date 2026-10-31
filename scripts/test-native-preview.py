#!/usr/bin/env python3
"""Check hardware rasterization against an analytic triangle; no game needed."""
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / '.local/native-preview/native-mesh-preview'


def main():
    vertices = [(-1, -1, 1, 0, 0), (1, -1, 0, 1, 0), (-1, 1, 0, 0, 1)]
    good = struct.pack('<4sI', b'NFM1', 3) + b''.join(struct.pack('<5f', *v) for v in vertices)
    with tempfile.TemporaryDirectory(prefix='native-preview-test-') as folder:
        folder = Path(folder)
        source, output = folder/'triangle.nfm', folder/'triangle.ppm'
        source.write_bytes(good)
        run = subprocess.run([str(BINARY), str(source), str(output)], capture_output=True, text=True, timeout=30)
        if run.returncode:
            raise RuntimeError(run.stdout + run.stderr)
        assert 'Vulkan hardware adapter' in run.stderr
        with output.open('rb') as image:
            assert image.readline() == b'P6\n'
            assert image.readline() == b'512 512\n'
            assert image.readline() == b'255\n'
            pixels = image.read()
        assert len(pixels) == 512*512*3
        checked = 0
        for y in range(32, 481, 17):
            for x in range(32, 481, 17):
                if abs(x+y+1-512) < 4:
                    continue  # Ignore the API's edge inclusion rule.
                actual = pixels[(y*512+x)*3:(y*512+x)*3+3]
                if x+y+1 < 512:
                    green, blue = (x+0.5-25.6)/460.8, (y+0.5-25.6)/460.8
                    expected = [(1-green-blue)*255, green*255, blue*255]
                    assert all(abs(a-b) <= 1.1 for a, b in zip(actual, expected)), (x,y,actual,expected)
                else:
                    assert actual == b'\0\0\0', (x,y,actual)
                checked += 1
        for bad in (b'', b'BAD!'+good[4:], good[:-1], good[:4]+struct.pack('<I', 4)+good[8:],
                    good[:8]+struct.pack('<f',float('nan'))+good[12:]):
            source.write_bytes(bad)
            result = subprocess.run([str(BINARY), str(source), str(output)], capture_output=True, timeout=10)
            assert result.returncode != 0
        print(f'Hardware triangle matches analytic colour/coverage at {checked} pixels; malformed meshes rejected.')
        print(run.stderr.strip())


if __name__ == '__main__':
    main()
