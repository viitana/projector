#include "gpu.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace Projector
{
    GPU::GPU(const VkInstance instance, const VkPhysicalDevice device, VkSurfaceKHR surface)
        : m_surface(surface), m_device(device)
    {
        // Get device properties
        vkGetPhysicalDeviceProperties(device, &m_deviceProperties);

        // Get Device features
        vkGetPhysicalDeviceFeatures(device, &m_deviceFeatures);

        // Figure out max sample count for both color & depth
        VkSampleCountFlags counts =
            m_deviceProperties.limits.framebufferColorSampleCounts &
            m_deviceProperties.limits.framebufferDepthSampleCounts;
        if (counts & VK_SAMPLE_COUNT_64_BIT) m_maxSampleCount = VK_SAMPLE_COUNT_64_BIT;
        else if (counts & VK_SAMPLE_COUNT_32_BIT) m_maxSampleCount = VK_SAMPLE_COUNT_32_BIT;
        else if (counts & VK_SAMPLE_COUNT_16_BIT) m_maxSampleCount = VK_SAMPLE_COUNT_16_BIT;
        else if (counts & VK_SAMPLE_COUNT_8_BIT) m_maxSampleCount = VK_SAMPLE_COUNT_8_BIT;
        else if (counts & VK_SAMPLE_COUNT_4_BIT) m_maxSampleCount = VK_SAMPLE_COUNT_4_BIT;
        else if (counts & VK_SAMPLE_COUNT_2_BIT) m_maxSampleCount = VK_SAMPLE_COUNT_2_BIT;
        else m_maxSampleCount = VK_SAMPLE_COUNT_1_BIT;

        // Get device available extensions count
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(m_device, nullptr, &extensionCount, nullptr);

        // Get device available extensions
        m_availableDeviceExtensions.resize(extensionCount);
        vkEnumerateDeviceExtensionProperties(m_device, nullptr, &extensionCount, m_availableDeviceExtensions.data());
    
        // Device extensions required by Projector itself
        m_requiredDeviceExtensions =
        {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME, // VK_KHR_swapchain
            // VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, // VK_KHR_fragment_shading_rate
            VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME, // VK_KHR_depth_stencil_resolve
        };

        // Get device extensions required by GLFW
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        // Combine required device extensions
        for (int i = 0; i < glfwExtensionCount; i++)
        {
            bool alreadyRequired = false;
            for (const std::string& requiredExtension : m_requiredDeviceExtensions)
            {
                if (requiredExtension.compare(glfwExtensions[i]))
                {
                    alreadyRequired = true;
                    break;
                }
            }
            if (!alreadyRequired) m_requiredDeviceExtensions.push_back(glfwExtensions[i]);
        }
    
        // Find missing device extensions
        m_missingDeviceExtensions = std::vector<std::string>(m_requiredDeviceExtensions);
        for (int i = m_missingDeviceExtensions.size() - 1; i >= 0; i--)
        {
            for (const VkExtensionProperties& availableExtension : m_availableDeviceExtensions)
            {
                if (!strcmp(availableExtension.extensionName, m_missingDeviceExtensions[i].c_str()))
                {
                    m_missingDeviceExtensions.erase(std::next(m_missingDeviceExtensions.begin(), i));
                    break;
                }
            }
        }

        // Get fragment shading rate properties
        m_fragmentShadingRateProperties =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR,
        };
        m_depthStencilResolveProperties =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES,
            .pNext = &m_fragmentShadingRateProperties
        };
        VkPhysicalDeviceProperties2 deviceProperties
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &m_depthStencilResolveProperties,
        };
        vkGetPhysicalDeviceProperties2(device, &deviceProperties);

        // Get shading rate modes
        auto vkGetPhysicalDeviceFragmentShadingRatesKHR_ = (PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFragmentShadingRatesKHR");
        uint32_t shadingRatesCount = 0;
        vkGetPhysicalDeviceFragmentShadingRatesKHR_(device, &shadingRatesCount, VK_NULL_HANDLE);
        if (shadingRatesCount > 0)
        {
            m_shadingRates.resize(shadingRatesCount);
            for (VkPhysicalDeviceFragmentShadingRateKHR& fragmentShadingRate : m_shadingRates)
            {
                fragmentShadingRate.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_KHR;
            }
            vkGetPhysicalDeviceFragmentShadingRatesKHR_(device, &shadingRatesCount, m_shadingRates.data());
        }

        // Get capabilities for surface
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device, surface, &m_surfaceCapabilities);

        // Get supported surface formats
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        if (formatCount != 0)
        {
            m_surfaceFormats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, m_surfaceFormats.data());
        }

        // Pick surface format to use
        bool pickedSurfaceFormat = false;
        for (const VkSurfaceFormatKHR& surfaceFormat : m_surfaceFormats)
        {
            if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB && surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                m_surfaceFormat = surfaceFormat;
                pickedSurfaceFormat = true;
                break;
            }
        }
        if(!pickedSurfaceFormat && !m_surfaceFormats.empty()) m_surfaceFormat = m_surfaceFormats[0];

        // Get supported present modes for surface
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        if (presentModeCount != 0)
        {
            m_presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, m_presentModes.data());
        }

        // Pick present mode to use
        bool pickedPresentMode = false;
        for (const VkPresentModeKHR& presentMode : m_presentModes)
        {
            if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                m_presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                pickedPresentMode = true;
                break;
            }
        }
        if(!pickedPresentMode && !m_presentModes.empty()) m_presentMode = m_presentModes[0];

        // Get queue families
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        m_queueFamilies.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, m_queueFamilies.data());

        m_renderQueueIndex = 0;
        m_warpQueueIndex = 0;
        m_presentQueueIndex = 0;
        
        // Find suitable queue families
        for (int p = 0; p < queueFamilyCount; p++)
        {
            if (IsSuitableQueueFamilyForPresent(m_queueFamilies[p], p))
            {
                for (int w = 0; w < queueFamilyCount; w++)
                {
                    if (
                        IsSuitableQueueFamilyForWarp(m_queueFamilies[w], w) &&
                        (p != w || m_queueFamilies[p].queueCount > 1)
                    )
                    {
                        for (int r = 0; r < queueFamilyCount; r++)
                        {
                            if (
                                IsSuitableQueueFamilyForWarp(m_queueFamilies[r], r) &&
                                (p != r || m_queueFamilies[p].queueCount > 1) &&
                                (w != r || m_queueFamilies[w].queueCount > 1) &&
                                ((p != r || w != r) || m_queueFamilies[p].queueCount > 2)
                            )
                            {
                                m_presentQueueFamily = m_queueFamilies[p];
                                m_presentQueueFamilyIndex = p;

                                m_warpQueueFamily = m_queueFamilies[w];
                                m_warpQueueFamilyIndex = w;

                                m_renderQueueFamily = m_queueFamilies[r];
                                m_renderQueueFamilyIndex = r;


                                m_foundQueues = true;
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (!m_foundQueues)
        {
            for (int p = 0; p < queueFamilyCount; p++)
            {
                if (IsSuitableQueueFamilyForPresent(m_queueFamilies[p], p))
                {
                    for (int w = 0; w < queueFamilyCount; w++)
                    {
                        if (IsSuitableQueueFamilyForWarp(m_queueFamilies[w], w))
                        {
                            for (int r = 0; r < queueFamilyCount; r++)
                            {
                                if (
                                    IsSuitableQueueFamilyForWarp(m_queueFamilies[r], r) &&
                                    (p != r || m_queueFamilies[p].queueCount > 1) &&
                                    (w != r || m_queueFamilies[w].queueCount > 1)
                                )
                                {
                                    m_presentQueueFamily = m_queueFamilies[p];
                                    m_presentQueueFamilyIndex = p;

                                    m_warpQueueFamily = m_queueFamilies[w];
                                    m_warpQueueFamilyIndex = w;

                                    m_renderQueueFamily = m_queueFamilies[r];
                                    m_renderQueueFamilyIndex = r;

                                    m_foundQueues = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!m_foundQueues)
        {
            for (int p = 0; p < queueFamilyCount; p++)
            {
                if (IsSuitableQueueFamilyForPresent(m_queueFamilies[p], p))
                {
                    for (int w = 0; w < queueFamilyCount; w++)
                    {
                        if (IsSuitableQueueFamilyForWarp(m_queueFamilies[w], w))
                        {
                            for (int r = 0; r < queueFamilyCount; r++)
                            {
                                if (IsSuitableQueueFamilyForWarp(m_queueFamilies[r], r))
                                {
                                    m_presentQueueFamily = m_queueFamilies[p];
                                    m_presentQueueFamilyIndex = p;

                                    m_warpQueueFamily = m_queueFamilies[w];
                                    m_warpQueueFamilyIndex = w;

                                    m_renderQueueFamily = m_queueFamilies[r];
                                    m_renderQueueFamilyIndex = r;

                                    m_foundQueues = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (m_warpQueueFamilyIndex == m_renderQueueFamilyIndex) m_warpQueueIndex++;
        if (m_presentQueueIndex == m_renderQueueFamilyIndex) m_presentQueueIndex++;
        if (m_presentQueueIndex == m_warpQueueIndex) m_presentQueueIndex++;

        m_renderQueueIndex = std::min(m_renderQueueIndex, m_queueFamilies[m_renderQueueFamilyIndex].queueCount - 1);
        m_warpQueueIndex = std::min(m_warpQueueIndex, m_queueFamilies[m_warpQueueFamilyIndex].queueCount - 1);
        m_presentQueueIndex = std::min(m_presentQueueIndex, m_queueFamilies[m_presentQueueFamilyIndex].queueCount - 1);
    }

    const VkExtent2D GPU::GetSurfaceExtent(const int windowWidth, const int windowHeight)
    {
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device, m_surface, &m_surfaceCapabilities);
       
        if (m_surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            // Predefined surface extent
            return m_surfaceCapabilities.currentExtent;
        }
        
        // Surface extent decided by the swapchain targeting the surface
        VkExtent2D actualExtent =
        {
            static_cast<uint32_t>(windowWidth),
            static_cast<uint32_t>(windowHeight)
        };
        actualExtent.width = std::clamp(actualExtent.width, m_surfaceCapabilities.minImageExtent.width, m_surfaceCapabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, m_surfaceCapabilities.minImageExtent.height, m_surfaceCapabilities.maxImageExtent.height);
        return actualExtent;
    }

    const bool GPU::IsSuitableQueueFamilyForRender(const VkQueueFamilyProperties& queueFamily, const uint32_t queueFamilyIndex)
    {
        bool hasGraphics = queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT;
        return hasGraphics;
    }

    const bool GPU::IsSuitableQueueFamilyForWarp(const VkQueueFamilyProperties& queueFamily, const uint32_t queueFamilyIndex)
    {
        bool hasGraphics = queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT;
        return hasGraphics;
    }

    const bool GPU::IsSuitableQueueFamilyForPresent(const VkQueueFamilyProperties& queueFamily, const uint32_t queueFamilyIndex)
    {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_device, queueFamilyIndex, m_surface, &presentSupport);
        bool canPresent = presentSupport == VK_TRUE;

        return canPresent;
    }

    const bool GPU::IsSuitable()
    {
        return (
            m_missingDeviceExtensions.empty() &&
            !m_surfaceFormats.empty() &&
            !m_presentModes.empty() &&
            m_deviceFeatures.samplerAnisotropy &&
            m_foundQueues
        );
    }

    const bool GPU::IsDiscrete()
    {
        return m_deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    }
}

