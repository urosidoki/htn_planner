// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNAtomC.h"
#include "Domain/Source/HTNSourceText.h"
#include "HTNCoreMinimal.h"
#include "Translator/HTNGeneratedDebug.h"
#include "Translator/HTNCompilerOptions.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Internal, linked-domain representation used by the C compiler backend.
// Indices are stable within one compilation and never cross the planner ABI.
constexpr uint32 HTN_IR_NO_INDEX = 0xFFFFFFFFu;

enum class HTNIRValueKind : uint8_t
{
    Identifier,
    Literal,
    Variable,
    Constant,
    Call
};

enum class HTNIRListSplitOperation : uint32
{
    Split = 0,
    Front = 1,
    Back = 2
};

struct HTNIRSourceLocation
{
    uint32 FileIndex = 0u;
    HTNSourceRange Range;
};

struct HTNIRStringTable
{
    uint32 Add(const std::string& inValue)
    {
        const auto It = Indices.find(inValue);
        if (It != Indices.end()) return It->second;
        const uint32 Index = static_cast<uint32>(Values.size());
        Values.push_back(inValue);
        Indices.emplace(inValue, Index);
        return Index;
    }

    uint32 Find(const std::string& inValue) const
    {
        const auto It = Indices.find(inValue);
        return It == Indices.end() ? HTN_IR_NO_INDEX : It->second;
    }

    std::vector<std::string> Values;
    std::unordered_map<std::string, uint32> Indices;
};

struct HTNIRValue { HTNIRValueKind Kind=HTNIRValueKind::Literal; uint32 Text=0, DebugText=0, SourceLine=0, VariableSlot=HTN_IR_NO_INDEX, StaticValueIndex=HTN_IR_NO_INDEX; HTNIRSourceLocation Source; HTNAtomType AtomType=HTN_ATOM_TYPE_UNBOUND; int32 IntValue=0; float FloatValue=0.0f; uint32 BoolValue=0, ListElement=HTN_IR_NO_INDEX; bool DebugAsVariable=true; };
struct HTNIRStaticValue { uint32 Text=0; HTNAtomType AtomType=HTN_ATOM_TYPE_UNBOUND; int32 IntValue=0; float FloatValue=0.0f; uint32 BoolValue=0, ListElement=HTN_IR_NO_INDEX; };
struct HTNIRListElement { HTNAtomType AtomType=HTN_ATOM_TYPE_UNBOUND; uint32 Text=0; int32 IntValue=0; float FloatValue=0.0f; uint32 BoolValue=0, FirstChildRef=0, ChildCount=0; };
struct HTNIRCondition
{
    HTNGeneratedConditionKind Kind=HTN_CONDITION_FACT;
    uint32 Id=HTN_IR_NO_INDEX, FirstArgument=0, ArgumentCount=0, FirstChildRef=0, ChildCount=0, OutputValue=HTN_IR_NO_INDEX, ResolvedIndex=HTN_IR_NO_INDEX, SourceLine=0;
    std::string DomainExpression;
    HTNIRSourceLocation Source;
};
struct HTNIRTask
{
    HTNGeneratedTaskKind Kind=HTN_TASK_COMPOUND;
    uint32 Id=0, PlanStepHeadSymbolSlot=HTN_IR_NO_INDEX, FirstArgument=0, ArgumentCount=0, SourceLine=0;
    std::string DomainExpression;
    HTNIRSourceLocation Source;
};
struct HTNIRTaskCallExpression
{
    uint32 Id=HTN_IR_NO_INDEX, CallTermSlot=HTN_IR_NO_INDEX, OutputSlot=HTN_IR_NO_INDEX, SourceLine=0;
    std::string DomainExpression;
    std::vector<HTNIRValue> Arguments;
    HTNIRSourceLocation Source;
};
struct HTNIRBranch { uint32 Id=0, Condition=HTN_IR_NO_INDEX, FirstTask=0, TaskCount=0, SourceLine=0; HTNIRSourceLocation Source; };
struct HTNIRMethod
{
    uint32 Id=0, FirstParameter=0, ParameterCount=0, FirstBranch=0, BranchCount=0, IsTopLevel=0, IsExternallyDecomposable=0, SourceLine=0;
    std::array<uint64_t, HTN_GENERATED_VARIABLE_SLOT_MASK_WORDS> VariableSlotMask{};
    HTNIRSourceLocation Source;
};
struct HTNIRAxiom
{
    uint32 Id=0, FirstParameter=0, ParameterCount=0, Condition=HTN_IR_NO_INDEX, SourceLine=0;
    std::array<uint64_t, HTN_GENERATED_VARIABLE_SLOT_MASK_WORDS> VariableSlotMask{};
    HTNIRSourceLocation Source;
};
struct HTNIRConstant { uint32 GroupId=0, Id=0, Value=0, SourceLine=0; HTNIRSourceLocation Source; };

