#!/usr/bin/env python3
"""Check hardware rasterization against an analytic triangle; no game needed."""
from pathlib import Path
import struct
import math
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
        texels = [(255, 0, 0, 128), (0, 255, 0, 128), (0, 0, 255, 128), (255, 255, 255, 128)]
        cases = [('nearest-repeat', 0, False, False, False, True, 128, False),
                 ('bilinear-clamp', 0, True, True, False, True, 128, False),
                 ('perspective-decal', 1, False, False, True, True, 128, False),
                 ('highlight-alpha', 2, False, False, False, True, 128, True),
                 ('highlight2-alpha', 3, False, False, False, True, 128, True),
                 ('decal-vertex-alpha', 1, False, False, False, False, 255, True)]
        for label, tfx, linear, clamp, perspective, tcc, alpha, ate in cases:
            flags = 2 | (8192 if tcc else 0) | (16384 if linear else 0) | (64 if ate else 0)
            modes = (tfx << 20) | (0x05000000 if clamp else 0) | (6 if ate else 0)
            qs = [1, 2, 4] if perspective else [1, 1, 1]
            # Deliberately extend coordinates beyond [0,1] to exercise addressing.
            coordinates = [(-0.25, -0.25), (1.75, -0.25), (-0.25, 1.75)]
            records = []
            for vertex, (u,v), q in zip(vertices, coordinates, qs):
                records.append(struct.pack('<9f', vertex[0], vertex[1], 128/255,128/255,128/255,alpha/255,u*q,v*q,q))
            payload = struct.pack('<4s6I', b'NFM2', 3, 2, 2, flags, modes, 200 if ate else 0)
            payload += b''.join(records) + bytes(c for texel in texels for c in texel)
            source.write_bytes(payload)
            run = subprocess.run([str(BINARY), str(source), str(output)], capture_output=True, text=True, timeout=30)
            if run.returncode:raise RuntimeError(label + ': ' + run.stdout + run.stderr)
            with output.open('rb') as image:
                image.readline();image.readline();image.readline();rendered = image.read()
            def sample(ix, iy):
                if clamp:ix,iy=max(0,min(1,ix)),max(0,min(1,iy))
                else:ix,iy=ix%2,iy%2
                return texels[iy*2+ix]
            for y in range(32,481,17):
                for x in range(32,481,17):
                    if abs(x+y+1-512)<4:continue
                    g,b=(x+0.5-25.6)/460.8,(y+0.5-25.6)/460.8
                    weights=[1-g-b,g,b]
                    actual=rendered[(y*512+x)*3:(y*512+x)*3+3]
                    if x+y+1>512:
                        assert actual==b'\0\0\0',(label,x,y,actual)
                        continue
                    q=sum(w*q for w,q in zip(weights,qs))
                    u=sum(w*c[0]*q for w,c,q in zip(weights,coordinates,qs))/q
                    v=sum(w*c[1]*q for w,c,q in zip(weights,coordinates,qs))/q
                    if linear:
                        sx,sy=u*2-0.5,v*2-0.5;ix,iy=math.floor(sx),math.floor(sy);fx,fy=sx-ix,sy-iy
                        taps=[sample(ix,iy),sample(ix+1,iy),sample(ix,iy+1),sample(ix+1,iy+1)]
                        factors=[(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy]
                        colour=[math.floor(sum(t[c]*f for t,f in zip(taps,factors))+0.5) for c in range(4)]
                    else:colour=list(sample(math.floor(u*2),math.floor(v*2)))
                    expected=colour[:3]
                    if tfx in (2,3):expected=[min(255,c+alpha) for c in expected]
                    out_alpha = alpha if not tcc else min(255,128+alpha) if tfx==2 else 128
                    if ate and out_alpha<=200:expected=[0,0,0]
                    assert all(abs(a-e)<=3 for a,e in zip(actual,expected)),(label,x,y,actual,expected)
            print(label, 'matches analytic texture/material checks')
        for bad in (b'',  b'BAD!'+good[4:], good[:-1], good[:4]+struct.pack('<I', 4)+good[8:],
                    good[:8]+struct.pack('<f',float('nan'))+good[12:]):
            source.write_bytes(bad)
            result = subprocess.run([str(BINARY), str(source), str(output)], capture_output=True, timeout=10)
            assert result.returncode != 0
        print(f'Hardware triangle matches analytic colour/coverage at {checked} pixels; malformed meshes rejected.')
        print(run.stderr.strip())


if __name__ == '__main__':
    main()
