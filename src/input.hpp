#pragma once

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

#include <iostream>

namespace Input
{
	struct UserInput
	{
		glm::vec2 mousePos;
		glm::vec2 mouseDelta;
		glm::vec2 moveDelta;
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
		static glm::vec3 mouseDelta_;
		static glm::vec3 mousePos_;

		static glm::vec4 windowedSizePos_;
	};
}
