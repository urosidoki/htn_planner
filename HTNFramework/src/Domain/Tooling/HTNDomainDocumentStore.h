// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Domain/Source/HTNDomainDocumentSourceProvider.h"
#include "Translator/HTNCompilerToolingModel.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

struct HTNDomainDocument final
{
    std::filesystem::path FilePath;
    std::string Text;
    std::uint64_t Version = 0;
};

class HTNDomainDocumentStore final : public HTNDomainDocumentSourceProvider
{
public:
    HTNDomainDocumentStore();

    // LSP-style document lifecycle. The store never writes these buffers to disk.
    void OpenDocument(
        const std::filesystem::path& inFilePath,
        std::string inText,
        std::uint64_t inVersion);

    bool UpdateDocument(
        const std::filesystem::path& inFilePath,
        std::string inText,
        std::uint64_t inVersion);

    bool CloseDocument(const std::filesystem::path& inFilePath);
    void Clear();

    bool IsDocumentOpen(const std::filesystem::path& inFilePath) const;

    const HTNDomainDocument* GetDocument(
        const std::filesystem::path& inFilePath) const;

    // Returns a cached semantic model for the current workspace generation.
    // A change to any open document invalidates all cached models because includes can
    // make one document's semantic meaning depend on another unsaved document.
    const HTNCompilerToolingModel* GetToolingModel(
        const std::filesystem::path& inFilePath);

    // HTNDomainDocumentSourceProvider
    // Open in-memory buffers win; files not currently open fall back to disk.
    bool Read(
        const std::filesystem::path& inFilePath,
        std::string& outSourceText) const override;

    std::uint64_t GetGeneration() const { return mGeneration; }

private:
    struct SemanticCacheEntry
    {
        HTNCompilerToolingModel Model;
        std::uint64_t Generation = 0;
        std::uint64_t DocumentVersion = 0;
        bool HasModel = false;
    };

    static std::string MakePathKey(const std::filesystem::path& inFilePath);
    void InvalidateWorkspace();

private:
    HTNFileSystemDomainDocumentSourceProvider mFileSystemProvider;
    std::unordered_map<std::string, HTNDomainDocument> mDocuments;
    std::unordered_map<std::string, SemanticCacheEntry> mSemanticCache;

    std::uint64_t mGeneration = 1;
};
