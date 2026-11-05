cmake_minimum_required(VERSION 3.16)
project(SnipTool VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows subsystem (no console)
if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")
else()
    add_compile_options(-Wall -Wextra)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mwindows")
endif()

add_executable(SnipTool
    src/main.cpp
    src/SnipOverlay.cpp
    src/ImageEditor.cpp
)

target_include_directories(SnipTool PRIVATE include)

target_link_libraries(SnipTool PRIVATE
    gdi32
    user32
    msimg32
    ole32
    shell32
    comdlg32
)

# For MinGW
if(MINGW)
    target_link_libraries(SnipTool PRIVATE -static-libgcc -static-libstdc++)
endif()

install(TARGETS SnipTool RUNTIME DESTINATION bin)