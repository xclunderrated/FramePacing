// vkspinner: hand-rolled minimal Vulkan presenter (no SDK headers required).
// Acquire + present, no pipeline -- the only goal is an uncapped
// vkQueuePresentKHR stream to validate the Vulkan hook path.
// Exit code 9 = no Vulkan runtime/driver on this machine (skip, not a bug).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <windows.h>

// ---- minimal Vulkan decls (verbatim layouts from the Vulkan spec) ----------
typedef std::uint32_t VkFlags;
typedef std::uint32_t VkBool32;
typedef std::uint32_t VkStructureType;
typedef std::int32_t VkResult;
typedef struct VkInstance_T* VkInstance;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkDevice_T* VkDevice;
typedef struct VkQueue_T* VkQueue;
typedef std::uint64_t VkSurfaceKHR;
typedef std::uint64_t VkSwapchainKHR;
typedef std::uint64_t VkSemaphore;
typedef std::uint64_t VkFence;

constexpr VkStructureType VK_STRUCTURE_TYPE_APPLICATION_INFO = 0;
constexpr VkStructureType VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1;
constexpr VkStructureType VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO = 2;
constexpr VkStructureType VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO = 3;
constexpr VkStructureType VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO = 9;
constexpr VkStructureType VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR = 1000009000;
constexpr VkStructureType VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR = 1000001000;
constexpr VkStructureType VK_STRUCTURE_TYPE_PRESENT_INFO_KHR = 1000001001;
constexpr std::uint32_t VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT = 0x10;
constexpr std::uint32_t VK_SHARING_MODE_EXCLUSIVE = 0;
constexpr std::uint32_t VK_PRESENT_MODE_IMMEDIATE_KHR = 0;
constexpr std::uint32_t VK_PRESENT_MODE_MAILBOX_KHR = 1;
constexpr std::uint32_t VK_PRESENT_MODE_FIFO_KHR = 2;
constexpr std::uint32_t VK_FORMAT_R8G8B8A8_UNORM = 37;
constexpr std::uint32_t VK_FORMAT_B8G8R8A8_UNORM = 44;
constexpr std::uint32_t VK_FORMAT_UNDEFINED = 0;
constexpr std::uint32_t VK_COLOR_SPACE_SRGB_NONLINEAR_KHR = 0;
constexpr std::uint32_t VK_QUEUE_GRAPHICS_BIT = 1;
constexpr std::uint32_t VK_API_VERSION_1_0 = (1u << 12);

struct VkExtent2D { std::uint32_t width, height; };
struct VkExtent3D { std::uint32_t width, height, depth; };
struct VkApplicationInfo {
    VkStructureType sType; const void* pNext; const char* pApplicationName;
    std::uint32_t applicationVersion; const char* pEngineName;
    std::uint32_t engineVersion; std::uint32_t apiVersion;
};
struct VkInstanceCreateInfo {
    VkStructureType sType; const void* pNext; VkFlags flags;
    const VkApplicationInfo* pApplicationInfo; std::uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames; std::uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
};
struct VkWin32SurfaceCreateInfoKHR {
    VkStructureType sType; const void* pNext; VkFlags flags;
    HINSTANCE hinstance; HWND hwnd;
};
struct VkQueueFamilyProperties {
    VkFlags queueFlags; std::uint32_t queueCount; std::uint32_t timestampValidBits;
    VkExtent3D minImageTransferGranularity;
};
struct VkDeviceQueueCreateInfo {
    VkStructureType sType; const void* pNext; VkFlags flags;
    std::uint32_t queueFamilyIndex; std::uint32_t queueCount; const float* pQueuePriorities;
};
struct VkDeviceCreateInfo {
    VkStructureType sType; const void* pNext; VkFlags flags;
    std::uint32_t queueCreateInfoCount; const VkDeviceQueueCreateInfo* pQueueCreateInfos;
    std::uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames;
    std::uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames;
    const void* pEnabledFeatures;
};
struct VkSurfaceCapabilitiesKHR {
    std::uint32_t minImageCount; std::uint32_t maxImageCount;
    VkExtent2D currentExtent; VkExtent2D minImageExtent; VkExtent2D maxImageExtent;
    std::uint32_t maxImageArrayLayers; VkFlags supportedTransforms;
    VkFlags currentTransform; VkFlags supportedCompositeAlpha;
    VkFlags supportedUsageFlags;
};
struct VkSurfaceFormatKHR { std::uint32_t format; std::uint32_t colorSpace; };
struct VkSwapchainCreateInfoKHR {
    VkStructureType sType; const void* pNext; VkFlags flags; VkSurfaceKHR surface;
    std::uint32_t minImageCount; std::uint32_t imageFormat; std::uint32_t imageColorSpace;
    VkExtent2D imageExtent; std::uint32_t imageArrayLayers; VkFlags imageUsage;
    std::uint32_t imageSharingMode; std::uint32_t queueFamilyIndexCount;
    const std::uint32_t* pQueueFamilyIndices; std::uint32_t preTransform;
    std::uint32_t compositeAlpha; std::uint32_t presentMode; VkBool32 clipped;
    VkSwapchainKHR oldSwapchain;
};
struct VkPresentInfoKHR {
    VkStructureType sType; const void* pNext; std::uint32_t waitSemaphoreCount;
    const VkSemaphore* pWaitSemaphores; std::uint32_t swapchainCount;
    const VkSwapchainKHR* pSwapchains; const std::uint32_t* pImageIndices;
    VkResult* pResults;
};
struct VkSemaphoreCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; };

