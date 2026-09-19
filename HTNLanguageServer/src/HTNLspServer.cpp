// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNLspServer.h"

#include "Domain/Diagnostics/HTNDiagnosticSink.h"
#include "Translator/HTNCompilerDomainLoader.h"
#include "Translator/HTNCompilerToolingModel.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <set>
#include <unordered_set>

namespace
{
const HTNLspJson* FindNested(
    const HTNLspJson& inRoot,
    std::initializer_list<std::string_view> inKeys)
{
    const HTNLspJson* Current = &inRoot;
    for (const std::string_view Key : inKeys)
    {
        Current = Current->Find(Key);
        if (Current == nullptr)
            return nullptr;
    }
    return Current;
}

HTNLspJson MakePosition(const int inLineOneBased, const int inColumnOneBased)
{
    return HTNLspJson::Object{
        { "line", static_cast<std::int64_t>(std::max(0, inLineOneBased - 1)) },
        { "character", static_cast<std::int64_t>(std::max(0, inColumnOneBased - 1)) }
    };
}


std::int64_t ToLspDiagnosticSeverity(const HTNDiagnosticSeverity inSeverity)
{
    switch (inSeverity)
    {
    case HTNDiagnosticSeverity::Error: return 1;
    case HTNDiagnosticSeverity::Warning: return 2;
    case HTNDiagnosticSeverity::Info: return 3;
    }
    return 1;
}

HTNLspJson::Object MakeCompilerDiagnosticFields(
    const HTNDiagnostic& inDiagnostic,
    const std::string& inSource)
{
    const int BeginLine = std::max(1, inDiagnostic.Range.Begin.Line);
    const int BeginColumn = std::max(1, inDiagnostic.Range.Begin.Column);

    int EndLine = std::max(BeginLine, inDiagnostic.Range.End.Line);
    int EndColumn = std::max(1, inDiagnostic.Range.End.Column);

    // Semantic/link diagnostics do not all carry exact AST ranges yet.
    // Keep them visible in editors with a one-character fallback range.
    if (EndLine == BeginLine && EndColumn <= BeginColumn)
        EndColumn = BeginColumn + 1;

    return HTNLspJson::Object{
        { "range", HTNLspJson::Object{
            { "start", MakePosition(BeginLine, BeginColumn) },
            { "end", MakePosition(EndLine, EndColumn) }
        }},
        { "severity", ToLspDiagnosticSeverity(inDiagnostic.Severity) },
        { "source", inSource },
        { "message", inDiagnostic.Message }
    };
}

bool PathEquals(const std::filesystem::path& inA, const std::filesystem::path& inB)
{
    std::error_code ErrorA;
    std::error_code ErrorB;
    const std::filesystem::path CanonicalA = std::filesystem::weakly_canonical(inA, ErrorA);
    const std::filesystem::path CanonicalB = std::filesystem::weakly_canonical(inB, ErrorB);
    return (ErrorA ? inA.lexically_normal() : CanonicalA) ==
           (ErrorB ? inB.lexically_normal() : CanonicalB);
}

bool RangeContainsOffset(const HTNSourceRange& inRange, const size_t inOffset)
{
    return inOffset >= inRange.Begin.Offset && inOffset <= inRange.End.Offset;
}

bool FindSemanticAtomAtOffset(
    const std::string& inText,
    size_t inOffset,
    size_t& outBegin,
    size_t& outEnd)
{
    const auto IsAtomChar = [](const char Character)
    {
        return std::isalnum(static_cast<unsigned char>(Character)) ||
               Character == '_' || Character == '-' || Character == '?' ||
               Character == '@' || Character == ':' || Character == '#';
    };

    if (inText.empty())
        return false;

    inOffset = std::min(inOffset, inText.size());
    size_t Probe = inOffset;
    if (Probe == inText.size() || !IsAtomChar(inText[Probe]))
    {
        if (Probe == 0 || !IsAtomChar(inText[Probe - 1]))
            return false;
        --Probe;
    }

    outBegin = Probe;
    while (outBegin > 0 && IsAtomChar(inText[outBegin - 1]))
        --outBegin;

    outEnd = Probe + 1;
    while (outEnd < inText.size() && IsAtomChar(inText[outEnd]))
        ++outEnd;

    return outBegin < outEnd;
}

}

