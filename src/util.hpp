#pragma once

#include <vector>
#include <string>

namespace Util
{
	const std::vector<char> ReadFile(const std::string& filename);
	void ListDirectoryFiles(const std::string& directory);
}
