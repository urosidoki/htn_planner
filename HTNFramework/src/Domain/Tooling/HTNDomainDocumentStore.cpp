// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Domain/Tooling/HTNDomainDocumentStore.h"

#include <utility>

HTNDomainDocumentStore::HTNDomainDocumentStore() = default;

void HTNDomainDocumentStore::OpenDocument(
    const std::filesystem::path& inFilePath,
    std::string inText,
    const std::uint64_t inVersion)
{
    const std::string Key = MakePathKey(inFilePath);

    HTNDomainDocument& Document = mDocuments[Key];
    Document.FilePath = inFilePath;
    Document.Text = std::move(inText);
    Document.Version = inVersion;

    InvalidateWorkspace();
}

bool HTNDomainDocumentStore::UpdateDocument(
    const std::filesystem::path& inFilePath,
    std::string inText,
    const std::uint64_t inVersion)
{
    const std::string Key = MakePathKey(inFilePath);
    const auto It = mDocuments.find(Key);
    if (It == mDocuments.end())
        return false;

    It->second.Text = std::move(inText);
    It->second.Version = inVersion;

    InvalidateWorkspace();
    return true;
}

bool HTNDomainDocumentStore::CloseDocument(
    const std::filesystem::path& inFilePath)
{
    const std::string Key = MakePathKey(inFilePath);
    if (mDocuments.erase(Key) == 0)
        return false;

    mSemanticCache.erase(Key);
    InvalidateWorkspace();
    return true;
}

void HTNDomainDocumentStore::Clear()
{
    if (mDocuments.empty() && mSemanticCache.empty())
        return;

    mDocuments.clear();
    mSemanticCache.clear();
    InvalidateWorkspace();
}

bool HTNDomainDocumentStore::IsDocumentOpen(
    const std::filesystem::path& inFilePath) const
{
    return mDocuments.contains(MakePathKey(inFilePath));
}

const HTNDomainDocument* HTNDomainDocumentStore::GetDocument(
    const std::filesystem::path& inFilePath) const
{
    const auto It = mDocuments.find(MakePathKey(inFilePath));
    return It == mDocuments.end() ? nullptr : &It->second;
}

const HTNCompilerToolingModel* HTNDomainDocumentStore::GetToolingModel(
    const std::filesystem::path& inFilePath)
{
    const std::string Key = MakePathKey(inFilePath);
    const auto DocumentIt = mDocuments.find(Key);
    if (DocumentIt == mDocuments.end())
        return nullptr;

    const HTNDomainDocument& Document = DocumentIt->second;
    SemanticCacheEntry& Cache = mSemanticCache[Key];

    if (!Cache.HasModel ||
        Cache.Generation != mGeneration ||
        Cache.DocumentVersion != Document.Version)
    {
        Cache.Model.Analyze(
            Document.FilePath,
            Document.Text,
            [this](const std::filesystem::path& inPath, std::string& outText)
            {
                return Read(inPath, outText);
            });

        Cache.Generation = mGeneration;
        Cache.DocumentVersion = Document.Version;
        Cache.HasModel = true;
    }

    return &Cache.Model;
}

bool HTNDomainDocumentStore::Read(
    const std::filesystem::path& inFilePath,
    std::string& outSourceText) const
{
    const auto It = mDocuments.find(MakePathKey(inFilePath));
    if (It != mDocuments.end())
    {
        outSourceText = It->second.Text;
        return true;
    }

    return mFileSystemProvider.Read(inFilePath, outSourceText);
}

std::string HTNDomainDocumentStore::MakePathKey(
    const std::filesystem::path& inFilePath)
{
    std::error_code Error;
    const std::filesystem::path Canonical =
        std::filesystem::weakly_canonical(inFilePath, Error);

    return (Error ? inFilePath.lexically_normal() : Canonical)
        .generic_string();
}

void HTNDomainDocumentStore::InvalidateWorkspace()
{
    ++mGeneration;

    // Generation zero is reserved for "never analyzed". In practice overflow is
    // irrelevant, but keeping the invariant makes cache state unambiguous.
    if (mGeneration == 0)
    {
        mGeneration = 1;
        mSemanticCache.clear();
    }
}
