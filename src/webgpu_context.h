#ifndef WEBGPU_CONTEXT_H
#define WEBGPU_CONTEXT_H

#include <SDL3/SDL.h>
#include <stdio.h>
#include <webgpu/webgpu.h>

#if defined(SDL_PLATFORM_WIN32)
#include <windows.h>
#elif defined(SDL_PLATFORM_ANDROID)
#include <android/log.h>
#include <android/native_window.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WebGPUContext
    {
        SDL_Window *window;
        WGPUInstance instance;
        WGPUAdapter adapter;
        WGPUDevice device;
        WGPUQueue queue;
        WGPUSurface surface;
        WGPUSurfaceConfiguration config;
        bool is_configured;
        bool need_reconfigure;
        bool need_recreate_surface;
    } WebGPUContext;

    extern WebGPUContext g_gpu;

    void LogApp(const char *fmt, ...);

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

    bool InitWebGPUContext(WebGPUContext *gpu, SDL_Window *win);
    void ReconfigureSurfaceIfNeeded(WebGPUContext *gpu);
    void DestroyWebGPUContext(WebGPUContext *gpu);

#ifdef __cplusplus
}
#endif

#endif // WEBGPU_CONTEXT_H
