#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <webgpu/webgpu.h>

#include "texture_utils.h"

#if defined(SDL_PLATFORM_WIN32)
#include <windows.h>
#elif defined(SDL_PLATFORM_ANDROID)
#include <android/log.h>
#include <android/native_window.h>
#endif

#if defined(__ANDROID__) || defined(SDL_PLATFORM_ANDROID)
#define ASSET_PATH(path) path
#else
#define ASSET_PATH(path) "assets/" path
#endif

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

static SDL_Window *window = NULL;
static WGPUInstance instance = NULL;
static WGPUAdapter adapter = NULL;
static WGPUDevice device = NULL;
static WGPUQueue queue = NULL;
static WGPUSurface surface = NULL;
static WGPUSurfaceConfiguration config = { 0 };
static WGPURenderPipeline pipeline = NULL;

static WGPUBuffer uniform_buffer = NULL;
static WGPUBindGroup bind_group = NULL;

static WebGPUTexture crate_texture = { 0 };

static bool is_configured = false;
static bool need_reconfigure = false;

// Interactive state variables
static float scale_val = 1.f;

// Data structure layout sent to WGSL uniform buffer
struct Uniforms
{
    float scale;
    float aspect_ratio;
    float padding[2]; // Pad out to 16 bytes alignment boundary
    float color[4];   // 16 bytes total (r, g, b, a)
};

static const int WIN_WIDTH = 1280;
static const int WIN_HEIGHT = 720;

const char *shader_code =
    "struct Uniforms {\n"
    "    scale: f32,\n"
    "    aspect_ratio: f32,\n"
    "    padding: vec2f,\n"
    "    color: vec4f,\n"
    "};\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "@group(0) @binding(1) var t_diffuse: texture_2d<f32>;\n"
    "@group(0) @binding(2) var s_diffuse: sampler;\n\n"
    "struct VertexOutput {\n"
    "    @builtin(position) position: vec4f,\n"
    "    @location(0) uv: vec2f,\n"
    "};\n\n"
    "@vertex\n"
    "fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> VertexOutput {\n"
    "    var pos = array<vec2f, 6>(\n"
    "        vec2f(-0.5,  0.5),\n"
    "        vec2f(-0.5, -0.5),\n"
    "        vec2f( 0.5, -0.5),\n"
    "        vec2f(-0.5,  0.5),\n"
    "        vec2f( 0.5, -0.5),\n"
    "        vec2f( 0.5,  0.5)\n"
    "    );\n"
    "    var uvs = array<vec2f, 6>(\n"
    "        vec2f(0.0, 0.0),\n"
    "        vec2f(0.0, 1.0),\n"
    "        vec2f(1.0, 1.0),\n"
    "        vec2f(0.0, 0.0),\n"
    "        vec2f(1.0, 1.0),\n"
    "        vec2f(1.0, 0.0)\n"
    "    );\n"
    "    var out: VertexOutput;\n"
    "    var p = pos[in_vertex_index] * u.scale;\n"
    "    var corrected_pos = p;\n"
    "    if (u.aspect_ratio >= 1.0) {\n"
    "        corrected_pos.x = p.x / u.aspect_ratio;\n"
    "    } else {\n"
    "        corrected_pos.y = p.y * u.aspect_ratio;\n"
    "    }\n"
    "    out.position = vec4f(corrected_pos.x, corrected_pos.y, 0.0, 1.0);\n"
    "    out.uv = uvs[in_vertex_index];\n"
    "    return out;\n"
    "}\n\n"
    "@fragment\n"
    "fn fs_main(in: VertexOutput) -> @location(0) vec4f {\n"
    "    return textureSample(t_diffuse, s_diffuse, in.uv);\n"
    "}\n";

#ifdef __cplusplus
static inline WGPUStringView WGPU_STR(const char *s)
{
    WGPUStringView view;
    view.data = s;
    view.length = (s != NULL) ? SDL_strlen(s) : WGPU_STRLEN;
    return view;
}
#else
#define WGPU_STR(s) \
    (WGPUStringView) { .data = s, .length = (s != NULL) ? SDL_strlen(s) : WGPU_STRLEN }
#endif

