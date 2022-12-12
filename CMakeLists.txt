cmake_minimum_required(VERSION 3.25)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

project(projector VERSION 0.1)
add_executable(projector main.cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set_property(TARGET projector PROPERTY CXX_STANDARD 17)
set_property(TARGET projector PROPERTY CXX_STANDARD_REQUIRED ON)

configure_file(config.hpp.in config.hpp)

find_package(Vulkan REQUIRED)
target_link_libraries(projector PRIVATE ${Vulkan_LIBRARIES})
target_include_directories(${PROJECT_NAME} PUBLIC ${Vulkan_INCLUDE_DIR})

find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(projector PRIVATE glfw)

find_package(glm CONFIG REQUIRED)
target_link_libraries(projector PRIVATE glm::glm)

target_include_directories(projector PUBLIC
                           "${PROJECT_BINARY_DIR}"
                           )
