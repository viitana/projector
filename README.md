# Projector

Projector is a WIP highly experimental asynchronous reprojection demo application built on Vulkan.

## Usage

The application features user-controllable first-person camera view in a basic 3D scene. Also included is a user interface that can be used to control various reprojection and rendering parameters.

### Controls

| Key        | Use                           |
|------------|-------------------------------|
| Mouse move | Look around                   |
| WASD       | Move camera/character         |
| ALT        | Release mouse cursor (use UI) |
| ESC        | Quit application              |

## Development

### Prerequisites

#### Tools
- CMake: [`https://cmake.org/download`](https://cmake.org/download)
- (Highly recommended) Vcpkg: [`https://github.com/microsoft/vcpkg`](https://github.com/microsoft/vcpkg)

#### Libraries
- Vulkan SDK: [`https://vulkan.lunarg.com`](https://vulkan.lunarg.com)
- Vulkan libktx: [`https://github.com/KhronosGroup/KTX-Software`](https://github.com/KhronosGroup/KTX-Software)
  - With Vcpkg: `vcpkg install ktx:x64-<windows|linux|osx>`
- glwf3: [`https://www.glfw.org/download`](https://www.glfw.org/download)
  - With Vcpkg: `vcpkg install glwf3:x64-<windows|linux|osx>`
- glm: [`https://www.glfw.org/download`](https://www.glfw.org/download)
  - With Vcpkg: `vcpkg install glm:x64-<windows|linux|osx>`
- glwf3: [`https://www.glfw.org/download`](https://www.glfw.org/download)
  - With Vcpkg: `vcpkg install glwf3:x64-<windows|linux|osx>`

### Generating build targets

Assuming a 64-bit system with vcpkg:
```bash
cmake -A x64 . -DCMAKE_TOOLCHAIN_FILE=<path_to_vcpkg>/scripts/buildsystems/vcpkg.cmake
```

### Building

Assuming a 64-bit system:
```bash
cmake --build .
```