#define VKFN(name) PFN_##name name
typedef void* (*PFN_vkGetInstanceProcAddr)(VkInstance, const char*);
typedef VkResult (*PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const void*, VkInstance*);
typedef VkResult (*PFN_vkCreateWin32SurfaceKHR)(VkInstance, const VkWin32SurfaceCreateInfoKHR*, const void*, VkSurfaceKHR*);
typedef VkResult (*PFN_vkEnumeratePhysicalDevices)(VkInstance, std::uint32_t*, VkPhysicalDevice*);
typedef void (*PFN_vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice, std::uint32_t*, VkQueueFamilyProperties*);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceSupportKHR)(VkPhysicalDevice, std::uint32_t, VkSurfaceKHR, VkBool32*);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR*);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)(VkPhysicalDevice, VkSurfaceKHR, std::uint32_t*, VkSurfaceFormatKHR*);
typedef VkResult (*PFN_vkCreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo*, const void*, VkDevice*);
typedef void* (*PFN_vkGetDeviceProcAddr)(VkDevice, const char*);
typedef void (*PFN_vkGetDeviceQueue)(VkDevice, std::uint32_t, std::uint32_t, VkQueue*);
typedef VkResult (*PFN_vkCreateSemaphore)(VkDevice, const VkSemaphoreCreateInfo*, const void*, VkSemaphore*);
typedef VkResult (*PFN_vkCreateSwapchainKHR)(VkDevice, const VkSwapchainCreateInfoKHR*, const void*, VkSwapchainKHR*);
typedef VkResult (*PFN_vkGetSwapchainImagesKHR)(VkDevice, VkSwapchainKHR, std::uint32_t*, void**);
typedef VkResult (*PFN_vkAcquireNextImageKHR)(VkDevice, VkSwapchainKHR, std::uint64_t, VkSemaphore, VkFence, std::uint32_t*);
typedef VkResult (*PFN_vkQueuePresentKHR)(VkQueue, const VkPresentInfoKHR*);

