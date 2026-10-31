#!/usr/bin/env python3
"""Compare a private one-draw native framebuffer proof with its GS reference."""
from collections import Counter
import argparse
import json
import math
from pathlib import Path
import struct


def compare(capture, image):
    with (capture/'frame-triangle.nfm').open('rb') as source:header=source.read(40)
    if len(header)!=40 or header[:4]!=b'NFM3':raise ValueError('Expected an NFM3 proof.')
    width,height,zmax=struct.unpack_from('<3I',header,28)
    if not width or not height or width*height>1<<20 or not zmax:raise ValueError('Invalid proof dimensions.')
    size=width*height*4
    def read(path):
        data=path.read_bytes()
        if len(data)!=size:raise ValueError(f'Unexpected buffer size: {path}')
        return data
    before,reference,native=(read(capture/'frame-before.rgba'),read(capture/'frame-reference.rgba'),read(Path(str(image)+'.rgba')))
    ref_changed=set();native_changed=set();errors=Counter()
    for i in range(0,size,4):
        a,b,c=before[i:i+4],reference[i:i+4],native[i:i+4]
        if a!=b:ref_changed.add(i//4)
        if a!=c:native_changed.add(i//4)
        if b!=c:errors[max(abs(x-y) for x,y in zip(b,c))]+=1
    reference_depth=struct.unpack('<'+str(width*height)+'I',read(capture/'depth-reference.u32'))
    native_depth=struct.unpack('<'+str(width*height)+'f',read(Path(str(image)+'.depth')))
    depth_errors=Counter()
    for a,b in zip(reference_depth,native_depth):
        if not math.isfinite(b) or not 0<=b<=1:raise ValueError('Invalid native depth.')
        error=abs(a-round(b*zmax))
        if error:depth_errors[error]+=1
    return dict(width=width,height=height,reference_changed_pixels=len(ref_changed),
                native_changed_pixels=len(native_changed),changed_mask_difference=len(ref_changed^native_changed),
                rgba_different_pixels=sum(errors.values()),rgba_max_channel_error=max(errors,default=0),
                rgba_error_histogram=dict(sorted(errors.items())),depth_different_pixels=sum(depth_errors.values()),
                depth_max_integer_error=max(depth_errors,default=0),depth_error_histogram=dict(sorted(depth_errors.items())),
                limitations=['One captured draw, not an FPS or full-game compatibility result.',
                             'Changed-pixel masks are not a direct measurement of raster coverage.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path);parser.add_argument('native_image',type=Path)
    args=parser.parse_args();result=compare(args.capture,args.native_image)
    (args.capture/'native-comparison.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
