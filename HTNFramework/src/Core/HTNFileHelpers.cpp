// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNFileHelpers.h"

namespace HTNFileHelpers
{
void CollectFilePathsRecursively(const std::filesystem::path& inDirectoryPath, const std::string& inFileExtension,
                                 std::vector<std::filesystem::path>& outFilePaths)
{
    for (const std::filesystem::directory_entry& DirectoryEntry : std::filesystem::directory_iterator(inDirectoryPath))
    {
        if (DirectoryEntry.is_directory())
        {
            const std::filesystem::path DirectoryPath = DirectoryEntry.path();
            CollectFilePathsRecursively(DirectoryPath, inFileExtension, outFilePaths);
        }
        else if (DirectoryEntry.is_regular_file())
        {
            const std::filesystem::path FilePath = DirectoryEntry.path();
            if (FilePath.extension() != inFileExtension)
            {
                continue;
            }

            outFilePaths.emplace_back(FilePath);
        }
    }
}

void CollectFilePaths(const std::filesystem::path& inDirectoryPath, const std::string& inFileExtension,
                      std::vector<std::filesystem::path>& outFilePaths)
{
    if (!DoesDirectoryExist(inDirectoryPath))
    {
        return;
    }

    static const size DefaultFilePathsSize = 10;
    outFilePaths.reserve(DefaultFilePathsSize);
    CollectFilePathsRecursively(inDirectoryPath, inFileExtension, outFilePaths);
}

std::string MakeFilePath(const std::string& inDirectoryName, const std::string& inFileName, const std::string& inFileExtension)
{
    return std::format("{}/{}{}", inDirectoryName, inFileName, inFileExtension);
}

std::string MakeFilePathDisplayName(const std::filesystem::path& inFilePath, const std::filesystem::path& inDirectoryPath)
{
    std::string FilePathDisplayName = inFilePath.lexically_relative(inDirectoryPath).replace_extension().string();

    // Convert backslashes to forward slashes
    std::replace(FilePathDisplayName.begin(), FilePathDisplayName.end(), '\\', '/');

    return FilePathDisplayName;
}
} // namespace HTNFileHelpers
