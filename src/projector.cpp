#include "projector.hpp"

#include <chrono>
#include <thread>

#include <stb_image.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/projection.hpp>
#include <glm/gtx/euler_angles.hpp>

#include "scene.hpp"

namespace Projector
{
    Projector::Projector()
    {
        assert(MAX_FRAMES_IN_FLIGHT > 1);

        if (!glfwInit())
        {
            throw std::runtime_error("failed to initialize glfw!");
        }

        CreateInstance();
        CreateSurface();

        PickPhysicalDevice();
        CreateLogicalDevice();
        CreateCommandPool();

        Input::InputHandler::Init(window_);

        scene_ = new Scene::Model(
            "res/sponza/Sponza.gltf",
            //"res/abeautifulgame/ABeautifulGame.gltf",
            physicalDevice_,
            device_,
            commandPool_,
            graphicsQueue_,
            1.0f
        );

        CreateUniformBuffers();

        RecreateSwapChain();
        InitImGui();

        CreateGraphicsPipeline();

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
                    if (ImGui::BeginCombo("Variable rate shade overdraw", VariableRateShadingNames[variableRateShadingMode_]))
                    {
                        for (int n = 0; n < VariableRateShadingNames.size(); n++)
                        {
                            bool is_selected = variableRateShadingMode_ == n;
                            if (ImGui::Selectable(VariableRateShadingNames[n], is_selected))
                            {
                                variableRateShadingMode_ = (VariableRateShadingMode)n;
                                doRecreateSwapchain = true;
                            }
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SliderFloat("Clamp image to edge", &clampOvershootPercent_, 0, 100, "%.0f%%");
                    ImGui::Indent(-12.0f);

                    ImGui::End();

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

    const VkSurfaceFormatKHR Projector::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const
    {
        for (const auto& availableFormat : availableFormats)
        {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return availableFormat;
            }
        }
        return availableFormats[0];
    }

    const VkPresentModeKHR Projector::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const
    {
        for (const auto& availablePresentMode : availablePresentModes)
        {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return availablePresentMode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    const VkExtent2D Projector::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities.currentExtent;
        }
        else
        {
            int width, height;
            glfwGetFramebufferSize(window_, &width, &height);

            VkExtent2D actualExtent =
            {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
            };
            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
            return actualExtent;
        }
    }

    const SwapChainSupportDetails Projector::QuerySwapChainSupport(VkPhysicalDevice device) const
    {
        SwapChainSupportDetails details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
        if (formatCount != 0)
        {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr);
        if (presentModeCount != 0)
        {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    const QueueFamilyIndices Projector::FindQueueFamilies(VkPhysicalDevice device) const
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        QueueFamilyIndices indices;
        int i = 0;
        for (const auto& queueFamily : queueFamilies)
        {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                indices.graphicsFamily = i;
            }
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
            if (presentSupport)
            {
                indices.presentFamily = i;
            }
            i++;
        }
        return indices;
    }

    void Projector::ListDeviceDetails(VkPhysicalDevice device) const
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        std::cout << "    Max MSAA sample count:  " << GetMaxUsableSampleCount(device) << std::endl;

        std::cout << "    Queues:" << std::endl;
        for (auto& q_family : queueFamilies)
        {
            std::cout << "    - Count: " << q_family.queueCount << std::endl;
            std::cout << "      Flags:" << std::endl;
            if (q_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) std::cout << "        VK_QUEUE_GRAPHICS_BIT" << std::endl;
            if (q_family.queueFlags & VK_QUEUE_COMPUTE_BIT) std::cout << "        VK_QUEUE_COMPUTE_BIT" << std::endl;
            if (q_family.queueFlags & VK_QUEUE_TRANSFER_BIT) std::cout << "        VK_QUEUE_TRANSFER_BIT" << std::endl;
            if (q_family.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) std::cout << "        VK_QUEUE_SPARSE_BINDING_BIT" << std::endl;
            if (q_family.queueFlags & VK_QUEUE_PROTECTED_BIT) std::cout << "        VK_QUEUE_PROTECTED_BIT" << std::endl;
            if (q_family.queueFlags & VK_QUEUE_OPTICAL_FLOW_BIT_NV) std::cout << "        VK_QUEUE_OPTICAL_FLOW_BIT_NV" << std::endl;
            if (q_family.queueFlags & VK_QUEUE_FLAG_BITS_MAX_ENUM) std::cout << "        VK_QUEUE_FLAG_BITS_MAX_ENUM" << std::endl;
        }

        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::cout << "    Available extensions: " << extensionCount << std::endl;
        for (const auto& extension : availableExtensions)
        {
            std::cout << "      " << extension.extensionName << std::endl;
        }
    }

    const bool Projector::CheckDeviceExtensionSupport(VkPhysicalDevice device) const
    {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
        for (const auto& extension : availableExtensions)
        {
            requiredExtensions.erase(extension.extensionName);
        }
        return requiredExtensions.empty();
    }

    const bool Projector::IsDeviceSuitable(VkPhysicalDevice device) const
    {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);
        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

        QueueFamilyIndices indices = FindQueueFamilies(device);

        bool extensionsSupported = CheckDeviceExtensionSupport(device);

        bool swapChainAdequate = false;
        if (extensionsSupported)
        {
            SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }

        return
            indices.IsComplete() &&
            extensionsSupported &&
            swapChainAdequate &&
            deviceFeatures.samplerAnisotropy &&
            deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
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
            throw std::runtime_error("failed to create shader module!");
        }
        return shaderModule;
    }

    const VkFormat Projector::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const
    {
        for (VkFormat format : candidates)
        {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
            {
                return format;
            }
            else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
            {
                return format;
            }
        }
        throw std::runtime_error("failed to find supported format!");
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

    const VkSampleCountFlagBits Projector::GetMaxUsableSampleCount(VkPhysicalDevice device) const
    {
        VkPhysicalDeviceProperties physicalDeviceProperties;
        vkGetPhysicalDeviceProperties(device, &physicalDeviceProperties);

        VkSampleCountFlags counts =
            physicalDeviceProperties.limits.framebufferColorSampleCounts &
            physicalDeviceProperties.limits.framebufferDepthSampleCounts;

        if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
        if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
        if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
        if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
        if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
        if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

        return VK_SAMPLE_COUNT_1_BIT;
    }

    void Projector::CreateInstance()
    {
        if (!CheckValidationLayerSupport())
        {
            throw std::runtime_error("validation layers not available!");
        }

        VkApplicationInfo appInfo
        {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "projector",
            .applicationVersion = VK_MAKE_VERSION(VERSION_MAJOR, VERSION_MINOR, 0),
            .pEngineName = "No Engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
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
            throw std::runtime_error("failed to create instance!");
        }
    }

    void Projector::CreateSurface()
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        window_ = glfwCreateWindow(1920, 1080, "projector", nullptr, nullptr);
        if (window_ == nullptr)
        {
            throw std::runtime_error("failed to create window!");
        }
        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, FramebufferResizeCallback);

        VkResult result = glfwCreateWindowSurface(vk_, window_, nullptr, &surface_);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create window surface!");
        }
    }

    void Projector::PickPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(vk_, &deviceCount, nullptr);
        if (deviceCount == 0)
        {
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        }

        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::cout << "Required extensions: " << glfwExtensionCount << std::endl;
        for (int i = 0; i < glfwExtensionCount; i++)
        {
            std::cout << "  " << glfwExtensions[i] << std::endl;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(vk_, &deviceCount, devices.data());

        std::string deviceName = "Unknown";
        std::cout << "Available devices: " << deviceCount << std::endl;
        for (const auto& device : devices)
        {
            VkPhysicalDeviceProperties deviceProperties;
            vkGetPhysicalDeviceProperties(device, &deviceProperties);
            std::cout << "  " << deviceProperties.deviceName << ":" << std::endl;
            ListDeviceDetails(device);
        }

        for (const auto& device : devices)
        {
            if (IsDeviceSuitable(device))
            {
                VkPhysicalDeviceFragmentShadingRatePropertiesKHR shadingRateProperties
                {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR,
                };
                VkPhysicalDeviceProperties2 deviceProperties
                {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                    .pNext = &shadingRateProperties,
                };
                vkGetPhysicalDeviceProperties2(device, &deviceProperties);

                physicalDevice_ = device;
                msaaSamples_ = GetMaxUsableSampleCount(device);
                msaaSamples_ = VK_SAMPLE_COUNT_2_BIT;
                deviceName = deviceProperties.properties.deviceName;
                shadingRateProperties_ = shadingRateProperties;

                auto vkGetPhysicalDeviceFragmentShadingRatesKHR_ = (PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR)vkGetInstanceProcAddr(vk_, "vkGetPhysicalDeviceFragmentShadingRatesKHR");

                uint32_t shadingRatesCount = 0;
                vkGetPhysicalDeviceFragmentShadingRatesKHR_(device, &shadingRatesCount, VK_NULL_HANDLE);
                if (shadingRatesCount > 0)
                {
                    shadingRates_.resize(shadingRatesCount);
                    for (VkPhysicalDeviceFragmentShadingRateKHR& fragment_shading_rate : shadingRates_)
                    {
                        fragment_shading_rate.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_KHR;
                    }
                    vkGetPhysicalDeviceFragmentShadingRatesKHR_(device, &shadingRatesCount, shadingRates_.data());
                }

                break;
            }
        }

        if (physicalDevice_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
        std::cout << "Picked device \"" << deviceName << "\"" << std::endl;
    }

    void Projector::CreateLogicalDevice()
    {
        float defaultPriority = 0.0f;
        float highPriority = 1.0f;

        QueueFamilyIndices queueFamilies = FindQueueFamilies(physicalDevice_);
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

        std::array<float, 2> graphicsQueuePriorities { defaultPriority, highPriority };
        VkDeviceQueueCreateInfo graphicsQueueCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = queueFamilies.graphicsFamily.value(),
            .queueCount = 2,
            .pQueuePriorities = graphicsQueuePriorities.data(),
        };
        queueCreateInfos.push_back(graphicsQueueCreateInfo);

        VkDeviceQueueCreateInfo presentQueueCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = queueFamilies.presentFamily.value(),
            .queueCount = 1,
            .pQueuePriorities = &defaultPriority,
        };
        queueCreateInfos.push_back(presentQueueCreateInfo);

        VkPhysicalDeviceFragmentShadingRateFeaturesKHR shadingRateFeatures
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR,
            .pipelineFragmentShadingRate = VK_FALSE,
            .primitiveFragmentShadingRate = VK_FALSE,
            .attachmentFragmentShadingRate = VK_TRUE,
        };
        VkPhysicalDeviceFeatures2 deviceFeatures2
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &shadingRateFeatures,
            .features = VkPhysicalDeviceFeatures
            {
                .samplerAnisotropy = VK_TRUE,
            }
        };
        VkDeviceCreateInfo createInfo
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &deviceFeatures2,
            .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
            .pQueueCreateInfos = queueCreateInfos.data(),
            .enabledLayerCount = static_cast<uint32_t>(validationLayers.size()),
            .ppEnabledLayerNames = validationLayers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
            .ppEnabledExtensionNames = deviceExtensions.data(),
        };

        if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create logical device!");
        }

        vkGetDeviceQueue(device_, queueFamilies.graphicsFamily.value(), 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, queueFamilies.graphicsFamily.value(), 1, &warpQueue_);
        vkGetDeviceQueue(device_, queueFamilies.presentFamily.value(), 0, &presentQueue_);
    }

    void Projector::CreateSwapChain()
    {
        SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(physicalDevice_);

        VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities);

        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
        {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        VkSharingMode imageSharingMode;
        uint32_t queueFamilyIndexCount;
        uint32_t* queueFamilyIndices;

        QueueFamilyIndices familyIndices = FindQueueFamilies(physicalDevice_);
        uint32_t families[2] = { familyIndices.graphicsFamily.value(), familyIndices.presentFamily.value() };

        if (familyIndices.graphicsFamily != familyIndices.presentFamily)
        {
            imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            queueFamilyIndexCount = 2;
            queueFamilyIndices = families;
        }
        else
        {
            imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            queueFamilyIndexCount = 0;
            queueFamilyIndices = nullptr;
        }

        VkSwapchainCreateInfoKHR createInfo
        {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface_,
            .minImageCount = imageCount,
            .imageFormat = surfaceFormat.format,
            .imageColorSpace = surfaceFormat.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = imageSharingMode,
            .queueFamilyIndexCount = queueFamilyIndexCount,
            .pQueueFamilyIndices = queueFamilyIndices,
            .preTransform = swapChainSupport.capabilities.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = presentMode,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE,
        };

        if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapChain_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create swap chain!");
        }

        vkGetSwapchainImagesKHR(device_, swapChain_, &imageCount, nullptr);
        swapChainImages_.resize(imageCount);
        vkGetSwapchainImagesKHR(device_, swapChain_, &imageCount, swapChainImages_.data());

        swapChainImageFormat_ = surfaceFormat.format;
        swapChainExtent_ = extent;
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
            swapChainImageViews_[i] = Util::CreateImageView(device_, swapChainImages_[i], swapChainImageFormat_, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        }
    }

    void Projector::CreateRenderPass()
    {
        // Main pass
        {
            VkAttachmentDescription2 colorAttachment
            {
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .format = swapChainImageFormat_,
                .samples = msaaSamples_,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
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
                .samples = msaaSamples_,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
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
                .format = swapChainImageFormat_,
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

            VkAttachmentDescription2 shadingRateAttachmentResolve
            {
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .format = VK_FORMAT_R8_UINT,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
                .finalLayout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
            };
            VkAttachmentReference2 shadingRateAttachmentResolveRef
            {
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
                .attachment = 3,
                .layout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
            };

            VkFragmentShadingRateAttachmentInfoKHR shadingRateAttachmentInfo =
            {
                .sType = VK_STRUCTURE_TYPE_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR,
                .pFragmentShadingRateAttachment = &shadingRateAttachmentResolveRef,
                .shadingRateAttachmentTexelSize = shadingRateProperties_.maxFragmentShadingRateAttachmentTexelSize,
            };

            VkSubpassDescription2 subpass
            {
                .sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
                .pNext = &shadingRateAttachmentInfo,
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
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            };

            std::array<VkAttachmentDescription2, 4> attachments = { colorAttachment, depthAttachment, colorAttachmentResolve, shadingRateAttachmentResolve };
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
                throw std::runtime_error("failed to create render pass!");
            }
        }

        // Warp pass
        {
            VkAttachmentDescription colorAttachment
            {
                .format = swapChainImageFormat_,
                .samples = msaaSamples_,
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
                .format = swapChainImageFormat_,
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
                .attachment = 1,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            };

            VkSubpassDescription subpass
            {
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount = 1,
                .pColorAttachments = &colorAttachmentRef,
                .pResolveAttachments = &colorAttachmentResolveRef,
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

            std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, colorAttachmentResolve };
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
                throw std::runtime_error("failed to create warp render pass!");
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

            std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR };
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
                .rasterizationSamples = msaaSamples_,
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
                throw std::runtime_error("failed to create pipeline layout!");
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
                throw std::runtime_error("failed to create graphics pipeline!");
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
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = VK_CULL_MODE_NONE,
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
                .rasterizationSamples = msaaSamples_,
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
                throw std::runtime_error("failed to create pipeline layout!");
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
                throw std::runtime_error("failed to create graphics pipeline!");
            }

            vkDestroyShaderModule(device_, fragShaderModule, nullptr);
            vkDestroyShaderModule(device_, vertShaderModule, nullptr);
        }
    }

    void Projector::CreateCommandPool()
    {
        QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(physicalDevice_);

        VkCommandPoolCreateInfo poolInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = queueFamilyIndices.graphicsFamily.value(),
        };

        if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create command pool!");
        }
    }

    void Projector::CreateRenderImageResources()
    {
        // Render color image
        {
            VkFormat colorFormat = swapChainImageFormat_;
            Util::CreateImage(physicalDevice_, device_, renderExtent_.width, renderExtent_.height, 1, msaaSamples_, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage_, colorImageMemory_);
            colorImageView_ = Util::CreateImageView(device_, colorImage_, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        }
        // Render depth image
        {
            VkFormat depthFormat = FindDepthFormat();
            Util::CreateImage(physicalDevice_, device_, renderExtent_.width, renderExtent_.height, 1, msaaSamples_, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage_, depthImageMemory_);
            depthImageView_ = Util::CreateImageView(device_, depthImage_, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
        }
        // Shading rate map image
        {
            VkFormat rateFormat = VK_FORMAT_R8_UINT;
            uint32_t width = static_cast<uint32_t>(ceil(renderExtent_.width / (float)shadingRateProperties_.maxFragmentShadingRateAttachmentTexelSize.width));
            uint32_t height = static_cast<uint32_t>(ceil(renderExtent_.height / (float)shadingRateProperties_.maxFragmentShadingRateAttachmentTexelSize.height));
            Util::CreateImage(physicalDevice_, device_, width, height, 1, VK_SAMPLE_COUNT_1_BIT, rateFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, shadingRateImage_, shadingRateImageMemory_);
            shadingRateImageView_ = Util::CreateImageView(device_, shadingRateImage_, rateFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        
            VkDeviceSize imageSize = width * height * sizeof(uint8_t);

            uint8_t noneVal = 0;
            uint8_t variableShaded = 0;
            switch (variableRateShadingMode_)
            {
            case VariableRateShadingMode::None:
                variableShaded = 0;
                break;
            case VariableRateShadingMode::TwoByTwo:
                variableShaded = (2 >> 1) | (2 << 1);
                break;
            case VariableRateShadingMode::FourByFour:
                variableShaded = (4 >> 1) | (4 << 1);
                break;
            }

            uint8_t* mapData = new uint8_t[imageSize];
            memset(mapData, noneVal, imageSize);

            float visibleRatio = viewScreenScale_ / renderScreenScale_;
            float outsideWidth = (width - width * visibleRatio) * 0.5f;
            float outsideHeight = (height - height * visibleRatio) * 0.5f;

            uint8_t* cursor = mapData;
            for (uint32_t y = 0; y < height; y++)
            {
                for (uint32_t x = 0; x < width; x++)
                {
                    const float deltaX = ((float)width / 2.0f - (float)x) / width * 100.0f;
                    const float deltaY = ((float)height / 2.0f - (float)y) / height * 100.0f;
                    const float dist = std::sqrt(deltaX * deltaX + deltaY * deltaY);
                    if (x < outsideWidth || x > width - outsideWidth || y < outsideHeight || y > height - outsideHeight)
                    {
                        *cursor = variableShaded;
                    }
                    cursor++;
                }
            }

            VkBuffer stagingBuffer;
            VkDeviceMemory stagingBufferMemory;
            Util::CreateBuffer(physicalDevice_, device_, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

            uint8_t* data;
            vkMapMemory(device_, stagingBufferMemory, 0, imageSize, 0, (void**)&data);
            memcpy(data, mapData, (size_t)imageSize);
            vkUnmapMemory(device_, stagingBufferMemory);

            Util::TransitionImageLayout(device_, commandPool_, graphicsQueue_, shadingRateImage_, VK_FORMAT_R8_UINT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
            Util::CopyBufferToImage(device_, commandPool_, graphicsQueue_, stagingBuffer, shadingRateImage_, width, height);

            vkDestroyBuffer(device_, stagingBuffer, nullptr);
            vkFreeMemory(device_, stagingBufferMemory, nullptr);

            Util::TransitionImageLayout(
                device_,
                commandPool_,
                graphicsQueue_,
                shadingRateImage_,
                VK_FORMAT_R8_UINT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
                1
            );


        }
        // Render result image
        {
            resultImages_.resize(MAX_FRAMES_IN_FLIGHT);
            resultImageViews_.resize(MAX_FRAMES_IN_FLIGHT);
            resultImagesMemory_.resize(MAX_FRAMES_IN_FLIGHT);
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                VkFormat colorFormat = swapChainImageFormat_;
                Util::CreateImage(physicalDevice_, device_, renderExtent_.width, renderExtent_.height, 1, VK_SAMPLE_COUNT_1_BIT, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, resultImages_[i], resultImagesMemory_[i]);
                resultImageViews_[i] = Util::CreateImageView(device_, resultImages_[i], colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
                Util::TransitionImageLayout(
                    device_,
                    commandPool_,
                    graphicsQueue_,
                    resultImages_[i],
                    swapChainImageFormat_,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    1
                );
            }
        }
        // Warp color image
        {
            VkFormat colorFormat = swapChainImageFormat_;
            Util::CreateImage(physicalDevice_, device_, swapChainExtent_.width, swapChainExtent_.height, 1, msaaSamples_, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, warpColorImage_, warpColorImageMemory_);
            warpColorImageView_ = Util::CreateImageView(device_, warpColorImage_, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        }
    }

    void Projector::CreateFramebuffers()
    {
        // Main render pass framebuffers
        {
            mainFramebuffers_.resize(MAX_FRAMES_IN_FLIGHT);
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                std::array<VkImageView, 4> attachments =
                {
                    colorImageView_,
                    depthImageView_,
                    resultImageViews_[i],
                    shadingRateImageView_,
                };
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
                    throw std::runtime_error("failed to create framebuffer!");
                }
            }
        }
        // Warp pass framebuffers
        {
            warpFramebuffers_.resize(swapChainImages_.size());
            for (size_t i = 0; i < swapChainImages_.size(); i++)
            {
                std::array<VkImageView, 2> attachments =
                {
                    warpColorImageView_,
                    swapChainImageViews_[i],
                };
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
                    throw std::runtime_error("failed to create framebuffer!");
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
                Util::CreateBuffer(physicalDevice_, device_, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffers_[i], uniformBuffersMemory_[i]);
                vkMapMemory(device_, uniformBuffersMemory_[i], 0, bufferSize, 0, &uniformBuffersMapped_[i]);
            }
        }

        {
            VkDeviceSize bufferSize = sizeof(WarpUniformBufferObject);

            Util::CreateBuffer(physicalDevice_, device_, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, warpUniformBuffer_, warpUniformBufferMemory_);
            vkMapMemory(device_, warpUniformBufferMemory_, 0, bufferSize, 0, &warpUniformBufferMapped_);
        }
    }

    void Projector::CreateWarpSampler()
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &properties);

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
            .anisotropyEnable = VK_TRUE,
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
            throw std::runtime_error("failed to create texture sampler!");
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
                throw std::runtime_error("failed to create descriptor set layout!");
            }
        }

        // Warp
        {
            VkDescriptorSetLayoutBinding warpUboLayoutBinding
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .pImmutableSamplers = nullptr, // Optional
            };

            VkDescriptorSetLayoutBinding warpSamplerLayoutBinding
            {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr,
            };

            std::array<VkDescriptorSetLayoutBinding, 2> bindings = { warpUboLayoutBinding, warpSamplerLayoutBinding };
            VkDescriptorSetLayoutCreateInfo layoutInfo
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            };

            VkResult result = vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &warpDescriptorSetLayout_);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create descriptor set layout!");
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
                .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
            },
            VkDescriptorPoolSize// For warp pass
            {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
            },
        };

        VkDescriptorPoolCreateInfo poolInfo
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 2 * static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes = poolSizes.data(),
        };

        if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create descriptor pool!");
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
                throw std::runtime_error("failed to allocate descriptor sets!");
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
                throw std::runtime_error("failed to allocate warp descriptor sets!");
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

                std::array<VkWriteDescriptorSet, 2> descriptorWrites
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
                throw std::runtime_error("failed to allocate command buffers!");
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
                throw std::runtime_error("failed to allocate warp command buffers!");
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
            .PhysicalDevice = physicalDevice_,
            .Device = device_,
            .Queue = graphicsQueue_,
            .DescriptorPool = imguiPool_,
            .MinImageCount = 3,
            .ImageCount = 3,
            .MSAASamples = msaaSamples_,
        };
        ImGui_ImplVulkan_Init(&init_info, warpRenderPass_);

        VkCommandBuffer commandBuffer = Util::BeginSingleTimeCommands(device_, commandPool_);
        ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);
        Util::EndSingleTimeCommands(device_, commandPool_, graphicsQueue_, commandBuffer);
        ImGui_ImplVulkan_DestroyFontUploadObjects();

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
            glm::eulerAngleY(playerWarp_.rotation.x) *
            glm::vec4(input.moveDelta.x, 0, input.moveDelta.y, 0);

        playerWarp_.position += relativeMovement;
        playerWarp_.rotation.x += input.mouseDelta.x;
        playerWarp_.rotation.y += input.mouseDelta.y;

        if (render)
        {
            playerRender_ = playerWarp_;
        }

        glm::mat4 rotation = glm::eulerAngleYX(
            playerRender_.rotation.x,
            playerRender_.rotation.y
        );
        glm::mat4 rotationI = glm::eulerAngleYX(
            -playerRender_.rotation.x,
            -playerRender_.rotation.y
        );
        glm::mat4 rotationw = glm::eulerAngleYX(
            -playerWarp_.rotation.x ,
            -playerWarp_.rotation.y
        );

        UniformBufferObject mainUbo
        {
            .view = glm::lookAt(
                glm::vec3(0, 1.2f, 0) + playerWarp_.position,
                glm::vec3(0, 1.2f, 0) + playerWarp_.position + glm::vec3(rotation * glm::vec4(0, 0, 1, 0)),
                glm::vec3(0.0f, 1.0f, 0.0f)
            ),
            .proj = glm::perspective(
                glm::radians(renderFov_),
                swapChainExtent_.width / (float)swapChainExtent_.height,
                0.01f,
                100.0f
            ),
        };
        mainUbo.proj[1][1] *= -1; // Compensate for inverted clip Y axis on OpenGL
        memcpy(uniformBuffersMapped_[renderFrame_], &mainUbo, sizeof(mainUbo));

        WarpUniformBufferObject warpUbo
        {
            .view = glm::lookAt(
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 0.0f, 0.0f) + glm::vec3(rotationw * glm::vec4(0, 0, 1, 0)),
                glm::vec3(0.0f, 1.0f, 0.0f)
            ),
            .proj = glm::perspective(
                glm::radians(fov_),
                swapChainExtent_.width / (float)swapChainExtent_.height,
                0.001f,
                100.0f
            ),
            .screen = rotationI * glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 1)),
            .screenScale = renderOvershotScreenScale_,
            .uvScale = renderOvershotScreenScale_ / renderScreenScale_,
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
            RecordDraw(drawCommandBuffers_[renderFrame_]);

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
                throw std::runtime_error("failed to submit draw command buffer!");
            }
        }

        firstFrame = false;
        renderFrame_ = nextFrame;
    }

    void Projector::WarpPresent()
    {
        vkWaitForFences(device_, 1, &warpInFlightFence_, VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device_, swapChain_, UINT64_MAX, imageAvailableSemaphore_, VK_NULL_HANDLE, &imageIndex);
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
            throw std::runtime_error("failed to acquire swap chain image!");
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
            RecordWarp(warpCommandBuffer_, imageIndex);

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
                throw std::runtime_error("failed to submit warp command buffer!");
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
            .pImageIndices = &imageIndex,
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
            throw std::runtime_error("failed to present swap chain image!");
        }
    }

    void Projector::RecordDraw(VkCommandBuffer commandBuffer) const
    {
        VkCommandBufferBeginInfo beginInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = 0, // Optional
            .pInheritanceInfo = nullptr, // Optional
        };

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to begin recording command buffer!");
        }

        std::array<VkClearValue, 2> clearValues
        {
            VkClearValue { .color = {{0.0f, 0.0f, 0.0f, 1.0f}} },
            VkClearValue { .depthStencil = {.depth = 1.0f, .stencil = 0 } },
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

        VkExtent2D fragmentSize = { 1, 1 };
        VkFragmentShadingRateCombinerOpKHR combinerOps[2] = { VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR , VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR };
        auto vkCmdSetFragmentShadingRateKHR = (PFN_vkCmdSetFragmentShadingRateKHR)vkGetInstanceProcAddr(vk_, "vkCmdSetFragmentShadingRateKHR");

        vkCmdSetFragmentShadingRateKHR(commandBuffer, &fragmentSize, combinerOps);

        scene_->Draw(
            commandBuffer,
            0u,
            pipelineLayout_,
            1u
        );

        vkCmdEndRenderPass(commandBuffer);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to record command buffer!");
        }
    }

    void Projector::RecordWarp(VkCommandBuffer commandBuffer, uint32_t imageIndex) const
    {
        VkCommandBufferBeginInfo beginInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = 0, // Optional
            .pInheritanceInfo = nullptr, // Optional
        };

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to begin recording warp command buffer!");
        }

        std::array<VkClearValue, 2> clearValues
        {
            VkClearValue {.color = {{0.0f, 0.0f, 0.0f, 1.0f}} },
            VkClearValue {.depthStencil = {.depth = 1.0f, .stencil = 0 } },
        };

        VkRenderPassBeginInfo renderPassInfo
        {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = warpRenderPass_,
            .framebuffer = warpFramebuffers_[imageIndex],
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

        vkCmdDraw(commandBuffer, 6, 1, 0, 0);

        ImDrawData* draw_data = ImGui::GetDrawData();
        ImGui_ImplVulkan_RenderDrawData(draw_data, commandBuffer);

        vkCmdEndRenderPass(commandBuffer);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to record command buffer!");
        }
    }

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
    }

    void Projector::CleanupSwapChain()
    {
        vkDestroyImageView(device_, colorImageView_, nullptr);
        vkDestroyImage(device_, colorImage_, nullptr);
        vkFreeMemory(device_, colorImageMemory_, nullptr);

        vkDestroyImageView(device_, depthImageView_, nullptr);
        vkDestroyImage(device_, depthImage_, nullptr);
        vkFreeMemory(device_, depthImageMemory_, nullptr);

        vkDestroyImageView(device_, shadingRateImageView_, nullptr);
        vkDestroyImage(device_, shadingRateImage_, nullptr);
        vkFreeMemory(device_, shadingRateImageMemory_, nullptr);

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

    const bool QueueFamilyIndices::IsComplete() const
    {
        return
            graphicsFamily.has_value() &&
            presentFamily.has_value();
    }
}
