// GPU-only PSMCT32/PSMZ24 attachment handoff. Hand-written; no game data.
#pragma once
#include <webgpu/webgpu.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace native_target {
inline WGPUStringView text(const char *s) { return {s, std::strlen(s)}; }
// Resources persist, contents do not: every begin imports authoritative VRAM,
// and every finish resolves before the fallback can read or write that VRAM.
// Callers must submit queued fallback commands before begin, on the same queue.
class Bridge {
    WGPUDevice device_ = nullptr;
    WGPUBuffer params_ = nullptr;
    WGPUComputePipeline load_ = nullptr, store_ = nullptr;
    WGPURenderPipeline depth_load_ = nullptr;
    WGPUTexture colour_ = nullptr, depth_ = nullptr;
    WGPUTextureView colour_view_ = nullptr, depth_view_ = nullptr;
    std::uint32_t width_ = 0, height_ = 0;
    std::uint64_t allocations_ = 0;
    static constexpr const char *shader_ = R"(
struct Config { v:array<vec4<u32>,12> }
@group(0) @binding(0) var<storage,read_write> vram:array<atomic<u32>>;
@group(0) @binding(1) var<storage,read> tables:array<u32>;
@group(0) @binding(2) var<uniform> config:Config;
@group(0) @binding(3) var output:texture_storage_2d<rgba8unorm,write>;
@group(0) @binding(4) var colour:texture_2d<f32>;
@group(0) @binding(5) var depth:texture_depth_2d;
fn cfg(i:u32)->u32 {return config.v[i/4u][i%4u];}
fn addr(i:u32,p:vec2<u32>)->u32 {
    let bits=cfg(i+3u); let px=bits&15u;let py=(bits>>4u)&15u;
    let offset=tables[cfg(i+2u)+((p.y&((1u<<py)-1u))<<px)+(p.x&((1u<<px)-1u))];
    return (cfg(i)+((p.x>>px)+(p.y>>py)*cfg(i+1u))*8192u+offset)>>2u;
}
@compute @workgroup_size(8,8) fn load_colour(@builtin(global_invocation_id) id:vec3<u32>) {
    if(id.x>=cfg(40u)||id.y>=cfg(41u)){return;}
    textureStore(output,vec2<i32>(id.xy),unpack4x8unorm(atomicLoad(&vram[addr(30u,id.xy)])));
}
@compute @workgroup_size(8,8) fn store_surfaces(@builtin(global_invocation_id) id:vec3<u32>) {
    if(id.x>=cfg(40u)||id.y>=cfg(41u)){return;}
    atomicStore(&vram[addr(30u,id.xy)],pack4x8unorm(textureLoad(colour,vec2<i32>(id.xy),0)));
    let location=addr(35u,id.xy);
    let z=min(16777215u,u32(round(clamp(textureLoad(depth,vec2<i32>(id.xy),0),0.0,1.0)*16777216.0)));
    // Power-of-two normalization preserves every 24-bit integer exactly.
    // PSMZ24 must not destroy the upper byte (it can contain unrelated data).
    let old=atomicLoad(&vram[location]);
    atomicStore(&vram[location],(old&0xff000000u)|(z&0xffffffu));
}
@vertex fn full(@builtin(vertex_index) i:u32)->@builtin(position) vec4<f32> {
    let p=array<vec2<f32>,3>(vec2<f32>(-1,-1),vec2<f32>(3,-1),vec2<f32>(-1,3));
    return vec4<f32>(p[i],0,1);
}
@fragment fn load_depth(@builtin(position) p:vec4<f32>)->@builtin(frag_depth) f32 {
    return f32(atomicLoad(&vram[addr(35u,vec2<u32>(p.xy))])&0xffffffu)/16777216.0;
}
)";
    void release_targets() {
        if(colour_view_) wgpuTextureViewRelease(colour_view_);
        if(depth_view_) wgpuTextureViewRelease(depth_view_);
        if(colour_) wgpuTextureRelease(colour_);
        if(depth_) wgpuTextureRelease(depth_);
        colour_=depth_=nullptr;colour_view_=depth_view_=nullptr;
        width_=height_=0;
    }
    WGPUBindGroup group(WGPUBindGroupLayout layout, WGPUBuffer vram, WGPUBuffer tables,
                        bool loading, bool depth_only = false) {
        WGPUBindGroupEntry e[6] = {};
        unsigned n=0;
        const auto buffer=[&](unsigned binding,WGPUBuffer b,std::uint64_t size) {
            auto &v=e[n++];v=WGPU_BIND_GROUP_ENTRY_INIT;v.binding=binding;v.buffer=b;v.size=size;
        };
        buffer(0,vram,4u<<20);buffer(1,tables,WGPU_WHOLE_SIZE);buffer(2,params_,192);
        if(!depth_only) {
            auto &c=e[n++];c=WGPU_BIND_GROUP_ENTRY_INIT;c.binding=loading?3:4;c.textureView=colour_view_;
            if(!loading) {auto &z=e[n++];z=WGPU_BIND_GROUP_ENTRY_INIT;z.binding=5;z.textureView=depth_view_;}
        }
        WGPUBindGroupDescriptor d=WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        d.layout=layout;d.entryCount=n;d.entries=e;
        return wgpuDeviceCreateBindGroup(device_,&d);
    }
    void compute(WGPUCommandEncoder encoder,WGPUComputePipeline pipeline,WGPUBindGroup group) {
        auto pass=wgpuCommandEncoderBeginComputePass(encoder,nullptr);
        wgpuComputePassEncoderSetPipeline(pass,pipeline);
        wgpuComputePassEncoderSetBindGroup(pass,0,group,0,nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass,(width_+7)/8,(height_+7)/8,1);
        wgpuComputePassEncoderEnd(pass);wgpuComputePassEncoderRelease(pass);
    }
