#pragma once

#include "Projector.hpp"

const int MAX_FRAMES_IN_FLIGHT = 2;

int main(int argc, char* argv[])
{
    const std::vector<Projector::Vertex> vertices =
    {
        {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
    };

    try
    {
        Projector::Projector app(vertices);
        app.Run();
    }
    catch (std::exception e)
    {
        std::cout << "Caught exception: '" << e.what() << "'" << std::endl;
    }
   
    return 0;
}
