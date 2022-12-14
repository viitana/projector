#include "util.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace Util
{
    const std::vector<char> ReadFile(const std::string& filename)
	{
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open())
        {
            throw std::runtime_error("failed to open file!");
        }

        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();

        return buffer;
	}

    void ListDirectoryFiles(const std::string& directory)
    {
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            std::cout << entry.path() << std::endl;
        }
    }
}
