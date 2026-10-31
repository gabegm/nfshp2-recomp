// Standalone hardware-rasterization research preview. No game code or assets.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

[[noreturn]] void fail(const char *message) {
  std::fprintf(stderr, "native preview: %s\n", message);
  std::exit(1);
}
struct Vertex {
  float x, y, r, g, b;
};
static_assert(sizeof(Vertex) == 20 && std::numeric_limits<float>::is_iec559);
std::uint32_t word(const unsigned char *p) {
  return p[0] | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
         (std::uint32_t(p[3]) << 24);
}
float number(const unsigned char *p) {
  auto w = word(p);
  float f;
  std::memcpy(&f, &w, 4);
  if (!std::isfinite(f))
    fail("non-finite vertex component");
  return f;
}
WGPUStringView text(const char *s) { return {s, std::strlen(s)}; }

int main(int argc, char **argv) {
  if (argc != 3)
    fail("usage: native-mesh-preview input.nfm output.ppm");
  std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
  if (!input)
    fail("cannot open mesh");
  const auto length = input.tellg();
  if (length < 8 || length > 60000008)
    fail("invalid mesh file size");
  std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
  input.seekg(0);
  input.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
  if (!input)
    fail("cannot read mesh");
  if (bytes.size() < 8 || std::memcmp(bytes.data(), "NFM1", 4))
    fail("invalid mesh header");
  const auto count = word(bytes.data() + 4);
  if (!count || count % 3 || count > 3000000 ||
      bytes.size() != 8 + std::size_t(count) * 20)
    fail("invalid triangle-list length");
  std::vector<Vertex> vertices(count);
  float minx = std::numeric_limits<float>::max(), miny = minx, maxx = -minx,
        maxy = -minx;
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto *p = bytes.data() + 8 + i * 20;
    auto &v = vertices[i];
    v = {number(p), number(p + 4), number(p + 8), number(p + 12),
         number(p + 16)};
    minx = std::min(minx, v.x);
    maxx = std::max(maxx, v.x);
    miny = std::min(miny, v.y);
    maxy = std::max(maxy, v.y);
  }
  const float span = std::max({maxx - minx, maxy - miny, 1e-5f});
  for (auto &v : vertices) {
    v.x = (v.x - (minx + maxx) * 0.5f) * 1.8f / span;
    v.y = -(v.y - (miny + maxy) * 0.5f) * 1.8f / span;
  }
  constexpr std::uint32_t size = 512, row_bytes = size * 4;
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
  const char *shader = R"(
