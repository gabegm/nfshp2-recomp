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
  float x, y, r, g, b, a, s, t, q, z;
};
static_assert(sizeof(Vertex) == 40 && std::numeric_limits<float>::is_iec559);
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

// Depth textures cannot be populated through queueWriteTexture. Initialize
// them through a real depth-only render pass instead.
void initialize_depth(WGPUDevice device, WGPUQueue queue, WGPUTextureView view,
                      const unsigned char *bytes, std::size_t count,
                      std::uint32_t width) {
  for (std::size_t i = 0; i < count / 4; ++i) {
    float d = number(bytes + i * 4);
    if (d < 0 || d > 1)
      fail("invalid initial depth");
  }
  WGPUBufferDescriptor buffer_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
  buffer_desc.size = count;
  buffer_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
  WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &buffer_desc);
  wgpuQueueWriteBuffer(queue, buffer, 0, bytes, count);
  std::string code = R"(
@group(0) @binding(0) var<storage,read> depths:array<f32>;
@vertex fn vs(@builtin(vertex_index) index:u32)->@builtin(position) vec4<f32>{
    let p=array<vec2<f32>,3>(vec2<f32>(-1.0,-1.0),vec2<f32>(3.0,-1.0),vec2<f32>(-1.0,3.0));
    return vec4<f32>(p[index],0.0,1.0);
}
@fragment fn fs(@builtin(position) p:vec4<f32>)->@builtin(frag_depth) f32 {
    return depths[u32(p.y)*WIDTH+u32(p.x)];
}
)";
  code = "const WIDTH:u32=" + std::to_string(width) + "u;\n" + code;
  WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
  source.code = {code.data(), code.size()};
  WGPUShaderModuleDescriptor module_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  module_desc.nextInChain = &source.chain;
  WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &module_desc);
  WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
  depth.format = WGPUTextureFormat_Depth32Float;
  depth.depthWriteEnabled = WGPUOptionalBool_True;
  depth.depthCompare = WGPUCompareFunction_Always;
  WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
  fragment.module = module;
  fragment.entryPoint = text("fs");
  WGPURenderPipelineDescriptor pipeline_desc =
      WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
  pipeline_desc.vertex.module = module;
  pipeline_desc.vertex.entryPoint = text("vs");
  pipeline_desc.fragment = &fragment;
  pipeline_desc.depthStencil = &depth;
  WGPURenderPipeline pipeline =
      wgpuDeviceCreateRenderPipeline(device, &pipeline_desc);
  WGPUBindGroupLayout layout =
      wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
  WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
  entry.binding = 0;
  entry.buffer = buffer;
  entry.size = count;
  WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
  group_desc.layout = layout;
  group_desc.entryCount = 1;
  group_desc.entries = &entry;
  WGPUBindGroup group = wgpuDeviceCreateBindGroup(device, &group_desc);
  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
  WGPURenderPassDepthStencilAttachment attachment =
      WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
  attachment.view = view;
  attachment.depthLoadOp = WGPULoadOp_Clear;
  attachment.depthStoreOp = WGPUStoreOp_Store;
  attachment.depthClearValue = 1.0;
  WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
  pass_desc.depthStencilAttachment = &attachment;
  WGPURenderPassEncoder pass =
      wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
  wgpuRenderPassEncoderSetPipeline(pass, pipeline);
  wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
  wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
  wgpuRenderPassEncoderEnd(pass);
  wgpuRenderPassEncoderRelease(pass);
  WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, nullptr);
  wgpuQueueSubmit(queue, 1, &command);
  wgpuCommandBufferRelease(command);
  wgpuCommandEncoderRelease(encoder);
  wgpuBindGroupRelease(group);
  wgpuBindGroupLayoutRelease(layout);
  wgpuRenderPipelineRelease(pipeline);
  wgpuShaderModuleRelease(module);
  wgpuBufferRelease(buffer);
}

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
  const bool frame_proof = std::memcmp(bytes.data(), "NFM3", 4) == 0;
  const bool textured =
      frame_proof || std::memcmp(bytes.data(), "NFM2", 4) == 0;
  if (std::memcmp(bytes.data(), "NFM1", 4) && !textured)
    fail("invalid mesh header");
  const auto count = word(bytes.data() + 4);
  std::uint32_t width = 1, height = 1, view_width = 512, view_height = 512;
  struct Material {
    std::uint32_t flags, modes, aref, pad;
    float tri0[4], tri1[4], tri2[4];
  } material = {};
  static_assert(sizeof(Material) == 64);
  const std::size_t header = frame_proof ? 40
                             : textured  ? 28
                                         : 8,
                    stride = frame_proof ? 40
                             : textured  ? 36
                                         : 20;
  if (bytes.size() < header)
    fail("truncated material header");
  if (textured) {
    width = word(bytes.data() + 8);
    height = word(bytes.data() + 12);
    material.flags = word(bytes.data() + 16);
    material.modes = word(bytes.data() + 20);
    material.aref = word(bytes.data() + 24);
    if (!width || !height || width > 1024 || height > 1024 ||
        material.aref > 255)
      fail("invalid material dimensions");
    if (((material.modes >> 24) & 3) > 1 || ((material.modes >> 26) & 3) > 1)
      fail("region texture wrapping is unsupported");
  }
  if (frame_proof) {
    view_width = word(bytes.data() + 28);
    view_height = word(bytes.data() + 32);
    if (!view_width || !view_height || view_width > 2048 ||
        view_height > 2048 ||
        std::size_t(view_width) * view_height > (1u << 20))
      fail("invalid framebuffer dimensions");
  }
  const std::size_t frame_bytes =
      frame_proof ? std::size_t(view_width) * view_height * 4 : 0;
  const std::size_t texture_bytes =
      textured ? std::size_t(width) * height * 4 : 0;
  if (!count || count % 3 || count > 3000000 ||
      bytes.size() != header + std::size_t(count) * stride + texture_bytes +
                          frame_bytes * 2)
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
                   textured ? number(v + 32) : 1.0f,
                   frame_proof ? number(v + 36) : 0.5f};
    minx = std::min(minx, double(vertices[i].x));
    maxx = std::max(maxx, double(vertices[i].x));
    miny = std::min(miny, double(vertices[i].y));
    maxy = std::max(maxy, double(vertices[i].y));
  }
  if (frame_proof) {
    if (count != 3 || word(bytes.data() + 36) != 0xFFFFFF)
      fail("frame proof requires one PSMZ24 triangle");
    const auto alpha = (material.modes >> 12) & 255;
    if (!(material.flags & 2048) || (material.flags & (4 | 128 | 4096)) ||
        ((material.modes >> 28) & 3) ||
        ((material.flags & 64) && ((material.modes >> 4) & 3)) ||
        ((material.flags & 8) &&
         ((alpha & 3) != ((alpha >> 2) & 3) || ((alpha >> 6) & 3))))
      fail("unsupported framebuffer proof state");
    for (const auto &v : vertices)
      if (v.z < 0 || v.z > 1)
        fail("invalid proof vertex depth");
    const auto &a = vertices[0], &b = vertices[1], &c = vertices[2];
    const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (!std::isfinite(area) || area == 0)
      fail("choose a finite, nondegenerate triangle");
    const float z0 = float(std::round(double(a.z) * 0xFFFFFF));
    const float z1 = float(std::round(double(b.z) * 0xFFFFFF));
    const float z2 = float(std::round(double(c.z) * 0xFFFFFF));
    material.tri0[0] = a.x;
    material.tri0[1] = a.y;
    material.tri0[2] = b.x;
    material.tri0[3] = b.y;
    material.tri1[0] = c.x;
    material.tri1[1] = c.y;
    material.tri1[2] = 1.0f / area;
    material.tri1[3] = z1 - z0;
    material.tri2[0] = z2 - z0;
    material.tri2[1] = z0;
    material.tri2[2] = float(0xFFFFFF);
  }
  const double span = std::max({maxx - minx, maxy - miny, 1e-5});
  for (auto &v : vertices) {
    if (frame_proof) {
      v.x = 2.0f * (v.x + 0.5f) / view_width - 1.0f;
      v.y = 1.0f - 2.0f * (v.y + 0.5f) / view_height;
    } else {
      v.x = float((v.x - (minx + maxx) * 0.5) * 1.8 / span);
      v.y = float(-(v.y - (miny + maxy) * 0.5) * 1.8 / span);
    }
  }
  const std::uint32_t row_bytes = (view_width * 4 + 255u) & ~255u;
  const std::uint64_t copy_bytes =
      static_cast<std::uint64_t>(row_bytes) * view_height;
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
struct Material { flags:u32, modes:u32, aref:u32, pad:u32, tri0:vec4<f32>, tri1:vec4<f32>, tri2:vec4<f32> }
@group(0) @binding(0) var image:texture_2d<f32>;
@group(0) @binding(1) var texture_sampler:sampler;
@group(0) @binding(2) var<uniform> material:Material;
@vertex fn vs(@location(0) p:vec2<f32>,@location(1) colour:vec4<f32>,@location(2) stq:vec3<f32>,@location(3) z:f32)->Out {
    var out:Out;out.position=vec4<f32>(p,z,1.0);out.colour=colour;out.stq=stq;return out;
}
fn shade(in:Out)->vec4<f32> {
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
@fragment fn fs(in:Out)->@location(0) vec4<f32> {return shade(in);}
struct FrameOut {@location(0) colour:vec4<f32>,@builtin(frag_depth) depth:f32}
@fragment fn fs_frame(in:Out)->FrameOut {
    var out:FrameOut;out.colour=shade(in);
    let px=in.position.x-0.5;let py=in.position.y-0.5;
    let a=material.tri0;let b=material.tri1;
    let w0=((a.z-px)*(b.y-py)-(b.x-px)*(a.w-py))*b.z;
    let w1=((b.x-px)*(a.y-py)-(a.x-px)*(b.y-py))*b.z;
    let w2=1.0-w0-w1;
    let delta=b.w*w1+material.tri2.x*w2;
    out.depth=clamp(material.tri2.y+floor(delta),0.0,material.tri2.z)/material.tri2.z;
    return out;
}
)";
  WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
  source.code = text(shader);
  WGPUShaderModuleDescriptor md = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  md.nextInChain = &source.chain;
  WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &md);
  WGPUVertexAttribute attributes[4] = {};
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
  attributes[3].format = WGPUVertexFormat_Float32;
  attributes[3].offset = 36;
  attributes[3].shaderLocation = 3;
  layout.attributeCount = 4;
  layout.attributes = attributes;
  WGPUColorTargetState colour = WGPU_COLOR_TARGET_STATE_INIT;
  colour.format = WGPUTextureFormat_RGBA8Unorm;
  colour.writeMask = WGPUColorWriteMask_All;
  WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
  fragment.module = module;
  fragment.entryPoint = text(frame_proof ? "fs_frame" : "fs");
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
  WGPUDepthStencilState depth_state = WGPU_DEPTH_STENCIL_STATE_INIT;
  if (frame_proof) {
    depth_state.format = WGPUTextureFormat_Depth32Float;
    depth_state.depthWriteEnabled =
        (material.flags & 512) && !(material.flags & 32)
            ? WGPUOptionalBool_True
            : WGPUOptionalBool_False;
    const WGPUCompareFunction comparisons[] = {
        WGPUCompareFunction_Never, WGPUCompareFunction_Always,
        WGPUCompareFunction_GreaterEqual, WGPUCompareFunction_Greater};
    depth_state.depthCompare = (material.flags & 512)
                                   ? comparisons[(material.modes >> 8) & 3]
                                   : WGPUCompareFunction_Always;
    pd.depthStencil = &depth_state;
  }

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
  td.size = {view_width, view_height, 1};
  td.format = WGPUTextureFormat_RGBA8Unorm;
  td.mipLevelCount = 1;
  td.sampleCount = 1;
  td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
             WGPUTextureUsage_CopyDst;
  WGPUTexture target = wgpuDeviceCreateTexture(device, &td);
  WGPUTextureView view = wgpuTextureCreateView(target, nullptr);
  WGPUTexture depth_texture = nullptr;
  WGPUTextureView depth_view = nullptr;
  if (frame_proof) {
    const auto *frame_data =
        bytes.data() + header + std::size_t(count) * stride + texture_bytes;
    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = target;
    WGPUTexelCopyBufferLayout frame_layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    frame_layout.bytesPerRow = view_width * 4;
    frame_layout.rowsPerImage = view_height;
    WGPUExtent3D frame_size = {view_width, view_height, 1};
    wgpuQueueWriteTexture(queue, &destination, frame_data, frame_bytes,
                          &frame_layout, &frame_size);
    WGPUTextureDescriptor depth_desc = td;
    depth_desc.format = WGPUTextureFormat_Depth32Float;
    depth_texture = wgpuDeviceCreateTexture(device, &depth_desc);
    depth_view = wgpuTextureCreateView(depth_texture, nullptr);
    initialize_depth(device, queue, depth_view, frame_data + frame_bytes,
                     frame_bytes, view_width);
  }
  WGPUBufferDescriptor rb = WGPU_BUFFER_DESCRIPTOR_INIT;
  rb.size = copy_bytes * (frame_proof ? 2 : 1);
  rb.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
  WGPUBuffer readback = wgpuDeviceCreateBuffer(device, &rb);
  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
  WGPURenderPassColorAttachment attachment =
      WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
  attachment.view = view;
  attachment.loadOp = frame_proof ? WGPULoadOp_Load : WGPULoadOp_Clear;
  attachment.storeOp = WGPUStoreOp_Store;
  attachment.clearValue = {0, 0, 0, 1};
  WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
  pass_desc.colorAttachmentCount = 1;
  pass_desc.colorAttachments = &attachment;
  WGPURenderPassDepthStencilAttachment depth_attachment =
      WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
  if (frame_proof) {
    depth_attachment.view = depth_view;
    depth_attachment.depthLoadOp = WGPULoadOp_Load;
    depth_attachment.depthStoreOp = WGPUStoreOp_Store;
    pass_desc.depthStencilAttachment = &depth_attachment;
  }

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
  bi.layout.rowsPerImage = view_height;
  WGPUExtent3D extent = {view_width, view_height, 1};
  wgpuCommandEncoderCopyTextureToBuffer(encoder, &ti, &bi, &extent);
  if (frame_proof) {
    ti.texture = depth_texture;
    ti.aspect = WGPUTextureAspect_DepthOnly;
    bi.layout.offset = copy_bytes;
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &ti, &bi, &extent);
  }

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
  std::vector<unsigned char> packed(std::size_t(view_width) * view_height * 4),
      rgb(std::size_t(view_width) * view_height * 3);
  for (std::size_t y = 0; y < view_height; ++y)
    for (std::size_t x = 0; x < view_width; ++x) {
      std::memcpy(packed.data() + (y * view_width + x) * 4,
                  rgba + y * row_bytes + x * 4, 4);
      std::memcpy(rgb.data() + (y * view_width + x) * 3,
                  rgba + y * row_bytes + x * 4, 3);
    }
  std::ofstream output(argv[2], std::ios::binary);
  if (!output)
    fail("cannot open output");
  output << "P6\n" << view_width << " " << view_height << "\n255\n";
  output.write(reinterpret_cast<const char *>(rgb.data()), rgb.size());
  if (!output)
    fail("cannot write output");
  if (frame_proof) {
    std::ofstream colour_file(std::string(argv[2]) + ".rgba", std::ios::binary);
    colour_file.write(reinterpret_cast<const char *>(packed.data()),
                      packed.size());
    std::ofstream depth_file(std::string(argv[2]) + ".depth", std::ios::binary);
    for (std::size_t y = 0; y < view_height; ++y)
      depth_file.write(
          reinterpret_cast<const char *>(rgba + copy_bytes + y * row_bytes),
          view_width * 4);
    if (!colour_file || !depth_file)
      fail("cannot write proof buffers");
  }
  wgpuBufferUnmap(readback);
  wgpuCommandBufferRelease(command);
  wgpuCommandEncoderRelease(encoder);
  wgpuBufferRelease(readback);
  wgpuTextureViewRelease(view);
  wgpuTextureRelease(target);
  if (depth_view)
    wgpuTextureViewRelease(depth_view);
  if (depth_texture)
    wgpuTextureRelease(depth_texture);
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
