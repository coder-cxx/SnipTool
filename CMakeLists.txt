cmake_minimum_required(VERSION 3.16)
project(SnipTool VERSION 1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Treat as Windows GUI application (uses wWinMain, no console)
set(CMAKE_WIN32_EXECUTABLE TRUE)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /D_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()

add_executable(SnipTool WIN32
    src/main.cpp
    src/SnipOverlay.cpp
    src/ImageEditor.cpp
    resources/SnipTool.rc
)

target_include_directories(SnipTool PRIVATE
    include
    resources
)

# Resource compiler needs to find scissor.ico relative to the .rc file
set_source_files_properties(resources/SnipTool.rc PROPERTIES
    COMPILE_FLAGS "/I${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

target_link_libraries(SnipTool PRIVATE
    gdi32
    user32
    msimg32
    ole32
    shell32
    comdlg32
    dwmapi
)

if(MINGW)
    target_link_libraries(SnipTool PRIVATE -static-libgcc -static-libstdc++)
endif()

install(TARGETS SnipTool RUNTIME DESTINATION bin)