public:
    Bridge() = default;
    Bridge(const Bridge&) = delete;
    Bridge &operator=(const Bridge&) = delete;
    ~Bridge() { reset(); }
    // Strict, conservative gate: bounded, non-wrapping, distinct surfaces.
    static bool supported(const std::uint32_t *s) {
        const auto w=std::uint64_t(s[2])*64,h=std::uint64_t(s[11]>>16)+1;
        if(s[3]!=0 || s[4]!=0 || s[6]!=0x31 || s[23]!=0xffffff ||
           !w || w>2048 || h>2048 || w*h>(1u<<20) ||
           (s[10]&65535) || (s[11]&65535) || (s[10]>>16)!=w-1) return false;
        std::uint64_t lo[2],hi[2];
        for(unsigned j=0;j<2;++j) {
            auto i=30+j*5;
            // Exactly 32-bit words in 64x32 pages; reject unexpected descriptors.
            if(s[i+2]!=(j?2048u:0u) || s[i+3]!=0x56 || s[i+4]!=(j?0xffffffu:0xffffffffu) ||
               (s[i]&8191) || s[i+1] != s[2]) return false;
            lo[j]=s[i];hi[j]=lo[j]+(((w-1)/64)+((h-1)/32)*s[i+1]+1)*8192;
            if(hi[j]>(1u<<22))return false;
        }
        return hi[0]<=lo[1] || hi[1]<=lo[0];
    }
    bool init(WGPUDevice device) {
        if(device_==device)return true;
        reset();device_=device;
        WGPUShaderSourceWGSL source=WGPU_SHADER_SOURCE_WGSL_INIT;source.code=text(shader_);
        WGPUShaderModuleDescriptor md=WGPU_SHADER_MODULE_DESCRIPTOR_INIT;md.nextInChain=&source.chain;
        auto module=wgpuDeviceCreateShaderModule(device_,&md);
        const auto pipeline=[&](const char *entry) {
            WGPUComputePipelineDescriptor p=WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
            p.compute.module=module;p.compute.entryPoint=text(entry);
            return wgpuDeviceCreateComputePipeline(device_,&p);
        };
        load_=pipeline("load_colour");store_=pipeline("store_surfaces");
        WGPUFragmentState fragment=WGPU_FRAGMENT_STATE_INIT;
        fragment.module=module;fragment.entryPoint=text("load_depth");
        WGPUDepthStencilState depth=WGPU_DEPTH_STENCIL_STATE_INIT;
        depth.format=WGPUTextureFormat_Depth32Float;depth.depthCompare=WGPUCompareFunction_Always;
        depth.depthWriteEnabled=WGPUOptionalBool_True;
        WGPURenderPipelineDescriptor p=WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        p.vertex.module=module;p.vertex.entryPoint=text("full");p.fragment=&fragment;p.depthStencil=&depth;
        depth_load_=wgpuDeviceCreateRenderPipeline(device_,&p);
        wgpuShaderModuleRelease(module);
        WGPUBufferDescriptor b=WGPU_BUFFER_DESCRIPTOR_INIT;b.size=192;
        b.usage=WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst;
        params_=wgpuDeviceCreateBuffer(device_,&b);
        return load_ && store_ && depth_load_ && params_;
    }
    void reset() {
        release_targets();
        if(load_)wgpuComputePipelineRelease(load_);
        if(store_)wgpuComputePipelineRelease(store_);
        if(depth_load_)wgpuRenderPipelineRelease(depth_load_);
        if(params_)wgpuBufferRelease(params_);
        load_=store_=nullptr;depth_load_=nullptr;params_=nullptr;device_=nullptr;
    }
    bool begin(WGPUQueue queue,WGPUCommandEncoder encoder,WGPUBuffer vram,
               WGPUBuffer tables,const std::uint32_t *setup) {
        if(!supported(setup)||!device_)return false;
        auto w=setup[2]*64,h=(setup[11]>>16)+1;
        if(w!=width_ || h!=height_) {
            release_targets();width_=w;height_=h;
            WGPUTextureDescriptor d=WGPU_TEXTURE_DESCRIPTOR_INIT;
            d.dimension=WGPUTextureDimension_2D;d.size={w,h,1};d.mipLevelCount=1;d.sampleCount=1;
            d.format=WGPUTextureFormat_RGBA8Unorm;
            d.usage=WGPUTextureUsage_StorageBinding|WGPUTextureUsage_TextureBinding|WGPUTextureUsage_RenderAttachment;
            colour_=wgpuDeviceCreateTexture(device_,&d);colour_view_=wgpuTextureCreateView(colour_,nullptr);
            d.format=WGPUTextureFormat_Depth32Float;
            d.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_RenderAttachment;
            depth_=wgpuDeviceCreateTexture(device_,&d);depth_view_=wgpuTextureCreateView(depth_,nullptr);
            ++allocations_;
        }
        std::uint32_t config[48]={};std::copy_n(setup,40,config);config[40]=w;config[41]=h;
        wgpuQueueWriteBuffer(queue,params_,0,config,sizeof config);
        auto layout=wgpuComputePipelineGetBindGroupLayout(load_,0);
        auto g=group(layout,vram,tables,true);compute(encoder,load_,g);
        wgpuBindGroupRelease(g);wgpuBindGroupLayoutRelease(layout);
        layout=wgpuRenderPipelineGetBindGroupLayout(depth_load_,0);
        g=group(layout,vram,tables,true,true);
        WGPURenderPassDepthStencilAttachment a=WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        a.view=depth_view_;a.depthLoadOp=WGPULoadOp_Clear;a.depthClearValue=0;a.depthStoreOp=WGPUStoreOp_Store;
        WGPURenderPassDescriptor rd=WGPU_RENDER_PASS_DESCRIPTOR_INIT;rd.depthStencilAttachment=&a;
        auto pass=wgpuCommandEncoderBeginRenderPass(encoder,&rd);
        wgpuRenderPassEncoderSetPipeline(pass,depth_load_);
        wgpuRenderPassEncoderSetBindGroup(pass,0,g,0,nullptr);
        wgpuRenderPassEncoderDraw(pass,3,1,0,0);
        wgpuRenderPassEncoderEnd(pass);wgpuRenderPassEncoderRelease(pass);
        wgpuBindGroupRelease(g);wgpuBindGroupLayoutRelease(layout);
        return true;
    }
    void finish(WGPUCommandEncoder encoder,WGPUBuffer vram,WGPUBuffer tables) {
        auto layout=wgpuComputePipelineGetBindGroupLayout(store_,0);
        auto g=group(layout,vram,tables,false);compute(encoder,store_,g);
        wgpuBindGroupRelease(g);wgpuBindGroupLayoutRelease(layout);
    }
    WGPUTextureView colour() const {return colour_view_;}
    WGPUTextureView depth() const {return depth_view_;}
    std::uint32_t width() const {return width_;}
    std::uint32_t height() const {return height_;}
    std::uint64_t allocations() const {return allocations_;}
};
} // namespace native_target
