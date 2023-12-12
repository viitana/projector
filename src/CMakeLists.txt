add_executable(projector main.cpp)
target_sources(projector
    PRIVATE
        config.hpp
        gpu.cpp
        gpu.hpp
        input.cpp
        input.hpp
        projector.cpp
        projector.hpp
        scene.cpp
        scene.hpp
        stats.cpp
        stats.hpp
        util.cpp
        util.hpp
)

add_subdirectory(shaders)
