#pragma once

#include <array>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <chrono>
#include <thread>
#include <fstream>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#include "config.hpp"
#include "util.hpp"

namespace Projector
{
	struct Vertex
	{
		glm::vec3 pos;
		glm::vec3 color;
		glm::vec2 texCoord;

		const static VkVertexInputBindingDescription GetBindingDescription();
		const static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions();
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
		alignas(16) glm::mat4 model;
		alignas(16) glm::mat4 view;
		alignas(16) glm::mat4 proj;
	};

	class Projector
	{
	public:
		Projector(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
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
		const bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;
		const bool IsDeviceSuitable(VkPhysicalDevice device) const;
		const VkShaderModule CreateShaderModule(const std::vector<char>& code) const;
		const uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
		void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) const;
		void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) const;
		void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) const;
		void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) const;
		void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
		const VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const;
		const VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const;
		const VkFormat Projector::FindDepthFormat() const;
		const bool HasStencilComponent(VkFormat format) const;

		const VkCommandBuffer BeginSingleTimeCommands() const;
		void EndSingleTimeCommands(const VkCommandBuffer commandBuffer) const;

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
		void CreateDepthResources();
		void CreateFramebuffers();
		void CreateTextureImage();
		void CreateTextureImageView();
		void CreateTextureSampler();
		void CreateVertexBuffer();
		void CreateIndexBuffer();
		void CreateUniformBuffers();
		void CreateDescriptorPool();
		void CreateDescriptorSets();
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

		// Render pipeline, resource descriptors & passes
		VkRenderPass renderPass_;
		VkDescriptorSetLayout descriptorSetLayout_;
		VkDescriptorPool descriptorPool_;
		std::vector<VkDescriptorSet> descriptorSets_;
		VkPipelineLayout pipelineLayout_;
		VkPipeline graphicsPipeline_;

		// Command buffers & syncing
		VkCommandPool commandPool_;
		std::vector<VkCommandBuffer> commandBuffers_;
		std::vector<VkSemaphore> imageAvailableSemaphores_;
		std::vector<VkSemaphore> renderFinishedSemaphores_;
		std::vector<VkFence> inFlightFences_;
		uint32_t currentFrame_ = 0;

		// Vertex, index & uniform buffer
		VkBuffer vertexBuffer_;
		VkDeviceMemory vertexBufferMemory_;
		VkBuffer indexBuffer_;
		VkDeviceMemory indexBufferMemory_;
		std::vector<VkBuffer> uniformBuffers_;
		std::vector<VkDeviceMemory> uniformBuffersMemory_;
		std::vector<void*> uniformBuffersMapped_;

		// Vertex data
		const std::vector<Vertex>& vertices_;
		const std::vector<uint32_t> indices_;

		// Texture data
		VkImage textureImage_;
		VkDeviceMemory textureImageMemory_;
		VkImageView textureImageView_;
		VkSampler textureSampler_;
	};
}
