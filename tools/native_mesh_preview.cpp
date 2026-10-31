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
  float x, y, r, g, b, a, s, t, q;
};
static_assert(sizeof(Vertex) == 36 && std::numeric_limits<float>::is_iec559);
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
  if (length < 8 || length > 128 * 1024 * 1024)
    fail("invalid mesh file size");
  std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
  input.seekg(0);
  input.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
  if (!input)
    fail("cannot read mesh");
  const bool textured = std::memcmp(bytes.data(), "NFM2", 4) == 0;
  if (std::memcmp(bytes.data(), "NFM1", 4) && !textured)
    fail("invalid mesh header");
  const auto count = word(bytes.data() + 4);
  std::uint32_t width = 1, height = 1;
  struct Material {
    std::uint32_t flags, modes, aref, pad;
  } material = {};
  const std::size_t header = textured ? 28 : 8, stride = textured ? 36 : 20;
  if (bytes.size() < header)
    fail("truncated material header");
  if (textured) {
    width = word(bytes.data() + 8);
    height = word(bytes.data() + 12);
    material = {word(bytes.data() + 16), word(bytes.data() + 20),
                word(bytes.data() + 24), 0};
    if (!width || !height || width > 1024 || height > 1024 ||
        material.aref > 255)
      fail("invalid material dimensions");
    if (((material.modes >> 24) & 3) > 1 || ((material.modes >> 26) & 3) > 1)
      fail("region texture wrapping is unsupported");
  }
  const std::size_t texture_bytes =
      textured ? std::size_t(width) * height * 4 : 0;
  if (!count || count % 3 || count > 3000000 ||
      bytes.size() != header + std::size_t(count) * stride + texture_bytes)
    fail("invalid triangle-list length");
  std::vector<Vertex> vertices(count);
  double minx = std::numeric_limits<double>::max(), miny = minx, maxx = -minx,
         maxy = -minx;
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto *v = bytes.data() + header + i * stride;
    vertices[i] = {number(v),
                   number(v + 4),
                   number(v + 8),
                   number(v + 12),
                   number(v + 16),
                   textured ? number(v + 20) : 1.0f,
                   textured ? number(v + 24) : 0.0f,
                   textured ? number(v + 28) : 0.0f,
                   textured ? number(v + 32) : 1.0f};
    minx = std::min(minx, double(vertices[i].x));
    maxx = std::max(maxx, double(vertices[i].x));
    miny = std::min(miny, double(vertices[i].y));
    maxy = std::max(maxy, double(vertices[i].y));
  }
  const double span = std::max({maxx - minx, maxy - miny, 1e-5});
  for (auto &v : vertices) {
    v.x = float((v.x - (minx + maxx) * 0.5) * 1.8 / span);
    v.y = float(-(v.y - (miny + maxy) * 0.5) * 1.8 / span);
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
struct Out { @builtin(position) position:vec4<f32>, @location(0) colour:vec4<f32>, @location(1) stq:vec3<f32> }
struct Material { flags:u32, modes:u32, aref:u32, pad:u32 }
@group(0) @binding(0) var image:texture_2d<f32>;
@group(0) @binding(1) var texture_sampler:sampler;
@group(0) @binding(2) var<uniform> material:Material;
@vertex fn vs(@location(0) p:vec2<f32>,@location(1) colour:vec4<f32>,@location(2) stq:vec3<f32>)->Out {
    var out:Out;out.position=vec4<f32>(p,0.5,1.0);out.colour=colour;out.stq=stq;return out;
}
@fragment fn fs(in:Out)->@location(0) vec4<f32> {
    if ((material.flags & 2u)==0u) { return in.colour; }
    let q=select(in.stq.z,1.0,in.stq.z==0.0);
    let tex=vec4<i32>(clamp(textureSample(image,texture_sampler,in.stq.xy/q)*255.0+vec4<f32>(0.5),vec4<f32>(0.0),vec4<f32>(255.0)));
    let vertex=vec4<i32>(clamp(in.colour*255.0,vec4<f32>(0.0),vec4<f32>(255.0)));
    let tcc=(material.flags & 8192u)!=0u;
    var colour=vertex;
    switch ((material.modes >> 20u) & 3u) {
        case 0u {colour=clamp((tex*vertex)>>vec4<u32>(7u),vec4<i32>(0),vec4<i32>(255));if(!tcc){colour.a=vertex.a;}}
        case 1u {colour=tex;if(!tcc){colour.a=vertex.a;}}
        default {colour=vec4<i32>(clamp(((tex.rgb*vertex.rgb)>>vec3<u32>(7u))+vec3<i32>(vertex.a),vec3<i32>(0),vec3<i32>(255)),vertex.a);
            if(tcc){colour.a=select(clamp(tex.a+vertex.a,0,255),tex.a,((material.modes>>20u)&3u)==3u);}}
    }
    if ((material.flags & 64u)!=0u) {
        let a=u32(colour.a);var alpha_ok=false;
        switch (material.modes & 7u) {
            case 0u {alpha_ok=false;}case 1u {alpha_ok=true;}case 2u {alpha_ok=a<material.aref;}
            case 3u {alpha_ok=a<=material.aref;}case 4u {alpha_ok=a==material.aref;}
            case 5u {alpha_ok=a>=material.aref;}case 6u {alpha_ok=a>material.aref;}
            default {alpha_ok=a!=material.aref;}
        }
        let alpha_failure=(material.modes>>4u)&3u;
        if(!alpha_ok && (alpha_failure==0u || alpha_failure==2u)){discard;}
    }
    return vec4<f32>(colour)/255.0;
}
)";
  WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
  source.code = text(shader);
  WGPUShaderModuleDescriptor md = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  md.nextInChain = &source.chain;
  WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &md);
  WGPUVertexAttribute attributes[3] = {};
  attributes[0].format = WGPUVertexFormat_Float32x2;
  attributes[0].offset = 0;
  attributes[0].shaderLocation = 0;
  attributes[1].format = WGPUVertexFormat_Float32x4;
  attributes[1].offset = 8;
  attributes[1].shaderLocation = 1;
  WGPUVertexBufferLayout layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
  layout.arrayStride = sizeof(Vertex);
  layout.stepMode = WGPUVertexStepMode_Vertex;
  attributes[2].format = WGPUVertexFormat_Float32x3;
  attributes[2].offset = 24;
  attributes[2].shaderLocation = 2;
  layout.attributeCount = 3;
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
  WGPUTextureDescriptor image_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
  image_desc.dimension = WGPUTextureDimension_2D;
  image_desc.size = {width, height, 1};
  image_desc.format = WGPUTextureFormat_RGBA8Unorm;
  image_desc.mipLevelCount = 1;
  image_desc.sampleCount = 1;
  image_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
  WGPUTexture image = wgpuDeviceCreateTexture(device, &image_desc);
  WGPUTextureView image_view = wgpuTextureCreateView(image, nullptr);
  WGPUTexelCopyTextureInfo image_dest = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  image_dest.texture = image;
  WGPUTexelCopyBufferLayout image_layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
  image_layout.bytesPerRow = width * 4;
  image_layout.rowsPerImage = height;
  const unsigned char white[4] = {255, 255, 255, 255};
  const void *texels =
      textured ? bytes.data() + header + std::size_t(count) * stride : white;
  WGPUExtent3D image_size = {width, height, 1};
  wgpuQueueWriteTexture(queue, &image_dest, texels,
                        std::size_t(width) * height * 4, &image_layout,
                        &image_size);
  WGPUSamplerDescriptor sd = WGPU_SAMPLER_DESCRIPTOR_INIT;
  sd.addressModeU = ((material.modes >> 24) & 3) == 0
                        ? WGPUAddressMode_Repeat
                        : WGPUAddressMode_ClampToEdge;
  sd.addressModeV = ((material.modes >> 26) & 3) == 0
                        ? WGPUAddressMode_Repeat
                        : WGPUAddressMode_ClampToEdge;
  sd.magFilter = sd.minFilter =
      (material.flags & 16384) ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
  WGPUSampler sampler = wgpuDeviceCreateSampler(device, &sd);
  WGPUBufferDescriptor ub = WGPU_BUFFER_DESCRIPTOR_INIT;
  ub.size = sizeof material;
  ub.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
  WGPUBuffer uniform = wgpuDeviceCreateBuffer(device, &ub);
  wgpuQueueWriteBuffer(queue, uniform, 0, &material, sizeof material);
  WGPUBindGroupLayout bindings =
      wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
  WGPUBindGroupEntry entries[3] = {};
  for (unsigned i = 0; i < 3; ++i) {
    entries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[i].binding = i;
  }
  entries[0].textureView = image_view;
  entries[1].sampler = sampler;
  entries[2].buffer = uniform;
  entries[2].size = sizeof material;
  WGPUBindGroupDescriptor bg = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
  bg.layout = bindings;
  bg.entryCount = 3;
  bg.entries = entries;
  WGPUBindGroup group = wgpuDeviceCreateBindGroup(device, &bg);
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
  wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
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
  wgpuBindGroupRelease(group);
  wgpuBindGroupLayoutRelease(bindings);
  wgpuBufferRelease(uniform);
  wgpuSamplerRelease(sampler);
  wgpuTextureViewRelease(image_view);
  wgpuTextureRelease(image);
  wgpuRenderPipelineRelease(pipeline);
  wgpuShaderModuleRelease(module);
  wgpuQueueRelease(queue);
  wgpuDeviceRelease(device);
  wgpuAdapterRelease(adapter);
  wgpuInstanceRelease(instance);
  std::printf("Rendered %u vertices using native Vulkan rasterization to %s\n",
              count, argv[2]);
}
