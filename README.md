# Projector

Projector is a WIP highly experimental asynchronous reprojection demo application built on Vulkan.

## Usage

The application features user-controllable first-person camera view in a basic 3D scene. Also included is a user interface that can be used to control various reprojection and rendering parameters.

### Controls

| Key         | Use                           |
|-------------|-------------------------------|
| Mouse move  | Look around                   |
| WASD        | Move camera/character         |
| ALT         | Release mouse cursor (use UI) |
| ALT + ENTER | Toggle fullscreen             |
| ESC         | Quit application              |

## Development

### Prerequisites

#### Tools
- CMake: [`https://cmake.org/download`](https://cmake.org/download)
- (Highly recommended) vcpkg: [`https://github.com/microsoft/vcpkg`](https://github.com/microsoft/vcpkg)

### SDKs
- Vulkan SDK: [`https://vulkan.lunarg.com`](https://vulkan.lunarg.com)

#### Libraries
- Vulkan libktx: [`https://github.com/KhronosGroup/KTX-Software`](https://github.com/KhronosGroup/KTX-Software)
- glwf3: [`https://www.glfw.org/download`](https://www.glfw.org)
- glm: [`https://github.com/g-truc/glm`](https://github.com/g-truc/glm)
- Dear ImGui: [`https://github.com/ocornut/imgui`](https://github.com/ocornut/imgui)

If using vcpkg, the libraries can be installed with

```
vcpkg install
```

### Generating build targets

```bash
cmake -B build
```

If using vcpkg, you may need to additionally provide the vcpkg toolchain file via the  `DCMAKE_TOOLCHAIN_FILE` flag, e.g.

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<path_to_vcpkg>/scripts/buildsystems/vcpkg.cmake
```

### Building

```bash
cmake --build build
```
