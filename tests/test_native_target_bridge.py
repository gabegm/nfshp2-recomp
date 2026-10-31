"""Check the production bridge state gate without GPU access or game data."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / '.local/ps2-native-recomp/third_party/wgpu-native/include'

class NativeTargetGateTests(unittest.TestCase):
    @unittest.skipUnless((INCLUDE/'webgpu/webgpu.h').exists(), 'Set up the private runtime headers first.')
    def test_rejects_wrapping_aliasing_masks_and_unknown_layouts(self):
        source = r'''
#include "native_target_bridge.hpp"
#include <array>
#include <cstdlib>
using native_target::Bridge;
#define CHECK(x) do {if(!(x)) std::exit(__LINE__);}while(0)
int main() {
    std::array<unsigned,40> s{};
    s[2]=8;s[6]=0x31;s[23]=0xffffff;s[10]=511u<<16;s[11]=447u<<16;
    s[31]=8;s[33]=0x56;s[34]=0xffffffff;
    s[35]=2u<<20;s[36]=8;s[37]=2048;s[38]=0x56;s[39]=0xffffff;
    CHECK(Bridge::supported(s.data()));
    for(auto field:{3,4,6,23,30,31,32,33,34,35,36,37,38,39}) {
        auto bad=s;bad[field]^=1;CHECK(!Bridge::supported(bad.data()));
    }
    auto bad=s;bad[35]=0;CHECK(!Bridge::supported(bad.data()));
    bad=s;bad[35]=8192;CHECK(!Bridge::supported(bad.data()));
    bad=s;bad[35]=(4u<<20)-8192;CHECK(!Bridge::supported(bad.data()));
    bad=s;bad[10]|=1;CHECK(!Bridge::supported(bad.data()));
    bad=s;bad[11]|=1;CHECK(!Bridge::supported(bad.data()));
    bad=s;bad[2]=0;CHECK(!Bridge::supported(bad.data()));
    bad=s;bad[2]=0xffffffff;CHECK(!Bridge::supported(bad.data()));
    bad=s;bad[11]=2048u<<16;CHECK(!Bridge::supported(bad.data()));
    bad=s;bad[2]=32;bad[31]=bad[36]=32;bad[10]=2047u<<16;bad[11]=2047u<<16;
    CHECK(!Bridge::supported(bad.data()));
}'''
        compiler=shutil.which('c++')
        if not compiler:self.skipTest('A C++17 compiler is required.')
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);(folder/'test.cpp').write_text(source)
            result=subprocess.run([compiler,'-std=c++17','-O2','-I',str(ROOT/'tools'),'-I',str(INCLUDE),
                                   str(folder/'test.cpp'),'-o',str(folder/'test')],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(folder/'test')],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)
