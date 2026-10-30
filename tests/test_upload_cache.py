"""Exercise the installed runtime cache with synthetic buffers; no game is needed."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / '.local/ps2-native-recomp/runtime/src/gs/gpu/gpu.cpp'
HARNESS = r"""#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"check failed, line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
constexpr std::uint32_t kCacheWords = 128;
struct CacheEntry { std::uint64_t key; std::uint32_t offset,words; std::size_t bytes; std::uint64_t used; };
std::vector<std::uint32_t> g_cache_copy(kCacheWords), device_cache(kCacheWords);
std::deque<CacheEntry> g_cache_ring;
std::unordered_multimap<std::uint64_t,CacheEntry*> g_cache_index;
std::uint32_t g_cache_head=0;
std::uint64_t g_serial=1;
int g_queue=0,g_cache=0;
struct Pending { std::uint32_t offset,words; };
std::vector<Pending> pending;
void wgpuQueueWriteBuffer(int,int,std::uint64_t offset,const void* bytes,std::uint64_t size) {
    CHECK(offset+size<=device_cache.size()*4);
    for (const auto &p:pending) CHECK(offset+size<=p.offset*4 || offset>=(p.offset+p.words)*4);
    std::memcpy(reinterpret_cast<unsigned char*>(device_cache.data())+offset,bytes,size);
}
// ACTUAL_FUNCTIONS
int main() {
    std::vector<std::uint8_t> a(16,0xA1),b(16,0xB2);
    std::uint32_t first=0,second=0;
    CHECK(cache_insert(1,a.data(),a.size(),first)); pending.push_back({first,5});
    CHECK(cache_insert(2,b.data(),b.size(),second)); pending.push_back({second,5});
    CHECK(first!=second && g_cache_ring.size()==2);
    CHECK(cache_find(1,a.data(),a.size(),first));
    CHECK(!cache_find(1,b.data(),b.size(),second));
    CHECK(cache_insert(1,b.data(),b.size(),second)); pending.push_back({second,5});
    CHECK(cache_find(1,a.data(),a.size(),first));
    CHECK(cache_find(1,b.data(),b.size(),second));
    ++g_serial;pending.clear();
    std::vector<std::vector<std::uint8_t>> inputs;
    for(unsigned i=0;i<97;++i) inputs.emplace_back(8+i%23*4,static_cast<std::uint8_t>(i));
    unsigned random=0x47ab3,blocked=0,wraps=0;
    for(unsigned step=0;step<20000;++step) {
        if(step%17==0) { ++g_serial;pending.clear(); }
        random=random*1664525u+1013904223u;
        unsigned id=(random>>16)%inputs.size();const auto &bytes=inputs[id];
        std::uint32_t offset=0,head=g_cache_head;
        if(!cache_find(id%7,bytes.data(),bytes.size(),offset)) {
            if(!cache_insert(id%7,bytes.data(),bytes.size(),offset)) {
                ++blocked;++g_serial;pending.clear();
                CHECK(cache_insert(id%7,bytes.data(),bytes.size(),offset));
            }
        }
        if(g_cache_head<head) ++wraps;
        CHECK(std::memcmp(device_cache.data()+offset,bytes.data(),bytes.size())==0);
        pending.push_back({offset,static_cast<std::uint32_t>((bytes.size()+3)/4+1)});
        CHECK(g_cache_index.size()==g_cache_ring.size());
        for(std::size_t i=0;i<g_cache_ring.size();++i) {
            const auto &x=g_cache_ring[i];
            CHECK(x.offset+x.words<=kCacheWords);
            CHECK(std::memcmp(device_cache.data()+x.offset,g_cache_copy.data()+x.offset,x.bytes)==0);
            for(std::size_t j=i+1;j<g_cache_ring.size();++j) {
                const auto &y=g_cache_ring[j];
                CHECK(x.offset+x.words<=y.offset || y.offset+y.words<=x.offset);
            }
        }
    }
    CHECK(blocked>0 && wraps>100);
    std::printf("20,000 cache operations passed, %u wraps, %u guarded submissions\n",wraps,blocked);
}
"""

class UploadCacheTests(unittest.TestCase):
    @unittest.skipUnless(RUNTIME.is_file(), 'Build the private runtime first; no ROM is needed by this test.')
    def test_append_collisions_wrap_and_pending_gpu_reads(self):
        compiler = shutil.which('c++') or shutil.which('clang++')
        if not compiler:
            self.skipTest('A C++17 compiler is required.')
        source = RUNTIME.read_text()
        functions = source[source.index('bool cache_find('):source.index('std::uint32_t arena_append(')]
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            test = directory / 'cache.cpp'
            test.write_text(HARNESS.replace('// ACTUAL_FUNCTIONS', functions))
            binary = directory / 'cache-test'
            build = subprocess.run([compiler, '-std=c++17', '-O2', str(test), '-o', str(binary)],
                                   text=True, capture_output=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(binary)], text=True, capture_output=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('20,000 cache operations passed', run.stdout)
