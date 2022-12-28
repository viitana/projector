cmake_minimum_required(VERSION 3.25)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

project(projector VERSION 0.1)

# configure_file(config.hpp.in src/config.hpp)

add_subdirectory(src)

add_library(stbi INTERFACE)
target_include_directories(stbi INTERFACE lib/stb_image)
target_link_libraries(projector PRIVATE stbi)

add_library(tinyobjloader INTERFACE)
target_link_libraries(projector PRIVATE tinyobjloader)
target_include_directories(tinyobjloader INTERFACE lib/tiny_obj_loader)

add_library(tinygltf INTERFACE)
target_link_libraries(projector PRIVATE tinygltf)
target_include_directories(tinygltf INTERFACE lib/tiny_gltf)

find_package(Ktx CONFIG REQUIRED)
target_link_libraries(projector PRIVATE KTX::ktx)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT projector)
set_property(TARGET projector PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set_property(TARGET projector PROPERTY CXX_STANDARD 20)
set_property(TARGET projector PROPERTY CXX_STANDARD_REQUIRED ON)

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
