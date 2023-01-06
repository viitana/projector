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

#include <imgui.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

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

	struct WarpUniformBufferObject
	{
		alignas(16) glm::mat4 view;
		alignas(16) glm::mat4 proj;
		alignas(16) glm::mat4 screen;
		alignas(4) float screenScale;
	};

	struct Player
	{
		glm::vec3 position;
		glm::vec2 rotation;
	};

	enum WarpMethod
	{
		None = 0,
		ClampEdge = 1,
		OverDraw = 2,
	};
	const std::vector<const char*> WarpMethodNames =
	{
		"None",
		"Clamp to edge",
		"Overdraw"
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
		void InitImGui();

		void UpdateUniformBuffer(bool render);
		void DrawFrame();
		void WarpPresent();
		void RecordDraw(VkCommandBuffer commandBuffer) const;
		void RecordWarp(VkCommandBuffer commandBuffer, uint32_t imageIndex) const;

		void RecreateSwapChain();
		void CleanupSwapChain();

		static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);

		// Instance & device
		VkInstance vk_ = VK_NULL_HANDLE;
		VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
		VkDevice device_ = VK_NULL_HANDLE;

		// Queues
		VkQueue graphicsQueue_ = VK_NULL_HANDLE;
		VkQueue warpQueue_ = VK_NULL_HANDLE;
		VkQueue presentQueue_ = VK_NULL_HANDLE;

		// Window & surface
		GLFWwindow* window_ = VK_NULL_HANDLE;
		VkSurfaceKHR surface_ = VK_NULL_HANDLE;

		// Swapchain
		VkSwapchainKHR swapChain_ = VK_NULL_HANDLE;
		std::vector<VkImage> swapChainImages_;
		std::vector<VkImageView> swapChainImageViews_;
		VkFormat swapChainImageFormat_;
		VkExtent2D swapChainExtent_;
		std::vector<VkFramebuffer> mainFramebuffers_;
		std::vector<VkFramebuffer> warpFramebuffers_;
		bool framebufferResized_ = false;

		// Depth buffer/image
		VkImage depthImage_ = VK_NULL_HANDLE;
		VkDeviceMemory depthImageMemory_ = VK_NULL_HANDLE;
		VkImageView depthImageView_ = VK_NULL_HANDLE;

		// MSAA / color buffer image
		VkImage colorImage_ = VK_NULL_HANDLE;
		VkDeviceMemory colorImageMemory_ = VK_NULL_HANDLE;
		VkImageView colorImageView_ = VK_NULL_HANDLE;

		// Warp MSAA / color buffer image
		VkImage warpColorImage_ = VK_NULL_HANDLE;
		VkDeviceMemory warpColorImageMemory_ = VK_NULL_HANDLE;
		VkImageView warpColorImageView_ = VK_NULL_HANDLE;

		std::vector<VkImage> resultImages_;
		std::vector<VkDeviceMemory> resultImagesMemory_;
		std::vector<VkImageView> resultImageViews_;
		VkExtent2D renderExtent_;

		// Render pipeline, resource descriptors & passes
		VkRenderPass renderPass_ = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
		VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;

		VkRenderPass warpRenderPass_ = VK_NULL_HANDLE;
		VkPipelineLayout warpPipelineLayout_ = VK_NULL_HANDLE;
		VkPipeline warpGraphicsPipeline_ = VK_NULL_HANDLE;

		// Global uniform buffer(s) & descriptor sets
		VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
		VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> descriptorSets_;
		std::vector<VkBuffer> uniformBuffers_;
		std::vector<VkDeviceMemory> uniformBuffersMemory_;
		std::vector<void*> uniformBuffersMapped_;

		VkDescriptorSetLayout warpDescriptorSetLayout_ = VK_NULL_HANDLE;
		VkSampler warpSampler_ = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> warpDescriptorSets_;
		VkBuffer warpUniformBuffer_ = VK_NULL_HANDLE;
		VkDeviceMemory warpUniformBufferMemory_ = VK_NULL_HANDLE;
		void* warpUniformBufferMapped_;

		// Command buffers & syncing
		VkCommandPool commandPool_ = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> drawCommandBuffers_;
		VkSemaphore renderReadySemaphore_; // VK_SEMAPHORE_TYPE_TIMELINE
		VkCommandBuffer warpCommandBuffer_ = VK_NULL_HANDLE;
		VkSemaphore imageAvailableSemaphore_ = VK_NULL_HANDLE;
		VkSemaphore warpFinishedSemaphore_ = VK_NULL_HANDLE;
		std::vector<VkFence> inFlightFences_;
		VkFence warpInFlightFence_ = VK_NULL_HANDLE;

		// UI Resources
		VkDescriptorPool imguiPool_ = VK_NULL_HANDLE;

		uint64_t renderFrame_ = 0;
		uint64_t warpFrame_ = 0;

		// MSAA
		VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;

		// Misc
		uint16_t objectIndex_ = 0;

		// GLFW scene model
		Scene::Model* scene_ = nullptr;

		// Input
		const Input::InputHandler* input_;

		// Settings
		bool doRender_ = true;
		bool doAsyncWarp_ = true;
		int renderFramerate_ = 60;
		int warpFramerate_ = 120;
		float fov_ = 75.0f;
		WarpMethod warpMethod_ = WarpMethod::ClampEdge;
		float overdrawDegreesChange_ = 5.0f;
		float overdrawDegrees_ = overdrawDegreesChange_;
		float clampOvershootPercent_ = 50.0f;
		float renderScale_ = 1.0f;

		// Player
		Player playerRender_ = {};
		Player playerWarp_ = {};
	};
}
