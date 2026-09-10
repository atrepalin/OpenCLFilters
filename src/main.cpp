#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <regex>
#include <memory>
#include <algorithm>
#include <chrono>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#define CL_TARGET_OPENCL_VERSION 120

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

#include "stb/stb_image.h"
#include "tinyfiledialogs/tinyfiledialogs.h"

// ================================================================
// Helpers
// ================================================================

static std::string readTextFile(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file)
    {
        return {};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static std::string getCLErrorString(cl_int error)
{
    switch (error)
    {
    case CL_SUCCESS:
        return "CL_SUCCESS";
    case CL_DEVICE_NOT_FOUND:
        return "CL_DEVICE_NOT_FOUND";
    case CL_DEVICE_NOT_AVAILABLE:
        return "CL_DEVICE_NOT_AVAILABLE";
    case CL_COMPILER_NOT_AVAILABLE:
        return "CL_COMPILER_NOT_AVAILABLE";
    case CL_MEM_OBJECT_ALLOCATION_FAILURE:
        return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
    case CL_OUT_OF_RESOURCES:
        return "CL_OUT_OF_RESOURCES";
    case CL_OUT_OF_HOST_MEMORY:
        return "CL_OUT_OF_HOST_MEMORY";
    case CL_BUILD_PROGRAM_FAILURE:
        return "CL_BUILD_PROGRAM_FAILURE";
    case CL_INVALID_VALUE:
        return "CL_INVALID_VALUE";
    case CL_INVALID_DEVICE:
        return "CL_INVALID_DEVICE";
    case CL_INVALID_CONTEXT:
        return "CL_INVALID_CONTEXT";
    case CL_INVALID_MEM_OBJECT:
        return "CL_INVALID_MEM_OBJECT";
    case CL_INVALID_COMMAND_QUEUE:
        return "CL_INVALID_COMMAND_QUEUE";
    case CL_INVALID_KERNEL:
        return "CL_INVALID_KERNEL";
    case CL_INVALID_KERNEL_ARGS:
        return "CL_INVALID_KERNEL_ARGS";
    case CL_INVALID_WORK_GROUP_SIZE:
        return "CL_INVALID_WORK_GROUP_SIZE";
    default:
        return "UNKNOWN_OPENCL_ERROR";
    }
}

// ================================================================
// Image
// ================================================================

struct Image
{
    int width = 0;
    int height = 0;

    // RGBA8
    std::vector<unsigned char> pixels;

    GLuint texture = 0;

    bool load(const char *filename)
    {
        int channels = 0;

        unsigned char *data = stbi_load(filename, &width, &height, &channels, 4);

        if (!data)
        {
            return false;
        }

        pixels.assign(data, data + width * height * 4);

        stbi_image_free(data);

        uploadTexture();
        return true;
    }

    void uploadTexture()
    {
        if (texture == 0)
        {
            glGenTextures(1, &texture);
        }

        glBindTexture(GL_TEXTURE_2D, texture);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA8,
                     width,
                     height,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     pixels.data());

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void destroy()
    {
        if (texture)
        {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
    }
};

// ================================================================
// OpenCL state
// ================================================================

struct OpenCLState
{
    std::vector<cl_platform_id> platforms;
    std::vector<cl_device_id> devices;

    int selectedPlatform = 0;
    int selectedDevice = 0;

    cl_context context = nullptr;
    cl_command_queue queue = nullptr;

    cl_program program = nullptr;

    std::vector<std::string> kernelNames;
    int selectedKernel = 0;

    std::string source;
    std::string sourceFilename;

    std::string buildLog;

    ~OpenCLState()
    {
        cleanup();
    }

    void cleanup()
    {
        if (program)
        {
            clReleaseProgram(program);
            program = nullptr;
        }

        if (queue)
        {
            clReleaseCommandQueue(queue);
            queue = nullptr;
        }

        if (context)
        {
            clReleaseContext(context);
            context = nullptr;
        }
    }

    void enumerate()
    {
        platforms.clear();
        devices.clear();

        cl_uint platformCount = 0;

        cl_int err = clGetPlatformIDs(0, nullptr, &platformCount);

        if (err != CL_SUCCESS || platformCount == 0)
        {
            return;
        }

        platforms.resize(platformCount);

        clGetPlatformIDs(platformCount, platforms.data(), nullptr);

        if (selectedPlatform >= (int)platforms.size())
        {
            selectedPlatform = 0;
        }

        if (platforms.empty())
        {
            return;
        }

        cl_uint deviceCount = 0;

        err = clGetDeviceIDs(platforms[selectedPlatform],
                             CL_DEVICE_TYPE_ALL,
                             0,
                             nullptr,
                             &deviceCount);

        if (err != CL_SUCCESS || deviceCount == 0)
        {
            return;
        }

        devices.resize(deviceCount);

        clGetDeviceIDs(platforms[selectedPlatform],
                       CL_DEVICE_TYPE_ALL,
                       deviceCount,
                       devices.data(),
                       nullptr);

        if (selectedDevice >= (int)devices.size())
        {
            selectedDevice = 0;
        }
    }

    std::string platformName(int index) const
    {
        if (index < 0 || index >= (int)platforms.size())
        {
            return {};
        }

        size_t size = 0;

        clGetPlatformInfo(platforms[index],
                          CL_PLATFORM_NAME,
                          0,
                          nullptr,
                          &size);

        std::string result(size, '\0');

        clGetPlatformInfo(platforms[index],
                          CL_PLATFORM_NAME,
                          size,
                          result.data(),
                          nullptr);

        if (!result.empty() && result.back() == '\0')
        {
            result.pop_back();
        }

        return result;
    }

    std::string deviceName(int index) const
    {
        if (index < 0 || index >= (int)devices.size())
        {
            return {};
        }

        size_t size = 0;

        clGetDeviceInfo(devices[index],
                        CL_DEVICE_NAME,
                        0,
                        nullptr,
                        &size);

        std::string result(size, '\0');

        clGetDeviceInfo(devices[index],
                        CL_DEVICE_NAME,
                        size,
                        result.data(),
                        nullptr);

        if (!result.empty() && result.back() == '\0')
        {
            result.pop_back();
        }

        return result;
    }

    bool createContext()
    {
        cleanup();

        if (platforms.empty() || devices.empty())
        {
            return false;
        }

        cl_int err = CL_SUCCESS;

        cl_device_id device = devices[selectedDevice];

        context = clCreateContext(nullptr,
                                  1,
                                  &device,
                                  nullptr,
                                  nullptr,
                                  &err);

        if (err != CL_SUCCESS)
        {
            context = nullptr;
            return false;
        }

        queue = clCreateCommandQueue(context,
                                     device,
                                     0,
                                     &err);

        if (err != CL_SUCCESS)
        {
            clReleaseContext(context);
            context = nullptr;
            return false;
        }

        return true;
    }

    // ============================================================
    // Kernel parser
    // ============================================================

    void parseKernels()
    {
        kernelNames.clear();
        selectedKernel = 0;

        const std::regex kernelRegex(
            R"((?:__kernel|kernel)\s+(?:(?:__attribute__\s*\(\([^)]+\)\)|[a-zA-Z_][a-zA-Z0-9_]*)\s+)*([a-zA-Z_][a-zA-Z0-9_]*)\s*\()");

        auto begin = std::sregex_iterator(source.begin(), source.end(), kernelRegex);

        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it)
        {
            if (it->size() > 1)
            {
                kernelNames.push_back((*it)[1].str());
            }
        }
    }

    bool build()
    {
        if (context == nullptr || devices.empty())
        {
            return false;
        }

        if (program)
        {
            clReleaseProgram(program);
            program = nullptr;
        }

        const char *sourcePtr = source.c_str();
        size_t sourceSize = source.size();

        cl_int err = CL_SUCCESS;

        program = clCreateProgramWithSource(context,
                                            1,
                                            &sourcePtr,
                                            &sourceSize,
                                            &err);

        if (err != CL_SUCCESS)
        {
            program = nullptr;
            buildLog = getCLErrorString(err);
            return false;
        }

        err = clBuildProgram(program,
                             1,
                             &devices[selectedDevice],
                             nullptr,
                             nullptr,
                             nullptr);

        // Always retrieve build log.
        size_t logSize = 0;

        clGetProgramBuildInfo(program,
                              devices[selectedDevice],
                              CL_PROGRAM_BUILD_LOG,
                              0,
                              nullptr,
                              &logSize);

        buildLog.resize(logSize);

        if (logSize)
        {
            clGetProgramBuildInfo(program,
                                  devices[selectedDevice],
                                  CL_PROGRAM_BUILD_LOG,
                                  logSize,
                                  buildLog.data(),
                                  nullptr);
        }

        if (err != CL_SUCCESS)
        {
            return false;
        }

        return true;
    }
};

// ================================================================
// Application
// ================================================================

struct Application
{
    GLFWwindow *window = nullptr;

    OpenCLState cl;

    Image inputImage;
    Image outputImage;

    bool initialized = false;

    std::string status;

    int kernelIterations = 1;

    // ------------------------------------------------------------
    // Initialization
    // ------------------------------------------------------------

    bool init()
    {
        if (!glfwInit())
        {
            std::cerr << "GLFW initialization failed\n";
            return false;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(
            GLFW_OPENGL_PROFILE,
            GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

        window = glfwCreateWindow(1280,
                                  720,
                                  "OpenCL Image Processor",
                                  nullptr,
                                  nullptr);

        if (!window)
        {
            glfwTerminate();
            return false;
        }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);

        glewExperimental = GL_TRUE;

        if (glewInit() != GLEW_OK)
        {
            std::cerr << "GLEW initialization failed\n";
            return false;
        }

        IMGUI_CHECKVERSION();

        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        (void)io;

        ImGui::StyleColorsLight();

        ImGui_ImplGlfw_InitForOpenGL(window, true);

        ImGui_ImplOpenGL3_Init("#version 330");

        cl.enumerate();

        if (!cl.platforms.empty())
        {
            cl.createContext();
        }

        initialized = true;

        return true;
    }

    // ------------------------------------------------------------
    // Load source
    // ------------------------------------------------------------

    void loadKernelFile()
    {
        const char *filters[] = {"*.cl", "*.ocl", "*.opencl"};

        const char *filename = tinyfd_openFileDialog("OpenCL source",
                                                     "",
                                                     3,
                                                     filters,
                                                     "OpenCL files",
                                                     0);

        if (!filename)
        {
            return;
        }

        std::string source = readTextFile(filename);

        if (source.empty())
        {
            status = "Failed to read source file";
            return;
        }

        cl.source = source;
        cl.sourceFilename = filename;

        cl.parseKernels();

        status = "Loaded: " +
                 std::string(filename) +
                 " | kernels: " +
                 std::to_string(cl.kernelNames.size());
    }

    // ------------------------------------------------------------
    // Load input image
    // ------------------------------------------------------------

    void loadInputImage()
    {
        const char *filters[] = {"*.png", "*.jpg", "*.jpeg", "*.bmp"};

        const char *filename = tinyfd_openFileDialog("Open image",
                                                     "",
                                                     4,
                                                     filters,
                                                     "Images",
                                                     0);

        if (!filename)
        {
            return;
        }

        if (!inputImage.load(filename))
        {
            status = "Failed to load image";
            return;
        }

        // Create output image with same size.
        outputImage.width = inputImage.width;
        outputImage.height = inputImage.height;
        outputImage.pixels.resize(inputImage.pixels.size());

        outputImage.uploadTexture();

        status = "Input image: " +
                 std::to_string(inputImage.width) +
                 " x " +
                 std::to_string(inputImage.height);
    }

    // ------------------------------------------------------------
    // Execute selected kernel
    // ------------------------------------------------------------

    bool executeKernel()
    {
        if (!cl.context || !cl.queue || !cl.program)
        {
            status = "OpenCL program is not built";
            return false;
        }

        if (cl.kernelNames.empty())
        {
            status = "No kernels found";
            return false;
        }

        if (inputImage.pixels.empty())
        {
            status = "Load an input image first";
            return false;
        }

        if (kernelIterations < 1)
        {
            kernelIterations = 1;
        }

        const std::string &name = cl.kernelNames[cl.selectedKernel];

        cl_int err = CL_SUCCESS;

        cl_kernel kernel = clCreateKernel(cl.program, name.c_str(), &err);

        if (err != CL_SUCCESS)
        {
            status = "clCreateKernel failed: " + getCLErrorString(err);

            return false;
        }

        // ============================================================
        // Image format
        // ============================================================

        cl_image_format format{};

        format.image_channel_order = CL_RGBA;
        format.image_channel_data_type = CL_UNSIGNED_INT8;

        cl_image_desc desc{};

        desc.image_type = CL_MEM_OBJECT_IMAGE2D;
        desc.image_width = inputImage.width;
        desc.image_height = inputImage.height;

        // ============================================================
        // Create two OpenCL images
        // ============================================================

        cl_mem imageA = clCreateImage(cl.context,
                                      CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                      &format,
                                      &desc,
                                      inputImage.pixels.data(),
                                      &err);

        if (err != CL_SUCCESS)
        {
            clReleaseKernel(kernel);

            status = "Failed to create input image: " + getCLErrorString(err);

            return false;
        }

        cl_mem imageB = clCreateImage(cl.context,
                                      CL_MEM_READ_WRITE,
                                      &format,
                                      &desc,
                                      nullptr,
                                      &err);

        if (err != CL_SUCCESS)
        {
            clReleaseMemObject(imageA);
            clReleaseKernel(kernel);

            status = "Failed to create output image: " + getCLErrorString(err);

            return false;
        }

        // ============================================================
        // Execute kernel N times
        // ============================================================

        auto start = std::chrono::high_resolution_clock::now();

        size_t globalSize[2] = {static_cast<size_t>(inputImage.width),
                                static_cast<size_t>(inputImage.height)};

        cl_mem inputCL = imageA;
        cl_mem outputCL = imageB;

        for (int i = 0; i < kernelIterations; ++i)
        {
            // input
            err = clSetKernelArg(kernel,
                                 0,
                                 sizeof(cl_mem),
                                 &inputCL);

            // output
            if (err == CL_SUCCESS)
            {
                err = clSetKernelArg(kernel,
                                     1,
                                     sizeof(cl_mem),
                                     &outputCL);
            }

            if (err != CL_SUCCESS)
            {
                status = "clSetKernelArg failed: " + getCLErrorString(err);

                clReleaseMemObject(imageB);
                clReleaseMemObject(imageA);
                clReleaseKernel(kernel);

                return false;
            }

            err = clEnqueueNDRangeKernel(cl.queue,
                                         kernel,
                                         2,
                                         nullptr,
                                         globalSize,
                                         nullptr,
                                         0,
                                         nullptr,
                                         nullptr);

            if (err != CL_SUCCESS)
            {
                status = "Kernel execution failed: " + getCLErrorString(err);

                clReleaseMemObject(imageB);
                clReleaseMemObject(imageA);
                clReleaseKernel(kernel);

                return false;
            }

            std::swap(inputCL, outputCL);
        }

        // Wait for all kernels to finish
        err = clFinish(cl.queue);

        if (err != CL_SUCCESS)
        {
            status = "clFinish failed: " + getCLErrorString(err);

            clReleaseMemObject(imageB);
            clReleaseMemObject(imageA);
            clReleaseKernel(kernel);

            return false;
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;

        // ============================================================
        // Read final image
        // ============================================================

        outputImage.width = inputImage.width;
        outputImage.height = inputImage.height;

        outputImage.pixels.resize(inputImage.width * inputImage.height * 4);

        size_t origin[3] = {0, 0, 0};

        size_t region[3] = {static_cast<size_t>(inputImage.width),
                            static_cast<size_t>(inputImage.height),
                            1};

        err = clEnqueueReadImage(cl.queue,
                                 inputCL,
                                 CL_TRUE,
                                 origin,
                                 region,
                                 0,
                                 0,
                                 outputImage.pixels.data(),
                                 0,
                                 nullptr,
                                 nullptr);

        if (err != CL_SUCCESS)
        {
            status = "clEnqueueReadImage failed: " + getCLErrorString(err);

            clReleaseMemObject(imageB);
            clReleaseMemObject(imageA);
            clReleaseKernel(kernel);

            return false;
        }

        // ============================================================
        // Upload result to OpenGL
        // ============================================================

        outputImage.uploadTexture();

        // ============================================================
        // Cleanup
        // ============================================================

        clReleaseMemObject(imageB);
        clReleaseMemObject(imageA);
        clReleaseKernel(kernel);

        status = "Executed kernel: " +
                 name +
                 " | iterations: " + std::to_string(kernelIterations) +
                 " | elapsed time: " + std::to_string(elapsed.count()) + " ms";

        return true;
    }

    // ------------------------------------------------------------
    // Toolbar
    // ------------------------------------------------------------

    void drawToolbar()
    {
        ImGui::BeginChild("Toolbar",
                          ImVec2(0, 48),
                          true,
                          ImGuiWindowFlags_NoScrollbar);

        // ============================================================
        // Platform
        // ============================================================

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Platform:");
        ImGui::SameLine();

        std::string platform = cl.platforms.empty()
                                   ? "None"
                                   : cl.platformName(cl.selectedPlatform);

        ImGui::SetNextItemWidth(150.0f);

        if (ImGui::BeginCombo("##platform", platform.c_str()))
        {
            for (int i = 0; i < (int)cl.platforms.size(); ++i)
            {
                bool selected = i == cl.selectedPlatform;

                if (ImGui::Selectable(cl.platformName(i).c_str(), selected))
                {
                    cl.selectedPlatform = i;
                    cl.selectedDevice = 0;

                    cl.enumerate();
                    cl.createContext();
                }

                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        ImGui::SameLine();

        // ============================================================
        // Device
        // ============================================================

        ImGui::TextUnformatted("Device:");
        ImGui::SameLine();

        std::string device = cl.devices.empty()
                                 ? "None"
                                 : cl.deviceName(cl.selectedDevice);

        ImGui::SetNextItemWidth(240.0f);

        if (ImGui::BeginCombo("##device", device.c_str()))
        {
            for (int i = 0; i < (int)cl.devices.size(); ++i)
            {
                bool selected = i == cl.selectedDevice;

                if (ImGui::Selectable(cl.deviceName(i).c_str(), selected))
                {
                    cl.selectedDevice = i;
                    cl.createContext();
                }

                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        ImGui::SameLine();

        // ============================================================
        // Load OpenCL source
        // ============================================================

        if (ImGui::Button("Load .cl"))
        {
            loadKernelFile();
        }

        ImGui::SameLine();

        // ============================================================
        // Load image
        // ============================================================

        if (ImGui::Button("Load Image"))
        {
            loadInputImage();
        }

        ImGui::SameLine();

        // ============================================================
        // Kernel
        // ============================================================

        ImGui::TextUnformatted("Kernel:");
        ImGui::SameLine();

        const char *currentKernel = cl.kernelNames.empty()
                                        ? "None"
                                        : cl.kernelNames[cl.selectedKernel].c_str();

        ImGui::SetNextItemWidth(150.0f);

        if (ImGui::BeginCombo("##kernel", currentKernel))
        {
            for (int i = 0; i < (int)cl.kernelNames.size(); ++i)
            {
                bool selected = i == cl.selectedKernel;

                if (ImGui::Selectable(cl.kernelNames[i].c_str(), selected))
                {
                    cl.selectedKernel = i;
                }

                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        ImGui::SameLine();

        // ============================================================
        // Build
        // ============================================================

        if (ImGui::Button("Build"))
        {
            if (cl.build())
            {
                status = "Build successful";
            }
            else
            {
                status = "Build failed";
            }
        }

        ImGui::SameLine();

        // ============================================================
        // Kernel iterations
        // ============================================================

        ImGui::TextUnformatted("Iterations:");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(80.0f);

        ImGui::InputInt("##iterations", &kernelIterations);

        if (kernelIterations < 1)
        {
            kernelIterations = 1;
        }

        ImGui::SameLine();

        // ============================================================
        // Execute
        // ============================================================

        if (ImGui::Button("Execute"))
        {
            executeKernel();
        }

        ImGui::EndChild();
    }

    // ------------------------------------------------------------
    // Image viewer
    // ------------------------------------------------------------
    void drawImage(const char *title, const Image &image, float width, float height)
    {
        ImGui::BeginChild(title,
                          ImVec2(width, height),
                          true,
                          ImGuiWindowFlags_NoScrollbar);

        // ------------------------------------------------------------
        // Header
        // ------------------------------------------------------------

        ImGui::TextUnformatted(title);
        ImGui::Separator();

        // ------------------------------------------------------------
        // Image
        // ------------------------------------------------------------

        if (image.texture != 0 && image.width > 0 && image.height > 0)
        {
            ImVec2 available = ImGui::GetContentRegionAvail();

            // Small padding
            available.x -= 4.0f;
            available.y -= 4.0f;

            if (available.x > 0 && available.y > 0)
            {
                float scaleX = available.x / static_cast<float>(image.width);

                float scaleY = available.y / static_cast<float>(image.height);

                // Save aspect ratio
                float scale = std::min(scaleX, scaleY);

                // Never scale small images
                scale = std::min(scale, 1.0f);

                ImVec2 imageSize(image.width * scale, image.height * scale);

                // Center the image
                float offsetX = (available.x - imageSize.x) * 0.5f;

                float offsetY = (available.y - imageSize.y) * 0.5f;

                if (offsetX > 0)
                {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                }

                if (offsetY > 0)
                {
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
                }

                ImGui::Image((ImTextureID)(intptr_t)image.texture, imageSize);
            }
        }
        else
        {
            ImGui::TextDisabled("No image loaded");
        }

        ImGui::EndChild();
    }

    // ================================================================
    // Main UI
    // ================================================================

    void drawUI()
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);

        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("OpenCL Image Processor", nullptr, flags);

        // ============================================================
        // Toolbar
        // ============================================================

        drawToolbar();

        // ============================================================
        // Calculate available layout
        // ============================================================

        const float statusHeight = 24.0f;
        const float logHeight = 110.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.y;

        ImVec2 available = ImGui::GetContentRegionAvail();

        // После toolbar
        float imageHeight = available.y - statusHeight - logHeight - spacing * 2.0f;

        if (imageHeight < 100.0f)
        {
            imageHeight = 100.0f;
        }

        float imageWidth = available.x;

        float halfWidth = (imageWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        // ============================================================
        // Images
        // ============================================================

        drawImage("Before", inputImage, halfWidth, imageHeight);

        ImGui::SameLine();

        drawImage("After", outputImage, halfWidth, imageHeight);

        // ============================================================
        // Status
        // ============================================================

        ImGui::BeginChild("StatusBar",
                          ImVec2(0, statusHeight),
                          false,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::Text("Status: %s", status.empty() ? "Ready" : status.c_str());

        ImGui::EndChild();

        // ============================================================
        // Build log
        // ============================================================

        ImGui::BeginChild("BuildLog", ImVec2(0, logHeight), true);

        if (ImGui::CollapsingHeader("OpenCL Build Log", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (cl.buildLog.empty())
            {
                ImGui::TextDisabled("No build output");
            }
            else
            {
                ImGui::TextUnformatted(cl.buildLog.c_str());
            }
        }

        ImGui::EndChild();

        ImGui::End();
    }

    // ------------------------------------------------------------
    // Main loop
    // ------------------------------------------------------------

    void run()
    {
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            drawUI();

            ImGui::Render();

            int displayW = 0;
            int displayH = 0;

            glfwGetFramebufferSize(window, &displayW, &displayH);

            glViewport(0, 0, displayW, displayH);

            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

            glClear(GL_COLOR_BUFFER_BIT);

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(window);
        }
    }

    // ------------------------------------------------------------
    // Cleanup
    // ------------------------------------------------------------

    void shutdown()
    {
        inputImage.destroy();
        outputImage.destroy();

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();

        ImGui::DestroyContext();

        if (window)
        {
            glfwDestroyWindow(window);
        }

        glfwTerminate();
    }
};

// ================================================================
// main
// ================================================================

int main()
{
    Application app;

    if (!app.init())
    {
        return -1;
    }

    app.run();
    app.shutdown();

    return 0;
}