#define SDL_MAIN_USE_CALLBACKS 1

#include "webgpu_context.h"
#include "texture_utils.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#if defined(__ANDROID__) || defined(SDL_PLATFORM_ANDROID)
#define ASSET_PATH(path) path
#else
#define ASSET_PATH(path) "assets/" path
#endif

static SDL_Window *window = NULL;
static WGPURenderPipeline pipeline = NULL;
static WGPUBuffer uniform_buffer = NULL;
static WGPUBindGroup bind_group = NULL;
static WebGPUTexture crate_texture = { 0 };

static float scale_val = 1.f;

struct Uniforms
{
    float scale;
    float aspect_ratio;
    float padding[2];
    float color[4];
};

typedef struct Uniforms Uniforms;

#if defined(__ANDROID__) || defined(SDL_PLATFORM_ANDROID)
static const int WIN_WIDTH = 1280;
static const int WIN_HEIGHT = 720;
#else
static const int WIN_WIDTH = 300;
static const int WIN_HEIGHT = 300;
#endif

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

static bool InitPipeline(void)
{
    LogApp(">>> Loading texture asset...");
    crate_texture = createTexture(g_gpu.device, g_gpu.queue, ASSET_PATH("images/crate.webp"));
    LogApp(">>> Texture loaded: view=%p sampler=%p", (void *)crate_texture.view, (void *)crate_texture.sampler);

    LogApp(">>> Creating Uniform Buffer");
    WGPUBufferDescriptor buffer_desc = { 0 };
    buffer_desc.size = sizeof(Uniforms);
    buffer_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniform_buffer = wgpuDeviceCreateBuffer(g_gpu.device, &buffer_desc);

    WGPUBindGroupLayoutEntry bgl_entries[3] = { { 0 } };

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

    WGPUBindGroupLayoutDescriptor bgl_desc = { 0 };
    bgl_desc.entryCount = 3;
    bgl_desc.entries = bgl_entries;
    WGPUBindGroupLayout bind_group_layout = wgpuDeviceCreateBindGroupLayout(g_gpu.device, &bgl_desc);

    WGPUBindGroupEntry bg_entries[3] = { { 0 } };

    bg_entries[0].binding = 0;
    bg_entries[0].buffer = uniform_buffer;
    bg_entries[0].size = sizeof(Uniforms);

    bg_entries[1].binding = 1;
    bg_entries[1].textureView = crate_texture.view;

    bg_entries[2].binding = 2;
    bg_entries[2].sampler = crate_texture.sampler;

    WGPUBindGroupDescriptor bg_desc = { 0 };
    bg_desc.layout = bind_group_layout;
    bg_desc.entryCount = 3;
    bg_desc.entries = bg_entries;
    bind_group = wgpuDeviceCreateBindGroup(g_gpu.device, &bg_desc);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = { 0 };
    pipeline_layout_desc.bindGroupLayoutCount = 1;
    pipeline_layout_desc.bindGroupLayouts = &bind_group_layout;
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(g_gpu.device, &pipeline_layout_desc);

    LogApp(">>> Compiling WGSL Shader Module");
    WGPUShaderSourceWGSL wgsl_desc = { 0 };
    wgsl_desc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_desc.code = WGPU_STR(shader_code);

    WGPUShaderModuleDescriptor shader_desc = { 0 };
    shader_desc.nextInChain = &wgsl_desc.chain;
    WGPUShaderModule shader_module = wgpuDeviceCreateShaderModule(g_gpu.device, &shader_desc);

    WGPUColorTargetState color_target = { 0 };
    color_target.format = g_gpu.config.format;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment_state = { 0 };
    fragment_state.module = shader_module;
    fragment_state.entryPoint = WGPU_STR("fs_main");
    fragment_state.targetCount = 1;
    fragment_state.targets = &color_target;

    WGPURenderPipelineDescriptor pipeline_desc = { 0 };
    pipeline_desc.layout = pipeline_layout;
    pipeline_desc.vertex.module = shader_module;
    pipeline_desc.vertex.entryPoint = WGPU_STR("vs_main");
    pipeline_desc.fragment = &fragment_state;
    pipeline_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeline_desc.multisample.count = 1;
    pipeline_desc.multisample.mask = 0xFFFFFFFF;

    LogApp(">>> Creating Render Pipeline");
    pipeline = wgpuDeviceCreateRenderPipeline(g_gpu.device, &pipeline_desc);
    LogApp(">>> Render Pipeline created: %p", (void *)pipeline);

    wgpuBindGroupLayoutRelease(bind_group_layout);
    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(shader_module);

    return pipeline != NULL;
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

    window = SDL_CreateWindow("WebGPU Texture Quad", WIN_WIDTH, WIN_HEIGHT,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window)
        return SDL_APP_FAILURE;

    if (!InitWebGPUContext(&g_gpu, window))
    {
        return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT)
    {
        return SDL_APP_SUCCESS;
    }

    if (event->type == SDL_EVENT_WINDOW_RESIZED ||
        event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        g_gpu.need_reconfigure = true;
    }

    if (event->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
        event->type == SDL_EVENT_DID_ENTER_FOREGROUND)
    {
        g_gpu.need_recreate_surface = true;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    if (g_gpu.instance)
    {
        wgpuInstanceProcessEvents(g_gpu.instance);
    }

    if (!g_gpu.device)
    {
        return SDL_APP_CONTINUE;
    }

    bool was_configured = g_gpu.is_configured;
    ReconfigureSurfaceIfNeeded(&g_gpu);

    if (!was_configured && g_gpu.is_configured)
    {
        if (!InitPipeline())
        {
            return SDL_APP_FAILURE;
        }
    }

    WGPUSurfaceTexture surfaceTexture = { 0 };
    wgpuSurfaceGetCurrentTexture(g_gpu.surface, &surfaceTexture);

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

    int w_pixels = 0, h_pixels = 0;
    SDL_GetWindowSizeInPixels(window, &w_pixels, &h_pixels);
    float aspect = (h_pixels > 0) ? ((float)w_pixels / (float)h_pixels) : 1.0f;

    Uniforms uniforms = { 0 };
    uniforms.scale = scale_val;
    uniforms.aspect_ratio = aspect;

    wgpuQueueWriteBuffer(g_gpu.queue, uniform_buffer, 0, &uniforms, sizeof(Uniforms));

    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, NULL);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(g_gpu.device, NULL);

    WGPURenderPassColorAttachment colorAttachment = { 0 };
    colorAttachment.nextInChain = NULL;
    colorAttachment.view = view;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.resolveTarget = NULL;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue.r = 0.15;
    colorAttachment.clearValue.g = 0.15;
    colorAttachment.clearValue.b = 0.18;
    colorAttachment.clearValue.a = 1.0;

    WGPURenderPassDescriptor renderPassDesc = { 0 };
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(g_gpu.queue, 1, &commandBuffer);

#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(g_gpu.surface);
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

    DestroyWebGPUContext(&g_gpu);

    if (window)
        SDL_DestroyWindow(window);
    SDL_Quit();
}
