#include "object.hpp"

#include <iostream>
#include <stdexcept>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#include "config.hpp"
#include "util.hpp"

namespace Rendering
{
    Object::Object(const Model& model, const VkPhysicalDevice& physicalDevice, const VkDevice& device, const std::vector<VkBuffer>& uniformBuffers, const VkDescriptorSetLayout& descriptorSetLayout, const VkSampler& textureSampler, const VkCommandPool& commandPool, const VkQueue& queue)
        : model_(model), device_(device)
    {
        std::cout << "Creating object based on '" << model.modelPath << "'" << std::endl;

        LoadModel();
        CreateTextureImage(physicalDevice, commandPool, queue);
        CreateTextureImageView();
        CreateDescriptorPool();
        CreateDescriptorSets(descriptorSetLayout, uniformBuffers, textureSampler);
        CreateVertexBuffer(physicalDevice, commandPool, queue);
        CreateIndexBuffer(physicalDevice, commandPool, queue);
    }

    Object::Object(const std::vector<Vertex> vertices, const std::vector<uint32_t> indices, const VkPhysicalDevice& physicalDevice, const VkDevice& device, const std::vector<VkBuffer>& uniformBuffers, const VkDescriptorSetLayout& descriptorSetLayout, const VkSampler& textureSampler, const VkCommandPool& commandPool, const VkQueue& queue)
        : device_(device), vertices_(vertices), indices_(indices)
    {
        CreateTextureImage(physicalDevice, commandPool, queue);
        CreateTextureImageView();
        CreateDescriptorPool();
        CreateDescriptorSets(descriptorSetLayout, uniformBuffers, textureSampler);
        CreateVertexBuffer(physicalDevice, commandPool, queue);
        CreateIndexBuffer(physicalDevice, commandPool, queue);
    }


    Object::~Object()
    {
        Destroy();
    }

    Object::Object(Object&& other) noexcept
        : device_(std::exchange(other.device_, {}))
        , model_(std::exchange(other.model_, {}))
        , descriptorPool_(std::exchange(other.descriptorPool_, {}))
        , descriptorSets_(std::exchange(other.descriptorSets_, {}))
        , vertices_(std::exchange(other.vertices_, {}))
        , indices_(std::exchange(other.indices_, {}))
        , vertexBuffer_(std::exchange(other.vertexBuffer_, {}))
        , vertexBufferMemory_(std::exchange(other.vertexBufferMemory_, {}))
        , indexBuffer_(std::exchange(other.indexBuffer_, {}))
        , indexBufferMemory_(std::exchange(other.indexBufferMemory_, {}))
        , mipLevels_(std::exchange(other.mipLevels_, {}))
        , textureImage_(std::exchange(other.textureImage_, {}))
        , textureImageMemory_(std::exchange(other.textureImageMemory_, {}))
        , textureImageView_(std::exchange(other.textureImageView_, {}))
    {
    }

