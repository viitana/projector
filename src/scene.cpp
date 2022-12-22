#include "scene.hpp"

#include <iostream>

#include "util.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace Scene
{
	Scene::Scene(const std::string& gltfFilename, const VkPhysicalDevice& physicalDevice, const VkDevice& device, const std::vector<VkBuffer>& uniformBuffers, const VkDescriptorSetLayout& descriptorSetLayout, const VkSampler& textureSampler, const VkCommandPool& commandPool, const VkQueue& queue)
	{
		tinygltf::TinyGLTF loader;
		tinygltf::Model model;
		std::string err;
		std::string warn;

		bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, gltfFilename);

		std::cout << "GLTF Error: " << err << std::endl;
		std::cout << "GLTF Warn: " << warn << std::endl;

		std::string position = "POSITION";
		std::string texCoord = "TEXCOORD_0"; 

		for (auto& mesh : model.meshes)
		{
			std::cout << "GLTF Mesh '" << mesh.name << "', primitives: " << mesh.primitives.size() << std::endl;

			for (auto& primitive : mesh.primitives)
			{
				std::cout << "GLTF primitive attributes: " << primitive.attributes.size() << ", indices index: " << primitive.indices << std::endl;

				for (auto& attribute : primitive.attributes)
				{
					std::cout << "  GLTF primitive attribute: '" << attribute.first << "': " << attribute.second << std::endl;
				}

				auto posIndex = primitive.attributes[position];
				auto posAccessor = model.accessors[posIndex];
				auto posBufferView = model.bufferViews[posAccessor.bufferView];
				auto posBuffer = model.buffers[posBufferView.buffer];

				auto texCoordIndex = primitive.attributes[texCoord];
				auto texCoordAccessor = model.accessors[texCoordIndex];
				auto texCoordBufferView = model.bufferViews[texCoordAccessor.bufferView];
				auto texCoordBuffer = model.buffers[texCoordBufferView.buffer];

				std::vector<Rendering::Vertex> vertices;
				std::vector<uint32_t> indices;

				std::cout << "  GLTF position atribute element count: " << model.accessors[posIndex].count << std::endl;

				auto posData = reinterpret_cast<glm::vec3*>(posBuffer.data.data());
				auto texCoordData = reinterpret_cast<glm::vec3*>(texCoordBuffer.data.data());

				std::cout << "  GLTF pos buffer size: " << posBuffer.data.size() << ", texCoord buffer size: " << texCoordBuffer.data.size() << std::endl;
				std::cout << "  GLTF pos buffer type: " << posAccessor.type << ", component type: " << posAccessor.componentType << std::endl;
				std::cout << "  GLTF pos buffer len: " << posBufferView.byteLength << ", offset: " << posBufferView.byteOffset << std::endl;

				int offset = posBufferView.byteOffset / sizeof(glm::vec3);
				int len = posBufferView.byteLength / sizeof(glm::vec3);

				for (int i = offset; i < offset + len; i++)
				{
					Rendering::Vertex vert =
					{
						.pos = posData[i],
						.color = {0.5f, 0.5f, 0.5f},
						.texCoord = {0, 0},
					};

					vertices.emplace_back(vert);
					indices.emplace_back(i - offset);
				}

				vertices.resize(posBufferView.byteLength / sizeof(glm::vec3));
				
				std::cout << "  GLTF texCoord buffer type: " << texCoordAccessor.type << ", component type: " << texCoordAccessor.componentType << std::endl;
				std::cout << "  GLTF pos buffer len: " << posBufferView.byteLength << ", offset: " << posBufferView.byteOffset << std::endl;
				std::cout << "  GLTF texCoord buffer len: " << texCoordBufferView.byteLength << ", offset: " << texCoordBufferView.byteOffset << std::endl;

				//for (int i = 0; i < posBuffer.data.size() / sizeof(glm::vec3); i++)
				//{
				//	Rendering::Vertex vert =
				//	{
				//		.pos = posData[i],
				//		.color = {0.5f, 0.5f, 0.5f},
				//		.texCoord = {0, 0},
				//	};

				//	vertices.emplace_back(vert);
				//	indices.emplace_back(i);
				//}

				std::cout << "  GLTF vertices: " << vertices.size() << "  indices: " << indices.size() << std::endl;


				Rendering::Object obj = Rendering::Object(
					vertices,
					indices,
					physicalDevice,
					device,
					uniformBuffers,
					descriptorSetLayout,
					textureSampler,
					commandPool,
					queue
				);
				objects_.push_back(std::move(obj));
			}
		}
	}

	void Scene::LoadNode(const tinygltf::Node& inputNode, const tinygltf::Model& input, Node* parent, std::vector<uint32_t>& indexBuffer, std::vector<Rendering::Vertex>& vertexBuffer)
	{
		Node* node = new Node{};
		node->matrix = glm::mat4(1.0f);
		node->parent = parent;

		// Get the local node matrix
		// It's either made up from translation, rotation, scale or a 4x4 matrix
		if (inputNode.translation.size() == 3)
		{
			node->matrix = glm::translate(node->matrix, glm::vec3(glm::make_vec3(inputNode.translation.data())));
		}
		if (inputNode.rotation.size() == 4)
		{
			glm::quat q = glm::make_quat(inputNode.rotation.data());
			node->matrix *= glm::mat4(q);
		}
		if (inputNode.scale.size() == 3)
		{
			node->matrix = glm::scale(node->matrix, glm::vec3(glm::make_vec3(inputNode.scale.data())));
		}
		if (inputNode.matrix.size() == 16)
		{
			node->matrix = glm::make_mat4x4(inputNode.matrix.data());
		};

		// Load node's children
		if (inputNode.children.size() > 0)
		{
			for (size_t i = 0; i < inputNode.children.size(); i++)
			{
				LoadNode(input.nodes[inputNode.children[i]], input, node, indexBuffer, vertexBuffer);
			}
		}

		// If the node contains mesh data, we load vertices and indices from the buffers
		// In glTF this is done via accessors and buffer views
		if (inputNode.mesh > -1)
		{
			const tinygltf::Mesh mesh = input.meshes[inputNode.mesh];
			// Iterate through all primitives of this node's mesh
			for (size_t i = 0; i < mesh.primitives.size(); i++)
			{
				const tinygltf::Primitive& glTFPrimitive = mesh.primitives[i];
				uint32_t firstIndex = static_cast<uint32_t>(indexBuffer.size());
				uint32_t vertexStart = static_cast<uint32_t>(vertexBuffer.size());
				uint32_t indexCount = 0;
				// Vertices
				{
					const float* positionBuffer = nullptr;
					const float* normalsBuffer = nullptr;
					const float* texCoordsBuffer = nullptr;
					size_t vertexCount = 0;

					// Get buffer data for vertex positions
					if (glTFPrimitive.attributes.find("POSITION") != glTFPrimitive.attributes.end())
					{
						const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("POSITION")->second];
						const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
						positionBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
						vertexCount = accessor.count;
					}
					// Get buffer data for vertex normals
					if (glTFPrimitive.attributes.find("NORMAL") != glTFPrimitive.attributes.end())
					{
						const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("NORMAL")->second];
						const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
						normalsBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
					}
					// Get buffer data for vertex texture coordinates
					// glTF supports multiple sets, we only load the first one
					if (glTFPrimitive.attributes.find("TEXCOORD_0") != glTFPrimitive.attributes.end())
					{
						const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("TEXCOORD_0")->second];
						const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
						texCoordsBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
					}

					// Append data to model's vertex buffer
					for (size_t v = 0; v < vertexCount; v++)
					{
						Rendering::Vertex vert
						{
							.pos = glm::make_vec3(&positionBuffer[v * 3]),
							.color = {0.5f, 0.5f, 0.5f},
							.texCoord = texCoordsBuffer ? glm::make_vec2(&texCoordsBuffer[v * 2]) : glm::vec2(0.0f)
						};
						//vert.pos = glm::vec4(glm::make_vec3(&positionBuffer[v * 3]), 1.0f);
						//vert.normal = glm::normalize(glm::vec3(normalsBuffer ? glm::make_vec3(&normalsBuffer[v * 3]) : glm::vec3(0.0f)));
						//vert.uv = texCoordsBuffer ? glm::make_vec2(&texCoordsBuffer[v * 2]) : glm::vec3(0.0f);
						//vert.color = glm::vec3(1.0f);
						vertexBuffer.push_back(vert);
					}
				}
				// Indices
				{
					const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.indices];
					const tinygltf::BufferView& bufferView = input.bufferViews[accessor.bufferView];
					const tinygltf::Buffer& buffer = input.buffers[bufferView.buffer];

					indexCount += static_cast<uint32_t>(accessor.count);

					// glTF supports different component types of indices
					switch (accessor.componentType) {
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
						const uint32_t* buf = reinterpret_cast<const uint32_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
						for (size_t index = 0; index < accessor.count; index++) {
							indexBuffer.push_back(buf[index] + vertexStart);
						}
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
						const uint16_t* buf = reinterpret_cast<const uint16_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
						for (size_t index = 0; index < accessor.count; index++) {
							indexBuffer.push_back(buf[index] + vertexStart);
						}
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
						const uint8_t* buf = reinterpret_cast<const uint8_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
						for (size_t index = 0; index < accessor.count; index++) {
							indexBuffer.push_back(buf[index] + vertexStart);
						}
						break;
					}
					default:
						std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
						return;
					}
				}
				Primitive primitive
				{
					.firstIndex = firstIndex,
					.indexCount = indexCount,
					.materialIndex = glTFPrimitive.material,
				};
				node->primitives.push_back(primitive);
			}
		}

		if (parent)
		{
			parent->children.push_back(node);
		}
		else
		{
			nodes_.push_back(node);
		}
	}

	void drawNode(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, Node* node)
	{
		if (node->mesh.primitives.size() > 0) {
			// Pass the node's matrix via push constants
			// Traverse the node hierarchy to the top-most parent to get the final matrix of the current node
			glm::mat4 nodeMatrix = node->matrix;
			Node* currentParent = node->parent;
			while (currentParent)
			{
				nodeMatrix = currentParent->matrix * nodeMatrix;
				currentParent = currentParent->parent;
			}
			// Pass the final matrix to the vertex shader using push constants
			vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &nodeMatrix);
			for (VulkanglTFModel::Primitive& primitive : node->mesh.primitives)
			{
				if (primitive.indexCount > 0)
				{
					// Get the texture index for this primitive
					//VulkanglTFModel::Texture texture = textures[materials[primitive.materialIndex].baseColorTextureIndex];
					// Bind the descriptor for the current primitive's texture
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &images[texture.imageIndex].descriptorSet, 0, nullptr);
					vkCmdDrawIndexed(commandBuffer, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
				}
			}
		}
		for (auto& child : node->children)
		{
			drawNode(commandBuffer, pipelineLayout, child);
		}
	}

	// Draw the glTF scene starting at the top-level-nodes
	void draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout)
	{
		// All vertices and indices are stored in single buffers, so we only need to bind once
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
		vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
		// Render all nodes at top-level
		for (auto& node : nodes_)
		{
			drawNode(commandBuffer, pipelineLayout, node);
		}
	}

	Scene::~Scene()
	{
		Destroy();
	}

	Scene::Scene(Scene&& other) noexcept
		: objects_(std::exchange(other.objects_, {}))
	{
	}

	Scene& Scene::operator=(Scene&& other) noexcept
	{
		Destroy();
		objects_ = std::exchange(other.objects_, {});
	}

	void Scene::Destroy()
	{
		objects_.clear();
	}

	const Rendering::Object& Scene::GetObj(int index) const
	{
		return objects_[index];
	}

	const size_t Scene::ObjectCount() const
	{
		return objects_.size();
	}

}
