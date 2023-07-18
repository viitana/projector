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

#### Hardware

A graphics card with Vulkan 1.3 and [variable rate shading support](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_KHR_fragment_shading_rate.html). A non-exhaustive and largely untested list of suitable cards:

- Nvidia: Turing architecture or newer
  - GeForce 20/16 series or newer
  - Quadro RTX 3000 or newer
- AMD: RDNA 2 architecture or newer
  - Radeon RX 6000 or newer
  - Radeon Pro W6000 or newer
- Intel: Xe (Gen12) architecture or newer
  - Arc 300/700 series or newer
  - UHD Graphics 700 series or newer
  - Iris Xe Graphics G7 or newer

#### Tools
- CMake: [`https://cmake.org/download`](https://cmake.org/download)
- (Highly recommended) vcpkg: [`https://github.com/microsoft/vcpkg`](https://github.com/microsoft/vcpkg)

#### SDKs
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
