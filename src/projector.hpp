#pragma once

#include <array>
#include <iostream>
#include <optional>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <chrono>
#include <thread>
#include <fstream>
#include <unordered_map>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include "config.hpp"
#include "scene.hpp"
#include "object.hpp"
#include "util.hpp"

namespace Projector
{
	const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
	const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	struct QueueFamilyIndices
	{
		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> presentFamily;

		const bool IsComplete() const;
	};

	struct SwapChainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

	class Projector
	{
	public:
		Projector();
		~Projector();

		void Run();

		void LoadObj(const Rendering::Model model);

		void Resized();
	private:
		const bool CheckValidationLayerSupport() const;
		const VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
		const VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
		const VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
		const SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const;
		const QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
		const bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;
		const bool IsDeviceSuitable(VkPhysicalDevice device) const;
		const VkShaderModule CreateShaderModule(const std::vector<char>& code) const;
		const VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const;
		const VkFormat FindDepthFormat() const;
		const bool HasStencilComponent(VkFormat format) const;
		const VkSampleCountFlagBits GetMaxUsableSampleCount() const;

		void LoadScene(const std::string& filename);

		void CreateInstance();
		void CreateSurface();
		void PickPhysicalDevice();
		void CreateLogicalDevice();
		void CreateSwapChain();
		void CreateImageViews();
		void CreateRenderPass();
		void CreateDescriptorSetLayout();
		void CreateGraphicsPipeline();
		void CreateCommandPool();
		void CreateColorResources();
		void CreateDepthResources();
		void CreateFramebuffers();
		void CreateTextureSampler();
		void CreateUniformBuffers();
		void CreateCommandBuffers();
		void CreateSyncObjects();

		void UpdateUniformBuffer(uint32_t currentImage);
		void DrawFrame();
		void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) const;

		void RecreateSwapChain();
		void CleanupSwapChain();

		static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);

		// Instance & device
		VkInstance vk_;
		VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
		VkDevice device_ = VK_NULL_HANDLE;

		// Queues
		VkQueue graphicsQueue_;
		VkQueue presentQueue_;

		// Window & surface
		GLFWwindow* window_;
		VkSurfaceKHR surface_;

		// Swapchain
		VkSwapchainKHR swapChain_;
		std::vector<VkImage> swapChainImages_;
		std::vector<VkImageView> swapChainImageViews_;
		VkFormat swapChainImageFormat_;
		VkExtent2D swapChainExtent_;
		std::vector<VkFramebuffer> swapChainFramebuffers_;
		bool framebufferResized_ = false;

		// Depth buffer/image
		VkImage depthImage_;
		VkDeviceMemory depthImageMemory_;
		VkImageView depthImageView_;

		// MSAA / color buffer(image
		VkImage colorImage_;
		VkDeviceMemory colorImageMemory_;
		VkImageView colorImageView_;

		// Render pipeline, resource descriptors & passes
		VkRenderPass renderPass_;
		VkDescriptorSetLayout descriptorSetLayout_;
		VkPipelineLayout pipelineLayout_;
		VkPipeline graphicsPipeline_;

		// Command buffers & syncing
		VkCommandPool commandPool_;
		std::vector<VkCommandBuffer> commandBuffers_;
		std::vector<VkSemaphore> imageAvailableSemaphores_;
		std::vector<VkSemaphore> renderFinishedSemaphores_;
		std::vector<VkFence> inFlightFences_;
		uint32_t currentFrame_ = 0;

		// Global uniform buffer(s)
		std::vector<VkBuffer> uniformBuffers_;
		std::vector<VkDeviceMemory> uniformBuffersMemory_;
		std::vector<void*> uniformBuffersMapped_;

		// Vertex data
		std::vector<Rendering::Vertex> vertices_;
		std::vector<uint32_t> indices_;

		// Texture data
		uint32_t mipLevels_;
		VkSampler textureSampler_;

		// Objects
		std::vector<Rendering::Object> objects_;

		// MSAA
		VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;

		// Misc
		uint16_t objectIndex_ = 0;

		std::optional<Scene::Scene> scene_;
	};
}