HTNLspServer::HTNLspServer(HTNLspTransport& inTransport)
    : mTransport(inTransport)
{
}

int HTNLspServer::Run()
{
    std::string Payload;
    while (!mExitRequested && mTransport.ReadMessage(Payload))
    {
        std::string Error;
        auto Message = HTNLspJson::Parse(Payload, Error);
        if (!Message)
        {
            std::cerr << "HTNLanguageServer: invalid JSON-RPC payload: "
                      << Error << '\n';
            continue;
        }

        HandleMessage(*Message);
    }

    return mShutdownRequested ? 0 : 1;
}

void HTNLspServer::HandleMessage(const HTNLspJson& inMessage)
{
    const HTNLspJson* MethodValue = inMessage.Find("method");
    if (MethodValue == nullptr || !MethodValue->IsString())
        return;

    const std::string Method = MethodValue->AsString();
    const HTNLspJson* Params = inMessage.Find("params");
    const HTNLspJson* Id = inMessage.Find("id");

    if (Id != nullptr)
        HandleRequest(*Id, Method, Params);
    else
        HandleNotification(Method, Params);
}

void HTNLspServer::HandleRequest(
    const HTNLspJson& inId,
    const std::string& inMethod,
    const HTNLspJson* inParams)
{
    if (inMethod == "initialize")
    {
        HTNLspJson::Object Capabilities;
        Capabilities.emplace("textDocumentSync", static_cast<std::int64_t>(1));
        Capabilities.emplace("definitionProvider", true);
        Capabilities.emplace("completionProvider", HTNLspJson::Object{{ "resolveProvider", false }});

        SendResponse(
            inId,
            HTNLspJson::Object{
                { "capabilities", HTNLspJson(std::move(Capabilities)) },
                { "serverInfo", HTNLspJson::Object{
                    { "name", "HTNLanguageServer" },
                    { "version", "0.1.0" }
                }}
            });
        return;
    }

    if (inMethod == "textDocument/definition")
    {
        if (inParams == nullptr) { SendError(inId, -32602, "Missing definition params"); return; }
        HandleDefinition(inId, *inParams);
        return;
    }

    if (inMethod == "textDocument/completion")
    {
        if (inParams == nullptr) { SendError(inId, -32602, "Missing completion params"); return; }
        HandleCompletion(inId, *inParams);
        return;
    }

    if (inMethod == "htn/compile")
    {
        if (inParams == nullptr) { SendError(inId, -32602, "Missing compile params"); return; }
        HandleCompile(inId, *inParams);
        return;
    }

    if (inMethod == "shutdown")
    {
        mShutdownRequested = true;
        SendResponse(inId, HTNLspJson(nullptr));
        return;
    }

    SendError(inId, -32601, "Method not found: " + inMethod);
}

void HTNLspServer::HandleNotification(
    const std::string& inMethod,
    const HTNLspJson* inParams)
{
    if (inMethod == "exit")
    {
        mExitRequested = true;
        return;
    }

    if (inMethod == "initialized")
        return;

    if (inParams == nullptr)
        return;

    if (inMethod == "textDocument/didOpen")
        HandleDidOpen(*inParams);
    else if (inMethod == "textDocument/didChange")
        HandleDidChange(*inParams);
    else if (inMethod == "textDocument/didClose")
        HandleDidClose(*inParams);
}

void HTNLspServer::HandleDidOpen(const HTNLspJson& inParams)
{
    const HTNLspJson* Uri = FindNested(inParams, { "textDocument", "uri" });
    const HTNLspJson* Text = FindNested(inParams, { "textDocument", "text" });
    const HTNLspJson* Version = FindNested(inParams, { "textDocument", "version" });

    if (Uri == nullptr || Text == nullptr || !Uri->IsString() || !Text->IsString())
        return;

    const std::uint64_t DocumentVersion =
        Version != nullptr && Version->IsInteger()
            ? static_cast<std::uint64_t>(std::max<std::int64_t>(0, Version->AsInteger()))
            : 0;

    const std::filesystem::path Path = UriToPath(Uri->AsString());
    mDocuments.OpenDocument(Path, Text->AsString(), DocumentVersion);
    PublishDiagnostics(Uri->AsString(), Path);
}

