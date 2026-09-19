// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Translator/HTNCompilerDomainLoader.h"
#include "Translator/HTNCompilerDomainSyntaxParser.h"
#include "Translator/HTNCompilerDomainValidator.h"

#include "Core/HTNFileReader.h"
#include "Domain/Diagnostics/HTNDiagnosticSink.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
std::string Trim(const std::string& inText)
{
    const size_t Begin = inText.find_first_not_of(" \t\r\n");
    if (Begin == std::string::npos) return {};
    const size_t End = inText.find_last_not_of(" \t\r\n");
    return inText.substr(Begin, End - Begin + 1);
}

bool SplitFile(const std::string& inText, std::vector<std::string>& outIncludes, std::string& outDomainText, std::string& outError)
{
    size_t Cursor = 0;
    // File-level grammar is deliberately strict: zero or more (:include "...") forms,
    // followed by exactly the normal (:domain ...) form. Whitespace/comments may precede forms.
    while (Cursor < inText.size())
    {
        while (Cursor < inText.size() && std::isspace(static_cast<unsigned char>(inText[Cursor]))) ++Cursor;
        if (Cursor + 1 < inText.size() && inText[Cursor] == '/' && inText[Cursor + 1] == '/')
        {
            const size_t Eol = inText.find('\n', Cursor);
            Cursor = Eol == std::string::npos ? inText.size() : Eol + 1;
            continue;
        }
        if (inText.compare(Cursor, 9, "(:include") != 0) break;
        size_t P = Cursor + 9;
        while (P < inText.size() && std::isspace(static_cast<unsigned char>(inText[P]))) ++P;
        if (P >= inText.size() || inText[P] != '"') { outError = "Expected quoted path after :include"; return false; }
        const size_t QuoteEnd = inText.find('"', P + 1);
        if (QuoteEnd == std::string::npos) { outError = "Unterminated :include path"; return false; }
        outIncludes.emplace_back(inText.substr(P + 1, QuoteEnd - P - 1));
        P = QuoteEnd + 1;
        while (P < inText.size() && std::isspace(static_cast<unsigned char>(inText[P]))) ++P;
        if (P >= inText.size() || inText[P] != ')') { outError = "Expected ')' after :include path"; return false; }
        Cursor = P + 1;
    }
    // Preserve original source coordinates for diagnostics. Characters belonging to
    // file-level include forms are replaced with spaces while line breaks are retained,
    // so parser token line/column positions still refer to the original file.
    outDomainText = inText;
    for (size_t Index = 0; Index < Cursor; ++Index)
    {
        if (outDomainText[Index] != '\n' && outDomainText[Index] != '\r')
            outDomainText[Index] = ' ';
    }
    if (Trim(outDomainText).empty()) { outError = "Missing :domain form"; return false; }
    // Includes inside/after the domain are intentionally illegal.
    if (outDomainText.find("(:include", Cursor) != std::string::npos)
    {
        outError = ":include directives are only allowed before :domain";
        return false;
    }
    return true;
}


std::string CanonicalKey(const std::filesystem::path& inPath)
{
    std::error_code Ec;
    const auto Canonical = std::filesystem::weakly_canonical(inPath, Ec);
    return (Ec ? inPath.lexically_normal() : Canonical).generic_string();
}
}

namespace
{
struct CompilerSourceCollection
{
    std::vector<std::string> SourceFiles;
    std::vector<std::string> SourceTexts;
    std::string LinkedSourceText;
};

class CompilerLoaderContext
{
public:
    CompilerLoaderContext(const std::string* inRootSourceText,
                          const HTNDomainSourceProvider& inSourceProvider,
                          HTNDiagnosticSink& outDiagnostics)
        : mRootSourceText(inRootSourceText)
        , mSourceProvider(inSourceProvider)
        , mDiagnostics(outDiagnostics)
    {
    }

