import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('benchmark',Path(__file__).resolve().parents[1]/'scripts/benchmark-performance.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
LOG='[gpu] Intel UHD test (Mesa) on Vulkan\n[audio] sound device synthetic (alsa): 48000 Hz\n[ee] stopped: frame limit reached\n'

class BenchmarkTests(unittest.TestCase):
    def content(self):
        return ' '.join(b.COLUMNS)+'\n'+''.join(f'{i} 20 5 12 0 30 0 40\n' for i in range(101,111))

    def test_guest_progress_and_pairs(self):
        result=b.summarize(self.content(),100,10,2,LOG)
        self.assertEqual(result['intervals'],9)
        self.assertAlmostEqual(result['guest_vblanks_per_second'],50)
        self.assertAlmostEqual(result['guest_speed_percent'],100*5/6)
        self.assertEqual(result['adjacent_pair_ms_p99'],40)
        self.assertEqual(result['interpreted_vu_calls'],0)

    def test_missing_duplicate_and_partial_intervals_fail(self):
        for content in [self.content().replace('105 20 5 12 0 30 0 40\n',''),
                        self.content()+'110 20 5 12 0 30 0 40\n',
                        '\n'.join(self.content().splitlines()[:-3]),
                        self.content().replace('20 5 12','nan 5 12'),
                        self.content().replace('20 5 12','-1 5 12')]:
            with self.subTest(content=content):
                with self.assertRaises(ValueError):b.summarize(content,100,10,2,LOG)

    def test_failed_software_or_instrumented_run_is_rejected(self):
        for log in [LOG.replace('frame limit reached','user quit'),LOG.replace('Intel UHD test','llvmpipe'),
                    LOG+'[gpu] error validation\n',LOG+'[native-batch] 32 triangles\n', LOG+'[native-coverage-build] diagnostic\n', LOG+'[play-gs-prototype] ordered-register backend\n',
                    LOG.replace('(alsa)','(dummy)'),LOG.replace('[audio] sound device synthetic (alsa): 48000 Hz\n','')]:
            with self.subTest(log=log):
                with self.assertRaises(ValueError):b.summarize(self.content(),100,10,2,log)

    def test_starvation_labels_preserve_warmup_boundary(self):
        log=LOG+'[audio] the sound ran dry at frame 101 (1 times so far)\n[audio] the sound ran dry at frame 106 (2 times so far)\n'
        log+='[audio] the device got 0.12 s of sound in 0.20 s; the queue ran dry 7 times (about 0.08 s of silence in between)\n'
        result=b.summarize(self.content(),100,10,2,log)
        self.assertEqual(result['logged_audio_starvation_events_after_warmup'],1)
        self.assertEqual(result['whole_run_audio_starvations'],7)
