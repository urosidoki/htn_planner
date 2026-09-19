// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Translator/HTNCompilerToolingModel.h"

#include "Core/HTNAtom.h"
#include "Domain/Diagnostics/HTNDiagnosticSink.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_set>

namespace
{
bool SamePath(const std::filesystem::path& inA, const std::filesystem::path& inB)
{
    std::error_code ErrorA;
    std::error_code ErrorB;
    const auto CanonicalA = std::filesystem::weakly_canonical(inA, ErrorA);
    const auto CanonicalB = std::filesystem::weakly_canonical(inB, ErrorB);
    return (ErrorA ? inA.lexically_normal() : CanonicalA) ==
           (ErrorB ? inB.lexically_normal() : CanonicalB);
}

bool Contains(const HTNSourceRange& inRange, const size_t inOffset)
{
    return inOffset >= inRange.Begin.Offset && inOffset <= inRange.End.Offset;
}

std::string ValueText(const HTNCompilerAST::ValuePtr& inValue)
{
    return inValue ? HTNAtomToString(inValue->GetValue(), false) : std::string{};
}

bool FindAtom(const std::string& inText, size_t inOffset, size_t& outBegin, size_t& outEnd)
{
    const auto IsAtom = [](const char Character)
    {
        return std::isalnum(static_cast<unsigned char>(Character)) ||
               Character == '_' || Character == '-' || Character == '?' ||
               Character == '@' || Character == ':' || Character == '#';
    };
    if (inText.empty()) return false;
    inOffset = std::min(inOffset, inText.size());
    size_t Probe = inOffset;
    if (Probe == inText.size() || !IsAtom(inText[Probe]))
    {
        if (Probe == 0 || !IsAtom(inText[Probe - 1])) return false;
        --Probe;
    }
    outBegin = Probe;
    while (outBegin > 0 && IsAtom(inText[outBegin - 1])) --outBegin;
    outEnd = Probe + 1;
    while (outEnd < inText.size() && IsAtom(inText[outEnd])) ++outEnd;
    return true;
}

void AddVariables(const HTNCompilerAST::ConditionPtr& inCondition,
                  const size_t inCursorOffset,
                  std::set<std::string>& ioVariables)
{
    if (!inCondition || inCondition->Range.Begin.Offset >= inCursorOffset) return;
    if (inCondition->Kind == HTNCompilerAST::ConditionKind::Not) return;

    for (const auto& Argument : inCondition->Arguments)
        if (Argument && Argument->Kind == HTNCompilerAST::ValueKind::Variable &&
            Argument->Range.Begin.Offset < inCursorOffset)
            ioVariables.insert("?" + ValueText(Argument));

    if (inCondition->Output && inCondition->Output->Range.Begin.Offset < inCursorOffset)
        ioVariables.insert("?" + ValueText(inCondition->Output));

    for (const auto& Child : inCondition->Children)
        AddVariables(Child, inCursorOffset, ioVariables);
}
}

void HTNCompilerToolingModel::Analyze(const std::filesystem::path& inFilePath,
                                      const std::string& inText,
                                      const HTNDomainSourceProvider& inSourceProvider)
{
    mFilePath = inFilePath;
    mText = inText;
    mResult = {};
    HTNDiagnosticSink Diagnostics;
    HTNDomainLoadOptions Options;
    Options.RequireTopLevelRoot = false;
    const HTNCompilerDomainLoader Loader;
    mLoaded = Loader.LoadFromSource(inFilePath.string(), inText, inSourceProvider,
                                    mResult, Diagnostics, Options);
    mDiagnostics = Diagnostics.GetDiagnostics();
}

int HTNCompilerToolingModel::FindCurrentFileIndex() const
{
    for (size_t Index = 0; Index < mResult.SourceFiles.size(); ++Index)
        if (SamePath(mResult.SourceFiles[Index], mFilePath))
            return static_cast<int>(Index);
    return -1;
}

std::vector<std::string> HTNCompilerToolingModel::GetAutocompleteCandidates(
    const size_t inCursorOffset) const
{
    const int FileIndex = FindCurrentFileIndex();
    if (FileIndex < 0) return {};
    std::set<std::string> Variables;
    for (const auto& Method : mResult.Domain.Methods)
    {
        if (!Method || Method->FileIndex != static_cast<uint32_t>(FileIndex) ||
            !Contains(Method->Range, inCursorOffset)) continue;
        for (const auto& Parameter : Method->Parameters)
            Variables.insert("?" + ValueText(Parameter));
        for (const auto& Branch : Method->Branches)
            if (Branch && Contains(Branch->Range, inCursorOffset))
            {
                AddVariables(Branch->Precondition, inCursorOffset, Variables);
                break;
            }
        break;
    }
    return { Variables.begin(), Variables.end() };
}

bool HTNCompilerToolingModel::GetDefinitionAtOffset(
    const size_t inCursorOffset, HTNCompilerToolingDefinition& outDefinition) const
{
    size_t Begin = 0;
    size_t End = 0;
    if (!FindAtom(mText, inCursorOffset, Begin, End)) return false;
    const std::string Symbol = mText.substr(Begin, End - Begin);

    if (!Symbol.empty() && Symbol[0] == '@')
    {
        const std::string Id = Symbol.substr(1);
        for (const auto& Group : mResult.Domain.ConstantGroups)
            for (const auto& Constant : Group->Constants)
                if (Constant->Id == Id && Constant->FileIndex < mResult.SourceFiles.size())
                {
                    outDefinition = { mResult.SourceFiles[Constant->FileIndex], Constant->Range };
                    return true;
                }
        return false;
    }

    for (const auto& Method : mResult.Domain.Methods)
    {
        if (!Method || Method->FileIndex >= mResult.SourceFiles.size()) continue;
        const size_t Separator = Method->Id.find("::");
        const std::string Unqualified = Separator == std::string::npos
            ? Method->Id : Method->Id.substr(Separator + 2);
        if (Method->Id == Symbol || (Symbol.find("::") == std::string::npos && Unqualified == Symbol))
        {
            outDefinition = { mResult.SourceFiles[Method->FileIndex], Method->Range };
            return true;
        }
    }
    return false;
}

HTNCompilerToolingTokenKind HTNCompilerToolingModel::GetTokenKind(const size_t inOffset) const
{
    size_t Begin = 0;
    size_t End = 0;
    if (!FindAtom(mText, inOffset, Begin, End)) return HTNCompilerToolingTokenKind::None;
    const std::string Symbol = mText.substr(Begin, End - Begin);
    if (!Symbol.empty() && Symbol[0] == '?')
    {
        const auto Candidates = GetAutocompleteCandidates(Begin);
        return std::find(Candidates.begin(), Candidates.end(), Symbol) != Candidates.end()
            ? HTNCompilerToolingTokenKind::VariableValid
            : HTNCompilerToolingTokenKind::VariableInvalid;
    }
    HTNCompilerToolingDefinition Definition;
    if (!Symbol.empty() && Symbol[0] == '@')
        return GetDefinitionAtOffset(Begin, Definition)
            ? HTNCompilerToolingTokenKind::ConstantValid
            : HTNCompilerToolingTokenKind::ConstantInvalid;
    if (GetDefinitionAtOffset(Begin, Definition)) return HTNCompilerToolingTokenKind::MethodValid;
    if (Symbol.find("::") != std::string::npos) return HTNCompilerToolingTokenKind::MethodInvalid;
    return HTNCompilerToolingTokenKind::None;
}
