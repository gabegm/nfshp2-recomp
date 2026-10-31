"""Synthetic PS2 texture/format fixtures; no game assets or GPU required."""
from array import array
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('native_material', ROOT/'scripts/prepare-native-material.py')
helper=importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


def setup(psm=0):
    result=[0]*40
    result[0]=2|1|8192
    result[14]=psm
    result[20]=64|(128<<8)
    result[25:30]=[0,1,0,6|(5<<4),0xFFFFFFFF]
    return result


def put_words(path, values):
    path.write_bytes(struct.pack('<'+str(len(values))+'I', *values))


def fixture(root):
    state=setup()
    vertices=[dict(x=x,y=y,z=100,rgba=[10+i,20+i,30+i,128],stq_bits=[0,0,0x3F800000],uv=[8,4],fog=0)
              for i,(x,y) in enumerate(((0,0),(16,0),(0,16)))]
    draw=dict(version=1,scale=1,attr=3,xyoffset=[0,0],vertices=vertices)
    (root/'draw.json').write_text(json.dumps(draw))
    put_words(root/'setup.bin',state)
    (root/'vram.bin').write_bytes(struct.pack('<I',0x80402010)+b'\0'*((1<<22)-4))
    put_words(root/'clut.bin',[0]*512)
    put_words(root/'tables.bin',[0])
    return state,draw


class NativeMaterialTests(unittest.TestCase):
    def test_four_bit_nibble_and_palette_bank(self):
        state=setup(0x14);state[17]=1
        state[25:30]=[0,1,0,7|(7<<4)|(3<<8)|(1<<10),15]
        palette=[0]*512;palette[16+11]=0x80402010
        width,height,rgba=helper.texture_rgba(state,[0xBA],palette,[1])
        self.assertEqual((width,height,rgba),(1,1,b'\x10\x20\x40\x80'))
        palette[16+10]=0x11223344
        self.assertEqual(helper.texture_rgba(state,[0xBA],palette,[0])[2],b'\x44\x33\x22\x11')

    def test_high_byte_index_and_vram_wrap(self):
        state=setup(0x1B);state[25:30]=[0,1,0,6|(5<<4)|(24<<11),255]
        palette=[0]*512;palette[0xAB]=0x87654321
        self.assertEqual(helper.texture_rgba(state,[0xAB000000],palette,[0])[2],b'\x21\x43\x65\x87')
        self.assertEqual(helper.address([0x3FFFFC,1,0,6|(5<<4),0xFFFFFFFF],64,0,[0]),(2047,0,0xFFFFFFFF))

    def test_direct_colour_alpha_expansion(self):
        state=setup(1)
        self.assertEqual(helper.convert(0x123456,state,[]),0x40123456)
        state[0]|=32768
        self.assertEqual(helper.convert(0,state,[]),0)
        state[14]=2
        self.assertEqual(helper.convert(0x801F,state,[]),0x800000F8)
        self.assertEqual(helper.convert(0,state,[]),0)
        self.assertEqual(helper.convert(0x8000,state,[]),0x80000000)

    def test_rejects_unknown_formats_and_out_of_bounds_tables(self):
        with self.assertRaises(ValueError):helper.address([0,1,5,6|(5<<4),255],0,0,[0])
        state=setup(0x55)
        with self.assertRaises(ValueError):helper.convert(0,state,[])
        state[15]=11
        with self.assertRaises(ValueError):helper.texture_rgba(state,[0],[],[0])

    def test_textured_mesh_header_and_reference_match(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);fixture(root)
            (root/'texture-reference.bin').write_bytes(b'\x10\x20\x40\x80')
            output=helper.prepare(root).read_bytes()
            self.assertEqual(struct.unpack_from('<4s3I',output),(b'NFM2',3,1,1))
            self.assertEqual(len(output),28+3*36+4)
            self.assertEqual(output[-4:],b'\x10\x20\x40\x80')
            (root/'texture-reference.bin').write_bytes(b'wrong')
            with self.assertRaises(ValueError):helper.prepare(root)

    def test_flat_shading_and_fixed_uv_conversion(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);state,draw=fixture(root)
            state[0]=(state[0]&~1)|16
            put_words(root/'setup.bin',state)
            data=helper.prepare(root).read_bytes()
            vertex=struct.unpack_from('<9f',data,28)
            self.assertAlmostEqual(vertex[2],draw['vertices'][-1]['rgba'][0]/255,places=6)
            self.assertEqual(vertex[-3:],(0.5,0.25,1.0))

    def test_invalid_capture_scale_buffers_and_coordinates(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);state,draw=fixture(root)
            draw['scale']=2;(root/'draw.json').write_text(json.dumps(draw))
            with self.assertRaises(ValueError):helper.prepare(root)
            draw['scale']=1;draw['vertices'][0]['stq_bits'][0]=0x7FC00000
            (root/'draw.json').write_text(json.dumps(draw))
            with self.assertRaises(ValueError):helper.prepare(root)
            (root/'vram.bin').write_bytes(b'\0'*4)
            with self.assertRaises(ValueError):helper.prepare(root)
