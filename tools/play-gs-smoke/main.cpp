// Synthetic GS commands only; no game data, BIOS or CPU emulator.
#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include "GSH_VulkanOffscreen.h"
#include "AppConfig.h"
#include "GSH_VulkanDeviceInfo.h"
#include "MemStream.h"
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
namespace { fs::path dataPath; }
fs::path CAppConfig::GetBasePath() const { return dataPath; }
void requireVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) +
        " failed: " + std::to_string(result));
}
// Preflight before starting the upstream GS worker: fail cleanly on unsupported
// or software-only machines rather than relying on upstream debug assertions.
void selectHardware() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "NFS setup synthetic GS smoke test";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    VkInstance instance{};
    requireVk(vkCreateInstance(&create, nullptr, &instance), "vkCreateInstance");
    struct Cleanup { VkInstance value; ~Cleanup() { vkDestroyInstance(value,nullptr); } } cleanup{instance};
    uint32_t count=0;
    requireVk(vkEnumeratePhysicalDevices(instance,&count,nullptr), "Enumerate GPUs");
    std::vector<VkPhysicalDevice> devices(count);
    requireVk(vkEnumeratePhysicalDevices(instance,&count,devices.data()), "Enumerate GPUs");
    for (auto device : devices) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(device,&p);
        if (p.deviceType!=VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
            p.deviceType!=VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) continue;
        if (p.apiVersion<VK_API_VERSION_1_2) continue;
        uint32_t n=0;
        requireVk(vkEnumerateDeviceExtensionProperties(device,nullptr,&n,nullptr), "Enumerate extensions");
        std::vector<VkExtensionProperties> extensions(n);
        requireVk(vkEnumerateDeviceExtensionProperties(device,nullptr,&n,extensions.data()), "Enumerate extensions");
        bool interlockExtension=false, swapchainExtension=false;
        for(const auto& e : extensions) {
            interlockExtension |= std::strcmp(e.extensionName,VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME)==0;
            swapchainExtension |= std::strcmp(e.extensionName,VK_KHR_SWAPCHAIN_EXTENSION_NAME)==0;
        }
        if (!interlockExtension || !swapchainExtension) continue;
        VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT interlock{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT};
        VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDevice16BitStorageFeatures storage{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext=&storage; storage.pNext=&v12; v12.pNext=&interlock;
        vkGetPhysicalDeviceFeatures2(device,&features);
        if (!features.features.fragmentStoresAndAtomics || !features.features.shaderInt16 ||
            !interlock.fragmentShaderPixelInterlock || !v12.shaderInt8 ||
            !v12.storageBuffer8BitAccess || !v12.uniformAndStorageBuffer8BitAccess ||
            !storage.storageBuffer16BitAccess || !storage.uniformAndStorageBuffer16BitAccess) continue;
        // Upstream currently creates queue family zero. Reject a device on which
        // that family cannot perform graphics rather than silently choosing it.
        uint32_t families=0;
        vkGetPhysicalDeviceQueueFamilyProperties(device,&families,nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);
        vkGetPhysicalDeviceQueueFamilyProperties(device,&families,queues.data());
        if (queues.empty() || !queues[0].queueCount || !(queues[0].queueFlags&VK_QUEUE_GRAPHICS_BIT)) continue;
        GSH_Vulkan::VULKAN_DEVICE selected;
        selected.deviceName=p.deviceName; selected.vendorId=p.vendorID; selected.deviceId=p.deviceID;
        GSH_Vulkan::CDeviceInfo::GetInstance().SetSelectedDevice(selected);
        return;
    }
    throw std::runtime_error("No hardware GPU supports the pinned desktop GS backend requirements");
}
class SmokeGS final : public CGSH_VulkanOffscreen {
public:
    void checkHardware() {
        SendGSCall([this] {
            VkPhysicalDeviceProperties p{};
            m_instance.vkGetPhysicalDeviceProperties(m_context->physicalDevice, &p);
            std::cout << "Vulkan adapter: " << p.deviceName << std::endl;
            hardware = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ||
                       p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        }, true);
        if (!hardware) throw std::runtime_error("Hardware GPU required");
    }
    std::vector<uint8> snapshot() {
        std::vector<uint8> result;
        SendGSCall([&] { SyncMemoryCache(); result.assign(GetRam(),GetRam()+RAMSIZE); },true);
        return result;
    }
private:
    bool hardware = false;
};
void reg(CGSHandler& gs, uint8 id, uint64 value) {
    gs.WriteRegister({id, value});
    gs.ProcessWriteBuffer(nullptr);
}
uint64 pos(unsigned x, unsigned y) { return uint64(x*16) | (uint64(y*16)<<16); }
uint16 pack16(uint32 c) {
    return ((c>>3)&31) | (((c>>11)&31)<<5) | (((c>>19)&31)<<10) | ((c>>16)&0x8000);
}
void transfer(CGSHandler& gs, unsigned psm, bool read, void* data, unsigned size,
              unsigned base=0, unsigned width=64, unsigned height=64) {
    const uint64 descriptor = (base/256) | (uint64(1)<<16) | (uint64(psm)<<24);
    reg(gs, GS_REG_BITBLTBUF, read ? descriptor : descriptor<<32);
    reg(gs, GS_REG_TRXPOS, 0);
    reg(gs, GS_REG_TRXREG, uint64(width) | (uint64(height)<<32));
    reg(gs, GS_REG_TRXDIR, read ? 1 : 0);
    if (read) gs.ReadImageData(data, size);
    else gs.FeedImageData(data, size);
}
void drawCase(SmokeGS& gs, unsigned psm, bool blend) {
    constexpr uint32 background = 0x80605040; // RGBA = 64,80,96,128
    constexpr uint32 source = 0x402070c0;     // RGBA = 192,112,32,64
    constexpr uint32 blended = 0x40406080;    // exact half blend; source alpha
    std::vector<uint32> pixels32(64*64, background);
    std::vector<uint16> pixels16(64*64, pack16(background));
    if (psm==0) transfer(gs, psm, false, pixels32.data(), pixels32.size()*4);
    else transfer(gs, psm, false, pixels16.data(), pixels16.size()*2);
    reg(gs, GS_REG_FRAME_1, (uint64(1)<<16) | (uint64(psm)<<24));
    reg(gs, GS_REG_ZBUF_1, uint64(1)<<32); // masked depth writes
    reg(gs, GS_REG_XYOFFSET_1, 0);
    reg(gs, GS_REG_SCISSOR_1, (uint64(63)<<16) | (uint64(63)<<48));
    reg(gs, GS_REG_TEST_1, (uint64(1)<<16) | (uint64(1)<<17)); // depth always
    reg(gs, GS_REG_PRMODECONT, 1);
    reg(gs, GS_REG_COLCLAMP, 1);
    reg(gs, 0x45, 0); // DTHE disabled
    reg(gs, GS_REG_ALPHA_1, 0x44); // (Cs-Cd)*As/128+Cd
    reg(gs, GS_REG_PRIM, 6 | (blend ? 0x40 : 0));
    reg(gs, GS_REG_RGBAQ, uint64(source) | (uint64(0x3f800000)<<32));
    reg(gs, GS_REG_XYZ2, pos(8,8));
    reg(gs, GS_REG_XYZ2, pos(24,24));
    gs.Finish(true);
    if (psm==0) transfer(gs, psm, true, pixels32.data(), pixels32.size()*4);
    else transfer(gs, psm, true, pixels16.data(), pixels16.size()*2);
    for (unsigned y=0;y<64;y++) for(unsigned x=0;x<64;x++) {
        const bool inside = x>=8 && x<24 && y>=8 && y<24;
        uint32 expected = inside ? (blend ? blended : source) : background;
        uint32 actual = psm==0 ? pixels32[y*64+x] : pixels16[y*64+x];
        if(psm!=0) expected = pack16(expected);
        if(actual!=expected) {
            std::cerr << "Mismatch PSM=" << psm << " blend=" << blend
                      << " x=" << x << " y=" << y << " expected=" << std::hex
                      << expected << " actual=" << actual << std::dec << std::endl;
            throw std::runtime_error("Synthetic framebuffer comparison failed");
        }
    }
    std::cout << "PASS PSM=" << psm << " blend=" << blend << " (4096 pixels)" << std::endl;
}

