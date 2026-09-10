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

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(glew-cmake_BUILD_SHARED OFF CACHE BOOL "" FORCE)

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
set(OPENCL_FOUND FALSE)

# Explicit OpenCL paths
if(OPENCL_INCLUDE_DIR AND OPENCL_LIBRARY)
    set(OPENCL_FOUND TRUE)

    message(STATUS "OpenCL found using explicitly specified paths:")
    message(STATUS "  Headers: ${OPENCL_INCLUDE_DIR}")
    message(STATUS "  Library: ${OPENCL_LIBRARY}")
endif()

# OpenCL_ROOT
if(NOT OPENCL_FOUND AND OPENCL_ROOT)
    find_path(OPENCL_INCLUDE_DIR
        NAMES CL/cl.h
        HINTS "${OPENCL_ROOT}"
        PATH_SUFFIXES include
    )

    find_library(OPENCL_LIBRARY
        NAMES OpenCL
        HINTS "${OPENCL_ROOT}"
        PATH_SUFFIXES
            lib
            lib64
            lib/x64
    )

    if(OPENCL_INCLUDE_DIR AND OPENCL_LIBRARY)
        set(OPENCL_FOUND TRUE)

        message(STATUS "OpenCL found using OPENCL_ROOT:")
        message(STATUS "  Root:    ${OPENCL_ROOT}")
        message(STATUS "  Headers: ${OPENCL_INCLUDE_DIR}")
        message(STATUS "  Library: ${OPENCL_LIBRARY}")
    endif()
endif()

# Windows: try CUDA Toolkit
if(WIN32 AND NOT OPENCL_FOUND)
    find_program(CUDA_NVCC
        NAMES nvcc
        HINTS
            "$ENV{CUDA_PATH}/bin"
            "$ENV{CUDA_HOME}/bin"
    )

    if(CUDA_NVCC)
        get_filename_component(CUDA_BIN_DIR "${CUDA_NVCC}" DIRECTORY)
        get_filename_component(CUDA_ROOT "${CUDA_BIN_DIR}" DIRECTORY)

        find_path(OPENCL_INCLUDE_DIR
            NAMES CL/cl.h
            HINTS
                "${CUDA_ROOT}"
                "$ENV{CUDA_PATH}"
            PATH_SUFFIXES include
        )

        find_library(OPENCL_LIBRARY
            NAMES OpenCL
            HINTS
                "${CUDA_ROOT}"
                "$ENV{CUDA_PATH}"
            PATH_SUFFIXES
                lib/x64
                lib64
                lib
        )

        if(OPENCL_INCLUDE_DIR AND OPENCL_LIBRARY)
            set(OPENCL_FOUND TRUE)

            message(STATUS "OpenCL found through CUDA Toolkit:")
            message(STATUS "  CUDA:    ${CUDA_ROOT}")
            message(STATUS "  Headers: ${OPENCL_INCLUDE_DIR}")
            message(STATUS "  Library: ${OPENCL_LIBRARY}")
        endif()
    endif()
endif()


if(NOT OPENCL_FOUND)
    find_package(OpenCL QUIET)

    if(OpenCL_FOUND)
        set(OPENCL_FOUND TRUE)

        message(STATUS "OpenCL found using CMake FindOpenCL:")
        message(STATUS "  Headers: ${OpenCL_INCLUDE_DIRS}")
        message(STATUS "  Library: ${OpenCL_LIBRARIES}")
    endif()
endif()

# Create a consistent OpenCL::OpenCL target
if(OPENCL_FOUND AND NOT TARGET OpenCL::OpenCL)
    add_library(OpenCL::OpenCL UNKNOWN IMPORTED)

    set_target_properties(OpenCL::OpenCL PROPERTIES
        IMPORTED_LOCATION "${OPENCL_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENCL_INCLUDE_DIR}"
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenCL::OpenCL
)

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