struct HTNCompilerIR
{
    std::string DomainId;
    std::vector<std::string> SourceFiles;
    uint32 FindPreparedSymbolSlot(uint32 inStringId) const
    {
        const auto It = PreparedSymbolSlotByStringId.find(inStringId);
        if (It != PreparedSymbolSlotByStringId.end()) return It->second;
        SetError("Generated symbol has no prepared-symbol slot");
        return HTN_IR_NO_INDEX;
    }

    int FindMethodByStringId(uint32 inStringId) const
    {
        for (size_t I = 0; I < Methods.size(); ++I)
            if (Methods[I].Id == inStringId) return static_cast<int>(I);
        return -1;
    }

    void MarkVariableSlot(std::array<uint64_t, HTN_GENERATED_VARIABLE_SLOT_MASK_WORDS>& ioMask,
                          const HTNIRValue& inValue) const
    {
        if (inValue.Kind == HTNIRValueKind::Variable && inValue.VariableSlot != HTN_IR_NO_INDEX)
            MarkVariableSlot(ioMask, inValue.VariableSlot);
    }

    void MarkVariableSlot(std::array<uint64_t, HTN_GENERATED_VARIABLE_SLOT_MASK_WORDS>& ioMask,
                          uint32 inVariableSlot) const
    {
        if (inVariableSlot != HTN_IR_NO_INDEX)
            ioMask[inVariableSlot >> 6u] |= (uint64_t{1} << (inVariableSlot & 63u));
    }

    void SetError(const std::string& inError) const { if (Error.empty()) Error = inError; }
    bool HasError() const { return !Error.empty(); }
    const std::string& GetError() const { return Error; }

    HTNGeneratedRuntimeBacktrackingSupport RuntimeBacktrackingSupport = HTNGeneratedRuntimeBacktrackingSupport::Disabled;
    HTNIRStringTable Strings;
    std::vector<HTNIRValue> Values;
    std::vector<HTNIRStaticValue> StaticValues;
    std::vector<HTNIRListElement> ListElements;
    std::vector<uint32> ListChildRefs;
    std::vector<uint32> VariableStringIds;
    std::unordered_map<uint32, uint32> VariableSlotByStringId;
    std::vector<uint32> PreparedSymbolStringIds;
    std::unordered_map<uint32, uint32> PreparedSymbolSlotByStringId;
    std::vector<HTNIRCondition> Conditions;
    std::vector<uint32> FactStringIds;
    std::unordered_map<uint32, uint32> FactSlotByStringId;
    std::vector<uint32> CallTermStringIds;
    std::unordered_map<uint32, uint32> CallTermSlotByStringId;
    std::vector<uint32> ConditionChildRefs;
    std::vector<HTNIRTask> Tasks;
    std::vector<std::vector<HTNIRTaskCallExpression>> TaskCallExpressions;
    uint32 SyntheticTaskCallCount = 0u;
    std::vector<HTNIRBranch> Branches;
    std::vector<HTNIRMethod> Methods;
    std::vector<HTNIRAxiom> Axioms;
    std::vector<HTNIRConstant> Constants;
    mutable std::string Error;
};
