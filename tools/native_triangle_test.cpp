// Synthetic GPU-only handoff verification; no game assets required.
#include "native_triangle.hpp"
#include <webgpu/wgpu.h>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <string>
[[noreturn]] void fail(const char *s) {std::fprintf(stderr,"bridge test: %s\n",s);std::exit(1);}
WGPUStringView text(const char *s) {return native_target::text(s);}
#define CHECK(x) do {if(!(x))fail(#x);}while(0)
int main() {
  WGPUInstanceExtras extras = {};
  extras.chain.sType = static_cast<WGPUSType>(WGPUSType_InstanceExtras);
  extras.backends = WGPUInstanceBackend_Vulkan;
  WGPUInstanceDescriptor id = WGPU_INSTANCE_DESCRIPTOR_INIT;
  id.nextInChain = &extras.chain;
  WGPUInstance instance = wgpuCreateInstance(&id);
  if (!instance)
    fail("cannot create Vulkan instance");
  WGPUAdapter adapter = nullptr;
  bool ready = false;
  WGPURequestAdapterOptions ao = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
  ao.powerPreference = WGPUPowerPreference_HighPerformance;
  WGPURequestAdapterCallbackInfo ac = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
  ac.mode = WGPUCallbackMode_AllowSpontaneous;
  ac.callback = [](WGPURequestAdapterStatus status, WGPUAdapter result,
                   WGPUStringView, void *out, void *done) {
    if (status == WGPURequestAdapterStatus_Success)
      *static_cast<WGPUAdapter *>(out) = result;
    *static_cast<bool *>(done) = true;
  };
  ac.userdata1 = &adapter;
  ac.userdata2 = &ready;
  wgpuInstanceRequestAdapter(instance, &ao, ac);
  while (!ready)
    wgpuInstanceProcessEvents(instance);
  if (!adapter)
    fail("no Vulkan adapter");
  WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
  if (wgpuAdapterGetInfo(adapter, &info) != WGPUStatus_Success)
    fail("cannot query adapter");
  if (info.adapterType == WGPUAdapterType_CPU ||
      info.backendType != WGPUBackendType_Vulkan)
    fail("a hardware Vulkan adapter is required");
  std::fprintf(stderr, "native preview: Vulkan hardware adapter %.*s\n",
               int(info.device.length), info.device.data);
  wgpuAdapterInfoFreeMembers(info);
  WGPUDevice device = nullptr;
  ready = false;
  WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
  dd.uncapturedErrorCallbackInfo.callback =
      [](WGPUDevice const *, WGPUErrorType, WGPUStringView message, void *,
         void *) {
        std::fprintf(stderr, "native preview GPU error: %.*s\n",
                     int(message.length), message.data);
        std::exit(1);
      };
  WGPURequestDeviceCallbackInfo dc = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
  dc.mode = WGPUCallbackMode_AllowSpontaneous;
  dc.callback = [](WGPURequestDeviceStatus status, WGPUDevice result,
                   WGPUStringView, void *out, void *done) {
    if (status == WGPURequestDeviceStatus_Success)
      *static_cast<WGPUDevice *>(out) = result;
    *static_cast<bool *>(done) = true;
  };
  dc.userdata1 = &device;
  dc.userdata2 = &ready;
  wgpuAdapterRequestDevice(adapter, &dd, dc);
  while (!ready)
    wgpuInstanceProcessEvents(instance);
  if (!device)
    fail("cannot create device");
  WGPUQueue queue = wgpuDeviceGetQueue(device);

  const auto buffer=[&](std::uint64_t size,WGPUBufferUsage usage) {
    WGPUBufferDescriptor d=WGPU_BUFFER_DESCRIPTOR_INIT;d.size=size;d.usage=usage;
    return wgpuDeviceCreateBuffer(device,&d);
  };
  auto vram=buffer(4u<<20,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst|WGPUBufferUsage_CopySrc);
  std::vector<std::uint32_t> tables(4096),input(1u<<20);
  // Distinct bijections for colour and Z: exercise address mapping independently.
  for(unsigned i=0;i<2048;++i) {tables[i]=(i^0x155u)*4;tables[i+2048]=(i^0x6aau)*4;}
  auto table_buffer=buffer(tables.size()*4,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst);
  wgpuQueueWriteBuffer(queue,table_buffer,0,tables.data(),tables.size()*4);
  auto readback=buffer(4u<<20,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
  native_target::Bridge bridge;CHECK(bridge.init(device));

  native_target::Triangle triangle;CHECK(triangle.prepare(device,1,1));
  const unsigned texel=0x80c08040;
  const unsigned initial_z[]={0x400000,0xc00000,0x800000,0x800000,0x400000,0x400000,0xc00000};
  const unsigned ztst[]={2,2,2,3,2,2,1};
  const bool passes[]={true,false,true,false,true,false,true};
  for(unsigned test=0;test<7;++test) {
    const unsigned width=128,height=128;
    std::uint32_t s[40]={};s[0]=2|8192|2048|512|((test==4)?32:0)|((test==5)?64:0);
    s[2]=width/64;s[6]=0x31;s[23]=0xffffff;s[7]=ztst[test]<<8;
    s[10]=(width-1)<<16;s[11]=(height-1)<<16;
    s[30]=0;s[31]=width/64;s[32]=0;s[33]=0x56;s[34]=0xffffffff;
    s[35]=2u<<20;s[36]=width/64;s[37]=2048;s[38]=0x56;s[39]=0xffffff;
    s[14]=0;s[25]=3u<<20;s[26]=1;s[28]=0x56;s[29]=0xffffffff;
    CHECK(native_target::Triangle::supported(s));
    unsigned seed=17313;
    for(auto &word:input){seed=seed*1664525u+1013904223u;word=seed;}
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
      auto page=((x/64)+(y/32)*(width/64))*8192;
      auto offset=(y%32)*64+x%64;
      auto &z=input[(s[35]+page+tables[2048+offset])/4];z=(z&0xff000000)|initial_z[test];
    }
    wgpuQueueWriteBuffer(queue,vram,0,input.data(),input.size()*4);
    wgpuQueueWriteBuffer(queue,triangle.decoded(),0,&texel,4);
    native_target::Vertex vertices[]={
      {16.25,8.25,128.0f/255,128.0f/255,128.0f/255,128.0f/255,0,0,1,8388608},
      {112.25,8.25,128.0f/255,128.0f/255,128.0f/255,128.0f/255,0,0,1,8388608},
      {16.25,104.25,128.0f/255,128.0f/255,128.0f/255,128.0f/255,0,0,1,8388608}};
    auto encoder=wgpuDeviceCreateCommandEncoder(device,nullptr);
    CHECK(bridge.begin(queue,encoder,vram,table_buffer,s));
    CHECK(triangle.encode(queue,encoder,bridge,vertices,s));
    bridge.finish(encoder,vram,table_buffer);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,vram,0,readback,0,4u<<20);
    auto command=wgpuCommandEncoderFinish(encoder,nullptr);wgpuQueueSubmit(queue,1,&command);
    bool mapped=false;
    WGPUBufferMapCallbackInfo cb=WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;cb.mode=WGPUCallbackMode_AllowProcessEvents;
    cb.callback=[](WGPUMapAsyncStatus status,WGPUStringView,void *flag,void*) {CHECK(status==WGPUMapAsyncStatus_Success);*static_cast<bool*>(flag)=true;};cb.userdata1=&mapped;
    wgpuBufferMapAsync(readback,WGPUMapMode_Read,0,4u<<20,cb);
    while(!mapped)wgpuDevicePoll(device,true,nullptr);
    auto actual=static_cast<const std::uint32_t*>(wgpuBufferGetConstMappedRange(readback,0,4u<<20));CHECK(actual);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
      if(!passes[test] || x<17 || y<9 || x+y>120)continue;
      auto page=((x/64)+(y/32)*(width/64))*8192;
      auto offset=(y%32)*64+x%64;
      input[(s[30]+page+tables[offset])/4]=texel;
      if(test!=4){auto &z=input[(s[35]+page+tables[2048+offset])/4];z=(z&0xff000000)|0x800000;}
    }
    for(unsigned i=0;i<input.size();++i)if(actual[i]!=input[i]) {
      std::fprintf(stderr,"native triangle case %u VRAM word %u: %08x expected %08x\n",test,i,actual[i],input[i]);fail("VRAM mismatch");
    }
    wgpuBufferUnmap(readback);wgpuCommandBufferRelease(command);wgpuCommandEncoderRelease(encoder);
    std::printf("Native textured triangle case %u: all 4 MB match analytic colour/depth/coverage\n",test);
  }
  triangle.reset();
  bridge.reset();wgpuBufferRelease(readback);wgpuBufferRelease(table_buffer);wgpuBufferRelease(vram);
  wgpuQueueRelease(queue);wgpuDeviceRelease(device);wgpuAdapterRelease(adapter);wgpuInstanceRelease(instance);
}