void HTNLspServer::HandleDidChange(const HTNLspJson& inParams)
{
    const HTNLspJson* Uri = FindNested(inParams, { "textDocument", "uri" });
    const HTNLspJson* Version = FindNested(inParams, { "textDocument", "version" });
    const HTNLspJson* Changes = inParams.Find("contentChanges");

    if (Uri == nullptr || !Uri->IsString() ||
        Changes == nullptr || !Changes->IsArray() ||
        Changes->AsArray().empty())
        return;

    // We advertise TextDocumentSyncKind::Full. VS Code therefore sends the complete
    // document in the first change entry.
    const HTNLspJson* Text = Changes->AsArray().front().Find("text");
    if (Text == nullptr || !Text->IsString())
        return;

    const std::uint64_t DocumentVersion =
        Version != nullptr && Version->IsInteger()
            ? static_cast<std::uint64_t>(std::max<std::int64_t>(0, Version->AsInteger()))
            : 0;

    const std::filesystem::path Path = UriToPath(Uri->AsString());
    if (!mDocuments.UpdateDocument(Path, Text->AsString(), DocumentVersion))
        mDocuments.OpenDocument(Path, Text->AsString(), DocumentVersion);

    PublishDiagnostics(Uri->AsString(), Path);
}

void HTNLspServer::HandleDidClose(const HTNLspJson& inParams)
{
    const HTNLspJson* Uri = FindNested(inParams, { "textDocument", "uri" });
    if (Uri == nullptr || !Uri->IsString())
        return;

    const std::filesystem::path Path = UriToPath(Uri->AsString());
    mDocuments.CloseDocument(Path);
    PublishEmptyDiagnostics(Uri->AsString());
}

void HTNLspServer::HandleDefinition(const HTNLspJson& inId, const HTNLspJson& inParams)
{
    const HTNLspJson* Uri = FindNested(inParams, { "textDocument", "uri" });
    const HTNLspJson* Line = FindNested(inParams, { "position", "line" });
    const HTNLspJson* Character = FindNested(inParams, { "position", "character" });
    if (!Uri || !Uri->IsString() || !Line || !Line->IsInteger() || !Character || !Character->IsInteger())
    {
        SendError(inId, -32602, "Invalid definition params");
        return;
    }

    const std::filesystem::path Path = UriToPath(Uri->AsString());
    const HTNDomainDocument* Document = mDocuments.GetDocument(Path);
    if (!Document)
    {
        SendResponse(inId, HTNLspJson(nullptr));
        return;
    }

    const size_t Offset = LspPositionToOffset(
        Document->Text,
        static_cast<int>(Line->AsInteger()),
        static_cast<int>(Character->AsInteger()));

    const HTNCompilerToolingModel* Model = mDocuments.GetToolingModel(Path);
    HTNCompilerToolingDefinition Definition;
    if (Model && Model->GetDefinitionAtOffset(Offset, Definition))
    {
        SendResponse(inId, HTNLspJson::Object{
            { "uri", PathToUri(Definition.FilePath) },
            { "range", HTNLspJson::Object{
                { "start", MakePosition(Definition.Range.Begin.Line, Definition.Range.Begin.Column) },
                { "end", MakePosition(Definition.Range.End.Line, Definition.Range.End.Column) }
            }}
        });
        return;
    }

    SendResponse(inId, HTNLspJson(nullptr));
}