void setupDraw(SmokeGS& gs, unsigned depthFormat=0, bool depthWrite=false) {
    reg(gs, GS_REG_FRAME_1, uint64(1)<<16);
    reg(gs, GS_REG_ZBUF_1, (0x10000/8192) | (uint64(depthFormat)<<24) |
        (uint64(!depthWrite)<<32));
    reg(gs, GS_REG_XYOFFSET_1, 0);
    reg(gs, GS_REG_SCISSOR_1, (uint64(63)<<16) | (uint64(63)<<48));
    reg(gs, GS_REG_TEST_1, (uint64(1)<<16) | (uint64(1)<<17));
    reg(gs, GS_REG_PRMODECONT, 1);
    reg(gs, GS_REG_COLCLAMP, 1);
    reg(gs, GS_REG_PABE, 0);
}
void sprite(SmokeGS& gs, unsigned x0,unsigned y0,unsigned x1,unsigned y1,
            uint32 color,uint32 z=0) {
    reg(gs, GS_REG_PRIM, 6);
    reg(gs, GS_REG_RGBAQ, uint64(color) | (uint64(0x3f800000)<<32));
    reg(gs, GS_REG_XYZ2, pos(x0,y0) | (uint64(z)<<32));
    reg(gs, GS_REG_XYZ2, pos(x1,y1) | (uint64(z)<<32));
}
void compare32(SmokeGS& gs, const std::vector<uint32>& expected, const char* label) {
    gs.Finish(true);
    std::vector<uint32> actual(64*64);
    transfer(gs,0,true,actual.data(),actual.size()*4);
    for(unsigned i=0;i<actual.size();i++) if(actual[i]!=expected[i]) {
        std::cerr << label << " mismatch x=" << i%64 << " y=" << i/64
                  << " expected=" << std::hex << expected[i] << " actual=" << actual[i]
                  << std::dec << std::endl;
        throw std::runtime_error("Synthetic comparison failed");
    }
    std::cout << "PASS " << label << " (4096 pixels)" << std::endl;
}
void depthCase(SmokeGS& gs, unsigned format) {
    setupDraw(gs,format,true);
    sprite(gs,0,0,64,64,0x80605040,100);
    reg(gs, GS_REG_TEST_1, (uint64(1)<<16) | (uint64(3)<<17)); // GREATER
    sprite(gs,8,8,24,24,0x80402010,200);
    sprite(gs,8,8,24,24,0x801020f0,150); // must fail
    sprite(gs,12,12,20,20,0x80c08040,300);
    sprite(gs,14,14,18,18,0x801020f0,300); // equality must fail
    reg(gs, GS_REG_TEST_1, (uint64(1)<<16) | (uint64(2)<<17)); // GEQUAL
    sprite(gs,14,14,18,18,0x80808080,300); // equality must pass
    std::vector<uint32> expected(64*64,0x80605040);
    for(unsigned y=8;y<24;y++) for(unsigned x=8;x<24;x++) expected[y*64+x]=0x80402010;
    for(unsigned y=12;y<20;y++) for(unsigned x=12;x<20;x++) expected[y*64+x]=0x80c08040;
    for(unsigned y=14;y<18;y++) for(unsigned x=14;x<18;x++) expected[y*64+x]=0x80808080;
    auto label="depth format "+std::to_string(format);
    compare32(gs,expected,label.c_str());
}
void textureCase(SmokeGS& gs, bool indexed, unsigned paletteVersion=0, bool upload=true) {
    constexpr unsigned textureBase=0x20000, paletteBase=0x30000;
    std::vector<uint32> texels(64*64);
    std::vector<uint8> indices(64*64/2);
    std::vector<uint32> palette(16);
    for(unsigned i=0;i<16;i++) palette[i]=0x80000000u | (i*8) | ((120-i*4)<<8) |
        (((i*13+paletteVersion*40)%256)<<16);
    for(unsigned y=0;y<64;y++) for(unsigned x=0;x<64;x++) {
        unsigned i=y*64+x;
        texels[i]=0x80000000u | ((x*3)%256) | (((y*5)%256)<<8) | (((x+y)*2%256)<<16);
        indices[i/2] |= ((x+y)%16) << ((i%2)*4);
    }
    if(upload && indexed) {
        transfer(gs,0x14,false,indices.data(),indices.size(),textureBase);
        transfer(gs,0,false,palette.data(),palette.size()*4,paletteBase,8,2); // CSM1 entries 0..15 occupy an 8-by-2 block
    } else if(upload) transfer(gs,0,false,texels.data(),texels.size()*4,textureBase);
    setupDraw(gs);
    sprite(gs,0,0,64,64,0x80605040);
    reg(gs, GS_REG_TEXFLUSH, 0);
    uint64 tex0=(textureBase/256) | (uint64(1)<<14) | (uint64(indexed?0x14:0)<<20) |
        (uint64(6)<<26) | (uint64(6)<<30) | (uint64(1)<<34) | (uint64(1)<<35);
    if(indexed) tex0 |= (uint64(paletteBase/256)<<37) | (uint64(1)<<61); // CSM1, CLD=1
    reg(gs, GS_REG_TEX0_1,tex0);
    reg(gs, GS_REG_TEX1_1,0); // nearest
    reg(gs, GS_REG_CLAMP_1,0); // repeat
    reg(gs, GS_REG_PRIM,6 | 0x10 | 0x100); // textured sprite, fixed UV
    reg(gs, GS_REG_UV,pos(2,3));
    reg(gs, GS_REG_XYZ2,pos(8,8));
    reg(gs, GS_REG_UV,pos(18,19));
    reg(gs, GS_REG_XYZ2,pos(24,24));
    std::vector<uint32> expected(64*64,0x80605040);
    for(unsigned y=8;y<24;y++) for(unsigned x=8;x<24;x++) {
        unsigned u=x-8+2,v=y-8+3;
        expected[y*64+x]=indexed ? palette[(u+v)%16] : texels[v*64+u];
    }
    auto label=indexed ? "PSMT4 palette version "+std::to_string(paletteVersion) : "PSMCT32 texture";
    compare32(gs,expected,label.c_str());
}