int main() {
    HMODULE vk = LoadLibraryW(L"vulkan-1.dll");
    if (!vk) { printf("no Vulkan runtime; skipping (err %lu)\n", GetLastError()); return 9; }

    auto gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(vk, "vkGetInstanceProcAddr");
    if (!gipa) { printf("no vkGetInstanceProcAddr\n"); return 10; }
    auto GetInstProc = [&](const char* n) { return gipa(nullptr, n); };

    VKFN(vkCreateInstance) = (PFN_vkCreateInstance)GetInstProc("vkCreateInstance");
    if (!vkCreateInstance) {
        printf("vkCreateInstance missing\n");
        return 11;
    }

    // Window first (surface needs hwnd).
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PacerVkSpinner";
    RegisterClassExW(&wc);
    wchar_t title[128];
    swprintf_s(title, L"pacer-vkspinner  pid=%lu", GetCurrentProcessId());
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 800, 450, nullptr, nullptr,
                                wc.hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    const char* inst_exts[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "vkspinner", 1,
                          "vkspinner", 1, VK_API_VERSION_1_0};
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app, 0,
                             nullptr, 2, inst_exts};
    VkInstance inst = nullptr;
    if (vkCreateInstance(&ici, nullptr, &inst) != 0 || !inst) {
        fprintf(stderr, "vkCreateInstance failed\n");
        return 3;
    }
    // Per spec, only pre-instance commands resolve through gipa(NULL, ...).
    // Everything else needs the live instance.
    auto gpi = [&](const char* n) { return gipa(inst, n); };
    VKFN(vkEnumeratePhysicalDevices) = (PFN_vkEnumeratePhysicalDevices)gpi("vkEnumeratePhysicalDevices");
    VKFN(vkCreateWin32SurfaceKHR) = (PFN_vkCreateWin32SurfaceKHR)gpi("vkCreateWin32SurfaceKHR");
    VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, nullptr,
                                    0, wc.hInstance, hwnd};
    VkSurfaceKHR surface = 0;
    if (!vkCreateWin32SurfaceKHR || vkCreateWin32SurfaceKHR(inst, &sci, nullptr, &surface) != 0) {
        fprintf(stderr, "surface creation failed\n");
        return 4;
    }

    VKFN(vkGetPhysicalDeviceQueueFamilyProperties) =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gpi("vkGetPhysicalDeviceQueueFamilyProperties");
    VKFN(vkGetPhysicalDeviceSurfaceSupportKHR) =
        (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)gpi("vkGetPhysicalDeviceSurfaceSupportKHR");
    VKFN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) =
        (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)gpi("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    VKFN(vkGetPhysicalDeviceSurfaceFormatsKHR) =
        (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)gpi("vkGetPhysicalDeviceSurfaceFormatsKHR");
    VKFN(vkCreateDevice) = (PFN_vkCreateDevice)gpi("vkCreateDevice");

    std::uint32_t ndev = 0;
    vkEnumeratePhysicalDevices(inst, &ndev, nullptr);
    if (!ndev) { fprintf(stderr, "no GPU\n"); return 5; }
    VkPhysicalDevice pds[8] = {};
    if (ndev > 8) ndev = 8;
    vkEnumeratePhysicalDevices(inst, &ndev, pds);

    VkPhysicalDevice pdev = nullptr;
    std::uint32_t qfamily = 0xFFFFFFFF;
    for (std::uint32_t i = 0; i < ndev && !pdev; ++i) {
        std::uint32_t nfam = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &nfam, nullptr);
        for (std::uint32_t f = 0; f < nfam; ++f) {
            VkQueueFamilyProperties props[16]{};
            std::uint32_t n = nfam > 16 ? 16 : nfam;
            vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &n, props);
            for (std::uint32_t q = 0; q < n; ++q) {
                VkBool32 present = 0;
                vkGetPhysicalDeviceSurfaceSupportKHR(pds[i], q, surface, &present);
                if (present && (props[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    pdev = pds[i];
                    qfamily = q;
                    break;
                }
            }
            if (pdev) break;
        }
    }
    if (!pdev) { fprintf(stderr, "no present-capable queue family\n"); return 5; }

    const char* dev_exts[] = {"VK_KHR_swapchain"};
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0,
                                qfamily, 1, &prio};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr, 0, 1, &qci, 0,
                           nullptr, 1, dev_exts, nullptr};
    VkDevice dev = nullptr;
    if (vkCreateDevice(pdev, &dci, nullptr, &dev) != 0 || !dev) {
        fprintf(stderr, "vkCreateDevice failed\n");
        return 6;
    }
    auto gpd = [&](const char* n) {
        return (void*)((PFN_vkGetDeviceProcAddr)gpi("vkGetDeviceProcAddr"))(dev, n);
    };
    PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)gpi("vkGetDeviceProcAddr");
    VKFN(vkGetDeviceQueue) = (PFN_vkGetDeviceQueue)gdpa(dev, "vkGetDeviceQueue");
    VKFN(vkCreateSemaphore) = (PFN_vkCreateSemaphore)gdpa(dev, "vkCreateSemaphore");
    VKFN(vkCreateSwapchainKHR) = (PFN_vkCreateSwapchainKHR)gdpa(dev, "vkCreateSwapchainKHR");
    VKFN(vkGetSwapchainImagesKHR) = (PFN_vkGetSwapchainImagesKHR)gdpa(dev, "vkGetSwapchainImagesKHR");
    VKFN(vkAcquireNextImageKHR) = (PFN_vkAcquireNextImageKHR)gdpa(dev, "vkAcquireNextImageKHR");
    VKFN(vkQueuePresentKHR) = (PFN_vkQueuePresentKHR)gdpa(dev, "vkQueuePresentKHR");
    (void)gpd;
    if (!vkCreateSwapchainKHR || !vkGetSwapchainImagesKHR || !vkAcquireNextImageKHR ||
        !vkQueuePresentKHR) {
        fprintf(stderr, "device procs missing\n");
        return 7;
    }

    VkQueue queue = nullptr;
    vkGetDeviceQueue(dev, qfamily, 0, &queue);

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pdev, surface, &caps);
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) extent = {800, 450};

    std::uint32_t nfmt = 0, fmt = VK_FORMAT_B8G8R8A8_UNORM;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pdev, surface, &nfmt, nullptr);
    VkSurfaceFormatKHR fmts[16]{};
    if (nfmt) {
        std::uint32_t n = nfmt > 16 ? 16 : nfmt;
        vkGetPhysicalDeviceSurfaceFormatsKHR(pdev, surface, &n, fmts);
        if (fmts[0].format != VK_FORMAT_UNDEFINED) fmt = fmts[0].format;
    }

    std::uint32_t present_mode = VK_PRESENT_MODE_FIFO_KHR;  // core guarantee
    // Try IMMEDIATE (uncapped) -- not required to exist.
    VkSwapchainCreateInfoKHR swi{};
    swi.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swi.surface = surface;
    swi.minImageCount = caps.minImageCount < 2 ? 2 : caps.minImageCount;
    swi.imageFormat = fmt;
    swi.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swi.imageExtent = extent;
    swi.imageArrayLayers = 1;
    swi.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swi.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swi.preTransform = caps.currentTransform;
    swi.compositeAlpha = 1;  // OPAQUE (required member of the standard set)
    swi.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    swi.clipped = 1;
    VkSwapchainKHR swap = 0;
    if (vkCreateSwapchainKHR(dev, &swi, nullptr, &swap) != 0) {
        swi.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // always available
        if (vkCreateSwapchainKHR(dev, &swi, nullptr, &swap) != 0) {
            fprintf(stderr, "vkCreateSwapchainKHR failed\n");
            return 8;
        }
        present_mode = VK_PRESENT_MODE_FIFO_KHR;  // throttled by vsync; fine to still show pacing caps below refresh
    }

    VkSemaphore sem = 0;
    VkSemaphoreCreateInfo semci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0};
    vkCreateSemaphore(dev, &semci, nullptr, &sem);

    std::uint32_t nimg = 0;
    vkGetSwapchainImagesKHR(dev, swap, &nimg, nullptr);
    printf("vkspinner running pid=%lu (%u images, mode=%s)\n", GetCurrentProcessId(), nimg,
           swi.presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "immediate" : "fifo");

    // Pre-register to defeat first-present startup costs from stats windows.
    std::uint32_t img = 0;
    vkAcquireNextImageKHR(dev, swap, 100000000, sem, 0, &img);
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr, 1, &sem, 1, &swap, &img,
                        nullptr};

    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 1) return 0;
        vkAcquireNextImageKHR(dev, swap, 100000000, sem, 0, &img);
        pi.pImageIndices = &img;
        vkQueuePresentKHR(queue, &pi);
    }
}
