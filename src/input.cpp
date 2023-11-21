#include "input.hpp"

#include <iostream>

namespace Input
{
	GLFWwindow* InputHandler::window_ = nullptr;
	glm::vec2 InputHandler::mouseDelta_ = {};
	glm::vec2 InputHandler::mousePos_ = {};
	glm::vec4 InputHandler::windowedSizePos_ = {};
	bool InputHandler::mouseDisabled_ = false;

	void InputHandler::Init(GLFWwindow* window)
	{
		window_ = window;

		glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

		glfwSetCursorPosCallback(window, OnCursor);
		glfwSetKeyCallback(window, OnKey);

		double xpos, ypos;
		glfwGetCursorPos(window, &xpos, &ypos);
		mousePos_.x = xpos;
		mousePos_.y = ypos;

		std::cout << "Initialized input for window " << window_ << std::endl;
	}

	const UserInput InputHandler::GetInput(const float deltaTime)
	{
		UserInput input =
		{
			.mouseDelta = mouseDelta_,
			.moveDelta = glm::vec3(0, 0, 0)
		};

		mouseDisabled_ = glfwGetKey(window_, GLFW_KEY_LEFT_ALT);
		glfwSetInputMode(
			window_,
			GLFW_CURSOR,
			mouseDisabled_ ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED
		);

		if (!mouseDisabled_)
		{
			if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS) input.moveDelta.x -= 3.0f * deltaTime;
			if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS) input.moveDelta.x += 3.0f * deltaTime;
			if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS) input.moveDelta.z -= 3.0f * deltaTime;
			if (glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS) input.moveDelta.z += 3.0f * deltaTime;
		}
		mouseDelta_ = {};

		return input;
	}

	void InputHandler::OnCursor(GLFWwindow* window, double xpos, double ypos)
	{
		if (window != window_) return;
		if (!mouseDisabled_)
		{
			mouseDelta_.x += 0.001f * (xpos - mousePos_.x);
			mouseDelta_.y += 0.001f * (ypos - mousePos_.y);
		}
		mousePos_.x = xpos;
		mousePos_.y = ypos;
	}

	void InputHandler::OnKey(GLFWwindow* window, int key, int scancode, int action, int mods)
	{
		if (window != window_ || action != GLFW_PRESS) return;

		if (key == GLFW_KEY_ESCAPE)
		{
			glfwSetWindowShouldClose(window_, GLFW_TRUE);
		}
		else if (key == GLFW_KEY_LEFT_ALT && glfwGetKey(window_, GLFW_KEY_ENTER) == GLFW_PRESS ||
			key == GLFW_KEY_ENTER && glfwGetKey(window_, GLFW_KEY_LEFT_ALT) == GLFW_PRESS)
		{
			if (windowedSizePos_.x)
			{
				std::cout << "Exiting fullscreen" << std::endl;

				GLFWmonitor* monitor = glfwGetPrimaryMonitor();
				const GLFWvidmode* mode = glfwGetVideoMode(monitor);
				glfwSetWindowMonitor(window_, nullptr, windowedSizePos_.z, windowedSizePos_.w, windowedSizePos_.x, windowedSizePos_.y, mode->refreshRate);
				windowedSizePos_ = {};
			}
			else
			{
				std::cout << "Entering fullscreen" << std::endl;

				int width, height;
				glfwGetWindowSize(window, &width, &height);
				int xpos, ypos;
				glfwGetWindowPos(window, &xpos, &ypos);

				GLFWmonitor* monitor = glfwGetPrimaryMonitor();
				const GLFWvidmode* mode = glfwGetVideoMode(monitor);
				glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
				windowedSizePos_ = { width, height, xpos, ypos };
			}
		}
	}
}