static WGPUSurface CreateWGPUSurface(WGPUInstance inst, SDL_Window *win)
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
        device = res;
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
        adapter = res;
        LogApp("ADAPTER CREATED SUCCESS");

        WGPUUncapturedErrorCallbackInfo errorCallbackInfo = {};
        errorCallbackInfo.callback = handle_device_error;

        WGPUDeviceDescriptor deviceDesc = {};
        deviceDesc.uncapturedErrorCallbackInfo = errorCallbackInfo;

        WGPURequestDeviceCallbackInfo deviceCallbackInfo = {};
        deviceCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        deviceCallbackInfo.callback = handle_device_request;

        wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCallbackInfo);
    }
    else
    {
        LogApp("ADAPTER CREATION FAILED");
    }
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
#ifndef __EMSCRIPTEN__
    if (!SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "60"))
    {
        SDL_Log("Failed to set a frame rate: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
#endif
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");

    if (!SDL_Init(SDL_INIT_VIDEO))
        return SDL_APP_FAILURE;

    window = SDL_CreateWindow("WebGPU Texture Quad", WIN_WIDTH, WIN_HEIGHT, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    instance = wgpuCreateInstance(NULL);
    surface = CreateWGPUSurface(instance, window);

    WGPURequestAdapterOptions opt = { .compatibleSurface = surface };

    WGPURequestAdapterCallbackInfo adapterCallbackInfo = {
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = (WGPURequestAdapterCallback)handle_adapter_request
    };

    wgpuInstanceRequestAdapter(instance, &opt, adapterCallbackInfo);

#ifndef __EMSCRIPTEN__
    while (adapter == NULL || device == NULL)
    {
        SDL_Delay(1);
    }
#endif

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT)
    {
        return SDL_APP_SUCCESS;
    }

    // Catch window resize / pixel scale changes
    if (event->type == SDL_EVENT_WINDOW_RESIZED ||
        event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        need_reconfigure = true;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    if (instance)
    {
        wgpuInstanceProcessEvents(instance);
    }

    if (!device)
    {
        return SDL_APP_CONTINUE;
    }

    // Handle initial setup OR window resize re-configuration
    if (!is_configured || need_reconfigure)
    {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        if (w <= 0 || h <= 0)
        {
            return SDL_APP_CONTINUE;
        }

        config.width = (uint32_t)w;
        config.height = (uint32_t)h;

        if (!is_configured)
        {
            LogApp(">>> Configuring WebGPU Device and Pipeline");
            queue = wgpuDeviceGetQueue(device);

            // Load texture asset
            LogApp(">>> Loading texture asset...");
            crate_texture = createTexture(device, queue, ASSET_PATH("images/crate.webp"));
            LogApp(">>> Texture loaded: view=%p sampler=%p", (void *)crate_texture.view, (void *)crate_texture.sampler);

            WGPUSurfaceCapabilities caps = { 0 };
            wgpuSurfaceGetCapabilities(surface, adapter, &caps);

            // Select valid surface format (Default to caps.formats[0] if available)
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

            config.device = device;
            config.format = surface_format;
            config.usage = WGPUTextureUsage_RenderAttachment;
            config.presentMode = present_mode;

            wgpuSurfaceCapabilitiesFreeMembers(caps);

            // Create Uniform Buffer
            LogApp(">>> Creating Uniform Buffer");
            WGPUBufferDescriptor buffer_desc = {};
            buffer_desc.size = sizeof(Uniforms);
            buffer_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            uniform_buffer = wgpuDeviceCreateBuffer(device, &buffer_desc);

            // Bind Group Layout Entries
            WGPUBindGroupLayoutEntry bgl_entries[3] = {};

            bgl_entries[0].binding = 0;
            bgl_entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            bgl_entries[0].buffer.type = WGPUBufferBindingType_Uniform;

            bgl_entries[1].binding = 1;
            bgl_entries[1].visibility = WGPUShaderStage_Fragment;
            bgl_entries[1].texture.sampleType = WGPUTextureSampleType_Float;
            bgl_entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

            bgl_entries[2].binding = 2;
            bgl_entries[2].visibility = WGPUShaderStage_Fragment;
            bgl_entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

            WGPUBindGroupLayoutDescriptor bgl_desc = {};
            bgl_desc.entryCount = 3;
            bgl_desc.entries = bgl_entries;
            WGPUBindGroupLayout bind_group_layout = wgpuDeviceCreateBindGroupLayout(device, &bgl_desc);

            // Create Bind Group
            WGPUBindGroupEntry bg_entries[3] = {};

            bg_entries[0].binding = 0;
            bg_entries[0].buffer = uniform_buffer;
            bg_entries[0].size = sizeof(Uniforms);

            bg_entries[1].binding = 1;
            bg_entries[1].textureView = crate_texture.view;

            bg_entries[2].binding = 2;
            bg_entries[2].sampler = crate_texture.sampler;

            WGPUBindGroupDescriptor bg_desc = {};
            bg_desc.layout = bind_group_layout;
            bg_desc.entryCount = 3;
            bg_desc.entries = bg_entries;
            bind_group = wgpuDeviceCreateBindGroup(device, &bg_desc);

            // Create Pipeline Layout
            WGPUPipelineLayoutDescriptor pipeline_layout_desc = {};
            pipeline_layout_desc.bindGroupLayoutCount = 1;
            pipeline_layout_desc.bindGroupLayouts = &bind_group_layout;
            WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(device, &pipeline_layout_desc);

            // Create Shader & Pipeline
            LogApp(">>> Compiling WGSL Shader Module");
            WGPUShaderSourceWGSL wgsl_desc = { 0 };
            wgsl_desc.chain.sType = WGPUSType_ShaderSourceWGSL;
            wgsl_desc.code = WGPU_STR(shader_code);

            WGPUShaderModuleDescriptor shader_desc = { 0 };
            shader_desc.nextInChain = &wgsl_desc.chain;
            WGPUShaderModule shader_module = wgpuDeviceCreateShaderModule(device, &shader_desc);

            WGPUColorTargetState color_target = { .format = config.format, .writeMask = WGPUColorWriteMask_All };
            WGPUFragmentState fragment_state = {
                .module = shader_module,
                .entryPoint = WGPU_STR("fs_main"),
                .targetCount = 1,
                .targets = &color_target
            };

            WGPURenderPipelineDescriptor pipeline_desc = { 0 };
            pipeline_desc.layout = pipeline_layout;
            pipeline_desc.vertex.module = shader_module;
            pipeline_desc.vertex.entryPoint = WGPU_STR("vs_main");
            pipeline_desc.fragment = &fragment_state;
            pipeline_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipeline_desc.multisample.count = 1;
            pipeline_desc.multisample.mask = 0xFFFFFFFF;

            LogApp(">>> Creating Render Pipeline");
            pipeline = wgpuDeviceCreateRenderPipeline(device, &pipeline_desc);
            LogApp(">>> Render Pipeline created: %p", (void *)pipeline);

            wgpuBindGroupLayoutRelease(bind_group_layout);
            wgpuPipelineLayoutRelease(pipeline_layout);
            wgpuShaderModuleRelease(shader_module);

            is_configured = true;
        }

        LogApp(">>> Configuring Surface: width=%u height=%u", config.width, config.height);
        wgpuSurfaceConfigure(surface, &config);
        need_reconfigure = false;
    }

    WGPUSurfaceTexture surfaceTexture = { 0 };
    wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);

#if defined(WGPUSurfaceGetCurrentTextureStatus_Success) && defined(WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
#define SURFACE_STATUS_SUCCESS(s) ((s) == WGPUSurfaceGetCurrentTextureStatus_Success || (s) == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
#elif defined(WGPUSurfaceGetCurrentTextureStatus_SuccessStatus)
#define SURFACE_STATUS_SUCCESS(s) ((s) == WGPUSurfaceGetCurrentTextureStatus_SuccessStatus)
#else
#define SURFACE_STATUS_SUCCESS(s) ((s) == 0 || (s) == 1)
#endif

    if (!SURFACE_STATUS_SUCCESS(surfaceTexture.status))
    {
        static bool status_logged = false;
        if (!status_logged)
        {
            LogApp(">>> wgpuSurfaceGetCurrentTexture failed with status: %d", (int)surfaceTexture.status);
            status_logged = true;
        }
        return SDL_APP_CONTINUE;
    }

    // Get canvas/window dimensions in pixels
    int w_pixels = 0, h_pixels = 0;
    SDL_GetWindowSizeInPixels(window, &w_pixels, &h_pixels);
    float aspect = (h_pixels > 0) ? ((float)w_pixels / (float)h_pixels) : 1.0f;

    Uniforms uniforms = {};
    uniforms.scale = scale_val;
    uniforms.aspect_ratio = aspect;

    wgpuQueueWriteBuffer(queue, uniform_buffer, 0, &uniforms, sizeof(Uniforms));

    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, NULL);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, NULL);

    WGPURenderPassColorAttachment colorAttachment = {
        .nextInChain = NULL,
        .view = view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .resolveTarget = NULL,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { 0.15, 0.15, 0.18, 1.0 }
    };

    WGPURenderPassDescriptor renderPassDesc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &colorAttachment
    };

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(queue, 1, &commandBuffer);

#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(surface);
#endif

    if (commandBuffer)
        wgpuCommandBufferRelease(commandBuffer);
    if (pass)
        wgpuRenderPassEncoderRelease(pass);
    if (encoder)
        wgpuCommandEncoderRelease(encoder);
    if (view)
        wgpuTextureViewRelease(view);
    if (surfaceTexture.texture)
        wgpuTextureRelease(surfaceTexture.texture);

#undef SURFACE_STATUS_SUCCESS

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    destroyWebGPUTexture(&crate_texture);

    if (bind_group)
        wgpuBindGroupRelease(bind_group);
    if (uniform_buffer)
        wgpuBufferRelease(uniform_buffer);
    if (pipeline)
        wgpuRenderPipelineRelease(pipeline);
    if (surface)
        wgpuSurfaceUnconfigure(surface);
    if (queue)
        wgpuQueueRelease(queue);
    if (device)
        wgpuDeviceRelease(device);
    if (adapter)
        wgpuAdapterRelease(adapter);
    if (surface)
        wgpuSurfaceRelease(surface);
    if (instance)
        wgpuInstanceRelease(instance);
    if (window)
        SDL_DestroyWindow(window);
    SDL_Quit();
}
