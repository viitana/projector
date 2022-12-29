#pragma once

#include "projector.hpp"

int main(int argc, char* argv[])
{
#ifdef _DEBUG
    std::cout << "Running in debug mode" << std::endl;
#else
    std::cout << "Running in release mode" << std::endl;
#endif

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
