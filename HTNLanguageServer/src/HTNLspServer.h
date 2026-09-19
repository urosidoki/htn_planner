// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Domain/Tooling/HTNDomainDocumentStore.h"
#include "HTNLspJson.h"
#include "HTNLspTransport.h"

#include <filesystem>
#include <string>

class HTNLspServer final
{
public:
    explicit HTNLspServer(HTNLspTransport& inTransport);

    int Run();

private:
    void HandleMessage(const HTNLspJson& inMessage);
    void HandleRequest(
        const HTNLspJson& inId,
        const std::string& inMethod,
        const HTNLspJson* inParams);
    void HandleNotification(
        const std::string& inMethod,
        const HTNLspJson* inParams);

    void HandleDidOpen(const HTNLspJson& inParams);
    void HandleDidChange(const HTNLspJson& inParams);
    void HandleDidClose(const HTNLspJson& inParams);

    void HandleDefinition(const HTNLspJson& inId, const HTNLspJson& inParams);
    void HandleCompletion(const HTNLspJson& inId, const HTNLspJson& inParams);
    void HandleCompile(const HTNLspJson& inId, const HTNLspJson& inParams);

    void PublishDiagnostics(
        const std::string& inUri,
        const std::filesystem::path& inFilePath);

    void PublishEmptyDiagnostics(const std::string& inUri);

    void SendResponse(
        const HTNLspJson& inId,
        HTNLspJson inResult);
    void SendError(
        const HTNLspJson& inId,
        std::int64_t inCode,
        std::string inMessage);
    void SendNotification(
        std::string inMethod,
        HTNLspJson inParams);

    static std::filesystem::path UriToPath(const std::string& inUri);
    static std::string PathToUri(const std::filesystem::path& inPath);
    static std::string PercentDecode(std::string_view inText);
    static std::string PercentEncodePath(std::string_view inText);
    static size_t LspPositionToOffset(const std::string& inText, int inLineZeroBased, int inCharacterUtf16);

private:
    HTNLspTransport& mTransport;
    HTNDomainDocumentStore mDocuments;
    bool mShutdownRequested = false;
    bool mExitRequested = false;
};
