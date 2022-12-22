add_executable(projector main.cpp)
target_sources(projector
    PRIVATE
        config.hpp
        object.cpp
        object.hpp
        projector.cpp
        projector.hpp
        scene.cpp
        scene.hpp
        util.cpp
        util.hpp
)

add_subdirectory(shaders)
