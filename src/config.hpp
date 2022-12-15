#define VERSION_MAJOR 0
#define VERSION_MINOR 1

#include "vulkan/vulkan.h"

static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

//namespace VkGlobal
//{
//	static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
//
//	struct SwapChain
//	{
//		const VkSwapchainKHR chain;
//		const std::vector<VkImage> images;
//		const std::vector<VkImageView> imageViews;
//		const std::vector<VkFramebuffer> framebuffers;
//		const VkFormat imageFormat;
//		const VkExtent2D extent;
//	};
//
//	struct RenderContext
//	{
//		// Vulkan instance & devices
//		const VkInstance vk_;
//		const VkDevice device;
//		const VkPhysicalDevice physicalDevice;
//
//		// Queues
//		const VkQueue graphicsQueue;
//		const VkQueue presentQueue;
//
//		// Window & surface
//		const GLFWwindow* window;
//		const VkSurfaceKHR surface;
//
//		// Swapchain
//		SwapChain swapChain;
//
//		// Depth buffer/image
//		const VkImage depthImage_;
//		const VkDeviceMemory depthImageMemory;
//		const VkImageView depthImageView;
//
//		// Color buffer/image
//		const VkImage colorImage_;
//		const VkDeviceMemory colorImageMemory;
//		const VkImageView colorImageView;
//
//		// Render pipeline, resource descriptors & passes
//		const VkPipeline graphicsPipeline;
//		const VkPipelineLayout graphicsPipelineLayout;
//		const VkRenderPass renderPass;
//		const VkDescriptorSetLayout descriptorSetLayout;
//
//		// Global command pool
//		const VkCommandPool commandPool_;
//
//		// Render command buffers & synchronization
//		const std::vector<VkCommandBuffer> drawCommandBuffers_;
//		const std::vector<VkSemaphore> imageAvailableSemaphores_;
//		const std::vector<VkSemaphore> renderFinishedSemaphores_;
//		const std::vector<VkFence> inFlightFences_;
//		uint32_t currentFrame_ = 0;
//	};
//}
