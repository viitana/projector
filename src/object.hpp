#pragma once

#include <array>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

namespace Rendering
{
	struct Vertex
	{
		glm::vec3 pos;
		glm::vec3 color;
		glm::vec2 texCoord;

		const static VkVertexInputBindingDescription GetBindingDescription();
		const static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions();

		bool operator==(const Vertex& other) const
		{
			return pos == other.pos && color == other.color && texCoord == other.texCoord;
		}
	};

	struct Model
	{
		std::string modelPath;
		std::string texturePath;
	};

	const std::vector<Model> MODELS =
	{
		{
			.modelPath = "../res/cruiser.obj",
			.texturePath = "../res/cruiser.bmp",
		},
		{
			.modelPath = "../res/viking_room.obj",
			.texturePath = "../res/viking_room.png",
		},
		{
			.modelPath = "../res/f16.obj",
			.texturePath = "../res/F16s.bmp",
		},
	};

	class Object
	{
	public:
		Object(const Model& model, const VkPhysicalDevice& physicalDevice, const VkDevice& device, const std::vector<VkBuffer>& uniformBuffers, const VkDescriptorSetLayout& descriptorSetLayout, const VkSampler& textureSampler, const VkCommandPool& commandPool, const VkQueue& queue);
		~Object();

		Object& operator=(Object&& other)
		{

			return *this;
		}

		const VkBuffer& GetVertexBuffer() const { return vertexBuffer_; }
		const VkBuffer& GetIndexBuffer() const { return indexBuffer_; }
		const VkDescriptorSet* GetDescriptorSet(int frame) const { return &descriptorSets_[frame]; }
		const uint32_t GetIndicesCount() const { return indices_.size(); }

	private:
		void LoadModel();
		void CreateTextureImage();
		void CreateTextureImageView();
		void CreateDescriptorPool();
		void CreateDescriptorSets();
		void CreateVertexBuffer();
		void CreateIndexBuffer();

		// Base model info
		const Model model_;

		// Device
		const VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
		const VkDevice device_ = VK_NULL_HANDLE;

		// Command pool & queue
		const VkCommandPool commandPool_;
		const VkQueue queue_;

		// Uniform buffers
		const std::vector<VkBuffer> uniformBuffers_;

		// Descriptor sets
		const VkDescriptorSetLayout descriptorSetLayout_;
		VkDescriptorPool descriptorPool_;
		std::vector<VkDescriptorSet> descriptorSets_;

		// Base vertex data
		std::vector<Vertex> vertices_;
		std::vector<uint32_t> indices_;

		// Vertex, index buffer
		VkBuffer vertexBuffer_;
		VkDeviceMemory vertexBufferMemory_;
		VkBuffer indexBuffer_;
		VkDeviceMemory indexBufferMemory_;

		// Texture data
		uint32_t mipLevels_;
		VkImage textureImage_;
		VkDeviceMemory textureImageMemory_;
		VkImageView textureImageView_;
		const VkSampler textureSampler_;
	};

	struct UniformBufferObject
	{
		alignas(16) glm::mat4 model;
		alignas(16) glm::mat4 view;
		alignas(16) glm::mat4 proj;
	};
}

namespace std
{
	template<> struct hash<Rendering::Vertex>
	{
		size_t operator()(Rendering::Vertex const& vertex) const
		{
			return ((hash<glm::vec3>()(vertex.pos) ^
				(hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.texCoord) << 1);
		}
	};
}
