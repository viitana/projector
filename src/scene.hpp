#pragma once

#include <string>
#include <vector>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"

#include "vulkan/vulkan.h"

#include "object.hpp"
namespace Scene
{
	struct Primitive
	{
		uint32_t firstIndex;
		uint32_t indexCount;
		int32_t materialIndex;
	};

	struct Node
	{
		Node* parent;
		std::vector<Node*> children;
		std::vector<Primitive> primitives;
		glm::mat4 matrix;
		~Node() {
			for (auto& child : children) {
				delete child;
			}
		}
	};

	class Scene
	{
	public:
		Scene(const std::string& gltfFilename, const VkPhysicalDevice& physicalDevice, const VkDevice& device, const std::vector<VkBuffer>& uniformBuffers, const VkDescriptorSetLayout& descriptorSetLayout, const VkSampler& textureSampler, const VkCommandPool& commandPool, const VkQueue& queue);
		~Scene();

		Scene(const Scene& o) = delete;
		Scene(Scene&& other) noexcept;
		Scene& operator=(Scene other) = delete;
		Scene& operator=(Scene&& other) noexcept;

		void Destroy();

		const Rendering::Object& GetObj(int index) const;
		const size_t ObjectCount() const;

	private:
		void LoadNode(const tinygltf::Node& inputNode, const tinygltf::Model& input, Node* parent, std::vector<uint32_t>& indexBuffer, std::vector<Rendering::Vertex>& vertexBuffer);

		void DrawNode(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, Node* node);
		
		std::vector<Rendering::Object> objects_;

		std::vector<Node*> nodes_;
	};
}
