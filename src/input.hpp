#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

#include <iostream>

namespace Input
{
	struct UserInput
	{
		glm::vec2 mouseDelta;
		glm::vec3 moveDelta;
	};

	class InputHandler
	{
	public:
		static void Init(GLFWwindow* window);
		static const UserInput GetInput(const float deltaTime);

		static void OnCursor(GLFWwindow* window, double xpos, double ypos);
		static void OnKey(GLFWwindow* window, int key, int scancode, int action, int mods);
private:
		InputHandler() {}

		static GLFWwindow* window_;
		static glm::vec2 mouseDelta_;
		static glm::vec2 mousePos_;
		static bool mouseDisabled_;

		static glm::vec4 windowedSizePos_;
	};
}
