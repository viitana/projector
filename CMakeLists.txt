cmake_minimum_required(VERSION 3.25)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

project(projector VERSION 0.1)
add_subdirectory(src)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set_property(TARGET projector PROPERTY CXX_STANDARD 20)
set_property(TARGET projector PROPERTY CXX_STANDARD_REQUIRED ON)

# Visual Studio nice-to-haves
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT projector)
set_property(TARGET projector PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Included lib; stb_image (https://github.com/nothings/stb)
add_library(stbi INTERFACE)
target_include_directories(stbi INTERFACE lib/stb_image)
target_link_libraries(projector PRIVATE stbi)

# Included lib; tinygltf (https://github.com/syoyo/tinygltf)
add_library(tinygltf INTERFACE)
target_link_libraries(projector PRIVATE tinygltf)
target_include_directories(tinygltf INTERFACE lib/tiny_gltf)

# External lib; Vulkan SDK
find_package(Vulkan REQUIRED)
target_link_libraries(projector PRIVATE ${Vulkan_LIBRARIES})
target_include_directories(${PROJECT_NAME} PUBLIC ${Vulkan_INCLUDE_DIR})

# External lib; The KTX library / libktx
find_package(Ktx CONFIG REQUIRED)
target_link_libraries(projector PRIVATE KTX::ktx)
# target_link_libraries(projector PRIVATE KTX::ktx KTX::astcenc-avx2-static)

# External lib; GLWF
find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(projector PRIVATE glfw)

# External lib; GLM
find_package(glm CONFIG REQUIRED)
target_link_libraries(projector PRIVATE glm::glm)

# External lib; Dear ImGui
find_package(imgui CONFIG REQUIRED)
target_link_libraries(projector PRIVATE imgui::imgui)
