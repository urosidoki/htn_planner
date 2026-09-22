// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Core/HTNCallableSignature.h"
#include "Translator/HTNCompilerDomainLoader.h"
#include "Translator/HTNCompilerDomainSyntaxParser.h"
#include "Translator/HTNCompilerDomainValidator.h"

#include "Core/HTNFileReader.h"
#include "Domain/Source/HTNDomainFileSyntax.h"
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
               std::string& outError,
               const std::string& inIncludingFile = {},
               const HTNSourceRange& inIncludeRange = {})
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
            mDiagnostics.Error(inIncludingFile.empty() ? inPath.generic_string() : inIncludingFile,
                               outError, HTNDiagnosticRecovery::Fatal, inIncludeRange);
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
                mDiagnostics.Error(inIncludingFile.empty() ? inPath.generic_string() : inIncludingFile,
                               outError, HTNDiagnosticRecovery::Fatal, inIncludeRange);
                return false;
            }
        }
        std::vector<HTNDomainInclude> Includes;
        std::string DomainText;
        HTNParserError FileError;
        if (!HTNSplitDomainFile(Text, Includes, DomainText, FileError))
        {
            outError = FileError.Message;
            mDiagnostics.Error(inPath.generic_string(), outError, HTNDiagnosticRecovery::Fatal, FileError.Range);
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
        for (const HTNDomainInclude& Include : Includes)
        {
            std::filesystem::path Child(Include.Path);
            if (Child.is_relative()) Child = inPath.parent_path() / Child;
            if (!Visit(Child, false, outSources, outError, inPath.generic_string(), Include.Range)) return false;
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
            EffectiveAxioms[HTNCallableSignature(Axiom->Id, Axiom->Parameters.size())] = Axiom;
        for (const auto& Method : Module.Methods)
            EffectiveMethods[HTNCallableSignature(Method->Id, Method->Parameters.size())] = Method;
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
            if (EffectiveAxioms[HTNCallableSignature(Axiom->Id, Axiom->Parameters.size())] == Axiom)
                outResult.Domain.Axioms.push_back(Axiom);
    for (const AST::Domain& Module : Modules)
        for (const auto& Method : Module.Methods)
            if (EffectiveMethods[HTNCallableSignature(Method->Id, Method->Parameters.size())] == Method)
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