    Object& Object::operator=(Object&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();

            device_ = std::exchange(other.device_, {});
            model_ = std::exchange(other.model_, {});
            descriptorPool_ = std::exchange(other.descriptorPool_, {});
            descriptorSets_ = std::exchange(other.descriptorSets_, {});
            vertices_ = std::exchange(other.vertices_, {});
            indices_ = std::exchange(other.indices_, {});
            vertexBuffer_ = std::exchange(other.vertexBuffer_, {});
            vertexBufferMemory_ = std::exchange(other.vertexBufferMemory_, {});
            indexBuffer_ = std::exchange(other.indexBuffer_, {});
            indexBufferMemory_ = std::exchange(other.indexBufferMemory_, {});
            mipLevels_ = std::exchange(other.mipLevels_, {});
            textureImage_ = std::exchange(other.textureImage_, {});
            textureImageMemory_ = std::exchange(other.textureImageMemory_, {});
            textureImageView_ = std::exchange(other.textureImageView_, {});
        }
        return *this;
    }

    void Object::Destroy()
    {
        if (device_ != VK_NULL_HANDLE)
        {
            std::cout << "Destroying object based on '" << model_.modelPath << "'" << std::endl;

            vkDestroyImageView(device_, textureImageView_, nullptr);
            vkDestroyImage(device_, textureImage_, nullptr);
            vkFreeMemory(device_, textureImageMemory_, nullptr);

            vkDestroyBuffer(device_, indexBuffer_, nullptr);
            vkFreeMemory(device_, indexBufferMemory_, nullptr);
            vkDestroyBuffer(device_, vertexBuffer_, nullptr);
            vkFreeMemory(device_, vertexBufferMemory_, nullptr);

            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        }
    }

    void Object::LoadModel()
    {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, model_.modelPath.c_str()))
        {
            throw std::runtime_error(warn + err);
        }

        vertices_.clear();
        indices_.clear();

        std::unordered_map<Rendering::Vertex, uint32_t> uniqueVertices{};

        for (const auto& shape : shapes)
        {
            for (const auto& index : shape.mesh.indices)
            {
                Rendering::Vertex vertex{};
                vertex.pos =
                {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };
                vertex.texCoord =
                {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };
                vertex.color = { 1.0f, 1.0f, 1.0f };

                if (uniqueVertices.count(vertex) == 0)
                {
                    uniqueVertices[vertex] = static_cast<uint32_t>(vertices_.size());
                    vertices_.push_back(vertex);
                }
                indices_.push_back(uniqueVertices[vertex]);
            }
        }
    }

    void Object::CreateTextureImage(const VkPhysicalDevice physicalDevice, const VkCommandPool commandPool, const VkQueue queue)
    {
        int texWidth, texHeight, texChannels;

        stbi_uc* pixels;
        if (model_.texturePath.empty())
        {
            pixels = stbi_load(DEFAULT_TEXTURE_PATH.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        }
        else
        {
            pixels = stbi_load(model_.texturePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        }
        VkDeviceSize imageSize = texWidth * texHeight * 4;

        if (!pixels)
        {
            throw std::runtime_error("failed to load texture image!");
        }

        std::cout << "Loaded texture image '" << model_.modelPath << "' with dimensions " << texWidth << 'x' << texHeight << std::endl;

        mipLevels_ = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;

        Util::CreateBuffer(physicalDevice, device_, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(device_, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device_, stagingBufferMemory);

        stbi_image_free(pixels);
        Util::CreateImage(physicalDevice, device_, texWidth, texHeight, mipLevels_, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage_, textureImageMemory_);

        Util::TransitionImageLayout(device_, commandPool, queue, textureImage_, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels_);
        Util::CopyBufferToImage(device_, commandPool, queue, stagingBuffer, textureImage_, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));

        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);

        //TransitionImageLayout(textureImage_, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels_);
        Util::GenerateMipmaps(physicalDevice, device_, commandPool, queue, textureImage_, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight, mipLevels_);
    }

    void Object::CreateTextureImageView()
    {
        textureImageView_ = Util::CreateImageView(device_, textureImage_, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels_);
    }

    void Object::CreateDescriptorPool()
    {
        std::array<VkDescriptorPoolSize, 2> poolSizes
        {
            VkDescriptorPoolSize
            {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
            },
            VkDescriptorPoolSize
            {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
            },
        };

        VkDescriptorPoolCreateInfo poolInfo
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes = poolSizes.data(),
        };

        if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }

    void Object::CreateDescriptorSets(const VkDescriptorSetLayout descriptorSetLayout, const std::vector<VkBuffer>& uniformBuffers, const VkSampler textureSampler)
    {
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);

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
                .buffer = uniformBuffers[i],
                .offset = 0,
                .range = sizeof(UniformBufferObject),
            };

            VkDescriptorImageInfo imageInfo
            {
                .sampler = textureSampler,
                .imageView = textureImageView_,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            std::array<VkWriteDescriptorSet, 2> descriptorWrites
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
                },
                VkWriteDescriptorSet
                {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSets_[i],
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

    void Object::CreateVertexBuffer(const VkPhysicalDevice physicalDevice, const VkCommandPool commandPool, const VkQueue queue)
    {
        VkDeviceSize bufferSize = sizeof(vertices_[0]) * vertices_.size();

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        Util::CreateBuffer(physicalDevice, device_, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(device_, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, vertices_.data(), (size_t)bufferSize);
        vkUnmapMemory(device_, stagingBufferMemory);

        Util::CreateBuffer(physicalDevice, device_, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer_, vertexBufferMemory_);
        Util::CopyBuffer(device_, commandPool, queue, stagingBuffer, vertexBuffer_, bufferSize);

        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);
    }

    void Object::CreateIndexBuffer(const VkPhysicalDevice physicalDevice, const VkCommandPool commandPool, const VkQueue queue)
    {
        VkDeviceSize bufferSize = sizeof(indices_[0]) * indices_.size();

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        Util::CreateBuffer(physicalDevice, device_, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(device_, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, indices_.data(), (size_t)bufferSize);
        vkUnmapMemory(device_, stagingBufferMemory);

        Util::CreateBuffer(physicalDevice, device_, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer_, indexBufferMemory_);
        Util::CopyBuffer(device_, commandPool, queue, stagingBuffer, indexBuffer_, bufferSize);

        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);
    }

    const VkVertexInputBindingDescription Vertex::GetBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription
        {
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        return bindingDescription;
    }

    const std::array<VkVertexInputAttributeDescription, 3> Vertex::GetAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions
        {
            VkVertexInputAttributeDescription
            {
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(Vertex, pos),
            },
            VkVertexInputAttributeDescription
            {
                .location = 1,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(Vertex, color),
            },
            VkVertexInputAttributeDescription
            {
                .location = 2,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(Vertex, texCoord),
            },
        };
        return attributeDescriptions;
    }
}
