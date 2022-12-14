#pragma once

#include "projector.hpp"

const int MAX_FRAMES_IN_FLIGHT = 2;

int main(int argc, char* argv[])
{
    try
    {
        Projector::Projector app;
        app.Run();
    }
    catch (std::exception e)
    {
        std::cout << "Caught exception: '" << e.what() << "'" << std::endl;
    }
   
    return 0;
}
