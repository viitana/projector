#pragma once

#include <string>
#include <vector>

#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#include "vulkan/vulkan.h"

#include "object.hpp"

namespace Scene
{
	extern VkDescriptorSetLayout descriptorSetLayoutImage;
	extern VkDescriptorSetLayout descriptorSetLayoutUbo;
	extern VkMemoryPropertyFlags memoryPropertyFlags;
	extern uint32_t descriptorBindingFlags;

	struct Texture
	{
		VkDevice device;

		VkImage image;
		VkDeviceMemory deviceMemory;
		VkImageView view;
		uint32_t width, height;
		uint32_t mipLevels;
		VkDescriptorImageInfo descriptor;
		VkSampler sampler;
		void Destroy();

		Texture(tinygltf::Image& gltfimage, const std::string path, const VkPhysicalDevice& physicalDevice, const VkDevice& d, const VkCommandPool& commandPool, const VkQueue& copyQueue, const VkDescriptorPool& descriptorSetPool);
		Texture(const std::string path, const VkPhysicalDevice& physicalDevice, const VkDevice& d, const VkCommandPool& commandPool, const VkQueue& copyQueue, const VkDescriptorPool& descriptorSetPool);
		~Texture();

		Texture(const Texture& o) = delete;
		Texture(Texture&& other) noexcept;
		Texture& operator=(Texture other) = delete;
		Texture& operator=(Texture&& other) = delete;

		std::vector<VkDescriptorSet> descriptorSets;
	};

	struct Material {
		const VkDevice device = nullptr;

		enum AlphaMode { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
		AlphaMode alphaMode = ALPHAMODE_OPAQUE;

		float alphaCutoff = 1.0f;
		float metallicFactor = 1.0f;
		float roughnessFactor = 1.0f;
		glm::vec4 baseColorFactor = glm::vec4(1.0f);

		Texture* baseColorTexture = nullptr;
		//Texture* metallicRoughnessTexture = nullptr;
		Texture* normalTexture = nullptr;
		//Texture* occlusionTexture = nullptr;
		//Texture* emissiveTexture = nullptr;

		//Texture* specularGlossinessTexture;
		//Texture* diffuseTexture;

		Material(const VkDevice& device) : device(device) {};

		std::vector<VkDescriptorSet> descriptorSets;
		void CreateDescriptorSets(const VkDescriptorPool descriptorPool, const VkDescriptorSetLayout descriptorSetLayout/*, uint32_t descriptorBindingFlags*/);
	};

	struct Primitive
	{
		uint32_t firstIndex;
		uint32_t indexCount;
		uint32_t firstVertex;
		uint32_t vertexCount;
		Material& material;

		//struct Dimensions
		//{
		//	glm::vec3 min = glm::vec3(FLT_MAX);
		//	glm::vec3 max = glm::vec3(-FLT_MAX);
		//	glm::vec3 size;
		//	glm::vec3 center;
		//	float radius;
		//} dimensions;

		//void SetDimensions(glm::vec3 min, glm::vec3 max);
	};

	struct Mesh
	{
		const VkDevice device;
		const VkPhysicalDevice physicalDevice;

		std::vector<Primitive*> primitives;
		std::string name;

		struct UniformBuffer
		{
			VkBuffer buffer;
			VkDeviceMemory memory;
			VkDescriptorBufferInfo descriptor;
			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			void* mapped;
		} uniformBuffer;

		struct UniformBlock
		{
			glm::mat4 matrix;
			glm::mat4 jointMatrix[64]{};
			float jointcount{ 0 };
		} uniformBlock;

		Mesh(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const glm::mat4 matrix);
		~Mesh();
	};

	struct Node {
		Node* parent;
		uint32_t index;
		std::vector<Node*> children;
		glm::mat4 matrix;
		std::string name;
		Mesh* mesh;
		//Skin* skin;
		//int32_t skinIndex = -1;
		glm::vec3 translation{};
		glm::vec3 scale{ 1.0f };
		glm::quat rotation{};

		glm::mat4 GetLocalMatrix();
		glm::mat4 GetMatrix();

		void Update();
		~Node();

		Node* FindChild(uint32_t index);
	};

	enum class VertexComponent { Position, Normal, UV, Color, Tangent, Joint0, Weight0 };

	struct Vertex
	{
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec4 color;
		glm::vec4 joint0;
		glm::vec4 weight0;
		glm::vec4 tangent;

