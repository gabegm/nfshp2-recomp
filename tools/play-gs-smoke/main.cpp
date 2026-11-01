// Synthetic GS commands only; no game data, BIOS or CPU emulator.
#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include "GSH_VulkanOffscreen.h"
#include "AppConfig.h"
#include "GSH_VulkanDeviceInfo.h"
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
void transfer(CGSHandler& gs, unsigned psm, bool read, void* data, unsigned size) {
    const uint64 descriptor = (uint64(1)<<16) | (uint64(psm)<<24);
    reg(gs, GS_REG_BITBLTBUF, read ? descriptor : descriptor<<32);
    reg(gs, GS_REG_TRXPOS, 0);
    reg(gs, GS_REG_TRXREG, uint64(64) | (uint64(64)<<32));
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
        gs->Release();
        std::cout << "PASS synthetic GS hardware smoke test\n";
        return 0;
    } catch (const std::exception& e) {
        if(initialized) gs->Release();
        std::cerr << e.what() << '\n'; return 1;
    }
}
