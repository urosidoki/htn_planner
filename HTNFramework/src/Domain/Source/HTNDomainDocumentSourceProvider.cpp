// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Domain/Source/HTNDomainDocumentSourceProvider.h"

#include <fstream>
#include <iterator>

bool HTNFileSystemDomainDocumentSourceProvider::Read(
    const std::filesystem::path& inFilePath,
    std::string& outSourceText) const
{
    std::ifstream File(inFilePath, std::ios::binary);
    if (!File)
        return false;

    outSourceText.assign(
        std::istreambuf_iterator<char>(File),
        std::istreambuf_iterator<char>());

    return true;
}

HTNOverlayDomainDocumentSourceProvider::HTNOverlayDomainDocumentSourceProvider(
    const HTNDomainDocumentSourceProvider* inFallbackProvider)
    : mFallbackProvider(inFallbackProvider)
{
}

void HTNOverlayDomainDocumentSourceProvider::SetFallbackProvider(
    const HTNDomainDocumentSourceProvider* inFallbackProvider)
{
    mFallbackProvider = inFallbackProvider;
}

void HTNOverlayDomainDocumentSourceProvider::SetSource(
    const std::filesystem::path& inFilePath,
    std::string inSourceText)
{
    mSources[MakePathKey(inFilePath)] = std::move(inSourceText);
}

void HTNOverlayDomainDocumentSourceProvider::RemoveSource(
    const std::filesystem::path& inFilePath)
{
    mSources.erase(MakePathKey(inFilePath));
}

void HTNOverlayDomainDocumentSourceProvider::Clear()
{
    mSources.clear();
}

bool HTNOverlayDomainDocumentSourceProvider::Read(
    const std::filesystem::path& inFilePath,
    std::string& outSourceText) const
{
    const auto It = mSources.find(MakePathKey(inFilePath));
    if (It != mSources.end())
    {
        outSourceText = It->second;
        return true;
    }

    return mFallbackProvider != nullptr &&
           mFallbackProvider->Read(inFilePath, outSourceText);
}

std::string HTNOverlayDomainDocumentSourceProvider::MakePathKey(
    const std::filesystem::path& inFilePath)
{
    std::error_code Error;
    const std::filesystem::path Canonical =
        std::filesystem::weakly_canonical(inFilePath, Error);

    return (Error ? inFilePath.lexically_normal() : Canonical)
        .generic_string();
}
