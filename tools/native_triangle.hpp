// Experimental single native textured triangle, sharing live GPU VRAM.
#pragma once
#include "native_target_bridge.hpp"
#include <cmath>
#include <initializer_list>
#include <vector>

namespace native_target {
struct Vertex {float x,y,r,g,b,a,s,t,q,z;};
static_assert(sizeof(Vertex)==40);
class Triangle {
    WGPUDevice device_=nullptr;
    WGPUShaderModule module_=nullptr;
    WGPUComputePipeline copy_=nullptr;
    WGPURenderPipeline pipelines_[8]={};
    WGPUBuffer vertex_=nullptr,uniform_=nullptr,decoded_=nullptr,triangles_=nullptr;
    unsigned vertex_capacity_=3;
    WGPUTexture image_=nullptr;
    WGPUTextureView image_view_=nullptr;
    WGPUSampler samplers_[8]={};
    unsigned width_=0,height_=0;
    static constexpr const char *shader_=R"(
struct Out { @builtin(position) position:vec4<f32>, @location(0) colour:vec4<f32>, @location(1) stq:vec3<f32>, @location(2) @interpolate(flat) tri0:vec4<f32>, @location(3) @interpolate(flat) tri1:vec4<f32>, @location(4) @interpolate(flat) tri2:vec4<f32> }
struct Material { flags:u32, modes:u32, aref:u32, pad:u32, tri0:vec4<f32>, tri1:vec4<f32>, tri2:vec4<f32> }
@group(0) @binding(0) var image:texture_2d<f32>;
@group(0) @binding(1) var texture_sampler:sampler;
@group(0) @binding(2) var<uniform> material:Material;
@group(0) @binding(5) var<storage,read> triangles:array<vec4<f32>>;
@vertex fn vs(@builtin(vertex_index) vertex_index:u32,@location(0) p:vec2<f32>,@location(1) colour:vec4<f32>,@location(2) stq:vec3<f32>,@location(3) z:f32)->Out {
    var out:Out;out.position=vec4<f32>(p,z,1.0);out.colour=colour;out.stq=stq;let index=(vertex_index/3u)*3u;out.tri0=triangles[index];out.tri1=triangles[index+1u];out.tri2=triangles[index+2u];return out;
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
    let a=in.tri0;let b=in.tri1;
    let w0=((a.z-px)*(b.y-py)-(b.x-px)*(a.w-py))*b.z;
    let w1=((b.x-px)*(a.y-py)-(a.x-px)*(b.y-py))*b.z;
    let w2=1.0-w0-w1;
    let delta=b.w*w1+in.tri2.x*w2;
    out.depth=clamp(in.tri2.y+floor(delta),0.0,in.tri2.z)/16777216.0;
    return out;
}
@group(0) @binding(3) var<storage,read> decoded:array<u32>;
@group(0) @binding(4) var decoded_image:texture_storage_2d<rgba8unorm,write>;
@compute @workgroup_size(8,8) fn copy_texture(@builtin(global_invocation_id) id:vec3<u32>) {
    let size=textureDimensions(decoded_image);
    if(any(id.xy>=size)){return;}
    textureStore(decoded_image,vec2<i32>(id.xy),unpack4x8unorm(decoded[id.y*size.x+id.x]));
}
)";
    struct Material {std::uint32_t flags,modes,aref,pad;float tri[12];};
    WGPURenderPipeline pipeline(unsigned index) {
        if(pipelines_[index])return pipelines_[index];
        WGPUVertexAttribute a[4]={};
        a[0].format=WGPUVertexFormat_Float32x2;a[0].shaderLocation=0;
        a[1].format=WGPUVertexFormat_Float32x4;a[1].shaderLocation=1;a[1].offset=8;
        a[2].format=WGPUVertexFormat_Float32x3;a[2].shaderLocation=2;a[2].offset=24;
        a[3].format=WGPUVertexFormat_Float32;a[3].shaderLocation=3;a[3].offset=36;
        WGPUVertexBufferLayout vb=WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        vb.arrayStride=40;vb.stepMode=WGPUVertexStepMode_Vertex;vb.attributeCount=4;vb.attributes=a;
        WGPUColorTargetState c=WGPU_COLOR_TARGET_STATE_INIT;c.format=WGPUTextureFormat_RGBA8Unorm;c.writeMask=WGPUColorWriteMask_All;
        WGPUFragmentState f=WGPU_FRAGMENT_STATE_INIT;f.module=module_;f.entryPoint=text("fs_frame");f.targetCount=1;f.targets=&c;
        WGPUDepthStencilState d=WGPU_DEPTH_STENCIL_STATE_INIT;d.format=WGPUTextureFormat_Depth32Float;
        const WGPUCompareFunction comparisons[]={WGPUCompareFunction_Never,WGPUCompareFunction_Always,WGPUCompareFunction_GreaterEqual,WGPUCompareFunction_Greater};
        d.depthCompare=comparisons[index&3];d.depthWriteEnabled=(index&4)?WGPUOptionalBool_True:WGPUOptionalBool_False;
        WGPURenderPipelineDescriptor p=WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        p.vertex.module=module_;p.vertex.entryPoint=text("vs");p.vertex.bufferCount=1;p.vertex.buffers=&vb;
        p.primitive.topology=WGPUPrimitiveTopology_TriangleList;p.primitive.cullMode=WGPUCullMode_None;
        p.fragment=&f;p.depthStencil=&d;
        return pipelines_[index]=wgpuDeviceCreateRenderPipeline(device_,&p);
    }
    void release_image() {
        if(image_view_)wgpuTextureViewRelease(image_view_);
        if(image_)wgpuTextureRelease(image_);
        if(decoded_)wgpuBufferRelease(decoded_);
        image_view_=nullptr;image_=nullptr;decoded_=nullptr;width_=height_=0;
    }
public:
    Triangle()=default;
    Triangle(const Triangle&)=delete;
    Triangle &operator=(const Triangle&)=delete;
    ~Triangle(){reset();}
    static bool supported(const std::uint32_t *s) {
        if(!Bridge::supported(s) || !(s[0]&2) || !(s[0]&2048) || (s[0]&(4|128|4096)) ||
           (s[7]>>28)&3 || ((s[7]>>24)&3)>1 || ((s[7]>>26)&3)>1 ||
           ((s[15]&255)>10) || (s[15]>>8)>10 || ((s[0]&64) && ((s[7]>>4)&3)))return false;
        auto blend=(s[7]>>12)&255;
        if((s[0]&8) && ((blend&3)!=((blend>>2)&3) || ((blend>>6)&3)!=0))return false;
        switch(s[14]) {case 0:case 1:case 2:case 10:case 0x13:case 0x14:case 0x1b:case 0x24:case 0x2c:case 0x30:case 0x31:case 0x32:case 0x3a:break;default:return false;}
        const auto px=s[28]&15,py=(s[28]>>4)&15;
        if(!px||px>7||!py||py>7)return false;
        const auto w=1u<<(s[15]&255),h=1u<<(s[15]>>8);
        auto end=std::uint64_t(s[25])+(((w-1)>>px)+std::uint64_t((h-1)>>py)*s[26]+1)*8192;
        if(end>(4u<<20))return false;
        for(unsigned i:{30u,35u}) {
            auto surface_end=std::uint64_t(s[i])+(((s[2]*64-1)/64)+std::uint64_t((s[11]>>16)/32)*s[i+1]+1)*8192;
            if(s[25]<surface_end && s[i]<end)return false;
        }
        return true;
    }
    bool prepare(WGPUDevice device,unsigned w,unsigned h) {
        if(!w || !h || w>1024 || h>1024)return false;
        if(device_!=device) {
            reset();device_=device;
            WGPUShaderSourceWGSL src=WGPU_SHADER_SOURCE_WGSL_INIT;src.code=text(shader_);
            WGPUShaderModuleDescriptor md=WGPU_SHADER_MODULE_DESCRIPTOR_INIT;md.nextInChain=&src.chain;
            module_=wgpuDeviceCreateShaderModule(device_,&md);
            WGPUComputePipelineDescriptor cp=WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
            cp.compute.module=module_;cp.compute.entryPoint=text("copy_texture");copy_=wgpuDeviceCreateComputePipeline(device_,&cp);
            WGPUBufferDescriptor b=WGPU_BUFFER_DESCRIPTOR_INIT;b.size=120;b.usage=WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst;
            vertex_=wgpuDeviceCreateBuffer(device_,&b);b.size=64;b.usage=WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst;
            uniform_=wgpuDeviceCreateBuffer(device_,&b);
            b.size=48;b.usage=WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst;
            triangles_=wgpuDeviceCreateBuffer(device_,&b);vertex_capacity_=3;
        }
        if(w!=width_||h!=height_) {
            release_image();width_=w;height_=h;
            WGPUBufferDescriptor b=WGPU_BUFFER_DESCRIPTOR_INIT;b.size=std::uint64_t(w)*h*4;b.usage=WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst;
            decoded_=wgpuDeviceCreateBuffer(device_,&b);
            WGPUTextureDescriptor d=WGPU_TEXTURE_DESCRIPTOR_INIT;d.dimension=WGPUTextureDimension_2D;d.size={w,h,1};
            d.format=WGPUTextureFormat_RGBA8Unorm;d.mipLevelCount=1;d.sampleCount=1;
            d.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_StorageBinding;
            image_=wgpuDeviceCreateTexture(device_,&d);image_view_=wgpuTextureCreateView(image_,nullptr);
        }
        return module_&&copy_&&vertex_&&uniform_&&decoded_&&image_;
    }
    void reset() {
        release_image();
        for(auto &p:pipelines_) {if(p)wgpuRenderPipelineRelease(p);p=nullptr;}
        for(auto &s:samplers_) {if(s)wgpuSamplerRelease(s);s=nullptr;}
        if(vertex_)wgpuBufferRelease(vertex_);
        if(uniform_)wgpuBufferRelease(uniform_);
        if(triangles_)wgpuBufferRelease(triangles_);
        if(copy_)wgpuComputePipelineRelease(copy_);
        if(module_)wgpuShaderModuleRelease(module_);
        vertex_=uniform_=triangles_=nullptr;copy_=nullptr;module_=nullptr;device_=nullptr;
    }
    WGPUBuffer decoded() const {return decoded_;}
    // Input positions are raw GS screen coordinates, Z is an integer value.
    bool encode(WGPUQueue queue,WGPUCommandEncoder encoder,const Bridge &target,
                const Vertex *input,const std::uint32_t *setup) {
        return encode_many(queue,encoder,target,input,3,setup);
    }
    bool encode_many(WGPUQueue queue,WGPUCommandEncoder encoder,const Bridge &target,
                     const Vertex *input,unsigned count,const std::uint32_t *setup) {
        if(!count || count%3 || count>384)return false;
        Material m={setup[0],setup[7],setup[8],0,{}};
        std::vector<Vertex> vertices(input,input+count);
        std::vector<float> triangles(count*4);
        for(unsigned i=0;i<count;i+=3) {
            const auto &a=input[i],&b=input[i+1],&c=input[i+2];
            auto area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
            if(!std::isfinite(area)||std::abs(area)<1e-8f)return false;
            float tri[]={a.x,a.y,b.x,b.y,c.x,c.y,1/area,b.z-a.z,c.z-a.z,a.z,16777215.0f,0};
            std::copy_n(tri,12,triangles.data()+i*4);
        }
        if(count>vertex_capacity_) {
            wgpuBufferRelease(vertex_);wgpuBufferRelease(triangles_);vertex_capacity_=count;
            WGPUBufferDescriptor b=WGPU_BUFFER_DESCRIPTOR_INIT;b.size=count*40;b.usage=WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst;
            vertex_=wgpuDeviceCreateBuffer(device_,&b);b.size=count*16;b.usage=WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst;
            triangles_=wgpuDeviceCreateBuffer(device_,&b);
        }
        for(auto &v:vertices) {v.x=(v.x+0.5f)/target.width()*2-1;v.y=1-(v.y+0.5f)/target.height()*2;v.z/=16777216.0f;}
        wgpuQueueWriteBuffer(queue,vertex_,0,vertices.data(),count*40);wgpuQueueWriteBuffer(queue,uniform_,0,&m,sizeof m);
        wgpuQueueWriteBuffer(queue,triangles_,0,triangles.data(),count*16);
        auto layout=wgpuComputePipelineGetBindGroupLayout(copy_,0);
        WGPUBindGroupEntry ce[2]={};ce[0]=WGPU_BIND_GROUP_ENTRY_INIT;ce[0].binding=3;ce[0].buffer=decoded_;ce[0].size=WGPU_WHOLE_SIZE;
        ce[1]=WGPU_BIND_GROUP_ENTRY_INIT;ce[1].binding=4;ce[1].textureView=image_view_;
        WGPUBindGroupDescriptor bg=WGPU_BIND_GROUP_DESCRIPTOR_INIT;bg.layout=layout;bg.entryCount=2;bg.entries=ce;
        auto g=wgpuDeviceCreateBindGroup(device_,&bg);
        auto pass=wgpuCommandEncoderBeginComputePass(encoder,nullptr);wgpuComputePassEncoderSetPipeline(pass,copy_);
        wgpuComputePassEncoderSetBindGroup(pass,0,g,0,nullptr);wgpuComputePassEncoderDispatchWorkgroups(pass,(width_+7)/8,(height_+7)/8,1);
        wgpuComputePassEncoderEnd(pass);wgpuComputePassEncoderRelease(pass);wgpuBindGroupRelease(g);wgpuBindGroupLayoutRelease(layout);
        auto index=(setup[0]&512)?((setup[7]>>8)&3):1;
        if((setup[0]&512)&&!(setup[0]&32))index|=4;
        auto p=pipeline(index);layout=wgpuRenderPipelineGetBindGroupLayout(p,0);
        unsigned sampler_index=((setup[0]&16384)?4:0)|(((setup[7]>>24)&1)?1:0)|(((setup[7]>>26)&1)?2:0);
        if(!samplers_[sampler_index]) {
            WGPUSamplerDescriptor sd=WGPU_SAMPLER_DESCRIPTOR_INIT;
            sd.addressModeU=(sampler_index&1)?WGPUAddressMode_ClampToEdge:WGPUAddressMode_Repeat;
            sd.addressModeV=(sampler_index&2)?WGPUAddressMode_ClampToEdge:WGPUAddressMode_Repeat;
            sd.magFilter=sd.minFilter=(sampler_index&4)?WGPUFilterMode_Linear:WGPUFilterMode_Nearest;
            samplers_[sampler_index]=wgpuDeviceCreateSampler(device_,&sd);
        }
        WGPUBindGroupEntry e[4]={};for(unsigned i=0;i<4;++i) {e[i]=WGPU_BIND_GROUP_ENTRY_INIT;e[i].binding=i;}
        e[0].textureView=image_view_;e[1].sampler=samplers_[sampler_index];e[2].buffer=uniform_;e[2].size=64;e[3].binding=5;e[3].buffer=triangles_;e[3].size=count*16;
        bg.layout=layout;bg.entryCount=4;bg.entries=e;g=wgpuDeviceCreateBindGroup(device_,&bg);
        WGPURenderPassColorAttachment ca=WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        ca.view=target.colour();ca.loadOp=WGPULoadOp_Load;ca.storeOp=WGPUStoreOp_Store;
        WGPURenderPassDepthStencilAttachment za=WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        za.view=target.depth();za.depthLoadOp=WGPULoadOp_Load;za.depthStoreOp=WGPUStoreOp_Store;
        WGPURenderPassDescriptor rd=WGPU_RENDER_PASS_DESCRIPTOR_INIT;rd.colorAttachmentCount=1;rd.colorAttachments=&ca;rd.depthStencilAttachment=&za;
        auto rp=wgpuCommandEncoderBeginRenderPass(encoder,&rd);wgpuRenderPassEncoderSetPipeline(rp,p);
        wgpuRenderPassEncoderSetBindGroup(rp,0,g,0,nullptr);wgpuRenderPassEncoderSetVertexBuffer(rp,0,vertex_,0,count*40);
        wgpuRenderPassEncoderDraw(rp,count,1,0,0);wgpuRenderPassEncoderEnd(rp);wgpuRenderPassEncoderRelease(rp);
        wgpuBindGroupRelease(g);wgpuBindGroupLayoutRelease(layout);return true;
    }
};
} // namespace native_target
