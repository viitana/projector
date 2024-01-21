#pragma once

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"
#include <vulkan/vulkan.h>

namespace Projector
{
    class GPU
    {
        public:
        GPU(const VkInstance instance, const VkPhysicalDevice device, VkSurfaceKHR surface);

        const bool HasDeviceExtensions(const std::vector<const char*>& extensions) const;

        const VkPhysicalDevice PhysicalDevice() const { return m_device; };
        const VkPhysicalDeviceProperties Properties() const { return m_deviceProperties; };
        const VkPhysicalDeviceFeatures Features() const { return m_deviceFeatures; };
        const std::vector<VkExtensionProperties> AvailableDeviceExtensions() const { return m_availableDeviceExtensions; };
        const VkPhysicalDeviceFragmentShadingRatePropertiesKHR FramentShadingRateProperties() const { return m_fragmentShadingRateProperties; };
        const VkSurfaceCapabilitiesKHR SurfaceCapabilities() const { return m_surfaceCapabilities; }
        const VkSampleCountFlagBits MaxSampleCount() const { return m_maxSampleCount; }

        const VkExtent2D GetSurfaceExtent(const int windowWidth, const int windowHeight);
        const VkSurfaceFormatKHR SurfraceFormat() const { return m_surfaceFormat; }
        const VkPresentModeKHR PresentMode() const { return m_presentMode; }

        const VkQueueFamilyProperties RenderQueueFamily() const { return m_renderQueueFamily; }
        const VkQueueFamilyProperties WarpQueueFamily() const { return m_warpQueueFamily; }
        const VkQueueFamilyProperties PresentQueueFamily() const { return m_presentQueueFamily; }
        const uint32_t RenderQueueFamilyIndex() const { return m_renderQueueFamilyIndex; }
        const uint32_t WarpQueueFamilyIndex() const { return m_warpQueueFamilyIndex; }
        const uint32_t PresentQueueFamilyIndex() const { return m_presentQueueFamilyIndex; }

        const bool IsSuitable();
        const bool IsDiscrete();

        const std::vector<VkSampleCountFlagBits>& ValidSampleCounts() const { return m_validSampleCounts; }
        const void ChooseSampleCount(const VkSampleCountFlagBits sampleCount);
        const VkSampleCountFlagBits ChosenSampleCount() const { return m_chosenSampleCount; }

        private:

        const void FindFamilies();
        const bool IsSuitableQueueFamilyForRender(const VkQueueFamilyProperties& queueFamily, const uint32_t queueFamilyIndex);
        const bool IsSuitableQueueFamilyForWarp(const VkQueueFamilyProperties& queueFamily, const uint32_t queueFamilyIndex);
        const bool IsSuitableQueueFamilyForPresent(const VkQueueFamilyProperties& queueFamily, const uint32_t queueFamilyIndex);

        // Surface 
        const VkSurfaceKHR m_surface = VK_NULL_HANDLE;

        // Device, properties & features
        const VkPhysicalDevice m_device = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties m_deviceProperties;
        VkPhysicalDeviceFeatures m_deviceFeatures;

        std::vector<VkSampleCountFlagBits> m_validSampleCounts;
        VkSampleCountFlagBits m_maxSampleCount = VK_SAMPLE_COUNT_1_BIT;
        VkSampleCountFlagBits m_chosenSampleCount = VK_SAMPLE_COUNT_1_BIT;

        // Device extensions
        std::vector<VkExtensionProperties> m_availableDeviceExtensions;
        std::vector<std::string> m_requiredDeviceExtensions;
        std::vector<std::string> m_missingDeviceExtensions;

        // Fragment shading rate properties
        VkPhysicalDeviceFragmentShadingRatePropertiesKHR m_fragmentShadingRateProperties;
        std::vector<VkPhysicalDeviceFragmentShadingRateKHR> m_shadingRates;

        // Depth/stencil resolve properties
        VkPhysicalDeviceDepthStencilResolveProperties m_depthStencilResolveProperties;

        // Swapchain details
        VkSurfaceCapabilitiesKHR m_surfaceCapabilities;
		std::vector<VkSurfaceFormatKHR> m_surfaceFormats;
        VkSurfaceFormatKHR m_surfaceFormat;
		std::vector<VkPresentModeKHR> m_presentModes;
		VkPresentModeKHR m_presentMode;

        // Queue families
        std::vector<VkQueueFamilyProperties> m_queueFamilies;
        VkQueueFamilyProperties m_renderQueueFamily;
        VkQueueFamilyProperties m_warpQueueFamily;
        VkQueueFamilyProperties m_presentQueueFamily;
        uint32_t m_renderQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
        uint32_t m_warpQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
        uint32_t m_presentQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
        uint32_t m_renderQueueIndex = std::numeric_limits<uint32_t>::max();
        uint32_t m_warpQueueIndex = std::numeric_limits<uint32_t>::max();
        uint32_t m_presentQueueIndex = std::numeric_limits<uint32_t>::max();
        bool m_foundQueues = false;
    };
}

