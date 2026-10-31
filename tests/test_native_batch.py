"""Exercise the actual installed native queue with synthetic states and vertices."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
RUNTIME=ROOT/'.local/ps2-native-recomp/runtime/src/gs/gpu/gpu.cpp'
HARNESS=r'''
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
constexpr unsigned kSetupWords=40;
struct Vertex {unsigned id;};
namespace native_target {struct Vertex {unsigned id;};}
struct State {unsigned id=0,key=0;bool supported=true;};
struct {unsigned generation=0;} g_setup_cache;
unsigned frame=7;bool fail_render=false;
std::vector<std::vector<unsigned>> groups;
std::vector<unsigned> replay;
unsigned frame_count(){return frame;}
namespace os {std::string env(const char *name){return std::string(name)=="PS2_NATIVE_BATCH"?"7,2,4096":"128";}}
bool native_triangle_input(const State &s,const Vertex *v,int count,unsigned *setup,native_target::Vertex *input) {
    if(!s.supported || count!=3)return false;
    setup[0]=s.key;for(unsigned i=0;i<3;++i)input[i].id=v[i].id;return true;
}
void flush_native_batch();
bool native_batch_draw(const State&,const Vertex*,int);
void draw(const State&,const Vertex*,int);
bool render_native_triangles(const unsigned*,const native_target::Vertex *v,unsigned count) {
    flush_native_batch(); // Must be safe when the real renderer flushes fallback work.
    groups.emplace_back();for(unsigned i=0;i<count;++i)groups.back().push_back(v[i].id);
    return !fail_render;
}
// ACTUAL_QUEUE
void draw(const State &s,const Vertex *v,int count) {
    CHECK(g_native_replay);CHECK(!native_batch_draw(s,v,count));
    CHECK(g_setup_cache.generation==~0u);replay.push_back(s.id);
}
int main() {
    State state;Vertex vertices[3]={{11},{12},{13}};
    CHECK(!native_batch_draw(state,vertices,3));CHECK(!native_batch_draw(state,vertices,3)); // before selected ordinal
    CHECK(native_batch_draw(state,vertices,3));CHECK(native_batch_draw(state,vertices,3));
    CHECK(groups.empty() && g_native_pending.size()==2);
    state.key=1;vertices[0].id=21;CHECK(native_batch_draw(state,vertices,3));
    CHECK(groups.size()==1 && groups[0].size()==6 && groups[0][0]==11 && groups[0][3]==11);
    CHECK(g_native_pending.size()==1);
    state.supported=false;CHECK(!native_batch_draw(state,vertices,3));
    CHECK(groups.size()==2 && groups[1].size()==3 && groups[1][0]==21);
    CHECK(g_native_pending.empty());
    state.supported=true;
    for(unsigned i=0;i<128;++i){vertices[0].id=i;CHECK(native_batch_draw(state,vertices,3));}
    CHECK(g_native_pending.empty() && groups.back().size()==384);
    for(unsigned i=0;i<128;++i)CHECK(groups.back()[i*3]==i);
    CHECK(native_batch_draw(state,vertices,3));frame=8;CHECK(!native_batch_draw(state,vertices,3));
    CHECK(g_native_pending.empty() && groups.back().size()==3);
    frame=7;fail_render=true;
    state.id=31;CHECK(native_batch_draw(state,vertices,3));state.id=32;CHECK(native_batch_draw(state,vertices,3));
    flush_native_batch();CHECK(replay==std::vector<unsigned>({31,32}));CHECK(!g_native_replay);
    CHECK(g_native_pending.empty());
    fail_render=false;groups.clear();
    // The bounded selection eventually stops intercepting draws.
    unsigned intercepted=0;
    for(unsigned i=0;i<4200;++i)intercepted+=native_batch_draw(state,vertices,3);
    CHECK(intercepted>3000 && intercepted<4096);CHECK(g_native_pending.empty());
}
'''

class NativeBatchTests(unittest.TestCase):
    @unittest.skipUnless(RUNTIME.exists(),'Install the private runtime sources first.')
    def test_order_state_changes_bounds_frame_end_and_fallback_replay(self):
        source=RUNTIME.read_text()
        if 'struct NativeQueued {' not in source:self.skipTest('Install the native batch patch first.')
        actual=source[source.index('struct NativeQueued {'):source.index('} // namespace\n\nvoid draw(',source.index('struct NativeQueued {'))]
        compiler=shutil.which('c++')
        if not compiler:self.skipTest('A C++17 compiler is required.')
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);(folder/'test.cpp').write_text(HARNESS.replace('// ACTUAL_QUEUE',actual))
            build=subprocess.run([compiler,'-std=c++17','-O2',str(folder/'test.cpp'),'-o',str(folder/'test')],capture_output=True,text=True,timeout=30)
            self.assertEqual(build.returncode,0,build.stderr)
            run=subprocess.run([str(folder/'test')],capture_output=True,text=True,timeout=10)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
