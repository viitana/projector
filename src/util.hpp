#pragma once

#include <assert.h>
#include <vector>
#include <string>

#include "vulkan/vulkan.h"

#define VK_CHECK_RESULT(f)																				\
{																										\
	VkResult res = (f);																					\
	if (res != VK_SUCCESS)																				\
	{																									\
		std::cout << "Fatal : VkResult is \"" << res << "\" in " << __FILE__ << " at line " << __LINE__ << "\n"; \
		assert(res == VK_SUCCESS);																		\
	}																									\
}

namespace Util
{
	const std::vector<char> ReadFile(const std::string& filename);
	void ListDirectoryFiles(const std::string& directory);

	const uint32_t FindMemoryType(const VkPhysicalDevice& physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

	void CreateBuffer(const VkPhysicalDevice& physicalDevice, const VkDevice& device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
	void CopyBuffer(const VkDevice& device, const VkCommandPool& commandPool, const VkQueue& queue, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
	void CreateImage(const VkPhysicalDevice& physicalDevice, const VkDevice& device, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
	const VkImageView CreateImageView(const VkDevice& device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);
	void TransitionImageLayout(const VkDevice& device, const VkCommandPool& commandPool, const VkQueue& queue, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);
	void CopyBufferToImage(const VkDevice& device, const VkCommandPool& commandPool, const VkQueue& queue, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
	void GenerateMipmaps(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkCommandPool& commandPool, const VkQueue& queue, VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
	
	const VkCommandBuffer BeginSingleTimeCommands(const VkDevice& device, const VkCommandPool& commandPool);	
	void EndSingleTimeCommands(const VkDevice& device, const VkCommandPool& commandPool, const VkQueue& queue, const VkCommandBuffer commandBuffer);
}
