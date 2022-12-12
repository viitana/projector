#pragma once

#include "Projector.hpp"

const int MAX_FRAMES_IN_FLIGHT = 2;

int main(int argc, char* argv[])
{
    const std::vector<Projector::Vertex> vertices =
    {
        {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},

        {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}
    };

    const std::vector<uint32_t> indices =
    {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4
    };

    try
    {
        Projector::Projector app(vertices, indices);
        app.Run();
    }
    catch (std::exception e)
    {
        std::cout << "Caught exception: '" << e.what() << "'" << std::endl;
    }
   
    return 0;
}
