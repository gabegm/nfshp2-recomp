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
#include <vector>
#include <utility>
constexpr unsigned kSetupWords=40;
struct State {bool allowed;};struct Vertex {};
namespace native_target {struct Vertex {};}
struct NativeCoverage {std::uint64_t eligible=0,bbox=0,eligible_bbox=0;std::vector<std::pair<std::uint32_t,std::uint64_t>> reasons;};
NativeCoverage g_native_batch_coverage;
bool native_triangle_input(const State &s,const Vertex*,int count,unsigned*,native_target::Vertex*) {return s.allowed&&count==3;}
unsigned native_rejection_mask(const State&,const Vertex*,int,const unsigned*) {return 1;}
// ACTUAL
int main(){
    unsigned rec[4]={0,0,0,2047u|(2047u<<16)};Vertex vertices[3];
    for(unsigned i=0;i<2048;++i)note_native_coverage({true},vertices,3,rec);
    if(g_native_batch_coverage.eligible!=2048 || g_native_batch_coverage.bbox!=(1ull<<33) || g_native_batch_coverage.eligible_bbox!=(1ull<<33))return 1;
    note_native_coverage({false},vertices,3,rec);note_native_coverage({true},vertices,2,rec);
    if(g_native_batch_coverage.eligible!=2048 || g_native_batch_coverage.bbox!=(1ull<<33)+(2ull<<22) || g_native_batch_coverage.eligible_bbox!=(1ull<<33))return 2;
    if(g_native_batch_coverage.reasons.size()!=2 || g_native_batch_coverage.reasons[0].second!=2048 || g_native_batch_coverage.reasons[1].second!=2)return 3;
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
            if 'native_rejection_mask(' in source.read_text():
                undo=subprocess.run(['git','-C',str(folder),'apply','--reverse',str(root/'patches/native-rejection-profile.patch')],capture_output=True,text=True)
                self.assertEqual(undo.returncode,0,undo.stderr)
            undo=subprocess.run(['git','-C',str(folder),'apply','--reverse',str(root/'patches/native-coverage.patch')],capture_output=True,text=True)
            self.assertEqual(undo.returncode,0,undo.stderr)
            ordinary=re.sub(r'#ifdef PS2_NATIVE_COVERAGE_DIAGNOSTIC\n(.*?)#else\n(.*?)#endif\n',lambda m:m[2],source.read_text(),flags=re.S)
            self.assertEqual(ordinary,(folder/'runtime/src/gs/gpu/gpu.cpp').read_text())

    def test_rejection_combinations_preserve_multiple_blockers_and_do_not_double_count_time(self):
        trace='10 0 raster 200 1 1 4 1 4 0 0 0 0 4 native 0 400 0 reasons 4:4\n'
        trace+='10 1 raster 300 1 1 3 1 3 0 0 0 0 3 native 0 300 0 reasons 12:3\n'
        trace+='10 2 raster 500 1 1 2 1 2 0 0 0 0 2 native 1 200 100 reasons 0:1 8:1\n'
        result=c.analyze(trace)['rejection_profile']
        self.assertEqual(result['single_condition_dispatch_ns']['depth_format'],200)
        self.assertEqual(result['single_condition_dispatch_ns']['destination_blending'],0)
        self.assertEqual(result['heterogeneous_dispatch_nanoseconds'],500)
        pair=next(r for r in result['combinations'] if r['mask']==12)
        self.assertEqual(pair['reasons'],['depth_format','destination_blending'])
        self.assertEqual(pair['uniform_dispatch_nanoseconds'],300)
        for bad in [trace.replace('4:4','4:3'),trace.replace('4:4','4096:4'),trace.replace('4:4','4:2 4:2'),trace.replace('0:1 8:1','0:2')]:
            with self.assertRaises(ValueError):c.analyze(bad)

    def test_actual_classifier_keeps_depth_and_blending_as_independent_conditions(self):
        import shutil,subprocess,tempfile
        root=Path(__file__).resolve().parents[1];runtime=root/'.local/ps2-native-recomp/runtime/src/gs/gpu/gpu.cpp'
        if not runtime.exists() or 'std::uint32_t native_rejection_mask(' not in runtime.read_text():
            self.skipTest('Install rejection instrumentation first.')
        compiler=shutil.which('c++')
        if not compiler:self.skipTest('C++17 compiler required.')
        source=runtime.read_text();start=source.index('std::uint32_t native_rejection_mask(');actual=source[start:source.index('\n}\n',start)+3]
        harness=r'''
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#define CHECK(x) do {if(!(x))std::exit(__LINE__);}while(0)
struct State {std::uint64_t prim=3,prmode=0,prmodecont=1;};
struct Vertex {unsigned z=100;float s=0,t=0,q=1;};
enum class Mode {primary,off};Mode g_mode=Mode::primary;unsigned g_scale=1;
namespace os {std::string env(const char*){return {};}}
// ACTUAL
int main(){
    State state;Vertex v[3];std::array<unsigned,40> s{};
    s[0]=2|2048|8192;s[2]=8;s[6]=0x31;s[23]=0xffffff;s[10]=511u<<16;s[11]=447u<<16;
    s[25]=3u<<20;s[26]=1;s[28]=0x56;s[29]=0xffffffff;
    s[30]=1u<<20;s[31]=8;s[33]=0x56;s[34]=0xffffffff;
    s[35]=2u<<20;s[36]=8;s[37]=2048;s[38]=0x56;s[39]=0xffffff;
    CHECK(native_rejection_mask(state,v,3,s.data())==0);
    auto depth=s;depth[6]=0x3a;depth[23]=0xffff;depth[37]=16384;depth[38]=0x266;depth[39]=0xffff;
    CHECK(native_rejection_mask(state,v,3,depth.data())==4);
    auto blend=s;blend[0]|=8;blend[7]=0x44u<<12;
    CHECK(native_rejection_mask(state,v,3,blend.data())==8);
    depth[0]|=8;depth[7]=0x44u<<12;CHECK(native_rejection_mask(state,v,3,depth.data())==12);
    auto b=s;b[3]=2;b[0]|=4096;CHECK(native_rejection_mask(state,v,3,b.data())==18);
    b=s;b[25]=s[30];CHECK(native_rejection_mask(state,v,3,b.data())==256);
    b=s;b[0]|=64;b[7]=1u<<4;CHECK(native_rejection_mask(state,v,3,b.data())==32);
    b=s;b[7]=2u<<24;CHECK(native_rejection_mask(state,v,3,b.data())==64);
    b=s;b[10]|=1;CHECK(native_rejection_mask(state,v,3,b.data())==128);
    v[0].q=0;CHECK(native_rejection_mask(state,v,3,s.data())==512);
    b=s;b[0]|=16;CHECK(native_rejection_mask(state,v,3,b.data())==0);v[0].q=1;
    g_scale=2;CHECK(native_rejection_mask(state,v,3,s.data())==1024);g_scale=1;
    state.prim=6;b=s;b[0]&=~2u;CHECK(native_rejection_mask(state,v,2,b.data())==65);
}'''
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);(folder/'test.cpp').write_text(harness.replace('// ACTUAL',actual))
            build=subprocess.run([compiler,'-std=c++17',str(folder/'test.cpp'),'-o',str(folder/'test')],capture_output=True,text=True,timeout=30)
            self.assertEqual(build.returncode,0,build.stderr)
            run=subprocess.run([str(folder/'test')],capture_output=True,text=True,timeout=10)
            self.assertEqual(run.returncode,0,run.stderr)
