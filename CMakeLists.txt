set(CMAKE_BUILD_TYPE "Debug")

cmake_minimum_required(VERSION 3.21)
project(simple-texture-webgpu-sdl3-cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# Define Project Source Files
# -----------------------------------------------------------------------------
set(PROJECT_SOURCES
    src/main.cpp
    src/texture_utils.h
    src/webgpu_context.h
    src/webgpu_context.cpp
)

# -----------------------------------------------------------------------------
# Declare Target Type based on OS
# -----------------------------------------------------------------------------
if(ANDROID)
    # Android requires a shared library (.so) for SDLActivity
    add_library(app SHARED)
else()
    # Windows & Emscripten require an executable target
    add_executable(app)
endif()

# Attach sources to the target dynamically
target_sources(app PRIVATE ${PROJECT_SOURCES})

# -----------------------------------------------------------------------------
# Platform Linking & Dependencies
# -----------------------------------------------------------------------------
if(EMSCRIPTEN)
    # Emscripten / WebAssembly Build Settings
    set(WEBP_PATH "C:/libs/webp-1.6.0-wasm")

    # Core WebP library
    add_library(webp STATIC IMPORTED)
    set_target_properties(webp PROPERTIES
        IMPORTED_LOCATION "${WEBP_PATH}/lib/libwebp.a"
        INTERFACE_INCLUDE_DIRECTORIES "${WEBP_PATH}/include"
    )

    # WebP Demuxing (reading animations)
    add_library(webpdemux STATIC IMPORTED)
    set_target_properties(webpdemux PROPERTIES
        IMPORTED_LOCATION "${WEBP_PATH}/lib/libwebpdemux.a"
        INTERFACE_INCLUDE_DIRECTORIES "${WEBP_PATH}/include"
    )

    # WebP Muxing (writing/encoding animations - resolves WebPMux and WebPAnimEncoder)
    add_library(webpmux STATIC IMPORTED)
    set_target_properties(webpmux PROPERTIES
        IMPORTED_LOCATION "${WEBP_PATH}/lib/libwebpmux.a"
        INTERFACE_INCLUDE_DIRECTORIES "${WEBP_PATH}/include"
    )

    # SharpYUV helper library
    add_library(sharpyuv STATIC IMPORTED)
    set_target_properties(sharpyuv PROPERTIES
        IMPORTED_LOCATION "${WEBP_PATH}/lib/libsharpyuv.a"
        INTERFACE_INCLUDE_DIRECTORIES "${WEBP_PATH}/include"
    )

    # SDL3 Image Static
    add_library(SDL3_image_Static STATIC IMPORTED)
    set_target_properties(SDL3_image_Static PROPERTIES
        IMPORTED_LOCATION "C:/libs/sdl3-image-3.4.4-wasm/lib/libSDL3_image.a"
        INTERFACE_INCLUDE_DIRECTORIES "C:/libs/sdl3-image-3.4.4-wasm/include"
        # Order is crucial: webpmux before webp, sharpyuv at the end
        INTERFACE_LINK_LIBRARIES "webpmux;webpdemux;webp;sharpyuv"
    )

    # SDL3 Core Static
    add_library(SDL3_Static STATIC IMPORTED)
    set_target_properties(SDL3_Static PROPERTIES
        IMPORTED_LOCATION "C:/libs/sdl3-3.4.16-wasm/lib/libSDL3.a"
        INTERFACE_INCLUDE_DIRECTORIES "C:/libs/sdl3-3.4.16-wasm/include"
    )

    target_link_libraries(app PRIVATE
        SDL3_image_Static
        SDL3_Static
    )

    target_compile_options(app PRIVATE
        -Os
        --use-port=emdawnwebgpu
    )

    target_link_options(app PRIVATE
        "-Os"
        "-sMALLOC=emmalloc"
        "-sUSE_SDL=0"
        "--use-port=emdawnwebgpu"
        "SHELL:--embed-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
    )

elseif(ANDROID)
    add_library(SDL3 SHARED IMPORTED)
    set_target_properties(SDL3 PROPERTIES
        IMPORTED_LOCATION "C:/libs/sdl3-3.4.16-android/${ANDROID_ABI}/lib/libSDL3.so"
        INTERFACE_INCLUDE_DIRECTORIES "C:/libs/sdl3-3.4.16-android/${ANDROID_ABI}/include"
    )

    add_library(SDL3_image SHARED IMPORTED)
    set_target_properties(SDL3_image PROPERTIES
        IMPORTED_LOCATION "C:/libs/sdl3-image-3.4.4-android/${ANDROID_ABI}/lib/libSDL3_image.so"
        INTERFACE_INCLUDE_DIRECTORIES "C:/libs/sdl3-image-3.4.4-android/${ANDROID_ABI}/include"
        INTERFACE_LINK_LIBRARIES "webpmux;webpdemux;webp;sharpyuv"
    )

    add_library(wgpu_native STATIC IMPORTED)
    set_target_properties(wgpu_native PROPERTIES
        IMPORTED_LOCATION "C:/libs/wgpu-29.0.1.1-android/${ANDROID_ABI}/lib/libwgpu_native.a"
        INTERFACE_INCLUDE_DIRECTORIES "C:/libs/wgpu-29.0.1.1-android/${ANDROID_ABI}/include"
    )

    # -------------------------------------------------------------------------
    # WebP Libraries Setup
    # -------------------------------------------------------------------------
    set(WEBP_BASE_DIR "C:/libs/webp-1.6.0-android/${ANDROID_ABI}")

    add_library(webp SHARED IMPORTED)
    set_target_properties(webp PROPERTIES
        IMPORTED_LOCATION "${WEBP_BASE_DIR}/lib/libwebp.so"
        INTERFACE_INCLUDE_DIRECTORIES "${WEBP_BASE_DIR}/include"
    )

    add_library(webpdemux SHARED IMPORTED)
    set_target_properties(webpdemux PROPERTIES
        IMPORTED_LOCATION "${WEBP_BASE_DIR}/lib/libwebpdemux.so"
        INTERFACE_INCLUDE_DIRECTORIES "${WEBP_BASE_DIR}/include"
    )

    add_library(webpmux SHARED IMPORTED)
    set_target_properties(webpmux PROPERTIES
        IMPORTED_LOCATION "${WEBP_BASE_DIR}/lib/libwebpmux.so"
        INTERFACE_INCLUDE_DIRECTORIES "${WEBP_BASE_DIR}/include"
    )

    add_library(sharpyuv SHARED IMPORTED)
    set_target_properties(sharpyuv PROPERTIES
        IMPORTED_LOCATION "${WEBP_BASE_DIR}/lib/libsharpyuv.so"
        INTERFACE_INCLUDE_DIRECTORIES "${WEBP_BASE_DIR}/include"
    )

    find_library(ANDROID_LIB android REQUIRED)
    find_library(LOG_LIB log REQUIRED)

    target_link_libraries(app PRIVATE
        SDL3_image
        SDL3
        wgpu_native
        ${ANDROID_LIB}
        ${LOG_LIB}
    )

elseif(WIN32)
    set(WEBP_PATH "C:/libs/webp-1.6.0-mingw")

    # Core WebP library
    add_library(webp STATIC IMPORTED)
    set_target_properties(webp PROPERTIES
        IMPORTED_LOCATION "${WEBP_PATH}/lib/libwebp.a"
        INTERFACE_INCLUDE_DIRECTORIES "${WEBP_PATH}/include"
    )

    # WebP Demuxing (reading animations)
    add_library(webpdemux STATIC IMPORTED)
    set_target_properties(webpdemux PROPERTIES
        IMPORTED_LOCATION "${WEBP_PATH}/lib/libwebpdemux.a"
        INTERFACE_INCLUDE_DIRECTORIES "${WEBP_PATH}/include"
    )

    # WebP Muxing (writing/encoding animations - resolves WebPMux and WebPAnimEncoder)
    add_library(webpmux STATIC IMPORTED)
    set_target_properties(webpmux PROPERTIES
        IMPORTED_LOCATION "${WEBP_PATH}/lib/libwebpmux.a"
        INTERFACE_INCLUDE_DIRECTORIES "${WEBP_PATH}/include"
    )

    # SharpYUV helper library
    add_library(sharpyuv STATIC IMPORTED)
    set_target_properties(sharpyuv PROPERTIES
        IMPORTED_LOCATION "${WEBP_PATH}/lib/libsharpyuv.a"
    )

    # SDL3 Image Static
    add_library(SDL3_image_Static STATIC IMPORTED)
    set_target_properties(SDL3_image_Static PROPERTIES
        IMPORTED_LOCATION "C:/libs/sdl3-image-3.4.4-mingw/lib/libSDL3_image.a"
        INTERFACE_INCLUDE_DIRECTORIES "C:/libs/sdl3-image-3.4.4-mingw/include"
        # Order is crucial: webpmux before webp, sharpyuv at the end
        INTERFACE_LINK_LIBRARIES "webpmux;webpdemux;webp;sharpyuv"
    )

    # SDL3 Core Static
    add_library(SDL3_Static STATIC IMPORTED)
    set_target_properties(SDL3_Static PROPERTIES
        IMPORTED_LOCATION "C:/libs/sdl3-3.4.16-mingw/lib/libSDL3.a"
        INTERFACE_INCLUDE_DIRECTORIES "C:/libs/sdl3-3.4.16-mingw/include"
        INTERFACE_LINK_LIBRARIES "winmm;imm32;version;setupapi;dinput8"
    )

    # WGPU Static
    add_library(wgpu_native STATIC IMPORTED)
    set_target_properties(wgpu_native PROPERTIES
        IMPORTED_LOCATION "C:/libs/wgpu-29.0.1.1-mingw/lib/libwgpu_native.a"
        INTERFACE_INCLUDE_DIRECTORIES "C:/libs/wgpu-29.0.1.1-mingw/include"
        INTERFACE_LINK_LIBRARIES "ws2_32;userenv;ntdll;bcrypt;d3d12;dxgi"
    )

    target_link_libraries(app PRIVATE
        SDL3_image_Static
        SDL3_Static
        wgpu_native
    )

    target_compile_options(app PRIVATE -Os)
    target_link_options(app PRIVATE -static -s -static-libgcc -static-libstdc++)

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(app PRIVATE -mconsole)
    else()
        target_link_options(app PRIVATE -mwindows)
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
        add_custom_command(TARGET app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:app>/assets"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_SOURCE_DIR}/assets"
                    "$<TARGET_FILE_DIR:app>/assets"
            COMMENT "Copying assets directory"
        )
    endif()
endif()
