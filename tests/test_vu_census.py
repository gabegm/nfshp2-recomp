"""Synthetic graphics packets only; no game executable or assets required."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'scripts/analyze-vu-census.py'
spec = importlib.util.spec_from_file_location('vu_census', SOURCE)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


def tag(loops, regs, prim=3, mode=0, pre=True):
    low = loops | 0x8000 | (int(pre) << 46) | (prim << 47) | (mode << 58) | (len(regs) << 60)
    return struct.pack('<QQ', low, sum(reg << (4*i) for i, reg in enumerate(regs)))


def xyz(x, y, z=100, adc=False):
    return struct.pack('<4I', x, y, z, 0x8000 if adc else 0)


class GifPacketTests(unittest.TestCase):
    def test_triangle_list_and_fixed_point_vertices(self):
        result = helper.decode_packet(tag(3, [5]) + xyz(16, 32) + xyz(64, 32) + xyz(16, 80))
        self.assertEqual(result['triangles'], [[0, 1, 2]])
        self.assertEqual((result['vertices'][0]['x'], result['vertices'][0]['y']), (16, 32))
        self.assertTrue(result['mesh_complete'])

    def test_strip_adc_advances_without_emitting_a_triangle(self):
        data = tag(5, [5], prim=4) + b''.join(xyz(i*16, 32, adc=(i == 2)) for i in range(5))
        self.assertEqual(helper.decode_packet(data)['triangles'], [[1, 2, 3], [2, 3, 4]])

    def test_fan_and_packed_colour_stq(self):
        data = tag(4, [2, 1, 5], prim=5)
        for i in range(4):
            data += struct.pack('<4I', 0x3F800000, 0, 0x40000000, 0)
            data += struct.pack('<4I', 10, 20, 30, 128) + xyz(i*16, 32)
        result = helper.decode_packet(data)
        self.assertEqual(result['triangles'], [[0, 1, 2], [0, 2, 3]])
        self.assertEqual(result['vertices'][0]['rgba'], [10, 20, 30, 128])
        self.assertEqual(result['vertices'][0]['stq_bits'], [0x3F800000, 0, 0x40000000])

    def test_unknown_primitive_is_not_assumed_to_be_triangles(self):
        result = helper.decode_packet(tag(3, [5], pre=False) + xyz(0, 0)*3, {0: 3})
        self.assertEqual(result['triangles'], [])
        self.assertFalse(result['mesh_complete'])

    def test_rejects_truncated_and_oversized_packets(self):
        for data in (b'x', tag(4, [5]) + xyz(0, 0), b'\0'*16400):
            with self.subTest(length=len(data)), self.assertRaises(helper.PacketError):
                helper.decode_packet(data)

    def test_nonpacked_and_ad_vertex_paths_are_reported_incomplete(self):
        result = helper.decode_packet(tag(2, [5], mode=1) + b'\0'*16)
        self.assertFalse(result['mesh_complete'])
        result = helper.decode_packet(tag(1, [14]) + struct.pack('<QQ', 0, 5))
        self.assertFalse(result['mesh_complete'])

    def test_capture_path_cannot_escape_or_follow_a_symlink(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ('../packet.gif', '/tmp/packet.gif', ''):
                with self.assertRaises(helper.PacketError):helper.capture_file(root, name)
            (root/'link').symlink_to(root/'target')
            with self.assertRaises(helper.PacketError):helper.capture_file(root, 'link')

    def test_manifest_aggregates_whole_calls_and_checks_packet_lengths(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            packet = tag(3, [5]) + xyz(0, 0)*3
            (root/'sample.gif').write_bytes(packet)
            rows = [dict(type='header', version=1),
                    dict(type='call', id=1, key='synthetic', entry=0, sample='sample'),
                    dict(type='kick', id=1, packet='sample.gif', qwords=len(packet)//16),
                    dict(type='summary', frame=10, key='synthetic', entry=0, calls=3, kicks=4, qwords=20)]
            (root/'calls.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in rows))
            result = helper.summarize(root, meshes=True)
            self.assertEqual(result['programs'][0]['calls'], 3)
            self.assertEqual(result['frames'], 1)
            self.assertIn('f 1 2 3', (root/'sample.obj').read_text())
            self.assertEqual(struct.unpack_from('<4sI', (root/'sample.nfm').read_bytes()), (b'NFM1', 3))
            (root/'sample.gif').write_bytes(packet[:-16])
            with self.assertRaises(helper.PacketError):helper.summarize(root)
