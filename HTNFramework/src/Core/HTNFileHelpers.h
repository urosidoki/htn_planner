// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"

#include <filesystem>
#include <string>

namespace HTNFileHelpers
{
// Returns all the file paths at the given directory with the given extension
void CollectFilePaths(const std::filesystem::path& inDirectoryPath, const std::string& inFileExtension,
                      std::vector<std::filesystem::path>& outFilePaths);

// Combines the given directory name, file name, and file extension into a file path
HTN_NODISCARD std::string MakeFilePath(const std::string& inDirectoryName, const std::string& inFileName, const std::string& inFileExtension);

// Returns the display name of the given file path
HTN_NODISCARD std::string MakeFilePathDisplayName(const std::filesystem::path& inFilePath, const std::filesystem::path& inDirectoryPath);

// Converts the given directory path into an absolute path
HTN_NODISCARD std::filesystem::path MakeAbsolutePath(const std::string& inRelativePath);

// Returns whether the given directory path exists
HTN_NODISCARD bool DoesDirectoryExist(const std::filesystem::path& inDirectoryPath);

inline const std::string kWorldStatesDirectoryName = "WorldStates";
inline const std::string kDomainsDirectoryName     = "Domains";
inline const std::string kTestDirectoryName        = "Test";
inline const std::string kCapturesDirectoryName    = "Captures";
inline const std::string kCaptureFileName          = "test";
inline const std::string kWorldStateFileExtension  = ".worldstate";
inline const std::string kDomainFileExtension      = ".domain";
} // namespace HTNFileHelpers

namespace HTNFileHelpers
{
inline std::filesystem::path MakeAbsolutePath(const std::string& inRelativePath)
{
    const std::filesystem::path ParentPath = std::filesystem::current_path().parent_path();
    return ParentPath / std::filesystem::path(inRelativePath);
}

inline bool DoesDirectoryExist(const std::filesystem::path& inDirectoryPath)
{
    return std::filesystem::exists(inDirectoryPath) && std::filesystem::is_directory(inDirectoryPath);
}
} // namespace HTNFileHelpers
