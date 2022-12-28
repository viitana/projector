#include "util.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

#include <glm/gtx/projection.hpp>

namespace Util
{
    const std::vector<char> ReadFile(const std::string& filename)
	{
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        
        if (!file.is_open())
        {
            throw std::runtime_error("failed to open file!");
        }

        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();

        return buffer;
	}

    void ListDirectoryFiles(const std::string& directory)
    {
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            std::cout << entry.path() << std::endl;
        }
    }

    const uint32_t FindMemoryType(const VkPhysicalDevice& physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }
        throw std::runtime_error("no suitable memory on physical device!");
    }

    void CreateBuffer(const VkPhysicalDevice& physicalDevice, const VkDevice& device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory)
    {
        VkBufferCreateInfo bufferInfo
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        VK_CHECK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer));

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo
        {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = FindMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties),
        };

        VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory))
        VK_CHECK_RESULT(vkBindBufferMemory(device, buffer, bufferMemory, 0));
    }

    void CopyBuffer(const VkDevice& device, const VkCommandPool& commandPool, const VkQueue& queue, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
    {
        VkCommandBuffer commandBuffer = BeginSingleTimeCommands(device, commandPool);

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        EndSingleTimeCommands(device, commandPool, queue, commandBuffer);
    }

    void CreateImage(const VkPhysicalDevice& physicalDevice, const VkDevice& device, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory)
    {
        VkImageCreateInfo imageInfo
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = VkExtent3D
            {
                .width = width,
                .height = height,
                .depth = 1,
            },
            .mipLevels = mipLevels,
            .arrayLayers = 1,
            .samples = numSamples,
            .tiling = tiling,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create image!");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo
        {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = FindMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties),
        };

        if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate image memory!");
        }

        VK_CHECK_RESULT(vkBindImageMemory(device, image, imageMemory, 0));
    }

    const VkImageView CreateImageView(const VkDevice& device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
    {
        VkImageViewCreateInfo viewInfo
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .subresourceRange = VkImageSubresourceRange
            {
                .aspectMask = aspectFlags,
                .baseMipLevel = 0,
                .levelCount = mipLevels,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };

        VkImageView imageView;
        if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create texture image view!");
        }

        return imageView;
    }
    
    void TransitionImageLayout(const VkDevice& device, const VkCommandPool& commandPool, const VkQueue& queue, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, VkCommandBuffer commandBuffer)
    {
        VkAccessFlags sourceAccessMask;
        VkAccessFlags destinationAccessMask;
        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            sourceAccessMask = 0;
            destinationAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            sourceAccessMask = 0;
            destinationAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            sourceAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            destinationAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            sourceAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            destinationAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            sourceAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            destinationAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        {
            sourceAccessMask = 0;
            destinationAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else
        {
            throw std::invalid_argument("unsupported layout transition!");
        }

        VkImageMemoryBarrier barrier
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = sourceAccessMask,
            .dstAccessMask = destinationAccessMask,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = VkImageSubresourceRange
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = mipLevels,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };

        bool createdCommandBuffer = false;
        if (!commandBuffer)
        {
            commandBuffer = BeginSingleTimeCommands(device, commandPool);
            createdCommandBuffer = true;
        }

        vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage, destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
        if (createdCommandBuffer) EndSingleTimeCommands(device, commandPool, queue, commandBuffer);
    }
    
    void CopyBufferToImage(const VkDevice& device, const VkCommandPool& commandPool, const VkQueue& queue, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
    {
        VkBufferImageCopy region
        {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = VkImageSubresourceLayers
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { width, height, 1 },
        };

        VkCommandBuffer commandBuffer = BeginSingleTimeCommands(device, commandPool);
        vkCmdCopyBufferToImage(
            commandBuffer,
            buffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region
        );
        EndSingleTimeCommands(device, commandPool, queue, commandBuffer);
    }

    void GenerateMipmaps(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkCommandPool& commandPool, const VkQueue& queue, VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels)
    {
        // Check if image format supports linear blitting
        VkFormatProperties formatProperties;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat, &formatProperties);
        if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        {
            throw std::runtime_error("texture image format does not support linear blitting!");
        }

        VkImageMemoryBarrier barrier
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_NONE, // Looped over below
            .dstAccessMask = VK_ACCESS_NONE, // Looped over below
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, // Looped over below
            .newLayout = VK_IMAGE_LAYOUT_UNDEFINED, // Looped over below
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = VkImageSubresourceRange
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0, // Looped over below
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };

        int32_t mipWidth = texWidth;
        int32_t mipHeight = texHeight;

        VkCommandBuffer commandBuffer = BeginSingleTimeCommands(device, commandPool);
        for (uint32_t i = 1; i < mipLevels; i++)
        {
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );

            VkImageBlit blit
            {
                .srcSubresource =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = i - 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .srcOffsets =
                {
                    { 0, 0, 0 },
                    { mipWidth, mipHeight, 1 },
                },
                .dstSubresource =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = i,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .dstOffsets =
                {
                    { 0, 0, 0 },
                    { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 },
                },
            };

            vkCmdBlitImage(commandBuffer,
                image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit,
                VK_FILTER_LINEAR
            );

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );

            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }

        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        EndSingleTimeCommands(device, commandPool, queue, commandBuffer);
    }

    void CopyImageToImage(const VkDevice& device, const VkCommandPool& commandPool, const VkQueue& queue, VkImage src, VkImageLayout srcLayout, VkImage dst, VkImageLayout dstLayout, uint32_t width, uint32_t height, VkCommandBuffer commandBuffer)
    {
        const VkImageCopy region
        {
            .srcSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .srcOffset = { 0, 0, 0 }, //{ mipWidth, mipHeight, 1 },*/
            .dstSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .dstOffset = { 0, 0, 0 }, //{ mipWidth, mipHeight, 1 },*/
            .extent = { width, height, 1 }, //{ mipWidth, mipHeight, 1 },*/
        };

        bool createdCommandBuffer = false;
        if (!commandBuffer)
        {
            commandBuffer = BeginSingleTimeCommands(device, commandPool);
            createdCommandBuffer = true;
        }

        vkCmdCopyImage(commandBuffer, src, srcLayout, dst, dstLayout, 1, &region);
        if (createdCommandBuffer) EndSingleTimeCommands(device, commandPool, queue, commandBuffer);
    }

    const VkCommandBuffer BeginSingleTimeCommands(const VkDevice& device, const VkCommandPool& commandPool)
    {
        VkCommandBufferAllocateInfo allocInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer commandBuffer;
        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

        VkCommandBufferBeginInfo beginInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };

        VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));

        return commandBuffer;
    }

    void EndSingleTimeCommands(const VkDevice& device, const VkCommandPool& commandPool, const VkQueue& queue, const VkCommandBuffer commandBuffer)
    {
        VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

        VkSubmitInfo submitInfo
        {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer,
        };

        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK_RESULT(vkQueueWaitIdle(queue));

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    //glm::quat TwistDecompose(glm::quat rotation, glm::vec3 direction)
    //{
    //    //glm::vec3 ra(rotation.x, rotation.y, rotation.z); // rotation axis
    //    //glm::vec3 p = glm::proj(ra, direction); 
    //    //glm::quat twist = glm::quat(p.x, p.y, p.z, rotation.w);
    //    //return glm::normalize(twist);
    //    return glm::quat();
    //}
}
