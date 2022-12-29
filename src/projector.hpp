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
#include "input.hpp"
#include "scene.hpp"
#include "util.hpp"

namespace Projector
{
	const std::vector<const char*> validationLayers =
	{
#ifdef _DEBUG
		"VK_LAYER_KHRONOS_validation",
#endif
	};
	const std::vector<const char*> deviceExtensions =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	};

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

	struct UniformBufferObject
	{
		alignas(16) glm::mat4 view;
		alignas(16) glm::mat4 proj;
	};

	class Projector
	{
	public:
		Projector();
		~Projector();

		void Run();
		void Resized();
	private:
		const bool CheckValidationLayerSupport() const;
		const VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
		const VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
		const VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
		const SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const;
		const QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
		void ListDeviceDetails(VkPhysicalDevice device) const;
		const bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;
		const bool IsDeviceSuitable(VkPhysicalDevice device) const;
		const VkShaderModule CreateShaderModule(const std::vector<char>& code) const;
		const VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const;
		const VkFormat FindDepthFormat() const;
		const bool HasStencilComponent(VkFormat format) const;
		const VkSampleCountFlagBits GetMaxUsableSampleCount(VkPhysicalDevice device) const;

		void CreateInstance();
		void CreateSurface();
		void PickPhysicalDevice();
		void CreateLogicalDevice();
		void CreateSwapChain();
		void CreateImageViews();
		void CreateCommandPool();
		void CreateRenderPass();
		void CreateDescriptorSetLayout();
		void CreateDescriptorPool();
		void CreateGraphicsPipeline();
		void CreateRenderImageResources();
		void CreateWarpSampler();
		void CreateUniformBuffers();
		void CreateDescriptorSets();
		void CreateFramebuffers();
		void CreateCommandBuffers();
		void CreateSyncObjects();

		void UpdateUniformBuffer(uint32_t currentImage);
		void DrawFrame();
		void RecordDraw(VkCommandBuffer commandBuffer) const;
		void RecordWarp(VkCommandBuffer commandBuffer, uint32_t imageIndex) const;

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
		std::vector<VkFramebuffer> mainFramebuffers_;
		std::vector<VkFramebuffer> warpFramebuffers_;
		bool framebufferResized_ = false;

		// Depth buffer/image
		VkImage depthImage_;
		VkDeviceMemory depthImageMemory_;
		VkImageView depthImageView_;

		// MSAA / color buffer image
		VkImage colorImage_;
		VkDeviceMemory colorImageMemory_;
		VkImageView colorImageView_;

		// Warp MSAA / color buffer image
		VkImage warpColorImage_;
		VkDeviceMemory warpColorImageMemory_;
		VkImageView warpColorImageView_;

		VkImage resultImage_;
		VkDeviceMemory resultImageMemory_;
		VkImageView resultImageView_;

		// Render pipeline, resource descriptors & passes
		VkRenderPass renderPass_;
		VkPipelineLayout pipelineLayout_;
		VkPipeline graphicsPipeline_;

		VkRenderPass warpRenderPass_;
		VkPipelineLayout warpPipelineLayout_;
		VkPipeline warpGraphicsPipeline_;

		// Global uniform buffer(s) & descriptor sets
		VkDescriptorSetLayout descriptorSetLayout_;
		VkDescriptorPool descriptorPool_;
		std::vector<VkDescriptorSet> descriptorSets_;
		std::vector<VkBuffer> uniformBuffers_;
		std::vector<VkDeviceMemory> uniformBuffersMemory_;
		std::vector<void*> uniformBuffersMapped_;

		VkDescriptorSetLayout warpDescriptorSetLayout_;
		VkSampler warpSampler_;
		std::vector<VkDescriptorSet> warpDescriptorSets_;
		std::vector<VkBuffer> warpUniformBuffers_;
		std::vector<VkDeviceMemory> warpUniformBuffersMemory_;
		std::vector<void*> warpUniformBuffersMapped_;

		// Command buffers & syncing
		VkCommandPool commandPool_;
		std::vector<VkCommandBuffer> drawCommandBuffers_;
		std::vector<VkCommandBuffer> warpCommandBuffers_;
		std::vector<VkSemaphore> imageAvailableSemaphores_;
		std::vector<VkSemaphore> renderFinishedSemaphores_;
		std::vector<VkSemaphore> warpFinishedSemaphores_;
		std::vector<VkFence> inFlightFences_;
		uint32_t currentFrame_ = 0;

		// MSAA
		VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;

		// Misc
		uint16_t objectIndex_ = 0;

		// GLFW scene model
		Scene::Model* scene_;

		// Input
		const Input::InputHandler* input_;

		// Player
		struct Player {
			glm::quat rotation;
			glm::vec3 position;
		} player_  = {
			glm::quat(glm::vec3(0.0, 0.0, 0.0)),
			glm::vec3()
		};
	};
}
