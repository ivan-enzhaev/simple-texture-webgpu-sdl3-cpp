#include "webgpu_context.h"

WebGPUContext g_gpu = { 0 };

void LogApp(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

#if defined(__ANDROID__)
    __android_log_vprint(ANDROID_LOG_ERROR, "MY_APP", fmt, args);
#else
    printf("[MY_APP] ");
    vprintf(fmt, args);
    printf("\n");
#endif

    va_end(args);
}

WGPUSurface CreateWGPUSurface(WGPUInstance inst, SDL_Window *win)
{
#if defined(__EMSCRIPTEN__)
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasSource = {
        .chain = { .sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector },
        .selector = WGPU_STR("#canvas")
    };
    WGPUSurfaceDescriptor desc = { .nextInChain = (WGPUChainedStruct *)&canvasSource };
    return wgpuInstanceCreateSurface(inst, &desc);

#elif defined(SDL_PLATFORM_WIN32)
    SDL_PropertiesID props = SDL_GetWindowProperties(win);
    HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    HINSTANCE hinstance = (HINSTANCE)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, NULL);

    WGPUSurfaceSourceWindowsHWND hwndSource = {
        .chain = { .sType = WGPUSType_SurfaceSourceWindowsHWND },
        .hinstance = hinstance,
        .hwnd = hwnd
    };
    WGPUSurfaceDescriptor desc = { .nextInChain = (WGPUChainedStruct *)&hwndSource };
    return wgpuInstanceCreateSurface(inst, &desc);

#elif defined(SDL_PLATFORM_ANDROID)
    SDL_PropertiesID props = SDL_GetWindowProperties(win);
    ANativeWindow *aNativeWindow = (ANativeWindow *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL);

    WGPUSurfaceSourceAndroidNativeWindow androidSource = {
        .chain = { .sType = WGPUSType_SurfaceSourceAndroidNativeWindow },
        .window = aNativeWindow
    };
    WGPUSurfaceDescriptor desc = { .nextInChain = (WGPUChainedStruct *)&androidSource };
    return wgpuInstanceCreateSurface(inst, &desc);
#else
#error "Platform surface mapping not implemented"
#endif
}

void handle_device_error(
    WGPUDevice const *device,
    WGPUErrorType type,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    const char *msg = (message.data && message.length > 0) ? message.data : "(No message)";
    int len = (message.data && message.length > 0) ? (int)message.length : 12;

    LogApp("WebGPU Error [%d]: %.*s", (int)type, len, msg);
}

void handle_device_request(
    WGPURequestDeviceStatus status,
    WGPUDevice res,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    const char *msg = (message.data && message.length > 0) ? message.data : "";
    int len = (message.data && message.length > 0) ? (int)message.length : 0;

    LogApp("Device callback: status=%d device=%p message=%.*s", (int)status, (void *)res, len, msg);

    if (status == WGPURequestDeviceStatus_Success)
    {
        g_gpu.device = res;
        LogApp("DEVICE CREATED SUCCESS");
    }
    else
    {
        LogApp("DEVICE CREATION FAILED");
    }
}

void handle_adapter_request(
    WGPURequestAdapterStatus status,
    WGPUAdapter res,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    const char *msg = (message.data && message.length > 0) ? message.data : "";
    int len = (message.data && message.length > 0) ? (int)message.length : 0;

    LogApp("Adapter callback: status=%d adapter=%p message=%.*s", (int)status, (void *)res, len, msg);

    if (status == WGPURequestAdapterStatus_Success)
    {
        g_gpu.adapter = res;
        LogApp("ADAPTER CREATED SUCCESS");

        WGPUUncapturedErrorCallbackInfo errorCallbackInfo = {};
        errorCallbackInfo.callback = handle_device_error;

        WGPUDeviceDescriptor deviceDesc = {};
        deviceDesc.uncapturedErrorCallbackInfo = errorCallbackInfo;

        WGPURequestDeviceCallbackInfo deviceCallbackInfo = {};
        deviceCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        deviceCallbackInfo.callback = handle_device_request;

        wgpuAdapterRequestDevice(g_gpu.adapter, &deviceDesc, deviceCallbackInfo);
    }
    else
    {
        LogApp("ADAPTER CREATION FAILED");
    }
}

