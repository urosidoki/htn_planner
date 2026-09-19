// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

class HTNDomainDocumentSourceProvider
{
public:
    virtual ~HTNDomainDocumentSourceProvider() = default;

    virtual bool Read(
        const std::filesystem::path& inFilePath,
        std::string& outSourceText) const = 0;
};

class HTNFileSystemDomainDocumentSourceProvider final : public HTNDomainDocumentSourceProvider
{
public:
    bool Read(
        const std::filesystem::path& inFilePath,
        std::string& outSourceText) const override;
};

// Tooling source overlay: unsaved buffers win over the fallback provider.
// This is intended for HTNEditor now and HTNLanguageServer later.
class HTNOverlayDomainDocumentSourceProvider final : public HTNDomainDocumentSourceProvider
{
public:
    explicit HTNOverlayDomainDocumentSourceProvider(
        const HTNDomainDocumentSourceProvider* inFallbackProvider = nullptr);

    void SetFallbackProvider(const HTNDomainDocumentSourceProvider* inFallbackProvider);
    void SetSource(const std::filesystem::path& inFilePath, std::string inSourceText);
    void RemoveSource(const std::filesystem::path& inFilePath);
    void Clear();

    bool Read(
        const std::filesystem::path& inFilePath,
        std::string& outSourceText) const override;

private:
    static std::string MakePathKey(const std::filesystem::path& inFilePath);

private:
    const HTNDomainDocumentSourceProvider* mFallbackProvider = nullptr;
    std::unordered_map<std::string, std::string> mSources;
};