void HTNLspServer::HandleCompletion(const HTNLspJson& inId, const HTNLspJson& inParams)
{
    const HTNLspJson* Uri = FindNested(inParams, { "textDocument", "uri" });
    const HTNLspJson* Line = FindNested(inParams, { "position", "line" });
    const HTNLspJson* Character = FindNested(inParams, { "position", "character" });
    if (!Uri || !Uri->IsString() || !Line || !Line->IsInteger() || !Character || !Character->IsInteger())
    {
        SendError(inId, -32602, "Invalid completion params");
        return;
    }

    const std::filesystem::path Path = UriToPath(Uri->AsString());
    const HTNDomainDocument* Document = mDocuments.GetDocument(Path);
    if (!Document)
    {
        SendResponse(inId, HTNLspJson::Array{});
        return;
    }

    const size_t Offset = LspPositionToOffset(
        Document->Text,
        static_cast<int>(Line->AsInteger()),
        static_cast<int>(Character->AsInteger()));

    HTNLspJson::Array Items;
    std::set<std::string> AddedCandidates;

    const HTNCompilerToolingModel* Model = mDocuments.GetToolingModel(Path);
    const std::vector<std::string> Candidates = Model
        ? Model->GetAutocompleteCandidates(Offset)
        : std::vector<std::string>{};
    for (const std::string& Candidate : Candidates)
    {
        if (!AddedCandidates.insert(Candidate).second)
            continue;

        Items.emplace_back(HTNLspJson::Object{
            { "label", Candidate },
            { "kind", static_cast<std::int64_t>(6) },
            { "insertText", Candidate }
        });
    }

    SendResponse(inId, HTNLspJson(std::move(Items)));
}

void HTNLspServer::HandleCompile(const HTNLspJson& inId, const HTNLspJson& inParams)
{
    const HTNLspJson* Uri = FindNested(inParams, { "textDocument", "uri" });
    if (Uri == nullptr || !Uri->IsString())
    {
        SendError(inId, -32602, "Invalid compile params");
        return;
    }

    const std::filesystem::path Path = UriToPath(Uri->AsString());
    const HTNDomainDocument* Document = mDocuments.GetDocument(Path);
    if (Document == nullptr)
    {
        SendResponse(inId, HTNLspJson::Object{
            { "success", false },
            { "message", "The active HTN document is not open in the language server." },
            { "diagnostics", HTNLspJson::Array{} }
        });
        return;
    }

    HTNCompilerDomainLoader Loader;
    HTNCompilerDomainLoadResult Result;
    HTNDiagnosticSink Diagnostics;

    // Compile exactly what the editor sees. Open/dirty include buffers win over disk.
    const HTNDomainSourceProvider SourceProvider =
        [this](const std::filesystem::path& inFilePath, std::string& outSourceText)
        {
            return mDocuments.Read(inFilePath, outSourceText);
        };

    const bool Success = Loader.LoadFromSource(
        Path.string(),
        Document->Text,
        SourceProvider,
        Result,
        Diagnostics);

    HTNLspJson::Array DiagnosticsJson;
    DiagnosticsJson.reserve(Diagnostics.GetDiagnostics().size());

    for (const HTNDiagnostic& Diagnostic : Diagnostics.GetDiagnostics())
    {
        HTNLspJson::Object DiagnosticJson =
            MakeCompilerDiagnosticFields(Diagnostic, "htn-compile");

        const std::filesystem::path DiagnosticPath =
            Diagnostic.FilePath.empty()
                ? Path
                : std::filesystem::path(Diagnostic.FilePath);

        DiagnosticJson.emplace("uri", PathToUri(DiagnosticPath));
        DiagnosticsJson.emplace_back(std::move(DiagnosticJson));
    }

    if (Success && !Diagnostics.HasErrors())
    {
        SendResponse(inId, HTNLspJson::Object{
            { "success", true },
            { "message", "Compile succeeded: " + Result.Domain.Id + " (" +
                std::to_string(Result.SourceFiles.size()) + " linked source file(s))" },
            { "diagnostics", HTNLspJson(std::move(DiagnosticsJson)) }
        });
        return;
    }

    const size_t ErrorCount = Diagnostics.GetErrorCount();
    SendResponse(inId, HTNLspJson::Object{
        { "success", false },
        { "message", "Compile failed with " + std::to_string(ErrorCount) + " error(s)." },
        { "diagnostics", HTNLspJson(std::move(DiagnosticsJson)) }
    });
}

