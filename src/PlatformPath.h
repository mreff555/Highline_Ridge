#ifndef PLATFORM_PATH_H
#define PLATFORM_PATH_H

#include <string>
#include <vector>

namespace testgame
{

std::string pathJoin(const std::string& base, const std::string& relative);

bool ensureParentDirectories(const std::string& filePath);

bool ensureDirectory(const std::string& directoryPath);

std::vector<std::string> listDirectoryFileNames(const std::string& directoryPath);

}

#endif /* PLATFORM_PATH_H */