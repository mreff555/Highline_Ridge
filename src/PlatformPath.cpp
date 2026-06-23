#include "PlatformPath.h"

#include <filesystem>

namespace testgame
{

namespace fs = std::filesystem;

std::string pathJoin(const std::string& base, const std::string& relative)
{
    if (base.empty())
        return relative;
    if (relative.empty())
        return base;

    return (fs::path(base) / fs::path(relative)).lexically_normal().string();
}

bool ensureParentDirectories(const std::string& filePath)
{
    const fs::path path(filePath);
    if (!path.has_parent_path())
        return true;

    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    return !error;
}

bool ensureDirectory(const std::string& directoryPath)
{
    if (directoryPath.empty())
        return false;

    std::error_code error;
    fs::create_directories(fs::path(directoryPath), error);
    return !error;
}

std::vector<std::string> listDirectoryFileNames(const std::string& directoryPath)
{
    std::vector<std::string> fileNames;
    std::error_code error;
    if (!fs::is_directory(directoryPath, error))
        return fileNames;

    for (const fs::directory_entry& entry : fs::directory_iterator(directoryPath, error))
    {
        if (error)
            break;

        if (!entry.is_regular_file(error))
            continue;

        fileNames.push_back(entry.path().filename().string());
    }

    return fileNames;
}

}