void HTNLspServer::PublishDiagnostics(
    const std::string& inUri,
    const std::filesystem::path& inFilePath)
{
    HTNLspJson::Array DiagnosticsJson;

    const HTNDomainDocument* Document = mDocuments.GetDocument(inFilePath);
    if (Document != nullptr)
    {
        HTNCompilerDomainLoader Loader;
        HTNCompilerDomainLoadResult Result;
        HTNDiagnosticSink Diagnostics;

        const HTNDomainSourceProvider SourceProvider =
            [this](const std::filesystem::path& inPath, std::string& outSourceText)
            {
                return mDocuments.Read(inPath, outSourceText);
            };

        HTNDomainLoadOptions ToolingOptions;
        ToolingOptions.RequireTopLevelRoot = false;
        Loader.LoadFromSource(
            inFilePath.string(),
            Document->Text,
            SourceProvider,
            Result,
            Diagnostics,
            ToolingOptions);

        std::error_code Error;
        const std::filesystem::path CanonicalCurrent =
            std::filesystem::weakly_canonical(inFilePath, Error);
        const std::filesystem::path NormalizedCurrent =
            Error ? inFilePath.lexically_normal() : CanonicalCurrent;

        for (const HTNDiagnostic& Diagnostic : Diagnostics.GetDiagnostics())
        {
            const std::filesystem::path DiagnosticPath =
                Diagnostic.FilePath.empty()
                    ? inFilePath
                    : std::filesystem::path(Diagnostic.FilePath);

            Error.clear();
            const std::filesystem::path CanonicalDiagnostic =
                std::filesystem::weakly_canonical(DiagnosticPath, Error);
            const std::filesystem::path NormalizedDiagnostic =
                Error ? DiagnosticPath.lexically_normal() : CanonicalDiagnostic;

            // didOpen/didChange publishes diagnostics for this document only.
            // F7/htn/compile returns diagnostics for every linked source file, each with its URI.
            if (NormalizedDiagnostic != NormalizedCurrent)
                continue;

            DiagnosticsJson.emplace_back(
                HTNLspJson(MakeCompilerDiagnosticFields(Diagnostic, "htn")));
        }
    }

    SendNotification(
        "textDocument/publishDiagnostics",
        HTNLspJson::Object{
            { "uri", inUri },
            { "diagnostics", HTNLspJson(std::move(DiagnosticsJson)) }
        });
}

void HTNLspServer::PublishEmptyDiagnostics(const std::string& inUri)
{
    SendNotification(
        "textDocument/publishDiagnostics",
        HTNLspJson::Object{
            { "uri", inUri },
            { "diagnostics", HTNLspJson::Array{} }
        });
}

void HTNLspServer::SendResponse(
    const HTNLspJson& inId,
    HTNLspJson inResult)
{
    mTransport.WriteMessage(
        HTNLspJson(
            HTNLspJson::Object{
                { "jsonrpc", "2.0" },
                { "id", inId },
                { "result", std::move(inResult) }
            }).Serialize());
}

void HTNLspServer::SendError(
    const HTNLspJson& inId,
    const std::int64_t inCode,
    std::string inMessage)
{
    mTransport.WriteMessage(
        HTNLspJson(
            HTNLspJson::Object{
                { "jsonrpc", "2.0" },
                { "id", inId },
                { "error", HTNLspJson::Object{
                    { "code", inCode },
                    { "message", std::move(inMessage) }
                }}
            }).Serialize());
}

void HTNLspServer::SendNotification(
    std::string inMethod,
    HTNLspJson inParams)
{
    mTransport.WriteMessage(
        HTNLspJson(
            HTNLspJson::Object{
                { "jsonrpc", "2.0" },
                { "method", std::move(inMethod) },
                { "params", std::move(inParams) }
            }).Serialize());
}

std::filesystem::path HTNLspServer::UriToPath(const std::string& inUri)
{
    constexpr std::string_view FilePrefix = "file://";
    if (inUri.rfind(FilePrefix, 0) != 0)
        return std::filesystem::path(PercentDecode(inUri));

    std::string Path = PercentDecode(
        std::string_view(inUri).substr(FilePrefix.size()));

#ifdef _WIN32
    // file:///C:/... -> C:/...
    if (Path.size() >= 3 && Path[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(Path[1])) &&
        Path[2] == ':')
    {
        Path.erase(Path.begin());
    }
#endif

    return std::filesystem::path(Path);
}