    bool Visit(const std::filesystem::path& inPath,
               bool inIsRoot,
               CompilerSourceCollection& outSources,
               std::string& outError)
    {
        const std::string Key = CanonicalKey(inPath);
        if (mVisited.contains(Key)) return true;
        const auto StackIt = std::find(mStack.begin(), mStack.end(), Key);
        if (StackIt != mStack.end())
        {
            std::ostringstream Chain;
            Chain << "Circular domain dependency: ";
            for (auto It = StackIt; It != mStack.end(); ++It) Chain << *It << " -> ";
            Chain << Key;
            outError = Chain.str();
            mDiagnostics.Error(inPath.generic_string(), outError, HTNDiagnosticRecovery::Fatal);
            return false;
        }

        std::string Text;
        if (inIsRoot && mRootSourceText)
            Text = *mRootSourceText;
        else if (!mSourceProvider || !mSourceProvider(inPath, Text))
        {
            HTNFileReader Reader(inPath.string());
            if (!Reader.ReadFile(Text))
            {
                outError = "Could not read included domain '" + inPath.string() + "'";
                mDiagnostics.Error(inPath.generic_string(), outError, HTNDiagnosticRecovery::Fatal);
                return false;
            }
        }
        Text.erase(std::remove(Text.begin(), Text.end(), '\r'), Text.end());
        std::vector<std::string> Includes;
        std::string DomainText;
        if (!SplitFile(Text, Includes, DomainText, outError))
        {
            outError = inPath.string() + ": " + outError;
            mDiagnostics.Error(inPath.generic_string(), outError, HTNDiagnosticRecovery::Fatal);
            return false;
        }

        HTNCompilerAST::Domain Parsed;
        HTNSourceRange ErrorRange;
        if (!HTNParseCompilerDomainSyntax(DomainText, 0u, Parsed, outError, &ErrorRange,
                                          &mDiagnostics, inPath.generic_string()))
        {
            if (!mDiagnostics.HasErrors())
                mDiagnostics.Error(inPath.generic_string(), outError, HTNDiagnosticRecovery::Fatal, ErrorRange);
            return false;
        }
        if (!inIsRoot && Parsed.IsTopLevel)
        {
            outError = "Included domain '" + inPath.string() + "' cannot be top_level_domain";
            mDiagnostics.Error(inPath.generic_string(), outError, HTNDiagnosticRecovery::Recoverable,
                               Parsed.Range);
            return false;
        }

        mStack.push_back(Key);
        for (const std::string& Include : Includes)
        {
            std::filesystem::path Child(Include);
            if (Child.is_relative()) Child = inPath.parent_path() / Child;
            if (!Visit(Child, false, outSources, outError)) return false;
        }
        mStack.pop_back();

        outSources.SourceFiles.push_back(inPath.generic_string());
        outSources.SourceTexts.push_back(std::move(DomainText));
        outSources.LinkedSourceText += "\n// ---- linked source: " + inPath.generic_string() +
            " ----\n" + outSources.SourceTexts.back() + "\n";
        mVisited.insert(Key);
        return true;
    }

private:
    const std::string* mRootSourceText = nullptr;
    HTNDomainSourceProvider mSourceProvider;
    HTNDiagnosticSink& mDiagnostics;
    std::vector<std::string> mStack;
    std::unordered_set<std::string> mVisited;
};

bool BuildCompilerDomain(const CompilerSourceCollection& inLinked,
                         HTNCompilerDomainLoadResult& outResult,
                         HTNDiagnosticSink& outDiagnostics,
                         bool inRequireTopLevelRoot,
                         std::string& outError)
{
    namespace AST = HTNCompilerAST;
    std::vector<AST::Domain> Modules;
    Modules.reserve(inLinked.SourceFiles.size());
    for (uint32_t FileIndex = 0; FileIndex < inLinked.SourceFiles.size(); ++FileIndex)
    {
        const std::filesystem::path Path(inLinked.SourceFiles[FileIndex]);
        if (FileIndex >= inLinked.SourceTexts.size())
        {
            outError = "Compiler source text is missing for '" + Path.string() + "'";
            return false;
        }

        AST::Domain Module;
        HTNSourceRange ErrorRange;
        if (!HTNParseCompilerDomainSyntax(inLinked.SourceTexts[FileIndex], FileIndex,
                                          Module, outError, &ErrorRange,
                                          &outDiagnostics, Path.generic_string()))
        {
            if (!outDiagnostics.HasErrors())
                outDiagnostics.Error(Path.generic_string(), outError, HTNDiagnosticRecovery::Fatal, ErrorRange);
            return false;
        }
        Modules.push_back(std::move(Module));
    }
    if (Modules.empty())
    {
        outError = "Compiler domain has no source modules";
        return false;
    }
    if (!HTNValidateCompilerDomainModules(Modules, inLinked.SourceFiles,
                                          inRequireTopLevelRoot, outDiagnostics))
    {
        outError = "Compiler syntax validation failed";
        return false;
    }

    // The compiler AST validator has checked the override chains. Build the
    // effective declarations from the independent syntax in post-order.
    // This is also the order used by the existing linker for emitted symbols.
    std::unordered_map<std::string, std::shared_ptr<const AST::Constant>> EffectiveConstants;
    std::unordered_map<std::string, std::shared_ptr<const AST::Axiom>> EffectiveAxioms;
    std::unordered_map<std::string, std::shared_ptr<const AST::Method>> EffectiveMethods;
    for (const AST::Domain& Module : Modules)
    {
        for (const auto& Group : Module.ConstantGroups)
            for (const auto& Constant : Group->Constants)
                EffectiveConstants[Constant->Id] = Constant;
        for (const auto& Axiom : Module.Axioms)
            EffectiveAxioms[Axiom->Id] = Axiom;
        for (const auto& Method : Module.Methods)
            EffectiveMethods[Method->Id] = Method;
    }

    outResult.Domain.Id = Modules.back().Id;
    for (const AST::Domain& Module : Modules)
    {
        for (const auto& Group : Module.ConstantGroups)
        {
            auto EffectiveGroup = std::make_shared<AST::ConstantGroup>(*Group);
            EffectiveGroup->Constants.clear();
            for (const auto& Constant : Group->Constants)
                if (EffectiveConstants[Constant->Id] == Constant)
                    EffectiveGroup->Constants.push_back(Constant);
            if (!EffectiveGroup->Constants.empty())
                outResult.Domain.ConstantGroups.push_back(std::move(EffectiveGroup));
        }
        for (const auto& Axiom : Module.Axioms)
        {
            auto Qualified = std::make_shared<AST::Axiom>(*Axiom);
            Qualified->Id = Module.Id + "::" + Axiom->Id;
            outResult.Domain.Axioms.push_back(std::move(Qualified));
        }
        for (const auto& Method : Module.Methods)
        {
            auto Qualified = std::make_shared<AST::Method>(*Method);
            Qualified->Id = Module.Id + "::" + Method->Id;
            Qualified->TopLevel = false;
            outResult.Domain.Methods.push_back(std::move(Qualified));
        }
    }
    for (const AST::Domain& Module : Modules)
        for (const auto& Axiom : Module.Axioms)
            if (EffectiveAxioms[Axiom->Id] == Axiom)
                outResult.Domain.Axioms.push_back(Axiom);
    for (const AST::Domain& Module : Modules)
        for (const auto& Method : Module.Methods)
            if (EffectiveMethods[Method->Id] == Method)
                outResult.Domain.Methods.push_back(Method);

    outResult.LinkedSourceText = inLinked.LinkedSourceText;
    outResult.SourceFiles = inLinked.SourceFiles;
    return true;
}
}

