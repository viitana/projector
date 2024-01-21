#include "projector.hpp"

#include <chrono>
#include <thread>
#include <limits>

#include <stb_image.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/projection.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <vulkan/vk_enum_string_helper.h>

#include "scene.hpp"

namespace Projector
{
    Projector::Projector()
    {
        // assert(MAX_FRAMES_IN_FLIGHT > 1);

        if (!glfwInit())
        {
            throw std::runtime_error("failed to initialize glfw");
        }

        CreateInstance();
        CreateSurface();

        PickGPU();
        CreateLogicalDevice();
        CreateCommandPool();
        CreateQueryPool();

        renderTimer_.Init(device_, gpu_->PhysicalDevice(), MAX_FRAMES_IN_FLIGHT, 200);
        warpTimer_.Init(device_, gpu_->PhysicalDevice(), 1, 200);

        Input::InputHandler::Init(window_);

        scene_ = new Scene::Model(
            "res/sponza/Sponza.gltf",
            // // "res/new/scenes/cube.gltf",
            // "res/new/scenes/rock.gltf",
            // "res/new/scenes/planet.gltf",
            // "res/abeautifulgame/ABeautifulGame.gltf",
            gpu_->PhysicalDevice(),
            device_,
            commandPool_,
            graphicsQueue_,
            1.0f
        );

        CreateUniformBuffers();

        RecreateSwapChain();
        InitImGui();

        CreateCommandBuffers();
        CreateSyncObjects();
    }

    Projector::~Projector()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(device_, imguiPool_, nullptr);

        CleanupSwapChain();

        delete scene_;

        vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        vkDestroyPipeline(device_, warpGraphicsPipeline_, nullptr);
        vkDestroyPipelineLayout(device_, warpPipelineLayout_, nullptr);
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            vkDestroyBuffer(device_, uniformBuffers_[i], nullptr);
            vkFreeMemory(device_, uniformBuffersMemory_[i], nullptr);
        }
        vkDestroyBuffer(device_, warpUniformBuffer_, nullptr);
        vkFreeMemory(device_, warpUniformBufferMemory_, nullptr);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            vkDestroyFence(device_, inFlightFences_[i], nullptr);
        }
        vkDestroySemaphore(device_, imageAvailableSemaphore_, nullptr);
        vkDestroySemaphore(device_, renderReadySemaphore_, nullptr);
        vkDestroySemaphore(device_, warpFinishedSemaphore_, nullptr);
        vkDestroyFence(device_, warpInFlightFence_, nullptr);

        vkDestroyCommandPool(device_, commandPool_, nullptr);
        vkDestroyDevice(device_, nullptr);

        vkDestroySurfaceKHR(vk_, surface_, nullptr);
        vkDestroyInstance(vk_, nullptr);

        glfwDestroyWindow(window_);
        glfwTerminate();

        std::cout << "Cleaned up" << std::endl; 
    }

    void Projector::Run()
    {
        static auto lastRefresh = std::chrono::high_resolution_clock::now();
        static float tillRender = 0;
        static float tillWarp = 0;

        while (!glfwWindowShouldClose(window_))
        {
            const auto currentTime = std::chrono::high_resolution_clock::now();
            const float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastRefresh).count();
            lastRefresh = currentTime;

            tillRender -= deltaTime;
            tillWarp -= deltaTime;

            if (tillRender < 0 || tillWarp < 0)
            {
                glfwPollEvents();

                bool rendering = tillRender < 0;
                bool warping = tillWarp < 0;

                if (rendering)
                {
                    if (doRender_) DrawFrame();
                    tillRender += 1.0f / (float)renderFramerate_;
                }
                if (doAsyncWarp_ && warping || (!doAsyncWarp_ && rendering))
                {
                    ImGui_ImplVulkan_NewFrame();
                    ImGui_ImplGlfw_NewFrame();
                    ImGui::NewFrame();

                    bool doRecreateSwapchain = false;
                    {
                        ImGui::Begin("Settings", nullptr,
                            ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_AlwaysAutoResize
                        );
                        ImGui::SetWindowPos(ImVec2(swapChainExtent_.width - ImGui::GetWindowSize().x, 0));

                        ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "Rendering");
                        ImGui::Spacing();
                        ImGui::Spacing();
                        ImGui::Indent(12.0f);
                        ImGui::Checkbox("Render", &doRender_);
                        ImGui::SliderInt("Render framerate", &renderFramerate_, 1, 120);
                        ImGui::SliderFloat("Field of view", &fov_, 0, MAX_VFOV_DEG - overdrawDegreesChange_);
                        ImGui::Indent(-12.0f);

                        ImGui::Spacing();
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "Asynchronous timewarp");
                        ImGui::Spacing();
                        ImGui::Spacing();
                        ImGui::Indent(12.0f);
                        ImGui::Checkbox("Enabled", &doAsyncWarp_);
                        ImGui::SliderInt("Warp framerate", &warpFramerate_, 1, 120);
                        ImGui::SliderFloat("Overdraw", &overdrawDegreesChange_, 0, MAX_VFOV_DEG - fov_, "%.1f degrees");
                        if (ImGui::IsItemDeactivatedAfterEdit())
                        {
                            doRecreateSwapchain = true;
                            overdrawDegrees_ = overdrawDegreesChange_;
                        }
                        // if (ImGui::BeginCombo("Variable rate shade overdraw", VariableRateShadingNames[variableRateShadingMode_]))
                        // {
                        //     for (int n = 0; n < VariableRateShadingNames.size(); n++)
                        //     {
                        //         bool is_selected = variableRateShadingMode_ == n;
                        //         if (ImGui::Selectable(VariableRateShadingNames[n], is_selected))
                        //         {
                        //             variableRateShadingMode_ = (VariableRateShadingMode)n;
                        //             doRecreateSwapchain = true;
                        //         }
                        //         if (is_selected) ImGui::SetItemDefaultFocus();
                        //     }
                        //     ImGui::EndCombo();
                        // }
                        ImGui::SliderFloat("Clamp image to edge", &clampOvershootPercent_, 0, 100, "%.0f%%");
                        ImGui::SliderFloat("Depth visualization", &depthBlend_, 0, 1);
                        if (ImGui::Checkbox("Wireframe", &wireFrame_))
                        {
                            doRecreateSwapchain = true;
                        }
                        ImGui::SliderInt("Grid resolution X", &gridResolution_.x, 1, 2048);
                        ImGui::SameLine();
                        ImGui::SliderInt("Y", &gridResolution_.y, 1, 2048);
                        ImGui::Indent(-12.0f);

                        ImGui::End();
                    }

                    {
                        ImGui::Begin("Debug", nullptr,
                            ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_AlwaysAutoResize
                        );
                        ImGui::SetWindowPos(ImVec2(0, 0));

                        ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "Perspective transforms");
                        ImGui::Spacing();
                        ImGui::Spacing();
                        ImGui::Text("Render :: Pos x: %f y: %f z: %f - Rot x: %f y: %f",
                            playerRender_.position.x, playerRender_.position.y, playerRender_.position.z, playerRender_.rotation.x, playerRender_.rotation.y);
                        ImGui::Text("Warp   :: Pos x: %f y: %f z: %f - Rot x: %f y: %f",
                            playerWarp_.position.x, playerWarp_.position.y, playerWarp_.position.z, playerWarp_.rotation.x, playerWarp_.rotation.y);

                        ImGui::Spacing();
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "Timing");
                        ImGui::Spacing();
                        ImGui::Spacing();
                        ImGui::Text("Device timestamp resolution: %f ns", gpu_->Properties().limits.timestampPeriod);
                        ImGui::Spacing();
                        ImGui::Spacing();
                        // for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
                        // {
                        //     ImGui::Text("Frame %d render start stamp: %llu, end stamp: %llu (delta %llu)", i, stats.renderStartStamps[i], stats.renderEndStamps[i], stats.renderEndStamps[i] - stats.renderStartStamps[i]);
                        // }
                        // ImGui::Text("Warp start stamp: %llu, end stamp: %llu (delta %llu)", stats.warpStartStamp, stats.warpEndStamp, stats.warpEndStamp - stats.warpStartStamp);
                        // ImGui::Spacing();
                        // ImGui::Spacing();
                        // for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
                        // {
                        //     ImGui::Text("Frame %d render time (ms): %f", i, stats.renderTimes[i]);
                        // }
                        // ImGui::Spacing();
                        // ImGui::Spacing();
                        // ImGui::Text("Warp time (ms): %f", stats.warpTime);

                        // ImGui::PlotLines("Frame Times", stats.renderTimes.data(), stats.renderTimes.size());
                        ImGui::PlotLines(
                            "",
                            renderTimer_.GetRenderTimes(),
                            renderTimer_.GetRenderTimesCount(),
                            renderTimer_.GetRenderTimesOffset(),
                            ("Render frame time (ms), average: " + std::to_string(renderTimer_.GetRenderTimesAverage())).c_str(),
                            0,
                            1.5f * renderTimer_.GetRenderTimesAverage(),
                            ImVec2(700, 100)
                        );
                        ImGui::PlotLines(
                            "",
                            warpTimer_.GetRenderTimes(),
                            warpTimer_.GetRenderTimesCount(),
                            warpTimer_.GetRenderTimesOffset(),
                            ("Warp frame time (ms), average: " + std::to_string(warpTimer_.GetRenderTimesAverage())).c_str(),
                            0,
                            1.5f * warpTimer_.GetRenderTimesAverage(),
                            ImVec2(700, 100)
                        );

                        ImGui::End();
                    }

                    
                    ImGui::ShowDemoWindow();
                    

                    // Compute setting-dependent variables
                    {
                        renderFov_ = fov_ + overdrawDegrees_;

                        float viewFovAngle = fov_ / 2.0f;
                        float renderFovAngle = renderFov_ / 2.0f;

                        float renderEdgeFromMid = glm::tan(glm::radians(renderFovAngle));
                        renderScreenScale_ = renderEdgeFromMid * 2.0f;

                        float renderOvershotFovAngle = renderFovAngle + ((clampOvershootPercent_ / 100.0f) * (89.9f - renderFovAngle));
                        float renderOvershotEdgeFromMid = glm::tan(glm::radians(renderOvershotFovAngle));
                        renderOvershotScreenScale_ = renderOvershotEdgeFromMid * 2.0f;

                        float viewEdgeFromMid = glm::tan(glm::radians(viewFovAngle));
                        viewScreenScale_ = viewEdgeFromMid * 2.0f;

                        renderScale_ = std::clamp(renderScreenScale_ / viewScreenScale_, 0.1f, 8.0f);
                    }

                    if (doRecreateSwapchain) RecreateSwapChain();

                    ImGui::Render();

                    overdrawDegrees_ = std::clamp(overdrawDegrees_, 0.0f, 180.0f - fov_);

                    WarpPresent();
                    tillWarp += 1.0f / (float)warpFramerate_;
                }
            }
        }
        vkDeviceWaitIdle(device_);
    }

    void Projector::Resized()
    {
        framebufferResized_ = true;
    }

    const bool Projector::CheckValidationLayerSupport() const
    {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char* layerName : validationLayers) {
            bool layerFound = false;
            for (const auto& layerProperties : availableLayers)
            {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }
            if (!layerFound)
            {
                std::cout << "Unsupported validation layer: " << layerName << std::endl;
                return false;
            };
        }
        return true;
    }

    const VkShaderModule Projector::CreateShaderModule(const std::vector<char>& code) const
    {
        VkShaderModuleCreateInfo createInfo
        {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = code.size(),
            .pCode = reinterpret_cast<const uint32_t*>(code.data()),
        };

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create shader module");
        }
        return shaderModule;
    }

    const VkFormat Projector::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const
    {
        for (VkFormat format : candidates)
        {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(gpu_->PhysicalDevice(), format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
            {
                return format;
            }
            else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
            {
                return format;
            }
        }
        throw std::runtime_error("failed to find supported format");
    }

    const VkFormat Projector::FindDepthFormat() const
    {
        return FindSupportedFormat(
            { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
        );
    }

    const bool Projector::HasStencilComponent(VkFormat format) const
    {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    void Projector::CreateInstance()
    {
        if (!CheckValidationLayerSupport())
        {
            throw std::runtime_error("validation layers not available");
        }

        VkApplicationInfo appInfo
        {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "projector",
            .applicationVersion = VK_MAKE_API_VERSION(0, VERSION_MAJOR, VERSION_MINOR, 0),
            .pEngineName = "No Engine",
            .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
            .apiVersion = VK_API_VERSION_1_3,
        };

        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        VkInstanceCreateInfo createInfo
        {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = static_cast<uint32_t>(validationLayers.size()),
            .ppEnabledLayerNames = validationLayers.data(),
            .enabledExtensionCount = glfwExtensionCount,
            .ppEnabledExtensionNames = glfwExtensions,
        };

        if (vkCreateInstance(&createInfo, nullptr, &vk_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create instance");
        }
    }

    void Projector::CreateSurface()
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        window_ = glfwCreateWindow(1280, 720, "projector", nullptr, nullptr);
        if (window_ == nullptr)
        {
            throw std::runtime_error("failed to create window");
        }
        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, FramebufferResizeCallback);

        VkResult result = glfwCreateWindowSurface(vk_, window_, nullptr, &surface_);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create window surface");
        }
    }

    void Projector::PickGPU()
    {
        // Get physical devices count
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(vk_, &deviceCount, nullptr);
        if (deviceCount == 0)
        {
            throw std::runtime_error("failed to find GPUs with Vulkan support");
        }

        // Get physical devices
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(vk_, &deviceCount, devices.data());

        // Init GPU list
        for (const VkPhysicalDevice& device : devices)
        {
            // GPU gpu(vk_, device, surface_);
            gpus_.emplace_back(std::make_shared<GPU>(vk_, device, surface_));
        }

        // Try picking first suitable discrete GPU
        for (const std::shared_ptr<GPU>& gpu : gpus_)
        {
            if (gpu->IsSuitable() && gpu->IsDiscrete())
            {
                gpu_ = gpu;
                break;
            }
        }

        // If no GPU, try picking first suitable any GPU
        if (gpu_ == nullptr)
        {
            for (const std::shared_ptr<GPU>& gpu : gpus_)
            {
                if (gpu->IsSuitable())
                {
                    gpu_ = gpu;
                    break;
                }
            }
        }

        if (gpu_ == nullptr)
        {
            throw std::runtime_error("failed to find a suitable GPU");
        }
        std::cout << "Picked device \"" << gpu_->Properties().deviceName << "\"" << std::endl;
    }

    void Projector::CreateLogicalDevice()
    {
        float renderPriority = 0.0f;
        float warpPriority = 1.0f;
        float presentPriority = 1.0f;

        uint32_t renderQueueFamilyIndex = gpu_->RenderQueueFamilyIndex();
        uint32_t warpQueueFamilyIndex = gpu_->WarpQueueFamilyIndex();
        uint32_t presentQueueFamilyIndex = gpu_->PresentQueueFamilyIndex();
        // uint32_t renderQueueIndex = gpu_->RenderQueueFamilyIndex();
        // uint32_t warpQueueIndex = gpu_->WarpQueueFamilyIndex();
        // uint32_t presentQueueIndex = gpu_->PresentQueueFamilyIndex();

        int renderQueueIndex = -1;
        int warpQueueIndex = -1;
        int presentQueueIndex = -1;

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::vector<std::vector<float>> queuePriorities;

        // Add render queue create info
        {
            queueCreateInfos.push_back(VkDeviceQueueCreateInfo
                {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = renderQueueFamilyIndex,
                    .queueCount = 1,
                }
            );
            queuePriorities.push_back({ renderPriority });
            renderQueueIndex = 0;
        }

        // Add warp queue info
        for (int i = 0; i < queueCreateInfos.size(); i++)
        {
            if (queueCreateInfos[i].queueFamilyIndex == warpQueueFamilyIndex)
            {
                queueCreateInfos[i].queueCount = std::min(queueCreateInfos[i].queueCount + 1, gpu_->WarpQueueFamily().queueCount);
                queuePriorities[i].push_back(warpPriority);
                warpQueueIndex = queueCreateInfos[i].queueCount - 1;
            }
        }
        if (warpQueueIndex == -1)
        {
            queueCreateInfos.push_back(VkDeviceQueueCreateInfo
                {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = warpQueueFamilyIndex,
                    .queueCount = 1,
                }
            );
            queuePriorities.push_back({ warpPriority });
            warpQueueIndex = 0;
        }
        
        // Add present queue info
        for (int i = 0; i < queueCreateInfos.size(); i++)
        {
            if (queueCreateInfos[i].queueFamilyIndex == presentQueueFamilyIndex)
            {
                queueCreateInfos[i].queueCount = std::min(queueCreateInfos[i].queueCount + 1, gpu_->PresentQueueFamily().queueCount);
                queuePriorities[i].push_back(presentPriority);
                presentQueueIndex = queueCreateInfos[i].queueCount - 1;
            }
        }
        if (presentQueueIndex == -1)
        {
            queueCreateInfos.push_back(VkDeviceQueueCreateInfo
                {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = presentQueueFamilyIndex,
                    .queueCount = 1,
                }
            );
            queuePriorities.push_back({ presentPriority });
            presentQueueIndex = 0;
        }

        // Link priorities to queue create infos
        for (int i = 0; i < queueCreateInfos.size(); i++)
        {
            queueCreateInfos[i].pQueuePriorities = queuePriorities[i].data();
        }

        // VkPhysicalDeviceFragmentShadingRateFeaturesKHR shadingRateFeatures
        // {
        //     .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR,
        //     .pipelineFragmentShadingRate = VK_FALSE,
        //     .primitiveFragmentShadingRate = VK_FALSE,
        //     .attachmentFragmentShadingRate = VK_TRUE,
        // };
        // VkPhysicalDeviceFeatures2 deviceFeatures2
        // {
        //     .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        //     .pNext = &shadingRateFeatures,
        //     .features = VkPhysicalDeviceFeatures
        //     {
        //         .samplerAnisotropy = VK_TRUE,
        //     }
        // };

        VkDeviceCreateInfo createInfo
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = nullptr, //&deviceFeatures2,
            .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
            .pQueueCreateInfos = queueCreateInfos.data(),
            .enabledLayerCount = static_cast<uint32_t>(validationLayers.size()),
            .ppEnabledLayerNames = validationLayers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
            .ppEnabledExtensionNames = deviceExtensions.data(),
        };

        VkResult result = vkCreateDevice(gpu_->PhysicalDevice(), &createInfo, nullptr, &device_);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error(std::string("failed to create logical device: ") + string_VkResult(result));
        }

        vkGetDeviceQueue(device_, renderQueueFamilyIndex, renderQueueIndex, &graphicsQueue_);
        vkGetDeviceQueue(device_, warpQueueFamilyIndex, warpQueueIndex, &warpQueue_);
        vkGetDeviceQueue(device_, presentQueueFamilyIndex, presentQueueIndex, &presentQueue_);

        std::cout << "hellllo" << std::endl;
    }

    void Projector::CreateQueryPool()
    {
        VkQueryPoolCreateInfo renderQueryPoolInfo =
        {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = MAX_FRAMES_IN_FLIGHT * 2, // 2 timestamps (before & after) for each pass
        };
        if (vkCreateQueryPool(device_, &renderQueryPoolInfo, nullptr, &renderQueryPool_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create create render query pool");
        }

        VkQueryPoolCreateInfo warpQueryPoolInfo =
        {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = 2,
        };
        if (vkCreateQueryPool(device_, &warpQueryPoolInfo, nullptr, &warpQueryPool_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create create warp query pool");
        }
    }

    void Projector::CreateSwapChain()
    {
        // Get window size
        int windowWidth, windowHeight;
        glfwGetFramebufferSize(window_, &windowWidth, &windowHeight);

        uint32_t imageCount = gpu_->SurfaceCapabilities().minImageCount + 1;
        if (
            gpu_->SurfaceCapabilities().maxImageCount > 0 &&
            imageCount > gpu_->SurfaceCapabilities().maxImageCount
        )
        {
            imageCount = gpu_->SurfaceCapabilities().maxImageCount;
        }

        VkSharingMode imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        uint32_t* queueFamilyIndices = nullptr;
        std::vector<uint32_t> queueFamilies = { gpu_->RenderQueueFamilyIndex() };

        bool warpFamilyIncluded = false;
        for (uint32_t queueFamilyIndex : queueFamilies)
        {
            if (queueFamilyIndex == gpu_->WarpQueueFamilyIndex()) warpFamilyIncluded = true;
        }
        if (!warpFamilyIncluded)
        {
            queueFamilies.push_back(gpu_->WarpQueueFamilyIndex());
            imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        }

        bool presentFamilyIncluded = false;
        for (uint32_t queueFamilyIndex : queueFamilies)
        {
            if (queueFamilyIndex == gpu_->PresentQueueFamilyIndex()) presentFamilyIncluded = true;
        }
        if (!warpFamilyIncluded)
        {
            queueFamilies.push_back(gpu_->PresentQueueFamilyIndex());
            imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        }

        if (imageSharingMode == VK_SHARING_MODE_CONCURRENT)
        {
            queueFamilyIndices = queueFamilies.data();
        }

        swapChainExtent_ = gpu_->GetSurfaceExtent(windowWidth, windowHeight);

        VkSwapchainCreateInfoKHR createInfo
        {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface_,
            .minImageCount = imageCount,
            .imageFormat = gpu_->SurfraceFormat().format,
            .imageColorSpace = gpu_->SurfraceFormat().colorSpace,
            .imageExtent = swapChainExtent_,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = imageSharingMode,
            .queueFamilyIndexCount = static_cast<uint32_t>(queueFamilies.size()),
            .pQueueFamilyIndices = queueFamilyIndices,
            .preTransform = gpu_->SurfaceCapabilities().currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = gpu_->PresentMode(),
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE,
        };

        if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapChain_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create swap chain");
        }

        vkGetSwapchainImagesKHR(device_, swapChain_, &imageCount, nullptr);
        swapChainImages_.resize(imageCount);
        vkGetSwapchainImagesKHR(device_, swapChain_, &imageCount, swapChainImages_.data());

        renderExtent_ = VkExtent2D
        {
            .width = static_cast<uint32_t>(swapChainExtent_.width * renderScale_),
            .height = static_cast<uint32_t>(swapChainExtent_.height * renderScale_),
        };
    }

    void Projector::CreateImageViews()
    {
        swapChainImageViews_.resize(swapChainImages_.size());
        for (uint32_t i = 0; i < swapChainImages_.size(); i++)
        {
            swapChainImageViews_[i] = Util::CreateImageView(device_, swapChainImages_[i], gpu_->SurfraceFormat().format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        }
    }

    void Projector::CreateRenderPass()
    {
        // Main pass
        {
            VkAttachmentDescription2 colorAttachment
            {
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .format = gpu_->SurfraceFormat().format,
                .samples = gpu_->MaxSampleCount(),
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                // .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            };
            VkAttachmentReference2 colorAttachmentRef
            {
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
                .attachment = 0,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            };

            VkAttachmentDescription2 depthAttachment
            {
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .format = FindDepthFormat(),
                .samples = gpu_->MaxSampleCount(),
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                // .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            };
            VkAttachmentReference2 depthAttachmentRef
            {
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
                .attachment = 1,
                .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            };

            VkAttachmentDescription2 colorAttachmentResolve
            {
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .format = gpu_->SurfraceFormat().format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            VkAttachmentReference2 colorAttachmentResolveRef
            {
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
                .attachment = 2,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            };

            VkAttachmentDescription2 depthAttachmentResolve
            {
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .format = FindDepthFormat(), //FindDepthFormat(),
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, //VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                // .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            VkAttachmentReference2 depthAttachmentResolveRef
            {
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
                .attachment = 3,
                .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            };
            VkSubpassDescriptionDepthStencilResolve depthStencilResolveInfo =
            {
                .sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE,
                .pNext = nullptr,
                .depthResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT,
                .stencilResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT,
                .pDepthStencilResolveAttachment = &depthAttachmentResolveRef,
            };

            VkSubpassDescription2 subpass
            {
                .sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
                .pNext = &depthStencilResolveInfo, //&shadingRateAttachmentInfo,
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount = 1,
                .pColorAttachments = &colorAttachmentRef,
                .pResolveAttachments = &colorAttachmentResolveRef,
                .pDepthStencilAttachment = &depthAttachmentRef,
            };

            VkSubpassDependency2 dependency
            {
                .sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2,
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                .srcAccessMask = VK_PIPELINE_STAGE_NONE,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            };

            std::cout << "color format: " << gpu_->SurfraceFormat().format << std::endl;
            std::cout << "depth format: " << FindDepthFormat() << std::endl;
            std::cout << "sample count: " << gpu_->MaxSampleCount() << std::endl;

            // std::vector<VkAttachmentDescription2> attachments = { colorAttachment, colorAttachmentResolve, depthAttachment, depthAttachmentResolve };
            std::vector<VkAttachmentDescription2> attachments = { colorAttachment, depthAttachment, colorAttachmentResolve, depthAttachmentResolve };
            if (gpu_->MaxSampleCount() == VK_SAMPLE_COUNT_1_BIT)
            {
                subpass.pNext = VK_NULL_HANDLE;
                subpass.pResolveAttachments = VK_NULL_HANDLE;
                colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                attachments = { colorAttachment, depthAttachment };
            }

            VkRenderPassCreateInfo2 renderPassInfo
            {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
                .attachmentCount = static_cast<uint32_t>(attachments.size()),
                .pAttachments = attachments.data(),
                .subpassCount = 1,
                .pSubpasses = &subpass,
                .dependencyCount = 1,
                .pDependencies = &dependency,
            };

            if (vkCreateRenderPass2(device_, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create render pass");
            }
        }

        // Warp pass
        {
            VkAttachmentDescription colorAttachment
            {
                .format = gpu_->SurfraceFormat().format,
                .samples = gpu_->MaxSampleCount(),
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            };
            VkAttachmentReference colorAttachmentRef
            {
                .attachment = 0,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            };

            VkAttachmentDescription colorAttachmentResolve
            {
                .format = gpu_->SurfraceFormat().format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            };
            VkAttachmentReference colorAttachmentResolveRef
            {
                .attachment = 2,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            };

            VkAttachmentDescription depthAttachment =
            {
                .format = FindDepthFormat(),
                .samples = gpu_->MaxSampleCount(),
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            };
            VkAttachmentReference depthAttachmentRef
            {
                .attachment = 1,
                .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            };

            VkSubpassDescription subpass
            {
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount = 1,
                .pColorAttachments = &colorAttachmentRef,
                .pResolveAttachments = &colorAttachmentResolveRef,
                .pDepthStencilAttachment = &depthAttachmentRef,
            };

            VkSubpassDependency dependency
            {
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            };

            // std::vector<VkAttachmentDescription> attachments = { colorAttachment, colorAttachmentResolve, depthAttachment };
            std::vector<VkAttachmentDescription> attachments = { colorAttachment, depthAttachment, colorAttachmentResolve };

            // if (gpu_->MaxSampleCount() == VK_SAMPLE_COUNT_1_BIT)
            // {
            //     subpass.pResolveAttachments = VK_NULL_HANDLE;
            //     colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            //     attachments = { colorAttachment, depthAttachment };
            // }
            
            VkRenderPassCreateInfo renderPassInfo
            {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                .attachmentCount = static_cast<uint32_t>(attachments.size()),
                .pAttachments = attachments.data(),
                .subpassCount = 1,
                .pSubpasses = &subpass,
                .dependencyCount = 1,
                .pDependencies = &dependency,
            };

            if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &warpRenderPass_) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create warp render pass");
            }
        }
    }

    void Projector::CreateGraphicsPipeline()
    {
        {
            std::vector<char> vertShaderCode = Util::ReadFile("src/shaders/vert.spv");
            std::vector<char> fragShaderCode = Util::ReadFile("src/shaders/frag.spv");

            VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
            VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

            VkPipelineShaderStageCreateInfo vertShaderStageInfo
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertShaderModule,
                .pName = "main",
            };
            VkPipelineShaderStageCreateInfo fragShaderStageInfo
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = fragShaderModule,
                    .pName = "main",
            };
            VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

            VkPipelineInputAssemblyStateCreateInfo inputAssembly
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                .primitiveRestartEnable = VK_FALSE,
            };

            std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, /*VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR*/ };
            VkPipelineDynamicStateCreateInfo dynamicState
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
                .pDynamicStates = dynamicStates.data(),
            };

            VkPipelineViewportStateCreateInfo viewportState
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .scissorCount = 1,
            };

            VkPipelineRasterizationStateCreateInfo rasterizer
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .depthClampEnable = VK_FALSE,
                .rasterizerDiscardEnable = VK_FALSE,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = VK_CULL_MODE_BACK_BIT,
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .depthBiasEnable = VK_FALSE,
                .depthBiasConstantFactor = 0.0f, // Optional
                .depthBiasClamp = 0.0f, // Optional
                .depthBiasSlopeFactor = 0.0f, // Optional
                .lineWidth = 1.0f,
            };

            VkPipelineMultisampleStateCreateInfo multisampling
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = gpu_->MaxSampleCount(),
                .sampleShadingEnable = VK_FALSE,
                .minSampleShading = 1.0f, // Optional
                .pSampleMask = nullptr, // Optional
                .alphaToCoverageEnable = VK_FALSE, // Optional
                .alphaToOneEnable = VK_FALSE, // Optional
            };

            VkPipelineColorBlendAttachmentState colorBlendAttachment
            {
                .blendEnable = VK_TRUE,
                .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .colorBlendOp = VK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                .alphaBlendOp = VK_BLEND_OP_ADD,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            };

            VkPipelineColorBlendStateCreateInfo colorBlending
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .logicOpEnable = VK_FALSE,
                .logicOp = VK_LOGIC_OP_COPY, // Optional
                .attachmentCount = 1,
                .pAttachments = &colorBlendAttachment,
                .blendConstants = { 0.0f, 0.0f, 0.0f, 0.0f }, // Optional
            };

            const std::vector<VkDescriptorSetLayout> setLayouts =
            {
                descriptorSetLayout_,
                Scene::descriptorSetLayoutUbo,
                Scene::descriptorSetLayoutImage,
            };

            VkPipelineLayoutCreateInfo pipelineLayoutInfo
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = static_cast<uint32_t>(setLayouts.size()), //1,
                .pSetLayouts = setLayouts.data(),
                .pushConstantRangeCount = 0,
                .pPushConstantRanges = nullptr,
            };

            if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create pipeline layout");
            }

            VkPipelineDepthStencilStateCreateInfo depthStencil
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = VK_TRUE,
                .depthWriteEnable = VK_TRUE,
                .depthCompareOp = VK_COMPARE_OP_LESS,
                .depthBoundsTestEnable = VK_FALSE,
                .stencilTestEnable = VK_FALSE,
                .front = {}, // Optional
                .back = {}, // Optional
                .minDepthBounds = 0.0f, // Optional
                .maxDepthBounds = 1.0f, // Optional
            };

            VkGraphicsPipelineCreateInfo pipelineInfo
            {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = 2,
                .pStages = shaderStages,

                .pVertexInputState = Scene::Vertex::GetPipelineVertexInputState({
                    Scene::VertexComponent::Position,
                    Scene::VertexComponent::Normal,
                    Scene::VertexComponent::UV,
                    Scene::VertexComponent::Color,
                }),

                .pInputAssemblyState = &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depthStencil,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,

                .layout = pipelineLayout_,
                .renderPass = renderPass_,
                .subpass = 0,

                .basePipelineHandle = VK_NULL_HANDLE, // Optional
                .basePipelineIndex = -1, // Optional
            };

            VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline_);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create graphics pipeline");
            }

            vkDestroyShaderModule(device_, fragShaderModule, nullptr);
            vkDestroyShaderModule(device_, vertShaderModule, nullptr);
        }

        // Warp pipeline
        {
            std::vector<char> vertShaderCode = Util::ReadFile("src/shaders/warp_vert.spv");
            std::vector<char> fragShaderCode = Util::ReadFile("src/shaders/warp_frag.spv");

            VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
            VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

            VkPipelineShaderStageCreateInfo vertShaderStageInfo
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertShaderModule,
                .pName = "main",
            };
            VkPipelineShaderStageCreateInfo fragShaderStageInfo
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fragShaderModule,
                .pName = "main",
            };
            VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

            VkPipelineInputAssemblyStateCreateInfo inputAssembly
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                .primitiveRestartEnable = VK_FALSE,
            };

            std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo dynamicState
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
                .pDynamicStates = dynamicStates.data(),
            };

            VkPipelineViewportStateCreateInfo viewportState
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .scissorCount = 1,
            };

            VkPipelineRasterizationStateCreateInfo rasterizer
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .depthClampEnable = VK_FALSE,
                .rasterizerDiscardEnable = VK_FALSE,
                .polygonMode = wireFrame_ ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
                .cullMode = VK_CULL_MODE_BACK_BIT,
                .frontFace = VK_FRONT_FACE_CLOCKWISE,
                .depthBiasEnable = VK_FALSE,
                .depthBiasConstantFactor = 0.0f, // Optional
                .depthBiasClamp = 0.0f, // Optional
                .depthBiasSlopeFactor = 0.0f, // Optional
                .lineWidth = 1.0f,
            };

            VkPipelineMultisampleStateCreateInfo multisampling
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = gpu_->MaxSampleCount(),
                .sampleShadingEnable = VK_FALSE,
                .minSampleShading = 1.0f, // Optional
                .pSampleMask = nullptr, // Optional
                .alphaToCoverageEnable = VK_FALSE, // Optional
                .alphaToOneEnable = VK_FALSE, // Optional
            };

            VkPipelineColorBlendAttachmentState colorBlendAttachment
            {
                .blendEnable = VK_TRUE,
                .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .colorBlendOp = VK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                .alphaBlendOp = VK_BLEND_OP_ADD,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            };

            VkPipelineColorBlendStateCreateInfo colorBlending
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .logicOpEnable = VK_FALSE,
                .logicOp = VK_LOGIC_OP_COPY, // Optional
                .attachmentCount = 1,
                .pAttachments = &colorBlendAttachment,
                .blendConstants = { 0.0f, 0.0f, 0.0f, 0.0f }, // Optional
            };

            const std::vector<VkDescriptorSetLayout> setLayouts =
            {
                warpDescriptorSetLayout_
            };

            VkPipelineLayoutCreateInfo pipelineLayoutInfo
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = static_cast<uint32_t>(setLayouts.size()), //1,
                .pSetLayouts = setLayouts.data(),
                .pushConstantRangeCount = 0,
                .pPushConstantRanges = nullptr,
            };

            if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &warpPipelineLayout_) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create pipeline layout");
            }

            VkPipelineDepthStencilStateCreateInfo depthStencil
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = VK_TRUE,
                .depthWriteEnable = VK_TRUE,
                .depthCompareOp = VK_COMPARE_OP_LESS,
                .depthBoundsTestEnable = VK_FALSE,
                .stencilTestEnable = VK_FALSE,
                .front = {}, // Optional
                .back = {}, // Optional
                .minDepthBounds = 0.0f, // Optional
                .maxDepthBounds = 1.0f, // Optional
            };

            VkPipelineVertexInputStateCreateInfo vertexInputInfo
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .vertexBindingDescriptionCount = 0,
                .pVertexBindingDescriptions = nullptr,
                .vertexAttributeDescriptionCount = 0,
                .pVertexAttributeDescriptions = nullptr,
            };

            VkGraphicsPipelineCreateInfo pipelineInfo
            {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = 2,
                .pStages = shaderStages,

                .pVertexInputState = &vertexInputInfo,

                .pInputAssemblyState = &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depthStencil,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,

                .layout = warpPipelineLayout_,
                .renderPass = warpRenderPass_,
                .subpass = 0,

                .basePipelineHandle = VK_NULL_HANDLE, // Optional
                .basePipelineIndex = -1, // Optional
            };

            VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &warpGraphicsPipeline_);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create graphics pipeline");
            }

            vkDestroyShaderModule(device_, fragShaderModule, nullptr);
            vkDestroyShaderModule(device_, vertShaderModule, nullptr);
        }
    }

    void Projector::CreateCommandPool()
    {
        VkCommandPoolCreateInfo poolInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = gpu_->RenderQueueFamilyIndex(),
        };

        if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create command pool");
        }
    }

    void Projector::CreateRenderImageResources()
    {
        // Render color image
        {
            VkFormat colorFormat = gpu_->SurfraceFormat().format;
            Util::CreateImage(gpu_->PhysicalDevice(), device_, renderExtent_.width, renderExtent_.height, 1, gpu_->MaxSampleCount(), colorFormat, VK_IMAGE_TILING_OPTIMAL, /*VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |*/ VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage_, colorImageMemory_);
            colorImageView_ = Util::CreateImageView(device_, colorImage_, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        }
        // Render depth image
        {
            VkFormat depthFormat = FindDepthFormat();
            Util::CreateImage(gpu_->PhysicalDevice(), device_, renderExtent_.width, renderExtent_.height, 1, gpu_->MaxSampleCount(), depthFormat, VK_IMAGE_TILING_OPTIMAL, /*VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |*/ VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, renderDepthImage_, renderDepthImageMemory_);
            renderDepthImageView_ = Util::CreateImageView(device_, renderDepthImage_, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
        }
        // Shading rate map image
        // {
        //     VkFormat rateFormat = VK_FORMAT_R8_UINT;
        //     uint32_t width = static_cast<uint32_t>(ceil(renderExtent_.width / (float)gpu_->FramentShadingRateProperties().maxFragmentShadingRateAttachmentTexelSize.width));
        //     uint32_t height = static_cast<uint32_t>(ceil(renderExtent_.height / (float)gpu_->FramentShadingRateProperties().maxFragmentShadingRateAttachmentTexelSize.height));
        //     Util::CreateImage(gpu_->PhysicalDevice(), device_, width, height, 1, VK_SAMPLE_COUNT_1_BIT, rateFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, shadingRateImage_, shadingRateImageMemory_);
        //     shadingRateImageView_ = Util::CreateImageView(device_, shadingRateImage_, rateFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        
        //     VkDeviceSize imageSize = width * height * sizeof(uint8_t);

        //     uint8_t noneVal = 0;
        //     uint8_t variableShaded = 0;
        //     switch (variableRateShadingMode_)
        //     {
        //     case VariableRateShadingMode::None:
        //         variableShaded = 0;
        //         break;
        //     case VariableRateShadingMode::TwoByTwo:
        //         variableShaded = (2 >> 1) | (2 << 1);
        //         break;
        //     case VariableRateShadingMode::FourByFour:
        //         variableShaded = (4 >> 1) | (4 << 1);
        //         break;
        //     }

        //     uint8_t* mapData = new uint8_t[imageSize];
        //     memset(mapData, noneVal, imageSize);

        //     float visibleRatio = viewScreenScale_ / renderScreenScale_;
        //     float outsideWidth = (width - width * visibleRatio) * 0.5f;
        //     float outsideHeight = (height - height * visibleRatio) * 0.5f;

        //     uint8_t* cursor = mapData;
        //     for (uint32_t y = 0; y < height; y++)
        //     {
        //         for (uint32_t x = 0; x < width; x++)
        //         {
        //             const float deltaX = ((float)width / 2.0f - (float)x) / width * 100.0f;
        //             const float deltaY = ((float)height / 2.0f - (float)y) / height * 100.0f;
        //             const float dist = std::sqrt(deltaX * deltaX + deltaY * deltaY);
        //             if (x < outsideWidth || x > width - outsideWidth || y < outsideHeight || y > height - outsideHeight)
        //             {
        //                 *cursor = variableShaded;
        //             }
        //             cursor++;
        //         }
        //     }

        //     VkBuffer stagingBuffer;
        //     VkDeviceMemory stagingBufferMemory;
        //     Util::CreateBuffer(gpu_->PhysicalDevice(), device_, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        //     uint8_t* data;
        //     vkMapMemory(device_, stagingBufferMemory, 0, imageSize, 0, (void**)&data);
        //     memcpy(data, mapData, (size_t)imageSize);
        //     vkUnmapMemory(device_, stagingBufferMemory);

        //     Util::TransitionImageLayout(device_, commandPool_, graphicsQueue_, shadingRateImage_, VK_FORMAT_R8_UINT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
        //     Util::CopyBufferToImage(device_, commandPool_, graphicsQueue_, stagingBuffer, shadingRateImage_, width, height);

        //     vkDestroyBuffer(device_, stagingBuffer, nullptr);
        //     vkFreeMemory(device_, stagingBufferMemory, nullptr);

        //     Util::TransitionImageLayout(
        //         device_,
        //         commandPool_,
        //         graphicsQueue_,
        //         shadingRateImage_,
        //         VK_FORMAT_R8_UINT,
        //         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        //         VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
        //         1
        //     );
        // }
        // Render result image
        {
            resultImages_.resize(MAX_FRAMES_IN_FLIGHT);
            resultImagesMemory_.resize(MAX_FRAMES_IN_FLIGHT);
            resultImageViews_.resize(MAX_FRAMES_IN_FLIGHT);
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                VkFormat colorFormat = gpu_->SurfraceFormat().format;
                Util::CreateImage(gpu_->PhysicalDevice(), device_, renderExtent_.width, renderExtent_.height, 1, VK_SAMPLE_COUNT_1_BIT, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, resultImages_[i], resultImagesMemory_[i]);
                resultImageViews_[i] = Util::CreateImageView(device_, resultImages_[i], colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
                // Util::TransitionImageLayout(
                //     device_,
                //     commandPool_,
                //     graphicsQueue_,
                //     resultImages_[i],
                //     gpu_->SurfraceFormat().format,
                //     VK_IMAGE_LAYOUT_UNDEFINED,
                //     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                //     1
                // );
            }
        }
        // Render depth result image
        {
            resultImagesDepth_.resize(MAX_FRAMES_IN_FLIGHT);
            resultImagesMemoryDepth_.resize(MAX_FRAMES_IN_FLIGHT);
            resultImageViewsDepth_.resize(MAX_FRAMES_IN_FLIGHT);
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                VkFormat depthFormat = FindDepthFormat();
                Util::CreateImage(gpu_->PhysicalDevice(), device_, renderExtent_.width, renderExtent_.height, 1, VK_SAMPLE_COUNT_1_BIT, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, resultImagesDepth_[i], resultImagesMemoryDepth_[i]);
                resultImageViewsDepth_[i] = Util::CreateImageView(device_, resultImagesDepth_[i], depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
                // Util::TransitionImageLayout(
                //     device_,
                //     commandPool_,
                //     graphicsQueue_,
                //     resultImagesDepth_[i],
                //     depthFormat,
                //     VK_IMAGE_LAYOUT_UNDEFINED,
                //     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                //     1
                // );
            }
        }
        // Warp color image
        {
            VkFormat colorFormat = gpu_->SurfraceFormat().format;
            Util::CreateImage(gpu_->PhysicalDevice(), device_, renderExtent_.width, renderExtent_.height, 1, gpu_->MaxSampleCount(), colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, warpColorImage_, warpColorImageMemory_);
            warpColorImageView_ = Util::CreateImageView(device_, warpColorImage_, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        }
        // Warp depth image
        {
            VkFormat depthFormat = FindDepthFormat();
            Util::CreateImage(gpu_->PhysicalDevice(), device_, renderExtent_.width, renderExtent_.height, 1, gpu_->MaxSampleCount(), depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, warpDepthImage_, warpDepthImageMemory_);
            warpDepthImageView_ = Util::CreateImageView(device_, warpDepthImage_, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
        }
    }

    void Projector::CreateFramebuffers()
    {
        // Main render pass framebuffers
        {
            mainFramebuffers_.resize(MAX_FRAMES_IN_FLIGHT);
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                std::vector<VkImageView> attachments =
                {
                    colorImageView_,
                    renderDepthImageView_,
                    resultImageViews_[i],
                    resultImageViewsDepth_[i],
                };

                if (gpu_->MaxSampleCount() == VK_SAMPLE_COUNT_1_BIT)
                {
                    attachments =
                    {
                        resultImageViews_[i],
                        resultImageViewsDepth_[i],
                    };
                }

                VkFramebufferCreateInfo framebufferInfo
                {
                    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                    .renderPass = renderPass_,
                    .attachmentCount = static_cast<uint32_t>(attachments.size()),
                    .pAttachments = attachments.data(),
                    .width = renderExtent_.width,
                    .height = renderExtent_.height,
                    .layers = 1,
                };

                if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &mainFramebuffers_[i]) != VK_SUCCESS)
                {
                    throw std::runtime_error("failed to create framebuffer");
                }
            }
        }
        // Warp pass framebuffers
        {
            warpFramebuffers_.resize(swapChainImages_.size());
            for (size_t i = 0; i < swapChainImages_.size(); i++)
            {
                std::vector<VkImageView> attachments =
                {
                    warpColorImageView_,
                    warpDepthImageView_,
                    swapChainImageViews_[i],
                };

                // if (gpu_->MaxSampleCount() == VK_SAMPLE_COUNT_1_BIT)
                // {
                //     attachments =
                //     {
                //         swapChainImageViews_[i],
                //         warpDepthImageView_,
                //     };
                // }

                VkFramebufferCreateInfo framebufferInfo
                {
                    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                    .renderPass = warpRenderPass_,
                    .attachmentCount = static_cast<uint32_t>(attachments.size()),
                    .pAttachments = attachments.data(),
                    .width = swapChainExtent_.width,
                    .height = swapChainExtent_.height,
                    .layers = 1,
                };

                if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &warpFramebuffers_[i]) != VK_SUCCESS)
                {
                    throw std::runtime_error("failed to create framebuffer");
                }
            }
        }
    }

    void Projector::CreateUniformBuffers()
    {
        {
            VkDeviceSize bufferSize = sizeof(UniformBufferObject);

            uniformBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
            uniformBuffersMemory_.resize(MAX_FRAMES_IN_FLIGHT);
            uniformBuffersMapped_.resize(MAX_FRAMES_IN_FLIGHT);

            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                Util::CreateBuffer(gpu_->PhysicalDevice(), device_, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffers_[i], uniformBuffersMemory_[i]);
                vkMapMemory(device_, uniformBuffersMemory_[i], 0, bufferSize, 0, &uniformBuffersMapped_[i]);
            }
        }

        {
            VkDeviceSize bufferSize = sizeof(WarpUniformBufferObject);

            Util::CreateBuffer(gpu_->PhysicalDevice(), device_, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, warpUniformBuffer_, warpUniformBufferMemory_);
            vkMapMemory(device_, warpUniformBufferMemory_, 0, bufferSize, 0, &warpUniformBufferMapped_);
        }
    }

    void Projector::CreateWarpSampler()
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(gpu_->PhysicalDevice(), &properties);

        VkSamplerCreateInfo samplerInfo
        {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.0f, // Optional
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0, // Optional
            .maxLod = 0,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };

        if (vkCreateSampler(device_, &samplerInfo, nullptr, &warpSampler_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create texture sampler");
        }

        VkSamplerCreateInfo samplerDepthInfo
        {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.0f, // Optional
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0, // Optional
            .maxLod = 0,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };

        if (vkCreateSampler(device_, &samplerDepthInfo, nullptr, &warpSamplerDepth_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create texture sampler");
        }
    }

    void Projector::CreateDescriptorSetLayout()
    {
        // Main render
        {
            VkDescriptorSetLayoutBinding uboLayoutBinding
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            };

            std::array<VkDescriptorSetLayoutBinding, 1> bindings = { uboLayoutBinding };
            VkDescriptorSetLayoutCreateInfo layoutInfo
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            };

            VkResult result = vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create descriptor set layout");
            }
        }

        // Warp
        {
            VkDescriptorSetLayoutBinding warpUboLayoutBinding // State params for vectex stage
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .pImmutableSamplers = nullptr, // Optional
            };

            VkDescriptorSetLayoutBinding warpSamplerLayoutBinding // Rendered image for fragment stage
            {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr,
            };

            VkDescriptorSetLayoutBinding warpSamplerDepthLayoutBindingFragment // Depth image for fragment stage
            {
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr,
            };

            VkDescriptorSetLayoutBinding warpSamplerDepthLayoutBindingVertex // Depth image for vertex stage
            {
                .binding = 3,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .pImmutableSamplers = nullptr,
            };

            std::array<VkDescriptorSetLayoutBinding, 4> bindings = { warpUboLayoutBinding, warpSamplerLayoutBinding, warpSamplerDepthLayoutBindingFragment, warpSamplerDepthLayoutBindingVertex };
            VkDescriptorSetLayoutCreateInfo layoutInfo
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            };

            VkResult result = vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &warpDescriptorSetLayout_);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create descriptor set layout");
            }
        }
    }

    void Projector::CreateDescriptorPool()
    {
        std::array<VkDescriptorPoolSize, 2> poolSizes
        {
            VkDescriptorPoolSize // For regular & warp pass
            {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 20 * static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
            },
            VkDescriptorPoolSize// For warp pass
            {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 20 * static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
            },
        };

        VkDescriptorPoolCreateInfo poolInfo
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 20 * static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes = poolSizes.data(),
        };

        if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create descriptor pool");
        }
    }

    void Projector::CreateDescriptorSets()
    {
        // Main render descriptor sets
        {
            std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout_);

            VkDescriptorSetAllocateInfo allocInfo
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptorPool_,
                .descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
                .pSetLayouts = layouts.data(),
            };

            descriptorSets_.resize(MAX_FRAMES_IN_FLIGHT);
            if (vkAllocateDescriptorSets(device_, &allocInfo, descriptorSets_.data()) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to allocate descriptor sets: " );
            }

            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                VkDescriptorBufferInfo bufferInfo
                {
                    .buffer = uniformBuffers_[i],
                    .offset = 0,
                    .range = sizeof(UniformBufferObject),
                };
                std::array<VkWriteDescriptorSet, 1> descriptorWrites
                {
                    VkWriteDescriptorSet
                    {
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = descriptorSets_[i],
                        .dstBinding = 0,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .pBufferInfo = &bufferInfo,
                    }
                };
                vkUpdateDescriptorSets(device_, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            }
        }
        
        // Warp descriptor sets
        {
            std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, warpDescriptorSetLayout_);

            VkDescriptorSetAllocateInfo allocInfo
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptorPool_,
                .descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
                .pSetLayouts = layouts.data(),
            };
            
            warpDescriptorSets_.resize(MAX_FRAMES_IN_FLIGHT);
            const VkResult result = vkAllocateDescriptorSets(device_, &allocInfo, warpDescriptorSets_.data());
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error(std::string("failed to allocate warp descriptor sets: ") + string_VkResult(result));
            }

            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                VkDescriptorBufferInfo bufferInfo
                {
                    .buffer = warpUniformBuffer_,
                    .offset = 0,
                    .range = sizeof(WarpUniformBufferObject),
                };

                VkDescriptorImageInfo imageInfo
                {
                    .sampler = warpSampler_,
                    .imageView = resultImageViews_[i],
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                };
                
                VkDescriptorImageInfo depthImageInfo
                {
                    .sampler = warpSamplerDepth_,
                    .imageView = resultImageViewsDepth_[i],
                    // .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                };

                std::array<VkWriteDescriptorSet, 4> descriptorWrites
                {
                    VkWriteDescriptorSet
                    {
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = warpDescriptorSets_[i],
                        .dstBinding = 0,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .pBufferInfo = &bufferInfo,
                    },
                    VkWriteDescriptorSet
                    {
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = warpDescriptorSets_[i],
                        .dstBinding = 1,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .pImageInfo = &imageInfo,
                    },
                    VkWriteDescriptorSet
                    {
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = warpDescriptorSets_[i],
                        .dstBinding = 2,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .pImageInfo = &depthImageInfo,
                    },
                    VkWriteDescriptorSet
                    {
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = warpDescriptorSets_[i],
                        .dstBinding = 3,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .pImageInfo = &depthImageInfo,
                    }
                };
                vkUpdateDescriptorSets(device_, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            }
        }
    }

    void Projector::CreateCommandBuffers()
    {
        // Main draw
        {
            drawCommandBuffers_.resize(MAX_FRAMES_IN_FLIGHT);

            VkCommandBufferAllocateInfo allocInfo
            {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = commandPool_,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = (uint32_t)drawCommandBuffers_.size(),
            };

            if (vkAllocateCommandBuffers(device_, &allocInfo, drawCommandBuffers_.data()) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to allocate command buffers");
            }
        }
        // Warp
        {
            VkCommandBufferAllocateInfo allocInfo
            {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = commandPool_,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };

            if (vkAllocateCommandBuffers(device_, &allocInfo, &warpCommandBuffer_) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to allocate warp command buffers");
            }
        }
    }

    void Projector::CreateSyncObjects()
    {
        inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreTypeCreateInfo timelineCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0,
        };

        VkSemaphoreCreateInfo semaphoreInfo
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        VkSemaphoreCreateInfo timelineSemaphoreInfo
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        VkFenceCreateInfo fenceInfo
        {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create synchronization objects for frame");
            }
        }
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailableSemaphore_) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &timelineSemaphoreInfo, nullptr, &renderReadySemaphore_) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &warpFinishedSemaphore_) != VK_SUCCESS ||
            vkCreateFence(device_, &fenceInfo, nullptr, &warpInFlightFence_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create synchronization objects for warps");
        }
    }

    void Projector::InitImGui()
    {
        IMGUI_CHECKVERSION();

        VkDescriptorPoolSize pool_sizes[] =
        {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };
        VkDescriptorPoolCreateInfo pool_info =
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 1000,
            .poolSizeCount = std::size(pool_sizes),
            .pPoolSizes = pool_sizes,
        };
        VK_CHECK_RESULT(vkCreateDescriptorPool(device_, &pool_info, nullptr, &imguiPool_));

        ImGui::CreateContext();

        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForVulkan(window_, true);
        Util::ApplyStyle(ImGui::GetStyle());

        ImGui_ImplVulkan_InitInfo init_info =
        {
            .Instance = vk_,
            .PhysicalDevice = gpu_->PhysicalDevice(),
            .Device = device_,
            .Queue = graphicsQueue_,
            .DescriptorPool = imguiPool_,
            .MinImageCount = 3,
            .ImageCount = 3,
            .MSAASamples = gpu_->MaxSampleCount(),
        };
        ImGui_ImplVulkan_Init(&init_info, warpRenderPass_);

        VkCommandBuffer commandBuffer = Util::BeginSingleTimeCommands(device_, commandPool_);
        ImGui_ImplVulkan_CreateFontsTexture();
        Util::EndSingleTimeCommands(device_, commandPool_, graphicsQueue_, commandBuffer);
        // ImGui_ImplVulkan_DestroyFontUploadObjects();

        int width = 0, height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        ImGui::GetIO().FontGlobalScale = height / 720.0f;
    }

    void Projector::UpdateUniformBuffer(bool render)
    {
        static auto startTime = std::chrono::high_resolution_clock::now();
        static auto lastTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
        float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
        lastTime = std::chrono::high_resolution_clock::now();

        Input::UserInput input = Input::InputHandler::GetInput(deltaTime);

        glm::vec3 relativeMovement =
            glm::eulerAngleY(playerWarp_.rotation.y) *
            glm::vec4(input.moveDelta, 0);

        playerWarp_.position += relativeMovement;
        playerWarp_.rotation.x -= input.mouseDelta.y;
        playerWarp_.rotation.y -= input.mouseDelta.x;

        if (render)
        {
            playerRender_ = playerWarp_;
        }

        glm::mat4 renderRotation = glm::eulerAngleYX(
            playerRender_.rotation.y,
            playerRender_.rotation.x
        );
        glm::mat4 warpRotation = glm::eulerAngleYX(
            playerWarp_.rotation.y,
            playerWarp_.rotation.x
        );

        glm::mat4 renderView = glm::lookAt(
            playerRender_.position,
            playerRender_.position + glm::vec3(renderRotation * glm::vec4(0, 0, -1, 0)),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        glm::mat4 warpView = glm::lookAt(
            playerWarp_.position,
            playerWarp_.position + glm::vec3(warpRotation * glm::vec4(0, 0, -1, 0)),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        glm::mat4 renderPerspective = glm::perspective(
            glm::radians(renderFov_),
            swapChainExtent_.width / (float)swapChainExtent_.height,
            0.01f,
            100.0f
        );

        glm::mat4 warpPerspective = glm::perspective(
            glm::radians(fov_),
            swapChainExtent_.width / (float)swapChainExtent_.height,
            0.01f,
            100.0f
        );

        // Compensate for inverted clip Y axis on OpenGL
        renderPerspective[1][1] *= -1;
        warpPerspective[1][1] *= -1;

        glm::mat4 inverseRenderPerspective = glm::inverse(renderPerspective);

        glm::mat4 screen = glm::translate(
            glm::mat4(1.0f),
            playerRender_.position
        );

        UniformBufferObject mainUbo
        {
            .view = renderView,
            .proj = renderPerspective,
        };
        memcpy(uniformBuffersMapped_[renderFrame_], &mainUbo, sizeof(mainUbo));

        WarpUniformBufferObject warpUbo
        {
            .view = warpView,
            .proj = warpPerspective,
            .inverseProj = inverseRenderPerspective,
            .screen = screen * renderRotation,
            .gridResolution = gridResolution_,
            .screenScale = renderOvershotScreenScale_,
            .uvScale = renderOvershotScreenScale_ / renderScreenScale_,
            .depthBlend = depthBlend_,
        };
        memcpy(warpUniformBufferMapped_, &warpUbo, sizeof(warpUbo));
    }

    void Projector::DrawFrame()
    {
        static bool firstFrame = true;

        vkWaitForFences(device_, 1, &inFlightFences_[renderFrame_], VK_TRUE, UINT64_MAX);
        vkResetFences(device_, 1, &inFlightFences_[renderFrame_]);

        UpdateUniformBuffer(true);

        uint64_t nextFrame = (renderFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;

        VkSemaphore waitSemaphores[] = { renderReadySemaphore_ };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore signalSemaphores[] = { renderReadySemaphore_ };

        // Main render record & submit
        {
            vkResetCommandBuffer(drawCommandBuffers_[renderFrame_], 0);
            RecordDraw(drawCommandBuffers_[renderFrame_], renderFrame_);

            VkTimelineSemaphoreSubmitInfo timelineSubmitInfo
            {
                .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
                .waitSemaphoreValueCount = 1,
                .pWaitSemaphoreValues = &renderFrame_,
                .signalSemaphoreValueCount = 1,
                .pSignalSemaphoreValues = &nextFrame,
            };

            VkSubmitInfo submitInfo
            {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .waitSemaphoreCount = firstFrame ? (uint32_t)0 : (uint32_t)1,
                .pWaitSemaphores = firstFrame ? VK_NULL_HANDLE : waitSemaphores,
                .pWaitDstStageMask = firstFrame ? VK_NULL_HANDLE : waitStages,
                .commandBufferCount = 1,
                .pCommandBuffers = &drawCommandBuffers_[renderFrame_],
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = signalSemaphores,
            };

            if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[renderFrame_]) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to submit draw command buffer");
            }
        }

        firstFrame = false;
        renderFrame_ = nextFrame;
    }

    void Projector::WarpPresent()
    {
        vkWaitForFences(device_, 1, &warpInFlightFence_, VK_TRUE, UINT64_MAX);

        uint32_t frameIndex;
        VkResult result = vkAcquireNextImageKHR(device_, swapChain_, UINT64_MAX, imageAvailableSemaphore_, VK_NULL_HANDLE, &frameIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            std::cout << "Out-of-date swapchain on image acquire" << std::endl;
            RecreateSwapChain();
            return;
        }
        else if (result == VK_SUBOPTIMAL_KHR)
        {
            std::cout << "Suboptimal swapchain on image acquire" << std::endl;
        }
        else if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to acquire swap chain image");
        }

        vkResetFences(device_, 1, &warpInFlightFence_);

        UpdateUniformBuffer(false);

        uint32_t nextFrame = (warpFrame_ + 1) % swapChainImages_.size();

        VkSemaphore waitSemaphores[] = { imageAvailableSemaphore_ };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore signalSemaphores[] = { warpFinishedSemaphore_ };

        // Warp record & submit
        {
            vkResetCommandBuffer(warpCommandBuffer_, 0);
            RecordWarp(warpCommandBuffer_, frameIndex);

            VkSubmitInfo submitInfo
            {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = waitSemaphores,
                .pWaitDstStageMask = waitStages,
                .commandBufferCount = 1,
                .pCommandBuffers = &warpCommandBuffer_,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = signalSemaphores,
            };

            if (vkQueueSubmit(warpQueue_, 1, &submitInfo, warpInFlightFence_) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to submit warp command buffer");
            }

            warpFrame_ = nextFrame;
        }

        VkSwapchainKHR swapChains[] = { swapChain_ };
        VkPresentInfoKHR presentInfo
        {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = signalSemaphores,
            .swapchainCount = 1,
            .pSwapchains = swapChains,
            .pImageIndices = &frameIndex,
        };

        result = vkQueuePresentKHR(presentQueue_, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            std::cout << "Out-of-date swapchain on image present" << std::endl;
            RecreateSwapChain();
        }
        if (result == VK_SUBOPTIMAL_KHR)
        {
            std::cout << "Suboptimal swapchain on image present" << std::endl;
            RecreateSwapChain();
        }
        if (framebufferResized_)
        {
            std::cout << "Framebuffer resized on image present" << std::endl;
            RecreateSwapChain();
            framebufferResized_ = false;
        }
        else if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to present swap chain image");
        }
    }

    void Projector::RecordDraw(VkCommandBuffer commandBuffer, uint32_t frameIndex)
    {
        VkCommandBufferBeginInfo beginInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = 0, // Optional
            .pInheritanceInfo = nullptr, // Optional
        };

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to begin recording command buffer");
        }

        vkCmdResetQueryPool(commandBuffer, renderQueryPool_, frameIndex * 2, 2);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, renderQueryPool_, frameIndex * 2 + 0);

        renderTimer_.RecordStartTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

        std::vector<VkClearValue> clearValues
        {
            VkClearValue { .color = {{0.0f, 0.0f, 0.0f, 1.0f}} }, // Color
            VkClearValue { .depthStencil = {.depth = 1.0f, .stencil = 0 } }, // Depth
            VkClearValue { }, // Color resolve
            VkClearValue { }, // Depth resolve
        };

        VkRenderPassBeginInfo renderPassInfo
        {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = renderPass_,
            .framebuffer = mainFramebuffers_[renderFrame_],
            .renderArea = VkRect2D
            {
                .offset = { 0, 0 },
                .extent = renderExtent_,
            },
            .clearValueCount = static_cast<uint32_t>(clearValues.size()), 
            .pClearValues = clearValues.data(),
        };

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);

        VkViewport viewport
        {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(renderExtent_.width),
            .height = static_cast<float>(renderExtent_.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor
        {
            .offset = { 0, 0 },
            .extent = renderExtent_,
        };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout_,
            0,
            1,
            &descriptorSets_[renderFrame_],
            0,
            nullptr
        );

        // VkExtent2D fragmentSize = { 1, 1 };
        // VkFragmentShadingRateCombinerOpKHR combinerOps[2] = { VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR , VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR };
        // auto vkCmdSetFragmentShadingRateKHR = (PFN_vkCmdSetFragmentShadingRateKHR)vkGetInstanceProcAddr(vk_, "vkCmdSetFragmentShadingRateKHR");

        // vkCmdSetFragmentShadingRateKHR(commandBuffer, &fragmentSize, combinerOps);

        scene_->Draw(
            commandBuffer,
            0u,
            pipelineLayout_,
            1u
        );

        vkCmdEndRenderPass(commandBuffer);

        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, renderQueryPool_, frameIndex * 2 + 1);
        renderTimer_.RecordEndTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to record command buffer");
        }
    }

    void Projector::RecordWarp(VkCommandBuffer commandBuffer, uint32_t frameIndex)
    {
        VkCommandBufferBeginInfo beginInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = 0, // Optional
            .pInheritanceInfo = nullptr, // Optional
        };

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to begin recording warp command buffer");
        }

        vkCmdResetQueryPool(commandBuffer, warpQueryPool_, 0, 2);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, warpQueryPool_, 0);

        warpTimer_.RecordStartTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

        std::array<VkClearValue, 3> clearValues
        {
            VkClearValue { .color = {{0.0f, 0.0f, 0.0f, 1.0f }} }, // Color
            VkClearValue { .depthStencil = { .depth = 1.0f, .stencil = 0 } }, // Depth
            VkClearValue { },
        };

        VkRenderPassBeginInfo renderPassInfo
        {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = warpRenderPass_,
            .framebuffer = warpFramebuffers_[frameIndex],
            .renderArea = VkRect2D
            {
                .offset = { 0, 0 },
                .extent = swapChainExtent_,
            },
            .clearValueCount = static_cast<uint32_t>(clearValues.size()),
            .pClearValues = clearValues.data(),
        };

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, warpGraphicsPipeline_);

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            warpPipelineLayout_,
            0,
            1,
            &warpDescriptorSets_[(renderFrame_ + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT],
            0,
            nullptr
        );

        VkViewport viewport
        {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(swapChainExtent_.width),
            .height = static_cast<float>(swapChainExtent_.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor
        {
            .offset = { 0, 0 },
            .extent = swapChainExtent_,
        };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdDraw(commandBuffer, 6 * gridResolution_.x * gridResolution_.y, 1, 0, 0);

        ImDrawData* draw_data = ImGui::GetDrawData();
        ImGui_ImplVulkan_RenderDrawData(draw_data, commandBuffer);

        vkCmdEndRenderPass(commandBuffer);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, warpQueryPool_, 1);
        warpTimer_.RecordEndTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to record command buffer");
        }
    }

    // const FrameStats Projector::GetFrameStats() const
    // {
    //     FrameStats stats =
    //     {
    //         .renderStartStamps = std::vector<uint64_t>(MAX_FRAMES_IN_FLIGHT, 0),
    //         .renderEndStamps = std::vector<uint64_t>(MAX_FRAMES_IN_FLIGHT, 0),
    //         .renderTimes = std::vector<float>(MAX_FRAMES_IN_FLIGHT, 0.0f),
    //         .warpTime = 0.0f,
    //     };

    //     // Render pass
    //     {
    //         std::vector<uint64_t> results(MAX_FRAMES_IN_FLIGHT * 2 * 2, 0);
    //         VkResult result = vkGetQueryPoolResults(
    //             device_,
    //             renderQueryPool_,
    //             0,
    //             MAX_FRAMES_IN_FLIGHT * 2, // 2 queries/timestamps per frame (start & end)
    //             results.size() * sizeof(uint64_t), // 2 uint64_t entries per query/timestamp (result & availability value)
    //             results.data(),
    //             2 * sizeof(uint64_t),
    //             VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT
    //         );
    //         if (result != VK_SUCCESS && result != VK_NOT_READY)
    //         {
    //             throw std::runtime_error(std::string("failed to get render query pool results: ") + string_VkResult(result));
    //         }

    //         for (uint64_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    //         {
    //             uint64_t startTimeStamp = 0;
    //             uint64_t endTimeStamp = 0;
    //             if (results[4*i + 1] != 0)
    //             {
    //                 startTimeStamp = results[4*i + 0];
    //             }
    //             if (results[4*i + 3] != 0)
    //             {
    //                 endTimeStamp = results[4*i + 2];
    //             }
    //             if (startTimeStamp != 0 && endTimeStamp != 0)
    //             {
    //                 stats.renderStartStamps[i] = startTimeStamp;
    //                 stats.renderEndStamps[i] = endTimeStamp;
    //                 stats.renderTimes[i] = (endTimeStamp - startTimeStamp) * gpu_->Properties().limits.timestampPeriod * 0.000001f;
    //             }
    //         }
    //     }
        

    //     // Warp pass
    //     {
    //         std::vector<uint64_t> results(2 * 2, 0);
    //         VkResult result = vkGetQueryPoolResults(
    //             device_,
    //             warpQueryPool_,
    //             0,
    //             2, // 2 queries/timestamps (start & end)
    //             results.size() * sizeof(uint64_t), // 2 uint64_t entries per query/timestamp (result & availability value)
    //             results.data(),
    //             2 * sizeof(uint64_t),
    //             VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT
    //         );
    //         if (result != VK_SUCCESS && result != VK_NOT_READY)
    //         {
    //             throw std::runtime_error(std::string("failed to get warp query pool results: ") + string_VkResult(result));
    //         }

    //         uint64_t startTimeStamp = 0;
    //         uint64_t endTimeStamp = 0;
    //         if (results[1] != 0)
    //         {
    //             startTimeStamp = results[0];
    //         }
    //         if (results[3] != 0)
    //         {
    //             endTimeStamp = results[2];
    //         }
    //         if (startTimeStamp != 0 && endTimeStamp != 0)
    //         {
    //             stats.warpStartStamp = startTimeStamp;
    //             stats.warpEndStamp = endTimeStamp;
    //             stats.warpTime = (endTimeStamp - startTimeStamp) * gpu_->Properties().limits.timestampPeriod * 0.000001f;
    //         }
    //     }

    //     return stats;
    // }

    void Projector::RecreateSwapChain()
    {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        while (width == 0 || height == 0)
        {
            glfwGetFramebufferSize(window_, &width, &height);
            glfwWaitEvents();
        }

        std::cout << "Recreating swapchain" << std::endl;

        vkDeviceWaitIdle(device_);
        CleanupSwapChain();

        CreateSwapChain();
        CreateImageViews();
        CreateRenderPass();
        CreateDescriptorSetLayout();
        CreateRenderImageResources();
        CreateWarpSampler();
        CreateDescriptorPool();
        CreateDescriptorSets();
        CreateFramebuffers();
        CreateGraphicsPipeline();
    }

    void Projector::CleanupSwapChain()
    {
        vkDestroyImageView(device_, colorImageView_, nullptr);
        vkDestroyImage(device_, colorImage_, nullptr);
        vkFreeMemory(device_, colorImageMemory_, nullptr);

        vkDestroyImageView(device_, renderDepthImageView_, nullptr);
        vkDestroyImage(device_, renderDepthImage_, nullptr);
        vkFreeMemory(device_, renderDepthImageMemory_, nullptr);

        // vkDestroyImageView(device_, shadingRateImageView_, nullptr);
        // vkDestroyImage(device_, shadingRateImage_, nullptr);
        // vkFreeMemory(device_, shadingRateImageMemory_, nullptr);

        for (size_t i = 0; i < resultImageViews_.size(); i++) // MAX_FRAMES_IN_FLIGHT
        {
            vkDestroyImageView(device_, resultImageViews_[i], nullptr);
            vkDestroyImage(device_, resultImages_[i], nullptr);
            vkFreeMemory(device_, resultImagesMemory_[i], nullptr);
        }

        vkDestroyImageView(device_, warpColorImageView_, nullptr);
        vkDestroyImage(device_, warpColorImage_, nullptr);
        vkFreeMemory(device_, warpColorImageMemory_, nullptr);

        vkDestroySampler(device_, warpSampler_, nullptr);

        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);

        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        vkDestroyDescriptorSetLayout(device_, warpDescriptorSetLayout_, nullptr);

        vkDestroyRenderPass(device_, renderPass_, nullptr);
        vkDestroyRenderPass(device_, warpRenderPass_, nullptr);

        for (size_t i = 0; i < mainFramebuffers_.size(); i++) // MAX_FRAMES_IN_FLIGHT
        {
            vkDestroyFramebuffer(device_, mainFramebuffers_[i], nullptr);
        }

        for (size_t i = 0; i < swapChainImages_.size(); i++)
        {
            vkDestroyFramebuffer(device_, warpFramebuffers_[i], nullptr);
            vkDestroyImageView(device_, swapChainImageViews_[i], nullptr);
        }

        vkDestroySwapchainKHR(device_, swapChain_, nullptr);
    }

    void Projector::FramebufferResizeCallback(GLFWwindow* window, int width, int height)
    {
        auto app = reinterpret_cast<Projector*>(glfwGetWindowUserPointer(window));
        app->Resized();
    }
}
