cmake_minimum_required(VERSION 4.4)
project(OpenCLFilters VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

# GLEW
FetchContent_Declare(
    glew
    GIT_REPOSITORY https://github.com/Perlmint/glew-cmake
    GIT_TAG glew-cmake-2.2.0-1
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glew)

# OpenGL
find_package(OpenGL REQUIRED)

# imgui_cmake_wrapper expects OpenGL::OpenGL.
# CMake normally provides OpenGL::GL.
if(NOT TARGET OpenGL::OpenGL)
    if(TARGET OpenGL::GL)
        add_library(OpenGL::OpenGL INTERFACE IMPORTED)
        set_target_properties(OpenGL::OpenGL PROPERTIES
            INTERFACE_LINK_LIBRARIES OpenGL::GL
        )
    else()
        message(FATAL_ERROR
            "OpenGL was found, but neither OpenGL::OpenGL nor OpenGL::GL exists."
        )
    endif()
endif()

# CPM.cmake
file(DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.43.1/CPM.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

# ImGui
CPMAddPackage(
    NAME imgui_cmake_wrapper
    GITHUB_REPOSITORY aosterthun/imgui_cmake_wrapper
    GIT_TAG 0.0.2
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
    OPTIONS
        "IMGUI_RENDERER_BACKEND opengl3"
        "IMGUI_PLATFORM_BACKEND glfw"
)

# stb
CPMAddPackage(
    NAME stb
    GITHUB_REPOSITORY gracicot/stb-cmake
    GIT_TAG master
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
)

# tinyfiledialogs
CPMAddPackage(
    NAME tinyfiledialogs
    GITHUB_REPOSITORY D31m05z/tinyfiledialogs
    GIT_TAG master
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
)

# Sources
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(${PROJECT_NAME} ${APP_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# OpenCL
if(APPLE)
    # macOS provides OpenCL as a system framework.
    find_package(OpenCL REQUIRED)

    target_link_libraries(${PROJECT_NAME} PRIVATE
        OpenCL::OpenCL
    )

elseif(WIN32)
    # Prefer OpenCL from CUDA toolkit on Windows.
    find_program(CUDA_NVCC nvcc)

    if(CUDA_NVCC)
        get_filename_component(CUDA_BIN_DIR "${CUDA_NVCC}" DIRECTORY)
        get_filename_component(CUDA_ROOT "${CUDA_BIN_DIR}" DIRECTORY)

        find_path(OPENCL_INCLUDE_DIR
            NAMES CL/cl.h
            HINTS "${CUDA_ROOT}"
            PATH_SUFFIXES include
        )

        find_library(OPENCL_LIBRARY
            NAMES OpenCL
            HINTS "${CUDA_ROOT}"
            PATH_SUFFIXES lib/x64 lib64 lib
        )
    endif()

    if(OPENCL_INCLUDE_DIR AND OPENCL_LIBRARY)
        target_include_directories(${PROJECT_NAME} PRIVATE
            "${OPENCL_INCLUDE_DIR}"
        )
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "${OPENCL_LIBRARY}"
        )
    else()
        find_package(OpenCL REQUIRED)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            OpenCL::OpenCL
        )
    endif()

else()
    # Linux and other Unix platforms.
    find_package(OpenCL REQUIRED)

    target_link_libraries(${PROJECT_NAME} PRIVATE
        OpenCL::OpenCL
    )
endif()

# Libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    libglew_static
    imgui_cmake_wrapper
    stb::image
    tinyfiledialogs
    OpenGL::GL
)

# Windows
if(WIN32)
    # GUI application: don't open a console window.
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# MinGW
if(MINGW)
    target_link_options(${PROJECT_NAME} PRIVATE
        -static-libgcc
        -static-libstdc++
        -static
    )
endif()

# macOS
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "edu.openclfilters.OpenCLFilters"
        MACOSX_BUNDLE_BUNDLE_NAME "OpenCLFilters"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

# Installation
include(GNUInstallDirs)

if(APPLE)
    install(TARGETS ${PROJECT_NAME}
        BUNDLE DESTINATION .
    )
else()
    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()