import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('coverage',Path(__file__).resolve().parents[1]/'scripts/analyze-native-coverage.py')
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)

class NativeCoverageTests(unittest.TestCase):
    def test_timing_bounds_do_not_weight_mixed_time_by_primitive_count(self):
        trace='10 0 upload 100 1 1\n10 1 raster 200 1 1 4 1 4 0 0 3211520 59979 4 native 4 400 400\n'
        trace+='10 2 raster 600 1 1 10 1 10 0 0 3211520 59979 10 native 1 1000 10\n'
        trace+='11 0 raster 100 1 1 2 1 2 0 0 3211520 59979 2 native 0 200 0\n'
        r=c.analyze(trace)
        self.assertEqual(r['eligible_primitives'],5);self.assertEqual(r['primitives'],16)
        self.assertAlmostEqual(r['fully_eligible_compute_percent'],20)
        self.assertAlmostEqual(r['possibly_eligible_compute_percent'],80)
        self.assertEqual(r['raster_classes_ns'],dict(fully_eligible=200,mixed=600,unsupported=100))
        self.assertEqual(r['eligible_bbox_pixels'],410)

    def test_missing_or_invalid_metadata_fails(self):
        for trace in ['','10 1 raster 20 1 1 4 1 4 0 0 0 0 4',
                      '10 1 raster 20 1 1 4 1 4 0 0 0 0 4 native 5 100 100',
                      '10 1 raster 20 1 1 4 1 4 0 0 0 0 4 native 0 100 1',
                      '10 1 raster 20 1 1 4 1 4 0 0 0 0 4 native 4 100 99']:
            with self.subTest(trace=trace):
                with self.assertRaises(ValueError):c.analyze(trace)

    def test_actual_runtime_accumulator_uses_native_guard_and_64_bit_areas(self):
        import shutil,subprocess,tempfile
        runtime=Path(__file__).resolve().parents[1]/'.local/ps2-native-recomp/runtime/src/gs/gpu/gpu.cpp'
        if not runtime.exists() or 'void note_native_coverage(' not in runtime.read_text():
            self.skipTest('Install native coverage instrumentation first.')
        compiler=shutil.which('c++')
        if not compiler:self.skipTest('C++17 compiler required.')
        source=runtime.read_text();start=source.index('void note_native_coverage(');actual=source[start:source.index('\n}\n',start)+3]
        harness=r'''
#include <cstdint>
#include <cstdlib>
constexpr unsigned kSetupWords=40;
struct State {bool allowed;};struct Vertex {};
namespace native_target {struct Vertex {};}
struct NativeCoverage {std::uint64_t eligible=0,bbox=0,eligible_bbox=0;};
NativeCoverage g_native_batch_coverage;
bool native_triangle_input(const State &s,const Vertex*,int count,unsigned*,native_target::Vertex*) {return s.allowed&&count==3;}
// ACTUAL
int main(){
    unsigned rec[4]={0,0,0,2047u|(2047u<<16)};Vertex vertices[3];
    for(unsigned i=0;i<2048;++i)note_native_coverage({true},vertices,3,rec);
    if(g_native_batch_coverage.eligible!=2048 || g_native_batch_coverage.bbox!=(1ull<<33) || g_native_batch_coverage.eligible_bbox!=(1ull<<33))return 1;
    note_native_coverage({false},vertices,3,rec);note_native_coverage({true},vertices,2,rec);
    if(g_native_batch_coverage.eligible!=2048 || g_native_batch_coverage.bbox!=(1ull<<33)+(2ull<<22) || g_native_batch_coverage.eligible_bbox!=(1ull<<33))return 2;
}'''
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);(folder/'test.cpp').write_text(harness.replace('// ACTUAL',actual))
            build=subprocess.run([compiler,'-std=c++17',str(folder/'test.cpp'),'-o',str(folder/'test')],capture_output=True,text=True,timeout=30)
            self.assertEqual(build.returncode,0,build.stderr)
            run=subprocess.run([str(folder/'test')],capture_output=True,text=True,timeout=10)
            self.assertEqual(run.returncode,0,run.stderr)

    def test_diagnostic_link_reuses_objects_without_mutating_normal_output(self):
        import importlib.util
        script=Path(__file__).resolve().parents[1]/'scripts/build-native-coverage.py'
        spec=importlib.util.spec_from_file_location('coverage_build',script);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        command=': && /compiler -Xlinker --dependency-file=CMakeFiles/ps2run.dir/link.d "object with spaces.o" -o ps2run /normal/runtime.a -lSDL2 && :'
        args=m.link_arguments(command,Path('/normal/runtime.a'),Path('/private/runtime.a'),Path('/private/ps2run'),'/compiler')
        self.assertIn('object with spaces.o',args);self.assertIn('/private/runtime.a',args)
        self.assertIn('--dependency-file=/private/link.d',args)
        self.assertEqual(args[args.index('-o')+1],'/private/ps2run')
        for bad in [command+' && touch /tmp/unexpected',command.replace('/normal/runtime.a','/wrong.a'),
                    command.replace(' -o ps2run',' -o other'),command.replace('object with spaces.o','@response.rsp')]:
            with self.assertRaises(ValueError):m.link_arguments(bad,Path('/normal/runtime.a'),Path('/private/runtime.a'),Path('/private/ps2run'),'/compiler')

    def test_default_source_exactly_preserves_pre_diagnostic_implementation(self):
        import re,shutil,subprocess,tempfile
        root=Path(__file__).resolve().parents[1];runtime=root/'.local/ps2-native-recomp'
        source=runtime/'runtime/src/gs/gpu/gpu.cpp'
        if not source.exists() or '#ifdef PS2_NATIVE_COVERAGE_DIAGNOSTIC' not in source.read_text():
            self.skipTest('Install gated coverage instrumentation first.')
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);subprocess.run(['git','init','-q',str(folder)],check=True)
            for name in ('runtime/src/gs/gpu/gpu.cpp','CMakeLists.txt'):
                p=folder/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(runtime/name,p)
            undo=subprocess.run(['git','-C',str(folder),'apply','--reverse',str(root/'patches/native-coverage.patch')],capture_output=True,text=True)
            self.assertEqual(undo.returncode,0,undo.stderr)
            ordinary=re.sub(r'#ifdef PS2_NATIVE_COVERAGE_DIAGNOSTIC\n(.*?)#else\n(.*?)#endif\n',lambda m:m[2],source.read_text(),flags=re.S)
            self.assertEqual(ordinary,(folder/'runtime/src/gs/gpu/gpu.cpp').read_text())
