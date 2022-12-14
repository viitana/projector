#pragma once

#include "projector.hpp"

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