bool InitWebGPUContext(WebGPUContext *gpu, SDL_Window *win)
{
    gpu->window = win;
    gpu->instance = wgpuCreateInstance(NULL);
    if (!gpu->instance)
        return false;

    gpu->surface = CreateWGPUSurface(gpu->instance, win);
    if (!gpu->surface)
        return false;

    WGPURequestAdapterOptions opt = { .compatibleSurface = gpu->surface };

    WGPURequestAdapterCallbackInfo adapterCallbackInfo = {
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = (WGPURequestAdapterCallback)handle_adapter_request
    };

    wgpuInstanceRequestAdapter(gpu->instance, &opt, adapterCallbackInfo);

#ifndef __EMSCRIPTEN__
    while (gpu->adapter == NULL || gpu->device == NULL)
    {
        SDL_Delay(1);
    }
#endif

    return true;
}

void ReconfigureSurfaceIfNeeded(WebGPUContext *gpu)
{
    if (!gpu->device)
        return;

    // Handle native window recreation on Android foreground return
    if (gpu->need_recreate_surface)
    {
        if (gpu->surface)
        {
            wgpuSurfaceUnconfigure(gpu->surface);
            wgpuSurfaceRelease(gpu->surface);
            gpu->surface = NULL;
        }

        gpu->surface = CreateWGPUSurface(gpu->instance, gpu->window);
        gpu->need_recreate_surface = false;

        gpu->is_configured = false;
        gpu->need_reconfigure = true;
    }

    if (!gpu->surface)
        return;

    if (!gpu->is_configured || gpu->need_reconfigure)
    {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(gpu->window, &w, &h);
        if (w <= 0 || h <= 0)
            return;

        gpu->config.width = (uint32_t)w;
        gpu->config.height = (uint32_t)h;

        if (!gpu->is_configured)
        {
            if (!gpu->queue)
            {
                gpu->queue = wgpuDeviceGetQueue(gpu->device);
            }

            WGPUSurfaceCapabilities caps = { 0 };
            wgpuSurfaceGetCapabilities(gpu->surface, gpu->adapter, &caps);

            WGPUTextureFormat surface_format = WGPUTextureFormat_Undefined;
            if (caps.formatCount > 0)
            {
                surface_format = caps.formats[0];
                for (size_t i = 0; i < caps.formatCount; ++i)
                {
                    if (caps.formats[i] == WGPUTextureFormat_RGBA8Unorm ||
                        caps.formats[i] == WGPUTextureFormat_BGRA8Unorm)
                    {
                        surface_format = caps.formats[i];
                        break;
                    }
                }
            }
            else
            {
                surface_format = WGPUTextureFormat_RGBA8Unorm;
            }

            LogApp(">>> Selected Surface Format: %d", (int)surface_format);

            WGPUPresentMode present_mode = (caps.presentModeCount > 0) ? caps.presentModes[0] : WGPUPresentMode_Fifo;

            gpu->config.device = gpu->device;
            gpu->config.format = surface_format;
            gpu->config.usage = WGPUTextureUsage_RenderAttachment;
            gpu->config.presentMode = present_mode;

            wgpuSurfaceCapabilitiesFreeMembers(caps);
            gpu->is_configured = true;
        }

        LogApp(">>> Configuring Surface: width=%u height=%u", gpu->config.width, gpu->config.height);
        wgpuSurfaceConfigure(gpu->surface, &gpu->config);
        gpu->need_reconfigure = false;
    }
}

void DestroyWebGPUContext(WebGPUContext *gpu)
{
    if (gpu->surface)
        wgpuSurfaceUnconfigure(gpu->surface);
    if (gpu->queue)
        wgpuQueueRelease(gpu->queue);
    if (gpu->device)
        wgpuDeviceRelease(gpu->device);
    if (gpu->adapter)
        wgpuAdapterRelease(gpu->adapter);
    if (gpu->surface)
        wgpuSurfaceRelease(gpu->surface);
    if (gpu->instance)
        wgpuInstanceRelease(gpu->instance);
}