bool HTNCompilerDomainLoader::Load(
    const std::string& inRootFilePath,
    HTNCompilerDomainLoadResult& outResult,
    HTNDiagnosticSink& outDiagnostics,
    const HTNDomainLoadOptions& inOptions) const
{
    outResult = {};
    outDiagnostics.Clear();
    CompilerSourceCollection Sources;
    std::string Error;
    CompilerLoaderContext Context(nullptr, {}, outDiagnostics);
    if (Context.Visit(std::filesystem::path(inRootFilePath), true, Sources, Error) &&
        BuildCompilerDomain(Sources, outResult, outDiagnostics, inOptions.RequireTopLevelRoot, Error))
        return true;

    if (!outDiagnostics.HasErrors())
        outDiagnostics.Error(inRootFilePath, Error, HTNDiagnosticRecovery::Fatal);
    return false;
}

bool HTNCompilerDomainLoader::LoadFromSource(
    const std::string& inRootFilePath,
    const std::string& inRootSourceText,
    const HTNDomainSourceProvider& inSourceProvider,
    HTNCompilerDomainLoadResult& outResult,
    HTNDiagnosticSink& outDiagnostics,
    const HTNDomainLoadOptions& inOptions) const
{
    outResult = {};
    outDiagnostics.Clear();
    CompilerSourceCollection Sources;
    std::string Error;
    CompilerLoaderContext Context(&inRootSourceText, inSourceProvider, outDiagnostics);
    if (Context.Visit(std::filesystem::path(inRootFilePath), true, Sources, Error) &&
        BuildCompilerDomain(Sources, outResult, outDiagnostics, inOptions.RequireTopLevelRoot, Error))
        return true;

    if (!outDiagnostics.HasErrors())
        outDiagnostics.Error(inRootFilePath, Error, HTNDiagnosticRecovery::Fatal);
    return false;
}