void triangleDepthCase(SmokeGS& gs, bool reverse) {
    setupDraw(gs,0,true);
    sprite(gs,0,0,64,64,0x80605040,0);
    reg(gs, GS_REG_PRIM,3);
    reg(gs, GS_REG_RGBAQ,uint64(0x80402010) | (uint64(0x3f800000)<<32));
    const uint64 vertices[]={pos(8,8)|(uint64(10000)<<32),
        pos(24,8)|(uint64(20000)<<32),pos(8,24)|(uint64(30000)<<32)};
    reg(gs, GS_REG_XYZ2,vertices[0]);
    reg(gs, GS_REG_XYZ2,vertices[reverse?2:1]);
    reg(gs, GS_REG_XYZ2,vertices[reverse?1:2]);
    gs.Finish(true);
    std::vector<uint32> depth(64*64);
    transfer(gs,0x30,true,depth.data(),depth.size()*4,0x10000);
    std::vector<uint32> color(64*64,0x80605040);
    for(unsigned y=0;y<64;y++) for(unsigned x=0;x<64;x++) {
        // Integer sample positions, top/left edges included, diagonal excluded.
        const bool inside=x>=8 && y>=8 && x+y<32;
        const uint32 expected=inside ? 10000+(x-8)*625+(y-8)*1250 : 0;
        if(depth[y*64+x]!=expected) {
            std::cerr << "Triangle depth mismatch reverse=" << reverse << " x=" << x
                      << " y=" << y << " expected=" << expected << " actual=" << depth[y*64+x] << std::endl;
            throw std::runtime_error("Triangle coverage/depth interpolation failed");
        }
        if(inside) color[y*64+x]=0x80402010;
    }
    compare32(gs,color,reverse ? "reverse triangle coverage/depth" : "triangle coverage/depth");
}