std::string HTNLspServer::PathToUri(const std::filesystem::path& inPath)
{
    std::error_code Error;
    std::filesystem::path Absolute = std::filesystem::absolute(inPath, Error);
    if (Error) Absolute = inPath;
    std::string Path = Absolute.generic_string();
#ifdef _WIN32
    if (Path.size() >= 2 && std::isalpha(static_cast<unsigned char>(Path[0])) && Path[1] == ':') Path.insert(Path.begin(), '/');
#endif
    return "file://" + PercentEncodePath(Path);
}

std::string HTNLspServer::PercentEncodePath(std::string_view inText)
{
    constexpr char Hex[] = "0123456789ABCDEF";
    std::string Result;
    for (const unsigned char C : inText)
    {
        const bool Safe = std::isalnum(C) || C == '-' || C == '_' || C == '.' || C == '~' || C == '/' || C == ':';
        if (Safe) Result.push_back(static_cast<char>(C));
        else { Result.push_back('%'); Result.push_back(Hex[(C >> 4) & 0x0F]); Result.push_back(Hex[C & 0x0F]); }
    }
    return Result;
}

size_t HTNLspServer::LspPositionToOffset(const std::string& inText, const int inLineZeroBased, const int inCharacterUtf16)
{
    if (inLineZeroBased < 0 || inCharacterUtf16 < 0) return 0;
    size_t Offset = 0; int Line = 0;
    while (Offset < inText.size() && Line < inLineZeroBased) if (inText[Offset++] == '\n') ++Line;
    if (Line != inLineZeroBased) return inText.size();

    int Utf16Units = 0;
    while (Offset < inText.size() && inText[Offset] != '\n' && Utf16Units < inCharacterUtf16)
    {
        const unsigned char Lead = static_cast<unsigned char>(inText[Offset]);
        size_t ByteCount = 1; unsigned CodePoint = Lead;
        if ((Lead & 0xE0) == 0xC0 && Offset + 1 < inText.size()) { ByteCount = 2; CodePoint = ((Lead & 0x1F) << 6) | (static_cast<unsigned char>(inText[Offset+1]) & 0x3F); }
        else if ((Lead & 0xF0) == 0xE0 && Offset + 2 < inText.size()) { ByteCount = 3; CodePoint = ((Lead & 0x0F) << 12) | ((static_cast<unsigned char>(inText[Offset+1]) & 0x3F) << 6) | (static_cast<unsigned char>(inText[Offset+2]) & 0x3F); }
        else if ((Lead & 0xF8) == 0xF0 && Offset + 3 < inText.size()) { ByteCount = 4; CodePoint = ((Lead & 0x07) << 18) | ((static_cast<unsigned char>(inText[Offset+1]) & 0x3F) << 12) | ((static_cast<unsigned char>(inText[Offset+2]) & 0x3F) << 6) | (static_cast<unsigned char>(inText[Offset+3]) & 0x3F); }
        const int Units = CodePoint > 0xFFFF ? 2 : 1;
        if (Utf16Units + Units > inCharacterUtf16) break;
        Utf16Units += Units; Offset += ByteCount;
    }
    return Offset;
}

std::string HTNLspServer::PercentDecode(std::string_view inText)
{
    std::string Result;
    Result.reserve(inText.size());

    auto Hex = [](const char C) -> int
    {
        if (C >= '0' && C <= '9') return C - '0';
        if (C >= 'a' && C <= 'f') return C - 'a' + 10;
        if (C >= 'A' && C <= 'F') return C - 'A' + 10;
        return -1;
    };

    for (size_t I = 0; I < inText.size(); ++I)
    {
        if (inText[I] == '%' && I + 2 < inText.size())
        {
            const int Hi = Hex(inText[I + 1]);
            const int Lo = Hex(inText[I + 2]);
            if (Hi >= 0 && Lo >= 0)
            {
                Result.push_back(static_cast<char>((Hi << 4) | Lo));
                I += 2;
                continue;
            }
        }

        Result.push_back(inText[I]);
    }

    return Result;
}
