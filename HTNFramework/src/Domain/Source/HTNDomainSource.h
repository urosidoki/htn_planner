// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include <filesystem>
#include <functional>
#include <string>

using HTNDomainSourceProvider = std::function<bool(
    const std::filesystem::path& inFilePath,
    std::string& outSourceText)>;

struct HTNDomainLoadOptions
{
    bool RequireTopLevelRoot = true;
};