void stateCase(SmokeGS& gs) {
    gs.Finish(true);
    const auto expected=gs.snapshot();
    std::array<uint64,CGSHandler::REGISTER_MAX> registers{};
    std::memcpy(registers.data(),gs.GetRegisters(),sizeof(registers));
    Framework::CMemStream stream;
    {
        Framework::CZipArchiveWriter writer;
        gs.SaveState(writer);
        writer.Write(stream);
    }
    textureCase(gs,true,2); // mutate framebuffer, texture and palette contents
    gs.Reset();
    stream.Seek(0,Framework::STREAM_SEEK_SET);
    Framework::CZipArchiveReader reader(stream);
    gs.LoadState(reader);
    gs.SendGSCall([]{},true); // wait for GPU memory restoration
    if(gs.snapshot()!=expected || std::memcmp(registers.data(),gs.GetRegisters(),sizeof(registers)))
        throw std::runtime_error("State restoration comparison failed");
    std::cout << "PASS state round trip (4 MiB VRAM and all GS registers)" << std::endl;
    textureCase(gs,true,1,false); // draw from restored texture/palette without reuploading
}

int main(int argc, char** argv) {
    if(argc!=2) { std::cerr << "Usage: play-gs-smoke PRIVATE_DATA_DIRECTORY\n"; return 2; }
    try {
        dataPath=fs::absolute(argv[1]);
        fs::create_directories(dataPath);
        selectHardware();
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    auto gs = std::make_unique<SmokeGS>();
    bool initialized = false;
    try {
        gs->Initialize(); initialized=true;
        gs->Reset();
        gs->checkHardware();
        for(unsigned psm : {0u, 2u}) for(bool blend : {false,true}) drawCase(*gs,psm,blend);
        for(unsigned format : {0u,1u,2u,10u}) depthCase(*gs,format);
        triangleDepthCase(*gs,false);
        triangleDepthCase(*gs,true);
        textureCase(*gs,false);
        textureCase(*gs,true);
        textureCase(*gs,true,1);
        stateCase(*gs);
        gs->Release();
        std::cout << "PASS synthetic GS hardware smoke test\n";
        return 0;
    } catch (const std::exception& e) {
        if(initialized) gs->Release();
        std::cerr << e.what() << '\n'; return 1;
    }
}
