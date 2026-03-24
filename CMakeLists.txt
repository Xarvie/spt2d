cmake_minimum_required(VERSION 3.20)
project(spt3d LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SDL3_ROOT "C:/msvcpkg/SDL3")
set(GLM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libs/glm")
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(game)

target_include_directories(game PRIVATE 
    ${SRC_DIR}
    ${SRC_DIR}/core
    ${SRC_DIR}/platform
    ${SRC_DIR}/graphics
    ${SRC_DIR}/resource
    ${SRC_DIR}/glad
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/libs
    ${GLM_ROOT}
)

set(SPT3D_SOURCES
    ${SRC_DIR}/core/Color.cpp
    ${SRC_DIR}/core/Geometry.cpp
    ${SRC_DIR}/core/Lifecycle.cpp
    ${SRC_DIR}/core/Texture.cpp
    ${SRC_DIR}/core/Shader.cpp
    ${SRC_DIR}/core/Material.cpp
    ${SRC_DIR}/core/RenderTarget.cpp
    ${SRC_DIR}/core/Mesh.cpp
    ${SRC_DIR}/core/Pipeline.cpp
    ${SRC_DIR}/core/Camera.cpp
    ${SRC_DIR}/core/Light.cpp
    ${SRC_DIR}/core/Model.cpp
    ${SRC_DIR}/graphics/Shader.cpp
    ${SRC_DIR}/vfs/VirtualFileSystem.cpp
    ${SRC_DIR}/vfs/VFS.cpp
    ${SRC_DIR}/vfs/providers/NativeFileSystem.cpp
)

if(EMSCRIPTEN)
    if(WXGAME)
        message(STATUS "Building for: WeChat Mini Game (WASM)")
        target_compile_definitions(game PRIVATE __WXGAME__)

        target_sources(game PRIVATE
            ${SRC_DIR}/Main.cpp
            ${SRC_DIR}/platform/wx/WxPlatform.cpp
            ${SRC_DIR}/graphics/Graphics.cpp
            ${SRC_DIR}/graphics/Texture.cpp
            ${SRC_DIR}/graphics/Canvas.cpp
            ${SRC_DIR}/core/ThreadModel.cpp
            ${SRC_DIR}/resource/ResourceManager.cpp
            ${SRC_DIR}/resource/TextureResource.cpp
            ${SRC_DIR}/resource/RawDataResource.cpp
            ${SRC_DIR}/glad/glad.c
            ${SPT3D_SOURCES}
        )

        target_link_options(game PRIVATE
            -sWASM=1 -sMODULARIZE=1 -sEXPORT_NAME=GameModule
            "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_WxBridge_OnTouch','_WxBridge_OnKey','_WxBridge_OnMouse','_WxBridge_OnResize','_WxBridge_OnSafeAreaChange','_WxBridge_OnHttpResponse','_WxBridge_OnAppShow','_WxBridge_OnAppHide','_WxBridge_OnError','_WxBridge_OnMemoryWarning']"
            "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','stringToUTF8','UTF8ToString','lengthBytesUTF8','getValue','setValue','allocateUTF8','GL']"
            -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=16777216
            -sUSE_WEBGL2=1 -sFULL_ES3=1
            -sNO_EXIT_RUNTIME=1 -sEXPORT_ES6=0 -sUSE_ES6_IMPORT_META=0
            -sDYNAMIC_EXECUTION=0 -sFILESYSTEM=0
            -O2
        )
    else()
        message(STATUS "Building for: Web (Emscripten)")
        target_sources(game PRIVATE
            ${SRC_DIR}/Main.cpp
            ${SRC_DIR}/platform/web/WebPlatform.cpp
            ${SRC_DIR}/graphics/Graphics.cpp
            ${SRC_DIR}/graphics/Texture.cpp
            ${SRC_DIR}/graphics/Canvas.cpp
            ${SRC_DIR}/core/ThreadModel.cpp
            ${SRC_DIR}/resource/ResourceManager.cpp
            ${SRC_DIR}/resource/TextureResource.cpp
            ${SRC_DIR}/resource/RawDataResource.cpp
            ${SRC_DIR}/glad/glad.c
            ${SPT3D_SOURCES}
        )
        target_link_options(game PRIVATE
            -sWASM=1
            "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']"
            "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','stringToUTF8','UTF8ToString','lengthBytesUTF8']"
            -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=16777216
            -sUSE_WEBGL2=1 -sFULL_ES3=1
            -O2
        )
    endif()
elseif(APPLE)
    message(STATUS "Building for: macOS (SDL3)")
    target_compile_definitions(game PRIVATE __OS_MACOS)
    target_sources(game PRIVATE
        ${SRC_DIR}/Main.cpp
        ${SRC_DIR}/platform/sdl3/Sdl3Platform.cpp
        ${SRC_DIR}/graphics/Graphics.cpp
        ${SRC_DIR}/graphics/Texture.cpp
        ${SRC_DIR}/graphics/Canvas.cpp
        ${SRC_DIR}/core/ThreadModel.cpp
        ${SRC_DIR}/resource/ResourceManager.cpp
        ${SRC_DIR}/resource/TextureResource.cpp
        ${SRC_DIR}/resource/RawDataResource.cpp
        ${SRC_DIR}/glad/glad.c
        ${SPT3D_SOURCES}
    )
    find_package(OpenGL REQUIRED)
    target_link_libraries(game PRIVATE OpenGL::GL)
elseif(UNIX)
    message(STATUS "Building for: Linux (SDL3)")
    target_compile_definitions(game PRIVATE __OS_LINUX)
    target_sources(game PRIVATE
        ${SRC_DIR}/Main.cpp
        ${SRC_DIR}/platform/sdl3/Sdl3Platform.cpp
        ${SRC_DIR}/graphics/Graphics.cpp
        ${SRC_DIR}/graphics/Texture.cpp
        ${SRC_DIR}/graphics/Canvas.cpp
        ${SRC_DIR}/core/ThreadModel.cpp
        ${SRC_DIR}/resource/ResourceManager.cpp
        ${SRC_DIR}/resource/TextureResource.cpp
        ${SRC_DIR}/resource/RawDataResource.cpp
        ${SRC_DIR}/glad/glad.c
        ${SPT3D_SOURCES}
    )
    find_package(OpenGL REQUIRED)
    target_link_libraries(game PRIVATE OpenGL::GL)
else()
    message(STATUS "Building for: Windows (SDL3)")
    target_compile_definitions(game PRIVATE __OS_WINDOWS)
    target_sources(game PRIVATE
        ${SRC_DIR}/Main.cpp
        ${SRC_DIR}/platform/sdl3/Sdl3Platform.cpp
        ${SRC_DIR}/graphics/Graphics.cpp
        ${SRC_DIR}/graphics/Texture.cpp
        ${SRC_DIR}/graphics/Canvas.cpp
        ${SRC_DIR}/core/ThreadModel.cpp
        ${SRC_DIR}/resource/ResourceManager.cpp
        ${SRC_DIR}/resource/TextureResource.cpp
        ${SRC_DIR}/resource/RawDataResource.cpp
        ${SRC_DIR}/glad/glad.c
        ${SPT3D_SOURCES}
    )
    target_include_directories(game PRIVATE "${SDL3_ROOT}/include")
    target_link_libraries(game PRIVATE "${SDL3_ROOT}/lib/x64/SDL3.lib")
    add_custom_command(TARGET game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL3_ROOT}/lib/x64/SDL3.dll"
        $<TARGET_FILE_DIR:game>
    )
    find_package(OpenGL REQUIRED)
    target_link_libraries(game PRIVATE OpenGL::GL)
endif()