		static VkVertexInputBindingDescription vertexInputBindingDescription;
		static std::vector<VkVertexInputAttributeDescription> vertexInputAttributeDescriptions;
		static VkPipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo;

		static VkVertexInputBindingDescription GetInputBindingDescription(uint32_t binding);
		static VkVertexInputAttributeDescription GetInputAttributeDescription(uint32_t binding, uint32_t location, VertexComponent component);
		static std::vector<VkVertexInputAttributeDescription> GetInputAttributeDescriptions(uint32_t binding, const std::vector<VertexComponent> components);
		/** @brief Returns the default pipeline vertex input state create info structure for the requested vertex components */
		static VkPipelineVertexInputStateCreateInfo* GetPipelineVertexInputState(const std::vector<VertexComponent> components);
	};

	class Model {
	private:
		Texture* GetTexture(uint32_t index);

		const VkPhysicalDevice physicalDevice_;
		const VkDevice device_;
		const VkQueue transferQueue_;
		const VkCommandPool commandPool_;
		VkDescriptorPool descriptorPool_;
		const float scale_;

		Texture* emptyTexture_;
	public:
		struct Vertices
		{
			int count;
			VkBuffer buffer;
			VkDeviceMemory memory;
		} vertices;

		struct Indices
		{
			int count;
			VkBuffer buffer;
			VkDeviceMemory memory;
		} indices;

		std::vector<Node*> nodes;
		std::vector<Node*> linearNodes;

		//std::vector<Skin*> skins;

		std::vector<Texture> textures;
		std::vector<Material> materials;
		//std::vector<Animation> animations;

		struct Dimensions {
			glm::vec3 min = glm::vec3(FLT_MAX);
			glm::vec3 max = glm::vec3(-FLT_MAX);
			glm::vec3 size;
			glm::vec3 center;
			float radius;
		} dimensions;

		bool metallicRoughnessWorkflow = true;
		bool buffersBound = false;
		std::string path;

		Model(const std::string filename, const VkPhysicalDevice& pd, const VkDevice& d, const VkCommandPool& commandPool, const VkQueue& transferQueue, const float scale);
		~Model();

		void LoadNode(Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model, std::vector<uint32_t>& indexBuffer, std::vector<Vertex>& vertexBuffer, float globalscale);
		//void LoadSkins(Model& gltfModel);
		void LoadImages(tinygltf::Model& gltfModel);
		void LoadMaterials(tinygltf::Model& gltfModel);
		//void LoadAnimations(Model& gltfModel);
		void BindBuffers(VkCommandBuffer commandBuffer);
		void DrawNode(Node* node, VkCommandBuffer commandBuffer, uint32_t renderFlags = 0, VkPipelineLayout pipelineLayout = VK_NULL_HANDLE, uint32_t bindImageSet = 1);
		void Draw(VkCommandBuffer commandBuffer, uint32_t renderFlags = 0, VkPipelineLayout pipelineLayout = VK_NULL_HANDLE, uint32_t bindImageSet = 1);
		//void GetNodeDimensions(Node* node, glm::vec3& min, glm::vec3& max);
		//void GetSceneDimensions();
		//void UpdateAnimation(uint32_t index, float time);
		Node* FindNode(Node* parent, uint32_t index);
		Node* NodeFromIndex(uint32_t index);
		void PrepareNodeDescriptor(Node* node, VkDescriptorSetLayout descriptorSetLayout);
	};

}

//
//	class Scene
//	{
//	public:
//		Scene(const std::string& gltfFilename, const VkPhysicalDevice& physicalDevice, const VkDevice& device, const std::vector<VkBuffer>& uniformBuffers, const VkDescriptorSetLayout& descriptorSetLayout, const VkSampler& textureSampler, const VkCommandPool& commandPool, const VkQueue& queue);
//		~Scene();
//
//		Scene(const Scene& o) = delete;
//		Scene(Scene&& other) noexcept;
//		Scene& operator=(Scene other) = delete;
//		Scene& operator=(Scene&& other) noexcept;
//
//		void Destroy();
//
//		const Rendering::Object& GetObj(int index) const;
//		const size_t ObjectCount() const;
//
//	private:
//		void LoadNode(const Node& inputNode, const Model& input, Node* parent, std::vector<uint32_t>& indexBuffer, std::vector<Rendering::Vertex>& vertexBuffer);
//
//		void DrawNode(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, Node* node);
//		
//		std::vector<Rendering::Object> objects_;
//
//		std::vector<Node*> nodes_;
//	};
//}
