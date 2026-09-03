#pragma once

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <webgpu/webgpu.h>

#include <stdio.h>
#include <string.h>

struct WebGPUTexture
{
    WGPUTexture texture;
    WGPUTextureView view;
    WGPUSampler sampler;
    uint32_t width;
    uint32_t height;
};

static inline WebGPUTexture createTextureFromSurface(WGPUDevice device, WGPUQueue queue, SDL_Surface *surface)
{
    WebGPUTexture result = { NULL, NULL, NULL, 0, 0 };

    if (!surface || !device || !queue)
    {
        return result;
    }

    // Convert surface format to RGBA32 (ABGR8888 maps correctly to WebGPU RGBA8Unorm in byte order)
    SDL_Surface *formattedSurface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ABGR8888);
    if (!formattedSurface)
    {
        SDL_Log("Surface conversion failed: %s", SDL_GetError());
        return result;
    }

    result.width = (uint32_t)formattedSurface->w;
    result.height = (uint32_t)formattedSurface->h;

    // 1. Create WebGPU Texture
    WGPUTextureDescriptor textureDesc = {};
    textureDesc.size.width = result.width;
    textureDesc.size.height = result.height;
    textureDesc.size.depthOrArrayLayers = 1;
    textureDesc.mipLevelCount = 1;
    textureDesc.sampleCount = 1;
    textureDesc.dimension = WGPUTextureDimension_2D;
    textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
    textureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    result.texture = wgpuDeviceCreateTexture(device, &textureDesc);
    if (!result.texture)
    {
        SDL_DestroySurface(formattedSurface);
        return result;
    }

    // 2. Upload Pixel Data using current WebGPU TexelCopy structs
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = result.texture;
    destination.mipLevel = 0;
    destination.origin = (WGPUOrigin3D) { 0, 0, 0 };
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = (uint32_t)formattedSurface->pitch;
    dataLayout.rowsPerImage = result.height;

    WGPUExtent3D writeSize = {
        .width = result.width,
        .height = result.height,
        .depthOrArrayLayers = 1
    };

    wgpuQueueWriteTexture(queue, &destination, formattedSurface->pixels,
        (size_t)formattedSurface->pitch * result.height,
        &dataLayout, &writeSize);

    SDL_DestroySurface(formattedSurface);

    // 3. Create Texture View
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    result.view = wgpuTextureCreateView(result.texture, &viewDesc);

    // 4. Create Sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.lodMinClamp = 0.0f;
    samplerDesc.lodMaxClamp = 1.0f;
    samplerDesc.maxAnisotropy = 1;

    result.sampler = wgpuDeviceCreateSampler(device, &samplerDesc);

    return result;
}

static inline WebGPUTexture createTexture(WGPUDevice device, WGPUQueue queue, const char *path)
{
    WebGPUTexture empty = { NULL, NULL, NULL, 0, 0 };

    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io)
    {
        SDL_Log("Failed to open file stream for %s: %s", path, SDL_GetError());
        return empty;
    }

    SDL_Surface *surface = NULL;

    // Detect format by extension to pass explicit hint to SDL_image
    const char *ext = strrchr(path, '.');
    if (ext && (SDL_strcasecmp(ext, ".webp") == 0))
    {
        surface = IMG_LoadTyped_IO(io, 0, "WEBP");
    }
    else if (ext && (SDL_strcasecmp(ext, ".png") == 0))
    {
        surface = IMG_LoadTyped_IO(io, 0, "PNG");
    }

    // Fallback: Reset stream seek position and try auto-detect if typed load failed or extension was untyped
    if (!surface)
    {
        SDL_SeekIO(io, 0, SDL_IO_SEEK_SET);
        surface = IMG_Load_IO(io, 0);
    }

    // Explicitly close the stream after decoding complete
    SDL_CloseIO(io);

    if (!surface)
    {
        SDL_Log("Image loading failed for %s: %s", path, SDL_GetError());
        return empty;
    }

    WebGPUTexture tex = createTextureFromSurface(device, queue, surface);
    SDL_DestroySurface(surface);

    return tex;
}

static inline void destroyWebGPUTexture(WebGPUTexture *tex)
{
    if (!tex)
        return;
    if (tex->sampler)
        wgpuSamplerRelease(tex->sampler);
    if (tex->view)
        wgpuTextureViewRelease(tex->view);
    if (tex->texture)
        wgpuTextureRelease(tex->texture);
    tex->sampler = NULL;
    tex->view = NULL;
    tex->texture = NULL;
}