struct Out { @builtin(position) position:vec4<f32>, @location(0) colour:vec3<f32> }
@vertex fn vs(@location(0) p:vec2<f32>,@location(1) colour:vec3<f32>)->Out {
    var out:Out;out.position=vec4<f32>(p,0.5,1.0);out.colour=colour;return out;
}
@fragment fn fs(in:Out)->@location(0) vec4<f32> { return vec4<f32>(in.colour,1.0); }
)";
  WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
  source.code = text(shader);
  WGPUShaderModuleDescriptor md = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  md.nextInChain = &source.chain;
  WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &md);
  WGPUVertexAttribute attributes[2] = {};
  attributes[0].format = WGPUVertexFormat_Float32x2;
  attributes[0].offset = 0;
  attributes[0].shaderLocation = 0;
  attributes[1].format = WGPUVertexFormat_Float32x3;
  attributes[1].offset = 8;
  attributes[1].shaderLocation = 1;
  WGPUVertexBufferLayout layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
  layout.arrayStride = sizeof(Vertex);
  layout.stepMode = WGPUVertexStepMode_Vertex;
  layout.attributeCount = 2;
  layout.attributes = attributes;
  WGPUColorTargetState colour = WGPU_COLOR_TARGET_STATE_INIT;
  colour.format = WGPUTextureFormat_RGBA8Unorm;
  colour.writeMask = WGPUColorWriteMask_All;
  WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
  fragment.module = module;
  fragment.entryPoint = text("fs");
  fragment.targetCount = 1;
  fragment.targets = &colour;
  WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
  pd.vertex.module = module;
  pd.vertex.entryPoint = text("vs");
  pd.vertex.bufferCount = 1;
  pd.vertex.buffers = &layout;
  pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
  pd.primitive.cullMode = WGPUCullMode_None;
  pd.fragment = &fragment;
  WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &pd);
  if (!pipeline)
    fail("cannot create render pipeline");
  WGPUBufferDescriptor vb = WGPU_BUFFER_DESCRIPTOR_INIT;
  vb.size = vertices.size() * sizeof(Vertex);
  vb.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
  WGPUBuffer vertex_buffer = wgpuDeviceCreateBuffer(device, &vb);
  wgpuQueueWriteBuffer(queue, vertex_buffer, 0, vertices.data(), vb.size);
  WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
  td.dimension = WGPUTextureDimension_2D;
  td.size = {size, size, 1};
  td.format = WGPUTextureFormat_RGBA8Unorm;
  td.mipLevelCount = 1;
  td.sampleCount = 1;
  td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
  WGPUTexture target = wgpuDeviceCreateTexture(device, &td);
  WGPUTextureView view = wgpuTextureCreateView(target, nullptr);
  WGPUBufferDescriptor rb = WGPU_BUFFER_DESCRIPTOR_INIT;
  rb.size = row_bytes * size;
  rb.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
  WGPUBuffer readback = wgpuDeviceCreateBuffer(device, &rb);
  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
  WGPURenderPassColorAttachment attachment =
      WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
  attachment.view = view;
  attachment.loadOp = WGPULoadOp_Clear;
  attachment.storeOp = WGPUStoreOp_Store;
  attachment.clearValue = {0, 0, 0, 1};
  WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
  pass_desc.colorAttachmentCount = 1;
  pass_desc.colorAttachments = &attachment;
  WGPURenderPassEncoder pass =
      wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
  wgpuRenderPassEncoderSetPipeline(pass, pipeline);
  wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertex_buffer, 0, vb.size);
  wgpuRenderPassEncoderDraw(pass, count, 1, 0, 0);
  wgpuRenderPassEncoderEnd(pass);
  wgpuRenderPassEncoderRelease(pass);
  WGPUTexelCopyTextureInfo ti = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  ti.texture = target;
  WGPUTexelCopyBufferInfo bi = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
  bi.buffer = readback;
  bi.layout.bytesPerRow = row_bytes;
  bi.layout.rowsPerImage = size;
  WGPUExtent3D extent = {size, size, 1};
  wgpuCommandEncoderCopyTextureToBuffer(encoder, &ti, &bi, &extent);
  WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, nullptr);
  wgpuQueueSubmit(queue, 1, &command);
  ready = false;
  WGPUBufferMapCallbackInfo map = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
  map.mode = WGPUCallbackMode_AllowProcessEvents;
  map.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void *flag,
                    void *) {
    if (status != WGPUMapAsyncStatus_Success)
      fail("readback failed");
    *static_cast<bool *>(flag) = true;
  };
  map.userdata1 = &ready;
  wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, rb.size, map);
  while (!ready)
    wgpuDevicePoll(device, true, nullptr);
  const auto *rgba = static_cast<const unsigned char *>(
      wgpuBufferGetConstMappedRange(readback, 0, rb.size));
  if (!rgba)
    fail("missing readback pixels");
  std::vector<unsigned char> rgb(size * size * 3);
  for (std::size_t i = 0; i < size * size; ++i)
    std::memcpy(rgb.data() + i * 3, rgba + i * 4, 3);
  std::ofstream output(argv[2], std::ios::binary);
  if (!output)
    fail("cannot open output");
  output << "P6\n" << size << " " << size << "\n255\n";
  output.write(reinterpret_cast<const char *>(rgb.data()), rgb.size());
  if (!output)
    fail("cannot write output");
  wgpuBufferUnmap(readback);
  wgpuCommandBufferRelease(command);
  wgpuCommandEncoderRelease(encoder);
  wgpuBufferRelease(readback);
  wgpuTextureViewRelease(view);
  wgpuTextureRelease(target);
  wgpuBufferRelease(vertex_buffer);
  wgpuRenderPipelineRelease(pipeline);
  wgpuShaderModuleRelease(module);
  wgpuQueueRelease(queue);
  wgpuDeviceRelease(device);
  wgpuAdapterRelease(adapter);
  wgpuInstanceRelease(instance);
  std::printf("Rendered %u vertices using native Vulkan rasterization to %s\n",
              count, argv[2]);
}
