#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define STB_IMAGE_IMPLEMENTATION

#include "scene.hpp"

#include <iostream>
#include <fstream>
#include <sys/stat.h>

#include <ktx.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "config.hpp"
#include "util.hpp"

namespace Scene
{
    VkDescriptorSetLayout descriptorSetLayoutImage = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayoutUbo = VK_NULL_HANDLE;
    VkMemoryPropertyFlags memoryPropertyFlags = 0;
    uint32_t descriptorBindingFlags = 0;/* DescriptorBindingFlags::ImageBaseColor;*/

	void Texture::Destroy()
	{
		if (device)
		{
			vkDestroyImageView(device, view, nullptr);
			vkDestroyImage(device, image, nullptr);
			vkFreeMemory(device, deviceMemory, nullptr);
			vkDestroySampler(device, sampler, nullptr);
            std::cout << "Destroyed GPU texture '" << uri << "'" << std::endl;
		}
	}

	Texture::Texture(tinygltf::Image& gltfimage, const std::string path, const VkPhysicalDevice& physicalDevice, const VkDevice& d, const VkCommandPool& commandPool, const VkQueue& copyQueue, const VkDescriptorPool& descriptorSetPool)
		: device(d) , uri(gltfimage.uri)
    {
        // Check if image points to an external ktx file
        bool isKtx = false;
        if (gltfimage.uri.find_last_of(".") != std::string::npos)
        {
            if (gltfimage.uri.substr(gltfimage.uri.find_last_of(".") + 1) == "ktx")
            {
                isKtx = true;
            }
        }

        if (!isKtx)
        {
            unsigned char* buffer = nullptr;
            VkDeviceSize bufferSize = 0;

            width = static_cast<uint32_t>(gltfimage.width);
            height = static_cast<uint32_t>(gltfimage.height);
            mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

            bool deleteBuffer = false;

            // Most devices don't support RGB only on Vulkan so convert if necessary
            if (gltfimage.component == 3)
            {
                bufferSize = gltfimage.width * gltfimage.height * 4;
                buffer = new unsigned char[bufferSize];

                unsigned char* rgba = buffer;
                unsigned char* rgb = &gltfimage.image[0];
                for (size_t i = 0; i < gltfimage.width * gltfimage.height; ++i)
                {
                    for (int32_t j = 0; j < 3; ++j)
                    {
                        rgba[j] = rgb[j];
                    }
                    rgba += 4;
                    rgb += 3;
                }
                deleteBuffer = true;
            }
            else
            {
                buffer = &gltfimage.image[0];
                bufferSize = gltfimage.image.size();
            }

            VkBuffer stagingBuffer;
            VkDeviceMemory stagingBufferMemory;

            Util::CreateBuffer(physicalDevice, device, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

            uint8_t* data;
            VK_CHECK_RESULT(vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, (void**)&data));
            memcpy(data, buffer, static_cast<size_t>(bufferSize));
            vkUnmapMemory(device, stagingBufferMemory);

            Util::CreateImage(
                physicalDevice,
                device,
                width,
                height,
                mipLevels,
                VK_SAMPLE_COUNT_1_BIT,
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                image,
                deviceMemory
            );

            Util::TransitionImageLayout(device, commandPool, copyQueue, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
            Util::CopyBufferToImage(device, commandPool, copyQueue, stagingBuffer, image, width, height);

            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingBufferMemory, nullptr);

            //Util::TransitionImageLayout(device, commandPool, copyQueue, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels);
            Util::GenerateMipmaps(physicalDevice, device, commandPool, copyQueue, image, VK_FORMAT_R8G8B8A8_UNORM, width, height, mipLevels);

            if (deleteBuffer)
            {
                delete[] buffer;
            }
        }
        else
        {
            // Texture is stored in an external ktx file
            std::string filename = path + "/" + gltfimage.uri;
                
            ktxTexture* ktxTexture;

            ktxResult result = KTX_SUCCESS;

            struct stat buf;
            if (!stat(filename.c_str(), &buf))
            {
                throw std::runtime_error("Could not load texture from " + filename);
            }

            result = ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
            assert(result == KTX_SUCCESS);

            width = ktxTexture->baseWidth;
            height = ktxTexture->baseHeight;
            mipLevels = ktxTexture->numLevels;

            ktx_uint8_t* ktxTextureData = ktxTexture_GetData(ktxTexture);
            ktx_size_t ktxTextureSize = ktxTexture_GetDataSize(ktxTexture);

            VkBuffer stagingBuffer;
            VkDeviceMemory stagingBufferMemory;

            Util::CreateBuffer(physicalDevice, device, ktxTextureSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

            uint8_t* data;
            vkMapMemory(device, stagingBufferMemory, 0, ktxTextureSize, 0, (void**)&data);
            memcpy(data, ktxTextureData, ktxTextureSize);
            vkUnmapMemory(device, stagingBufferMemory);

            Util::CreateImage(physicalDevice, device, width, height, mipLevels, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, deviceMemory);

            Util::TransitionImageLayout(device, commandPool, copyQueue, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
            Util::CopyBufferToImage(device, commandPool, copyQueue, stagingBuffer, image, width, height);

            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingBufferMemory, nullptr);

            //Util::TransitionImageLayout(device, commandPool, copyQueue, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels);
            Util::GenerateMipmaps(physicalDevice, device, commandPool, copyQueue, image, VK_FORMAT_R8G8B8A8_UNORM, width,height, mipLevels);

            ktxTexture_Destroy(ktxTexture);
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);

        VkSamplerCreateInfo samplerInfo
        {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
            .mipLodBias = 0.0f, // Optional
            .anisotropyEnable = VK_FALSE ,
            .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_NEVER,
            .maxLod = (float)mipLevels,
            .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        };
        VK_CHECK_RESULT(vkCreateSampler(device, &samplerInfo, nullptr, &sampler));

        view = Util::CreateImageView(device, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);

        descriptor = VkDescriptorImageInfo
        {
            .sampler = sampler,
            .imageView = view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        std::cout << "Created glTF GPU texture '" << uri << "' [" << width << 'x' << height << "]" << std::endl;
    }

    Texture::Texture(const std::string path, const VkPhysicalDevice& physicalDevice, const VkDevice& d, const VkCommandPool& commandPool, const VkQueue& copyQueue, const VkDescriptorPool& descriptorSetPool)
        : device(d), uri(path)
    {
        int texWidth, texHeight, texChannels;

        stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels)
        {
            throw std::runtime_error("failed to load texture image '" + path + "'");
        }

        width = texWidth;
        height = texHeight;

        VkDeviceSize imageSize = texWidth * texHeight * 4;

        mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;

        Util::CreateBuffer(physicalDevice, device, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        uint8_t* data;
        VK_CHECK_RESULT(vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, (void**)&data));
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device, stagingBufferMemory);

        stbi_image_free(pixels);
        Util::CreateImage(physicalDevice, device, texWidth, texHeight, mipLevels, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, deviceMemory);

        Util::TransitionImageLayout(device, commandPool, copyQueue, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
        Util::CopyBufferToImage(device, commandPool, copyQueue, stagingBuffer, image, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        //TransitionImageLayout(textureImage_, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels_);
        Util::GenerateMipmaps(physicalDevice, device, commandPool, copyQueue, image, VK_FORMAT_R8G8B8A8_UNORM, texWidth, texHeight, mipLevels);

        view = Util::CreateImageView(device, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);

        VkSamplerCreateInfo samplerInfo
        {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
            .mipLodBias = 0.0f, // Optional
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
            .compareOp = VK_COMPARE_OP_NEVER,
            .maxLod = (float)mipLevels,
            .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        };
        VK_CHECK_RESULT(vkCreateSampler(device, &samplerInfo, nullptr, &sampler));

        descriptor = VkDescriptorImageInfo
        {
            .sampler = sampler,
            .imageView = view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        std::cout << "Created GPU texture '" << uri << "' [" << width << 'x' << height << "]" << std::endl;
    }

    Texture::~Texture()
    {
        Destroy();
    }

    Texture::Texture(Texture&& other) noexcept
        : device(std::exchange(other.device, {}))
        , uri(std::exchange(other.uri, {}))
        , image(std::exchange(other.image, {}))
        , deviceMemory(std::exchange(other.deviceMemory, {}))
        , view(std::exchange(other.view, {}))
        , width(std::exchange(other.width, 0))
        , height(std::exchange(other.height, 0))
        , mipLevels(std::exchange(other.mipLevels, 0))
        , descriptor(std::exchange(other.descriptor, {}))
        , sampler(std::exchange(other.sampler, {}))
    {
    }

    //Texture& Texture::operator=(Texture&& other) noexcept
    //{
    //    if (this != &other)
    //    {
    //        Destroy();

    //        device = std::exchange(other.device, {});
    //        image = std::exchange(other.image, {});
    //        imageLayout = std::exchange(other.imageLayout, {});
    //        deviceMemory = std::exchange(other.deviceMemory, {});
    //        view = std::exchange(other.view, {});
    //        width = std::exchange(other.width, 0);
    //        height = std::exchange(other.height, 0);
    //        mipLevels = std::exchange(other.mipLevels, 0);
    //        layerCount = std::exchange(other.layerCount, 0);
    //        descriptor = std::exchange(other.descriptor, {});
    //        sampler = std::exchange(other.sampler, {});
    //    }
    //    return *this;
    //}

    void Material::CreateDescriptorSets(const VkDescriptorPool descriptorPool, const VkDescriptorSetLayout descriptorSetLayout/*, uint32_t descriptorBindingFlags*/)
    {
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);

        VkDescriptorSetAllocateInfo allocInfo
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
            .pSetLayouts = layouts.data(),
        };

        descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);    
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()));

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            std::vector<VkDescriptorImageInfo> imageDescriptors{};
            std::vector<VkWriteDescriptorSet> writeDescriptorSets{};

            //if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor)
            {
                imageDescriptors.push_back(baseColorTexture->descriptor);
                writeDescriptorSets.push_back(VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSets[i],
                    .dstBinding = static_cast<uint32_t>(writeDescriptorSets.size()),
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &baseColorTexture->descriptor,
                });
            }
            //if (normalTexture && descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap)
            {
                imageDescriptors.push_back(normalTexture->descriptor);
                writeDescriptorSets.push_back(VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSets[i],
                    .dstBinding = static_cast<uint32_t>(writeDescriptorSets.size()),
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &normalTexture->descriptor,
                });
            }
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
        }
    }

    Mesh::Mesh(const VkPhysicalDevice& pd, const VkDevice& d, const glm::mat4 matrix)
        : physicalDevice(pd), device(d)
    {
        Util::CreateBuffer(
            physicalDevice,
            device,
            sizeof(uniformBlock),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            uniformBuffer.buffer,
            uniformBuffer.memory
        );
        VK_CHECK_RESULT(vkMapMemory(d, uniformBuffer.memory, 0, sizeof(uniformBlock), 0, &uniformBuffer.mapped));
        uniformBuffer.descriptor = VkDescriptorBufferInfo
        {
           .buffer = uniformBuffer.buffer,
           .offset = 0,
           .range = sizeof(uniformBlock)
        };
    };

    Mesh::~Mesh()
    {
        vkDestroyBuffer(device, uniformBuffer.buffer, nullptr);
        vkFreeMemory(device, uniformBuffer.memory, nullptr);
        for (auto primitive : primitives)
        {
            delete primitive;
        }
    }

    glm::mat4 Node::GetLocalMatrix()
    {
        return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), scale) * matrix;
    }

    glm::mat4 Node::GetMatrix()
    {
        glm::mat4 m = GetLocalMatrix();
        Node* p = parent;
        while (p)
        {
            m = p->GetLocalMatrix() * m;
            p = p->parent;
        }
        return m;
    }

    void Node::Update()
    {
        if (mesh)
        {
            mesh->uniformBlock.matrix = GetMatrix();
            memcpy(mesh->uniformBuffer.mapped, &mesh->uniformBlock, sizeof(mesh->uniformBlock));

            //if (skin)
            //{
            //    mesh->uniformBlock.matrix = m;
            //    // Update join matrices
            //    glm::mat4 inverseTransform = glm::inverse(m);
            //    for (size_t i = 0; i < skin->joints.size(); i++) {
            //        vkglTF::Node* jointNode = skin->joints[i];
            //        glm::mat4 jointMat = jointNode->getMatrix() * skin->inverseBindMatrices[i];
            //        jointMat = inverseTransform * jointMat;
            //        mesh->uniformBlock.jointMatrix[i] = jointMat;
            //    }
            //    mesh->uniformBlock.jointcount = (float)skin->joints.size();
            //    memcpy(mesh->uniformBuffer.mapped, &mesh->uniformBlock, sizeof(mesh->uniformBlock));
            //}
            //else
            //{
            //    memcpy(mesh->uniformBuffer.mapped, &m, sizeof(glm::mat4));
            //}
        }

        for (auto& child : children)
        {
            child->Update();
        }
    }

    Node::~Node()
    {
        if (mesh)
        {
            delete mesh;
        }
        for (auto& child : children)
        {
            delete child;
        }
    }

    VkVertexInputBindingDescription Vertex::vertexInputBindingDescription;
    std::vector<VkVertexInputAttributeDescription> Vertex::vertexInputAttributeDescriptions;
    VkPipelineVertexInputStateCreateInfo Vertex::pipelineVertexInputStateCreateInfo;

    VkVertexInputBindingDescription Vertex::GetInputBindingDescription(uint32_t binding)
    {
        return VkVertexInputBindingDescription({
            .binding = binding,
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        });
    }

    VkVertexInputAttributeDescription Vertex::GetInputAttributeDescription(uint32_t binding, uint32_t location, VertexComponent component)
    {
        switch (component)
        {
        case VertexComponent::Position:
            return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos) });
        case VertexComponent::Normal:
            return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) });
        case VertexComponent::UV:
            return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) });
        case VertexComponent::Color:
            return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color) });
        case VertexComponent::Tangent:
            return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent) });
        case VertexComponent::Joint0:
            return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, joint0) });
        case VertexComponent::Weight0:
            return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weight0) });
        default:
            return VkVertexInputAttributeDescription({});
        }
    }

    std::vector<VkVertexInputAttributeDescription> Vertex::GetInputAttributeDescriptions(uint32_t binding, const std::vector<VertexComponent> components)
    {
        std::vector<VkVertexInputAttributeDescription> result;
        uint32_t location = 0;
        for (VertexComponent component : components)
        {
            result.push_back(Vertex::GetInputAttributeDescription(binding, location, component));
            location++;
        }
        return result;
    }

    /** @brief Returns the default pipeline vertex input state create info structure for the requested vertex components */
    VkPipelineVertexInputStateCreateInfo* Vertex::GetPipelineVertexInputState(const std::vector<VertexComponent> components)
    {
        Vertex::vertexInputBindingDescription = Vertex::GetInputBindingDescription(0);
        Vertex::vertexInputAttributeDescriptions = Vertex::GetInputAttributeDescriptions(0, components);
        Vertex::pipelineVertexInputStateCreateInfo = VkPipelineVertexInputStateCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &Vertex::vertexInputBindingDescription,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(Vertex::vertexInputAttributeDescriptions.size()),
            .pVertexAttributeDescriptions = Vertex::vertexInputAttributeDescriptions.data(),
        };
        
        return &pipelineVertexInputStateCreateInfo;
    }

    Texture* Model::GetTexture(uint32_t index)
    {
        if (index < textures.size())
        {
            return &textures[index];
        }
        return nullptr;
    }

    Model::~Model()
    {
        vkDestroyBuffer(device_, vertices.buffer, nullptr);
        vkFreeMemory(device_, vertices.memory, nullptr);
        vkDestroyBuffer(device_, indices.buffer, nullptr);
        vkFreeMemory(device_, indices.memory, nullptr);

        textures.clear();
        delete emptyTexture_;

        for (auto node : nodes)
        {
            delete node;
        }
        //for (auto skin : skins)
        //{
        //    delete skin;
        //}

        if (descriptorSetLayoutUbo != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device_, descriptorSetLayoutUbo, nullptr);
            descriptorSetLayoutUbo = VK_NULL_HANDLE;
        }
        if (descriptorSetLayoutImage != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device_, descriptorSetLayoutImage, nullptr);
            descriptorSetLayoutImage = VK_NULL_HANDLE;
        }
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    }

    void Model::LoadNode(Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model, std::vector<uint32_t>& indexBuffer, std::vector<Vertex>& vertexBuffer, float globalscale)
    {
        Node* newNode = new Node
        {
            .parent = parent,
            .index = nodeIndex,
            .matrix = glm::mat4(1.0f),
            .name = node.name,
            //.skinIndex = node.skin,
        };

        // Generate local node matrix
        glm::vec3 translation = glm::vec3(0.0f);
        if (node.translation.size() == 3)
        {
            translation = glm::make_vec3(node.translation.data());
            newNode->translation = translation;
        }
        glm::mat4 rotation = glm::mat4(1.0f);
        if (node.rotation.size() == 4)
        {
            glm::quat q = glm::make_quat(node.rotation.data());
            newNode->rotation = glm::mat4(q);
        }
        glm::vec3 scale = glm::vec3(1.0f);
        if (node.scale.size() == 3)
        {
            scale = glm::make_vec3(node.scale.data());
            newNode->scale = scale;
        }
        if (node.matrix.size() == 16)
        {
            newNode->matrix = glm::make_mat4x4(node.matrix.data());
            if (globalscale != 1.0f)
            {
                //newNode->matrix = glm::scale(newNode->matrix, glm::vec3(globalscale));
            }
        };

        // Node with children
        if (node.children.size() > 0)
        {
            for (auto i = 0; i < node.children.size(); i++)
            {
                LoadNode(newNode, model.nodes[node.children[i]], node.children[i], model, indexBuffer, vertexBuffer, globalscale);
            }
        }

        // Node contains mesh data
        if (node.mesh > -1)
        {
            const tinygltf::Mesh mesh = model.meshes[node.mesh];
            Mesh* newMesh = new Mesh(physicalDevice_, device_, newNode->matrix);
            newMesh->name = mesh.name;
            for (size_t j = 0; j < mesh.primitives.size(); j++)
            {
                const tinygltf::Primitive& primitive = mesh.primitives[j];
                if (primitive.indices < 0)
                {
                    continue;
                }
                uint32_t indexStart = static_cast<uint32_t>(indexBuffer.size());
                uint32_t vertexStart = static_cast<uint32_t>(vertexBuffer.size());
                uint32_t indexCount = 0;
                uint32_t vertexCount = 0;
                glm::vec3 posMin{};
                glm::vec3 posMax{};
                bool hasSkin = false;
                // Vertices
                {
                    const float* bufferPos = nullptr;
                    const float* bufferNormals = nullptr;
                    const float* bufferTexCoords = nullptr;
                    const float* bufferColors = nullptr;
                    const float* bufferTangents = nullptr;
                    uint32_t numColorComponents;
                    const uint16_t* bufferJoints = nullptr;
                    const float* bufferWeights = nullptr;

                    // Position attribute is required
                    assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

                    const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
                    const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
                    bufferPos = reinterpret_cast<const float*>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));
                    posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
                    posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);

                    if (primitive.attributes.find("NORMAL") != primitive.attributes.end())
                    {
                        const tinygltf::Accessor& normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
                        const tinygltf::BufferView& normView = model.bufferViews[normAccessor.bufferView];
                        bufferNormals = reinterpret_cast<const float*>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
                    }

                    if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end())
                    {
                        const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
                        const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
                        bufferTexCoords = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
                    }

                    if (primitive.attributes.find("COLOR_0") != primitive.attributes.end())
                    {
                        const tinygltf::Accessor& colorAccessor = model.accessors[primitive.attributes.find("COLOR_0")->second];
                        const tinygltf::BufferView& colorView = model.bufferViews[colorAccessor.bufferView];
                        // Color buffer are either of type vec3 or vec4
                        numColorComponents = colorAccessor.type == TINYGLTF_PARAMETER_TYPE_FLOAT_VEC3 ? 3 : 4;
                        bufferColors = reinterpret_cast<const float*>(&(model.buffers[colorView.buffer].data[colorAccessor.byteOffset + colorView.byteOffset]));
                    }

                    if (primitive.attributes.find("TANGENT") != primitive.attributes.end())
                    {
                        const tinygltf::Accessor& tangentAccessor = model.accessors[primitive.attributes.find("TANGENT")->second];
                        const tinygltf::BufferView& tangentView = model.bufferViews[tangentAccessor.bufferView];
                        bufferTangents = reinterpret_cast<const float*>(&(model.buffers[tangentView.buffer].data[tangentAccessor.byteOffset + tangentView.byteOffset]));
                    }

                    // Skinning
                    // Joints
                    if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end())
                    {
                        const tinygltf::Accessor& jointAccessor = model.accessors[primitive.attributes.find("JOINTS_0")->second];
                        const tinygltf::BufferView& jointView = model.bufferViews[jointAccessor.bufferView];
                        bufferJoints = reinterpret_cast<const uint16_t*>(&(model.buffers[jointView.buffer].data[jointAccessor.byteOffset + jointView.byteOffset]));
                    }

                    if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end())
                    {
                        const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("WEIGHTS_0")->second];
                        const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
                        bufferWeights = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
                    }

                    hasSkin = (bufferJoints && bufferWeights);

                    vertexCount = static_cast<uint32_t>(posAccessor.count);

                    for (size_t v = 0; v < posAccessor.count; v++)
                    {
                        Vertex vert{};
                        vert.pos = glm::vec4(glm::make_vec3(&bufferPos[v * 3]), 1.0f);
                        vert.normal = glm::normalize(glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * 3]) : glm::vec3(0.0f)));
                        vert.uv = bufferTexCoords ? glm::make_vec2(&bufferTexCoords[v * 2]) : glm::vec3(0.0f);
                        if (bufferColors)
                        {
                            switch (numColorComponents)
                            {
                            case 3:
                                vert.color = glm::vec4(glm::make_vec3(&bufferColors[v * 3]), 1.0f);
                            case 4:
                                vert.color = glm::make_vec4(&bufferColors[v * 4]);
                            }
                        }
                        else
                        {
                            vert.color = glm::vec4(1.0f);
                        }
                        vert.tangent = bufferTangents ? glm::vec4(glm::make_vec4(&bufferTangents[v * 4])) : glm::vec4(0.0f);
                        vert.joint0 = hasSkin ? glm::vec4(glm::make_vec4(&bufferJoints[v * 4])) : glm::vec4(0.0f);
                        vert.weight0 = hasSkin ? glm::make_vec4(&bufferWeights[v * 4]) : glm::vec4(0.0f);
                        vertexBuffer.push_back(vert);
                    }
                }
                // Indices
                {
                    const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
                    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

                    indexCount = static_cast<uint32_t>(accessor.count);

                    switch (accessor.componentType)
                    {
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
                    {
                        uint32_t* buf = new uint32_t[accessor.count];
                        memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
                        for (size_t index = 0; index < accessor.count; index++) {
                            indexBuffer.push_back(buf[index] + vertexStart);
                        }
                        delete[] buf;
                        break;
                    }
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
                    {
                        uint16_t* buf = new uint16_t[accessor.count];
                        memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
                        for (size_t index = 0; index < accessor.count; index++) {
                            indexBuffer.push_back(buf[index] + vertexStart);
                        }
                        delete[] buf;
                        break;
                    }
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
                    {
                        uint8_t* buf = new uint8_t[accessor.count];
                        memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint8_t));
                        for (size_t index = 0; index < accessor.count; index++)
                        {
                            indexBuffer.push_back(buf[index] + vertexStart);
                        }
                        delete[] buf;
                        break;
                    }
                    default:
                        std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
                        return;
                    }
                }
                Primitive* newPrimitive = new Primitive
                {
                    .firstIndex = indexStart,
                    .indexCount = indexCount,
                    .firstVertex = vertexStart,
                    .vertexCount = vertexCount,
                    .material = primitive.material > -1 ? materials[primitive.material] : materials.back(),
                };
                newMesh->primitives.push_back(newPrimitive);
            }
            newNode->mesh = newMesh;
        }
        if (parent)
        {
            parent->children.push_back(newNode);
        }
        else
        {
            nodes.push_back(newNode);
        }
        linearNodes.push_back(newNode);
    }

    void Model::LoadImages(tinygltf::Model& gltfModel)
    {
        textures.reserve(gltfModel.images.size());
        for (tinygltf::Image& image : gltfModel.images)
        {
            //Texture texture(image, path, physicalDevice_, device_, commandPool_, transferQueue_, descriptorPool_);
            textures.emplace_back(Texture(image, path, physicalDevice_, device_, commandPool_, transferQueue_, descriptorPool_));
        }
        // Create an empty texture to be used for empty material images
        emptyTexture_ = new Texture("res/empty.bmp", physicalDevice_, device_, commandPool_, transferQueue_, descriptorPool_);
    }

    void Model::LoadMaterials(tinygltf::Model& gltfModel)
    {
        for (tinygltf::Material& mat : gltfModel.materials)
        {
            Material material(device_);
            if (mat.values.find("baseColorTexture") != mat.values.end())
            {
                material.baseColorTexture = GetTexture(gltfModel.textures[mat.values["baseColorTexture"].TextureIndex()].source);
            }
            //// Metallic roughness workflow
            //if (mat.values.find("metallicRoughnessTexture") != mat.values.end())
            //{
            //    material.metallicRoughnessTexture = getTexture(gltfModel.textures[mat.values["metallicRoughnessTexture"].TextureIndex()].source);
            //}
            if (mat.values.find("roughnessFactor") != mat.values.end())
            {
                material.roughnessFactor = static_cast<float>(mat.values["roughnessFactor"].Factor());
            }
            if (mat.values.find("metallicFactor") != mat.values.end())
            {
                material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());
            }
            if (mat.values.find("baseColorFactor") != mat.values.end())
            {
                material.baseColorFactor = glm::make_vec4(mat.values["baseColorFactor"].ColorFactor().data());
            }
            //if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end())
            //{
            //    material.normalTexture = getTexture(gltfModel.textures[mat.additionalValues["normalTexture"].TextureIndex()].source);
            //}
            //else
            {
                material.normalTexture = emptyTexture_;
            }
            //if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end())
            //{
            //    material.emissiveTexture = getTexture(gltfModel.textures[mat.additionalValues["emissiveTexture"].TextureIndex()].source);
            //}
            //if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end())
            //{
            //    material.occlusionTexture = getTexture(gltfModel.textures[mat.additionalValues["occlusionTexture"].TextureIndex()].source);
            //}
            if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end())
            {
                tinygltf::Parameter param = mat.additionalValues["alphaMode"];
                if (param.string_value == "BLEND")
                {
                    material.alphaMode = Material::ALPHAMODE_BLEND;
                }
                if (param.string_value == "MASK")
                {
                    material.alphaMode = Material::ALPHAMODE_MASK;
                }
            }
            if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end())
            {
                material.alphaCutoff = static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
            }

            materials.push_back(material);
        }
        // Push a default material at the end of the list for meshes with no material assigned
        materials.push_back(Material(device_));
    }

    Model::Model(const std::string filename, const VkPhysicalDevice& pd, const VkDevice& d, const VkCommandPool& commandPool, const VkQueue& transferQueue, const float scale)
        : physicalDevice_(pd)
        , device_(d)
        , transferQueue_(transferQueue)
        , commandPool_(commandPool)
        , scale_(scale)
    {
        tinygltf::Model gltfModel;
        tinygltf::TinyGLTF gltfContext;
        
        size_t pos = filename.find_last_of('/');
        path = filename.substr(0, pos);

        std::string error, warning;

        bool fileLoaded = gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename);

        std::vector<uint32_t> indexBuffer;
        std::vector<Vertex> vertexBuffer;

        if (fileLoaded)
        {
            LoadImages(gltfModel);
            LoadMaterials(gltfModel);
            const tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
            for (size_t i = 0; i < scene.nodes.size(); i++) {
                const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
                LoadNode(nullptr, node, scene.nodes[i], gltfModel, indexBuffer, vertexBuffer, scale);
            }
            //if (gltfModel.animations.size() > 0)
            //{
            //    LoadAnimations(gltfModel);
            //}
            //LoadSkins(gltfModel);

            for (auto node : linearNodes)
            {
                // Assign skins
                //if (node->skinIndex > -1)
                //{
                //    node->skin = skins[node->skinIndex];
                //}

                // Initial pose
                node->Update();
            }
        }
        else
        {
            throw std::runtime_error("Could not load glTF file \"" + filename + "\": " + error);
        }

        for (auto extension : gltfModel.extensionsUsed)
        {
            if (extension == "KHR_materials_pbrSpecularGlossiness")
            {
                std::cout << "Required extension: " << extension;
                metallicRoughnessWorkflow = false;
            }
        }

        size_t vertexBufferSize = vertexBuffer.size() * sizeof(Vertex);
        size_t indexBufferSize = indexBuffer.size() * sizeof(uint32_t);
        indices.count = static_cast<uint32_t>(indexBuffer.size());
        vertices.count = static_cast<uint32_t>(vertexBuffer.size());

        assert((vertexBufferSize > 0) && (indexBufferSize > 0));

        struct StagingBuffer {
            VkBuffer buffer;
            VkDeviceMemory memory;
        } vertexStaging, indexStaging;

        // Create staging buffers
        Util::CreateBuffer(physicalDevice_, device_, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexStaging.buffer, vertexStaging.memory);
        Util::CreateBuffer(physicalDevice_, device_, indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indexStaging.buffer, indexStaging.memory);

        uint8_t* data;

        VK_CHECK_RESULT(vkMapMemory(device_, vertexStaging.memory, 0, vertexBufferSize, 0, (void**)&data));
        memcpy(data, vertexBuffer.data(), (size_t)vertexBufferSize);
        vkUnmapMemory(device_, vertexStaging.memory);

        VK_CHECK_RESULT(vkMapMemory(device_, indexStaging.memory, 0, indexBufferSize, 0, (void**)&data));
        memcpy(data, indexBuffer.data(), (size_t)indexBufferSize);
        vkUnmapMemory(device_, indexStaging.memory);

        // Create device local buffers
        Util::CreateBuffer(physicalDevice_, device_, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertices.buffer, vertices.memory);
        Util::CreateBuffer(physicalDevice_, device_, indexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indices.buffer, indices.memory);
       
        // Copy from staging buffers
        Util::CopyBuffer(device_, commandPool, transferQueue_, vertexStaging.buffer, vertices.buffer, vertexBufferSize);
        Util::CopyBuffer(device_, commandPool, transferQueue_, indexStaging.buffer, indices.buffer, indexBufferSize);

        vkDestroyBuffer(device_, vertexStaging.buffer, nullptr);
        vkFreeMemory(device_, vertexStaging.memory, nullptr);
        vkDestroyBuffer(device_, indexStaging.buffer, nullptr);
        vkFreeMemory(device_, indexStaging.memory, nullptr);

        /*getSceneDimensions();*/

        // Setup descriptors
        uint32_t uboCount{ 0 };
        uint32_t imageCount{ 0 };
        for (auto node : linearNodes)
        {
            if (node->mesh)
            {
                uboCount++;
            }
        }
        for (auto material : materials)
        {
            if (material.baseColorTexture != nullptr)
            {
                imageCount++;
            }
        }
        std::vector<VkDescriptorPoolSize> poolSizes =
        {
            { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = uboCount },
        };
        if (imageCount > 0)
        {
            //if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor)
            {
                poolSizes.push_back({ .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = imageCount });
            }
            //if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap)
            {
                poolSizes.push_back({ .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = imageCount });
            }
        }

        VkDescriptorPoolCreateInfo descriptorPoolCI
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = MAX_FRAMES_IN_FLIGHT * (uboCount + imageCount),
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes = poolSizes.data(),
        };
        VK_CHECK_RESULT(vkCreateDescriptorPool(device_, &descriptorPoolCI, nullptr, &descriptorPool_));

        // Descriptors for per-node uniform buffers
        {
            // Layout is global, so only create if it hasn't already been created before
            if (descriptorSetLayoutUbo == VK_NULL_HANDLE)
            {
                std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
                {
                    {
                        .binding = 0,
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                    },
                };
                VkDescriptorSetLayoutCreateInfo descriptorLayoutCI
                {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .bindingCount = static_cast<uint32_t>(setLayoutBindings.size()),
                    .pBindings = setLayoutBindings.data(),
                };
                VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device_, &descriptorLayoutCI, nullptr, &descriptorSetLayoutUbo));
            }

            for (auto node : nodes)
            {
                PrepareNodeDescriptor(node, descriptorSetLayoutUbo);
            }
        }

        // Descriptors for per-material images
        {
            // Layout is global, so only create if it hasn't already been created before
            if (descriptorSetLayoutImage == VK_NULL_HANDLE)
            {
                std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
                //if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor)
                {
                    setLayoutBindings.push_back({
                        .binding = static_cast<uint32_t>(setLayoutBindings.size()),
                        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    });
                }
                //if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap)
                {
                    setLayoutBindings.push_back({
                        .binding = static_cast<uint32_t>(setLayoutBindings.size()),
                        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    });
                }
                VkDescriptorSetLayoutCreateInfo descriptorLayoutCI
                {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .bindingCount = static_cast<uint32_t>(setLayoutBindings.size()),
                    .pBindings = setLayoutBindings.data(),
                };

                VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device_, &descriptorLayoutCI, nullptr, &descriptorSetLayoutImage));
            }
            for (auto& material : materials)
            {
                if (material.baseColorTexture != nullptr)
                {
                    material.CreateDescriptorSets(descriptorPool_, descriptorSetLayoutImage/*, descriptorBindingFlags*/);
                }
            }
        }
    }

    void Model::DrawNode(Node* node, VkCommandBuffer commandBuffer, uint32_t renderFlags, VkPipelineLayout pipelineLayout, uint32_t bindImageSet)
    {
        const VkDeviceSize offsets[1] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
        vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        if (node->mesh)
        {
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout,
                1,
                1,
                &node->mesh->uniformBuffer.descriptorSet,
                0,
                nullptr
            );  

            for (Primitive* primitive : node->mesh->primitives)
            {
                bool skip = false;
                const Material& material = primitive->material;

                //if (renderFlags & RenderFlags::RenderOpaqueNodes)
                //{
                //    skip = (material.alphaMode != Material::ALPHAMODE_OPAQUE);
                //}
                //if (renderFlags & RenderFlags::RenderAlphaMaskedNodes)
                //{
                //    skip = (material.alphaMode != Material::ALPHAMODE_MASK);
                //}
                //if (renderFlags & RenderFlags::RenderAlphaBlendedNodes)
                //{
                //    skip = (material.alphaMode != Material::ALPHAMODE_BLEND);
                //}
                if (!skip)
                {
                    //if (renderFlags & RenderFlags::BindImages)
                    {
                        vkCmdBindDescriptorSets(
                            commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout,
                            2,
                            1,
                            &material.descriptorSets[0],
                            0,
                            nullptr
                        );
                    }
                    vkCmdDrawIndexed(commandBuffer, primitive->indexCount, 1, primitive->firstIndex, 0, 0);
                }
            }
        }
        for (auto& child : node->children)
        {
            DrawNode(child, commandBuffer, renderFlags, pipelineLayout, renderFlags);
        }
    }

    void Model::Draw(VkCommandBuffer commandBuffer, uint32_t renderFlags, VkPipelineLayout pipelineLayout, uint32_t bindImageSet)
    {
        if (!buffersBound)
        {
            const VkDeviceSize offsets[1] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
            vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            buffersBound = true;
        }
        for (auto& node : nodes)
        {
            DrawNode(node, commandBuffer, renderFlags, pipelineLayout, bindImageSet);
        }
    }

    void Model::PrepareNodeDescriptor(Node* node, VkDescriptorSetLayout descriptorSetLayout) {
        if (node->mesh)
        {
            VkDescriptorSetAllocateInfo descriptorSetAllocInfo
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptorPool_,
                .descriptorSetCount = 1,
                .pSetLayouts = &descriptorSetLayout,
            };
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device_, &descriptorSetAllocInfo, &node->mesh->uniformBuffer.descriptorSet));

            VkWriteDescriptorSet writeDescriptorSet
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = node->mesh->uniformBuffer.descriptorSet,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &node->mesh->uniformBuffer.descriptor,
            };
            vkUpdateDescriptorSets(device_, 1, &writeDescriptorSet, 0, nullptr);
        }
        for (auto& child : node->children)
        {
            PrepareNodeDescriptor(child, descriptorSetLayout);
        }
    }
}
