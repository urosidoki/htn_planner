// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Translator/HTNCCodeGenerator.h"
#include "Translator/HTNGeneratedPlanner.h"
#include "Translator/HTNGeneratedBacktracking.h"
#include "Translator/HTNGeneratedProfiling.h"
#include "Translator/HTNGeneratedDebug.h"
#include "Core/HtnSymbolGenerated.h"
#include "WorldState/HTNGeneratedWorldState.h"
#include "Translator/HTNCallTermBridge.h"
#include "Translator/HTNCompilerIRBuilder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
constexpr uint32 kNoIndex = 0xFFFFFFFFu;

using ValueRecord = HTNIRValue;
using StaticValueRecord = HTNIRStaticValue;
using ListElementRecord = HTNIRListElement;
using ConditionRecord = HTNIRCondition;
using TaskRecord = HTNIRTask;
using TaskCallExpressionRecord = HTNIRTaskCallExpression;
using BranchRecord = HTNIRBranch;
using MethodRecord = HTNIRMethod;
using AxiomRecord = HTNIRAxiom;
using ConstantRecord = HTNIRConstant;

std::string EscapeCString(const std::string& inValue)
{
    std::ostringstream Result;
    for (const unsigned char Character : inValue)
    {
        switch (Character)
        {
        case '\\': Result << "\\\\"; break;
        case '"': Result << "\\\""; break;
        case '\n': Result << "\\n"; break;
        case '\r': Result << "\\r"; break;
        case '\t': Result << "\\t"; break;
        default:
            if (Character >= 32 && Character < 127) Result << static_cast<char>(Character);
            else
            {
                const char Hex[] = "0123456789ABCDEF";
                Result << "\\x" << Hex[(Character >> 4) & 0xF] << Hex[Character & 0xF];
            }
            break;
        }
    }
    return Result.str();
}

std::string MakeIdentifier(const std::string& inValue)
{
    std::string Result;
    Result.reserve(inValue.size()+1);
    for (const unsigned char Character : inValue)
        Result.push_back(std::isalnum(Character) ? static_cast<char>(std::toupper(Character)) : '_');
    if (Result.empty() || std::isdigit(static_cast<unsigned char>(Result.front()))) Result.insert(Result.begin(), '_');
    return Result;
}


const char* ConditionKindCName(const HTNGeneratedConditionKind inKind)
{
    switch (inKind)
    {
    case HTN_CONDITION_FACT: return "HTN_CONDITION_FACT";
    case HTN_CONDITION_AXIOM: return "HTN_CONDITION_AXIOM";
    case HTN_CONDITION_AND: return "HTN_CONDITION_AND";
    case HTN_CONDITION_OR: return "HTN_CONDITION_OR";
    case HTN_CONDITION_ALT: return "HTN_CONDITION_ALT";
    case HTN_CONDITION_NOT: return "HTN_CONDITION_NOT";
    case HTN_CONDITION_CALL: return "HTN_CONDITION_CALL";
    case HTN_CONDITION_CALL_BIND: return "HTN_CONDITION_CALL_BIND";
    case HTN_CONDITION_BUILTIN_COMPARISON: return "HTN_CONDITION_BUILTIN_COMPARISON";
    case HTN_CONDITION_BUILTIN_LIST_SPLIT: return "HTN_CONDITION_BUILTIN_LIST_SPLIT";
    default: return "HTN_CONDITION_INVALID";
    }
}

const char* TaskKindCName(const HTNGeneratedTaskKind inKind)
{
    switch (inKind)
    {
    case HTN_TASK_COMPOUND: return "HTN_TASK_COMPOUND";
    case HTN_TASK_PRIMITIVE: return "HTN_TASK_PRIMITIVE";
    case HTN_TASK_DEFERRED: return "HTN_TASK_DEFERRED";
    default: return "HTN_TASK_INVALID";
    }
}

const char* BuiltinComparisonOperatorCName(const uint32 inOperator)
{
    switch (inOperator)
    {
    case HTN_BUILTIN_COMPARE_EQUAL: return "HTN_BUILTIN_COMPARE_EQUAL";
    case HTN_BUILTIN_COMPARE_NOT_EQUAL: return "HTN_BUILTIN_COMPARE_NOT_EQUAL";
    case HTN_BUILTIN_COMPARE_LESS: return "HTN_BUILTIN_COMPARE_LESS";
    case HTN_BUILTIN_COMPARE_LESS_EQUAL: return "HTN_BUILTIN_COMPARE_LESS_EQUAL";
    case HTN_BUILTIN_COMPARE_GREATER: return "HTN_BUILTIN_COMPARE_GREATER";
    case HTN_BUILTIN_COMPARE_GREATER_EQUAL: return "HTN_BUILTIN_COMPARE_GREATER_EQUAL";
    default: return "HTN_BUILTIN_COMPARE_INVALID";
    }
}


class CodeWriter
{
public:
    uint32 NewLabel() { return NextLabel++; }
    std::string Label(uint32 inLabel) const { return "__label" + std::to_string(inLabel); }
    void DomainExpressionComment(const std::string& inExpression, const char* inIndent = "    ")
    {
        if (inExpression.empty())
            return;
        Out << inIndent << "// ";
        for (const char Character : inExpression)
        {
            if (Character == '\n') Out << "\\n";
            else if (Character == '\r') Out << "\\r";
            else Out << Character;
        }
        Out << "\n";
    }
    std::ostringstream Out;
    uint32 NextLabel=1;
};

using BoundVariableSet = std::unordered_set<uint32>;

struct ConditionAnalysis
{
    BoundVariableSet BoundAfter;
    bool MayProduceMultipleSolutions = false;
    bool MayBindVariables = false;
    bool HasSideEffects = false;
    bool CanLowerToCFG = true;
};

const AxiomRecord* FindAxiomRecord(const HTNCompilerIR& inBuilder, const uint32 inId)
{
    for (const AxiomRecord& Axiom : inBuilder.Axioms)
        if (Axiom.Id == inId)
            return &Axiom;
    return nullptr;
}

bool IsOutputParameterName(const std::string& inName)
{
    return inName.rfind("out_", 0) == 0 || inName.rfind("io_", 0) == 0;
}

ConditionAnalysis AnalyzeCondition(const HTNCompilerIR& inBuilder, const uint32 inConditionIndex,
                                   const BoundVariableSet& inBound, const bool inExpandAxiomBodies = true,
                                   const uint32 inAxiomExpansionDepth = 0u)
{
    ConditionAnalysis Result;
    Result.BoundAfter = inBound;
    if (inConditionIndex == kNoIndex || inConditionIndex >= inBuilder.Conditions.size())
        return Result;

    const ConditionRecord& Condition = inBuilder.Conditions[inConditionIndex];
    switch (Condition.Kind)
    {
    case HTN_CONDITION_FACT:
        for (uint32 I = 0; I < Condition.ArgumentCount; ++I)
        {
            const ValueRecord& Value = inBuilder.Values[Condition.FirstArgument + I];
            if (Value.Kind != HTNIRValueKind::Variable || Value.VariableSlot == kNoIndex)
                continue;
            if (Result.BoundAfter.find(Value.Text) == Result.BoundAfter.end())
            {
                Result.MayProduceMultipleSolutions = true;
                Result.MayBindVariables = true;
                Result.BoundAfter.insert(Value.Text);
            }
        }
        break;

    case HTN_CONDITION_AXIOM:
    {
        // Axiom calls can be lowered when their body is deterministic under
        // source-order semantics. Nested axiom calls remain
        // conservative to avoid recursive compile-time expansion.
        Result.MayProduceMultipleSolutions = true;
        Result.HasSideEffects = true;
        Result.CanLowerToCFG = false;
        const AxiomRecord* Axiom = FindAxiomRecord(inBuilder, Condition.Id);
        if (Axiom)
        {
            const uint32 Count = std::min(Axiom->ParameterCount, Condition.ArgumentCount);
            for (uint32 I = 0; I < Count; ++I)
            {
                const ValueRecord& Parameter = inBuilder.Values[Axiom->FirstParameter + I];
                if (Parameter.Text >= inBuilder.Strings.Values.size() ||
                    !IsOutputParameterName(inBuilder.Strings.Values[Parameter.Text]))
                    continue;
                const ValueRecord& Caller = inBuilder.Values[Condition.FirstArgument + I];
                if (Caller.Kind == HTNIRValueKind::Variable)
                {
                    Result.MayBindVariables = true;
                    Result.BoundAfter.insert(Caller.Text);
                }
            }

            if (Axiom->Condition == kNoIndex)
            {
                // An axiom with an empty body is the logical identity (true).
                // It is deterministic and can be lowered entirely by generated
                // axiom scope control flow emitted directly by the generator.
                Result.MayProduceMultipleSolutions = false;
                Result.HasSideEffects = false;
                Result.CanLowerToCFG = true;
            }
            else if (inExpandAxiomBodies && inAxiomExpansionDepth < inBuilder.Axioms.size())
            {
                BoundVariableSet AxiomBound;
                for (uint32 I = 0; I < Axiom->ParameterCount; ++I)
                {
                    const ValueRecord& Parameter = inBuilder.Values[Axiom->FirstParameter + I];
                    if (Parameter.Kind != HTNIRValueKind::Variable || Parameter.Text >= inBuilder.Strings.Values.size())
                        continue;
                    const std::string& Name = inBuilder.Strings.Values[Parameter.Text];
                    if (Name.rfind("out_", 0) != 0)
                        AxiomBound.insert(Parameter.Text);
                }
                // The linked domain has already resolved exact qualified implementations.
                // Expand nested deterministic axioms as well so an override can call its
                // base implementation (#Domain::axiom) without falling back to a generic
                // generic evaluator. The depth bound makes direct generator use robust even
                // if it receives an invalid cyclic domain outside the normal linker path.
                const ConditionAnalysis Body = AnalyzeCondition(
                    inBuilder, Axiom->Condition, AxiomBound, true, inAxiomExpansionDepth + 1u);
                Result.MayProduceMultipleSolutions = Body.MayProduceMultipleSolutions;
                Result.HasSideEffects = Body.HasSideEffects;
                Result.CanLowerToCFG = Body.CanLowerToCFG && !Body.MayProduceMultipleSolutions;
            }
        }
        break;
    }

    case HTN_CONDITION_CALL:
        Result.HasSideEffects = true;
        break;

    case HTN_CONDITION_CALL_BIND:
        Result.HasSideEffects = true;
        Result.MayBindVariables = true;
        if (Condition.OutputValue != kNoIndex && Condition.OutputValue < inBuilder.Values.size())
        {
            const ValueRecord& Output = inBuilder.Values[Condition.OutputValue];
            if (Output.Kind == HTNIRValueKind::Variable)
                Result.BoundAfter.insert(Output.Text);
        }
        break;

    case HTN_CONDITION_BUILTIN_LIST_SPLIT:
        Result.MayBindVariables = true;
        for (uint32 I = 1u; I < Condition.ArgumentCount; ++I)
        {
            const ValueRecord& Output = inBuilder.Values[Condition.FirstArgument + I];
            if (Output.Kind == HTNIRValueKind::Variable)
                Result.BoundAfter.insert(Output.Text);
        }
        break;

    case HTN_CONDITION_AND:
    {
        BoundVariableSet Current = inBound;
        bool EarlierChoice = false;
        for (uint32 I = 0; I < Condition.ChildCount; ++I)
        {
            const uint32 Ref = Condition.FirstChildRef + I;
            if (Ref >= inBuilder.ConditionChildRefs.size())
            {
                Result.CanLowerToCFG = false;
                break;
            }
            const ConditionAnalysis Child = AnalyzeCondition(inBuilder, inBuilder.ConditionChildRefs[Ref], Current, inExpandAxiomBodies, inAxiomExpansionDepth);
            if (I + 1u < Condition.ChildCount && Child.MayProduceMultipleSolutions)
                EarlierChoice = true;
            Result.MayProduceMultipleSolutions |= Child.MayProduceMultipleSolutions;
            Result.MayBindVariables |= Child.MayBindVariables;
            Result.HasSideEffects |= Child.HasSideEffects;
            Current = Child.BoundAfter;
        }
        Result.BoundAfter = std::move(Current);
        // Backtracking across earlier choice-producing children is emitted
        // explicitly as generated retry labels.
        (void)EarlierChoice;
        Result.CanLowerToCFG = Result.CanLowerToCFG;
        break;
    }

    case HTN_CONDITION_OR:
    {
        bool First = true;
        BoundVariableSet Intersection;
        bool AnyMayBind = false;
        bool AnySideEffect = false;
        for (uint32 I = 0; I < Condition.ChildCount; ++I)
        {
            const uint32 Ref = Condition.FirstChildRef + I;
            if (Ref >= inBuilder.ConditionChildRefs.size())
                continue;
            const ConditionAnalysis Child = AnalyzeCondition(inBuilder, inBuilder.ConditionChildRefs[Ref], inBound, inExpandAxiomBodies, inAxiomExpansionDepth);
            AnyMayBind |= Child.MayBindVariables;
            AnySideEffect |= Child.HasSideEffects;
            if (First)
            {
                Intersection = Child.BoundAfter;
                First = false;
            }
            else
            {
                for (auto It = Intersection.begin(); It != Intersection.end(); )
                {
                    if (Child.BoundAfter.find(*It) == Child.BoundAfter.end()) It = Intersection.erase(It);
                    else ++It;
                }
            }
        }
        Result.BoundAfter = First ? inBound : std::move(Intersection);
        Result.MayProduceMultipleSolutions = false; // OR exports only its first successful solution
        Result.MayBindVariables = AnyMayBind;
        Result.HasSideEffects = AnySideEffect;
        Result.CanLowerToCFG = true;
        break;
    }

    case HTN_CONDITION_ALT:
    {
        bool PureNoBind = true;
        for (uint32 I = 0; I < Condition.ChildCount; ++I)
        {
            const uint32 Ref = Condition.FirstChildRef + I;
            if (Ref >= inBuilder.ConditionChildRefs.size())
            {
                PureNoBind = false;
                continue;
            }
            const ConditionAnalysis Child = AnalyzeCondition(inBuilder, inBuilder.ConditionChildRefs[Ref], inBound, inExpandAxiomBodies, inAxiomExpansionDepth);
            if (Child.MayBindVariables || Child.HasSideEffects || Child.BoundAfter != inBound)
                PureNoBind = false;
        }
        // A pure ALT whose alternatives cannot change bindings is equivalent
        // to boolean source-order alternatives; preserving several identical
        // environments cannot affect later terms.
        Result.MayProduceMultipleSolutions = !PureNoBind;
        Result.CanLowerToCFG = PureNoBind;
        Result.BoundAfter = inBound;
        break;
    }

    case HTN_CONDITION_NOT:
    {
        if (Condition.ChildCount != 0u)
        {
            const uint32 Ref = Condition.FirstChildRef;
            if (Ref < inBuilder.ConditionChildRefs.size())
            {
                const ConditionAnalysis Child = AnalyzeCondition(inBuilder, inBuilder.ConditionChildRefs[Ref], inBound, inExpandAxiomBodies, inAxiomExpansionDepth);
                Result.HasSideEffects = Child.HasSideEffects; // WorldState effects intentionally persist
            }
        }
        Result.BoundAfter = inBound;
        Result.MayProduceMultipleSolutions = false;
        Result.MayBindVariables = false;
        Result.CanLowerToCFG = true;
        break;
    }
    default:
        Result.CanLowerToCFG = false;
        break;
    }
    return Result;
}

void CollectGeneratedChoiceCursors(const HTNCompilerIR& inBuilder, const uint32 inConditionIndex,
                                   const BoundVariableSet& inBound,
                                   std::vector<uint32>& outFactCursors,
                                   std::vector<uint32>& outAxiomCursors,
                                   const uint32 inAxiomExpansionDepth = 0u)
{
    if (inConditionIndex == kNoIndex || inConditionIndex >= inBuilder.Conditions.size())
        return;

    const ConditionRecord& Condition = inBuilder.Conditions[inConditionIndex];
    const ConditionAnalysis Analysis = AnalyzeCondition(inBuilder, inConditionIndex, inBound);

    if (Condition.Kind == HTN_CONDITION_AXIOM && Analysis.CanLowerToCFG)
    {
        const AxiomRecord* Axiom = FindAxiomRecord(inBuilder, Condition.Id);
        if (!Axiom || Axiom->Condition == kNoIndex || inAxiomExpansionDepth >= inBuilder.Axioms.size())
            return;

        BoundVariableSet AxiomBound;
        for (uint32 I = 0u; I < Axiom->ParameterCount; ++I)
        {
            const ValueRecord& Parameter = inBuilder.Values[Axiom->FirstParameter + I];
            if (Parameter.Kind != HTNIRValueKind::Variable || Parameter.Text >= inBuilder.Strings.Values.size())
                continue;
            if (inBuilder.Strings.Values[Parameter.Text].rfind("out_", 0u) != 0u)
                AxiomBound.insert(Parameter.Text);
        }
        CollectGeneratedChoiceCursors(inBuilder, Axiom->Condition, AxiomBound,
                                      outFactCursors, outAxiomCursors, inAxiomExpansionDepth + 1u);
        return;
    }

    if (Condition.Kind == HTN_CONDITION_AND)
    {
        BoundVariableSet Current = inBound;
        for (uint32 I = 0u; I < Condition.ChildCount; ++I)
        {
            const uint32 Ref = Condition.FirstChildRef + I;
            if (Ref >= inBuilder.ConditionChildRefs.size())
                return;

            const uint32 ChildIndex = inBuilder.ConditionChildRefs[Ref];
            if (ChildIndex >= inBuilder.Conditions.size())
                return;

            const ConditionRecord& Child = inBuilder.Conditions[ChildIndex];
            const ConditionAnalysis ChildAnalysis = AnalyzeCondition(inBuilder, ChildIndex, Current);
            if (ChildAnalysis.MayProduceMultipleSolutions && Child.Kind == HTN_CONDITION_FACT)
            {
                if (std::find(outFactCursors.begin(), outFactCursors.end(), ChildIndex) == outFactCursors.end())
                    outFactCursors.push_back(ChildIndex);
            }
            else if (ChildAnalysis.MayProduceMultipleSolutions && Child.Kind == HTN_CONDITION_AXIOM)
            {
                if (std::find(outAxiomCursors.begin(), outAxiomCursors.end(), ChildIndex) == outAxiomCursors.end())
                    outAxiomCursors.push_back(ChildIndex);
            }
            else
            {
                CollectGeneratedChoiceCursors(inBuilder, ChildIndex, Current,
                                              outFactCursors, outAxiomCursors, inAxiomExpansionDepth);
            }
            Current = ChildAnalysis.BoundAfter;
        }
        return;
    }

    if (Condition.Kind == HTN_CONDITION_OR || Condition.Kind == HTN_CONDITION_ALT ||
        Condition.Kind == HTN_CONDITION_NOT)
    {
        for (uint32 I = 0u; I < Condition.ChildCount; ++I)
        {
            const uint32 Ref = Condition.FirstChildRef + I;
            if (Ref < inBuilder.ConditionChildRefs.size())
                CollectGeneratedChoiceCursors(inBuilder, inBuilder.ConditionChildRefs[Ref], inBound,
                                              outFactCursors, outAxiomCursors, inAxiomExpansionDepth);
        }
    }
}

using GeneratedVariableWriteMap = std::unordered_map<uint32, uint32>;

void AddGeneratedVariableWrite(const ValueRecord& inValue, GeneratedVariableWriteMap& outWrites)
{
    if (inValue.Kind == HTNIRValueKind::Variable && inValue.VariableSlot != kNoIndex)
        outWrites.emplace(inValue.Text, inValue.VariableSlot);
}

void CollectGeneratedConditionWrites(const HTNCompilerIR& inBuilder,
                                     const uint32 inConditionIndex,
                                     const BoundVariableSet& inBound,
                                     GeneratedVariableWriteMap& outWrites)
{
    if (inConditionIndex == kNoIndex || inConditionIndex >= inBuilder.Conditions.size())
        return;

    const ConditionRecord& Condition = inBuilder.Conditions[inConditionIndex];
    switch (Condition.Kind)
    {
    case HTN_CONDITION_FACT:
        for (uint32 I = 0u; I < Condition.ArgumentCount; ++I)
        {
            const ValueRecord& Value = inBuilder.Values[Condition.FirstArgument + I];
            if (Value.Kind == HTNIRValueKind::Variable &&
                Value.VariableSlot != kNoIndex &&
                inBound.find(Value.Text) == inBound.end())
            {
                AddGeneratedVariableWrite(Value, outWrites);
            }
        }
        break;

    case HTN_CONDITION_BUILTIN_LIST_SPLIT:
        for (uint32 I = 1u; I < Condition.ArgumentCount; ++I)
        {
            const ValueRecord& Output = inBuilder.Values[Condition.FirstArgument + I];
            if (Output.Kind == HTNIRValueKind::Variable &&
                Output.VariableSlot != kNoIndex &&
                inBound.find(Output.Text) == inBound.end())
            {
                AddGeneratedVariableWrite(Output, outWrites);
            }
        }
        break;

    case HTN_CONDITION_CALL_BIND:
        if (Condition.OutputValue != kNoIndex && Condition.OutputValue < inBuilder.Values.size())
        {
            const ValueRecord& Output = inBuilder.Values[Condition.OutputValue];
            if (Output.Kind == HTNIRValueKind::Variable &&
                inBound.find(Output.Text) == inBound.end())
            {
                AddGeneratedVariableWrite(Output, outWrites);
            }
        }
        break;

    case HTN_CONDITION_AXIOM:
    {
        const AxiomRecord* Axiom = FindAxiomRecord(inBuilder, Condition.Id);
        if (!Axiom)
            break;
        const uint32 Count = std::min(Axiom->ParameterCount, Condition.ArgumentCount);
        for (uint32 I = 0u; I < Count; ++I)
        {
            const ValueRecord& Parameter = inBuilder.Values[Axiom->FirstParameter + I];
            if (Parameter.Text >= inBuilder.Strings.Values.size() ||
                !IsOutputParameterName(inBuilder.Strings.Values[Parameter.Text]))
            {
                continue;
            }
            AddGeneratedVariableWrite(inBuilder.Values[Condition.FirstArgument + I], outWrites);
        }
        break;
    }

    case HTN_CONDITION_AND:
    {
        BoundVariableSet Current = inBound;
        for (uint32 I = 0u; I < Condition.ChildCount; ++I)
        {
            const uint32 Ref = Condition.FirstChildRef + I;
            if (Ref >= inBuilder.ConditionChildRefs.size())
                break;
            const uint32 Child = inBuilder.ConditionChildRefs[Ref];
            CollectGeneratedConditionWrites(inBuilder, Child, Current, outWrites);
            Current = AnalyzeCondition(inBuilder, Child, Current).BoundAfter;
        }
        break;
    }

    case HTN_CONDITION_OR:
    case HTN_CONDITION_ALT:
        for (uint32 I = 0u; I < Condition.ChildCount; ++I)
        {
            const uint32 Ref = Condition.FirstChildRef + I;
            if (Ref < inBuilder.ConditionChildRefs.size())
                CollectGeneratedConditionWrites(inBuilder, inBuilder.ConditionChildRefs[Ref], inBound, outWrites);
        }
        break;

    case HTN_CONDITION_NOT:
        if (Condition.ChildCount != 0u && Condition.FirstChildRef < inBuilder.ConditionChildRefs.size())
            CollectGeneratedConditionWrites(inBuilder, inBuilder.ConditionChildRefs[Condition.FirstChildRef], inBound, outWrites);
        break;

    default:
        break;
    }
}

struct GeneratedCheckpointPlan
{
    uint32 Id = 0u;
    std::vector<uint32> UnboundSlots;
    std::vector<uint32> SavedSlots;
};

GeneratedCheckpointPlan BuildGeneratedCheckpointPlan(const uint32 inId,
                                                       const BoundVariableSet& inBound,
                                                       const GeneratedVariableWriteMap& inWrites)
{
    GeneratedCheckpointPlan Plan;
    Plan.Id = inId;
    for (const auto& [Variable, Slot] : inWrites)
    {
        if (inBound.find(Variable) == inBound.end())
            Plan.UnboundSlots.push_back(Slot);
        else
            Plan.SavedSlots.push_back(Slot);
    }
    std::sort(Plan.UnboundSlots.begin(), Plan.UnboundSlots.end());
    std::sort(Plan.SavedSlots.begin(), Plan.SavedSlots.end());
    return Plan;
}

GeneratedCheckpointPlan BuildGeneratedConditionCheckpointPlan(const HTNCompilerIR& inBuilder,
                                                                const uint32 inId,
                                                                const uint32 inCondition,
                                                                const BoundVariableSet& inBound)
{
    GeneratedVariableWriteMap Writes;
    CollectGeneratedConditionWrites(inBuilder, inCondition, inBound, Writes);
    return BuildGeneratedCheckpointPlan(inId, inBound, Writes);
}

GeneratedCheckpointPlan BuildGeneratedAndSuffixCheckpointPlan(const HTNCompilerIR& inBuilder,
                                                              const uint32 inId,
                                                              const ConditionRecord& inAndCondition,
                                                              const uint32 inChildOffset,
                                                              const BoundVariableSet& inBound)
{
    GeneratedVariableWriteMap Writes;
    BoundVariableSet Current = inBound;
    for (uint32 I = inChildOffset; I < inAndCondition.ChildCount; ++I)
    {
        const uint32 Ref = inAndCondition.FirstChildRef + I;
        if (Ref >= inBuilder.ConditionChildRefs.size())
            break;
        const uint32 Child = inBuilder.ConditionChildRefs[Ref];
        CollectGeneratedConditionWrites(inBuilder, Child, Current, Writes);
        Current = AnalyzeCondition(inBuilder, Child, Current).BoundAfter;
    }
    return BuildGeneratedCheckpointPlan(inId, inBound, Writes);
}

GeneratedCheckpointPlan BuildGeneratedFactSuffixCheckpointPlan(const HTNCompilerIR& inBuilder,
                                                               const uint32 inId,
                                                               const std::vector<uint32>& inFacts,
                                                               const uint32 inFactOffset,
                                                               const BoundVariableSet& inBound)
{
    GeneratedVariableWriteMap Writes;
    BoundVariableSet Current = inBound;
    for (uint32 I = inFactOffset; I < inFacts.size(); ++I)
    {
        const uint32 Fact = inFacts[I];
        CollectGeneratedConditionWrites(inBuilder, Fact, Current, Writes);
        Current = AnalyzeCondition(inBuilder, Fact, Current).BoundAfter;
    }
    return BuildGeneratedCheckpointPlan(inId, inBound, Writes);
}

void EmitGeneratedCheckpointDeclarations(CodeWriter& W, const GeneratedCheckpointPlan& inPlan)
{
    for (const uint32 Slot : inPlan.SavedSlots)
        W.Out << "    HTNAtom environment_checkpoint_" << inPlan.Id << "_slot_" << Slot << ";\n";
}

void EmitGeneratedVariableUnbind(CodeWriter& W, const std::string& inSlot, const std::string& inIndent)
{
    W.Out << inIndent << "HTN_GENERATED_EXECUTION(context)->variables.bound_mask[(" << inSlot
          << ") >> 6u] &= ~(UINT64_C(1) << ((" << inSlot << ") & 63u));\n";
}

void EmitGeneratedVariableSetMove(CodeWriter& W, const std::string& inSlot, const std::string& inValue, const std::string& inIndent)
{
    W.Out << inIndent << "HTNAtom_AssignMove(&HTN_GENERATED_EXECUTION(context)->variables.values[" << inSlot
          << "], " << inValue << ");\n";
    W.Out << inIndent << "HTN_GENERATED_EXECUTION(context)->variables.bound_mask[(" << inSlot
          << ") >> 6u] |= (UINT64_C(1) << ((" << inSlot << ") & 63u));\n";
}

void EmitGeneratedVariableSetCopy(CodeWriter& W, const std::string& inSlot, const std::string& inValue, const std::string& inIndent)
{
    W.Out << inIndent << "HTNAtom_AssignCopy(&HTN_GENERATED_EXECUTION(context)->variables.values[" << inSlot
          << "], " << inValue << ");\n";
    W.Out << inIndent << "HTN_GENERATED_EXECUTION(context)->variables.bound_mask[(" << inSlot
          << ") >> 6u] |= (UINT64_C(1) << ((" << inSlot << ") & 63u));\n";
}

void EmitGeneratedSetCopyIfChanged(CodeWriter& W, const uint32 inSlot, const std::string& inValue, const char* inIndent)
{
    W.Out << inIndent << "{ const HTNAtom* existing_value = HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, "
          << inSlot << "u); if (HTNAtom_IsBound(" << inValue
          << ") && (!existing_value || !HTNAtom_Equals(existing_value, " << inValue << "))) {\n";
    EmitGeneratedVariableSetCopy(W, std::to_string(inSlot) + "u", inValue, std::string(inIndent) + "    ");
    W.Out << inIndent << "} }\n";
}

void EmitGeneratedSetMoveIfChanged(CodeWriter& W, const uint32 inSlot, const std::string& inValue, const char* inIndent)
{
    W.Out << inIndent << "{ const HTNAtom* existing_value = HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, "
          << inSlot << "u); if (HTNAtom_IsBound(" << inValue
          << ") && (!existing_value || !HTNAtom_Equals(existing_value, " << inValue << "))) {\n";
    EmitGeneratedVariableSetMove(W, std::to_string(inSlot) + "u", inValue, std::string(inIndent) + "    ");
    W.Out << inIndent << "} }\n";
}

void EmitGeneratedCheckpointPush(CodeWriter& W, const GeneratedCheckpointPlan& inPlan, const char* inIndent = "    ")
{
    W.Out << inIndent << "HTN_GENERATED_STRUCTURAL_EVENT(context, HTN_GENERATED_STRUCTURAL_CHECKPOINT_PUSH);\n";
    for (const uint32 Slot : inPlan.SavedSlots)
    {
        W.Out << inIndent << "HTNAtom_Copy(&environment_checkpoint_" << inPlan.Id << "_slot_" << Slot
              << ", HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, " << Slot << "u));\n";
    }
}

void EmitGeneratedCheckpointRollback(CodeWriter& W, const GeneratedCheckpointPlan& inPlan, const char* inIndent = "    ")
{
    W.Out << inIndent << "HTN_GENERATED_STRUCTURAL_EVENT(context, HTN_GENERATED_STRUCTURAL_CHECKPOINT_ROLLBACK);\n";
    for (const uint32 Slot : inPlan.UnboundSlots)
        EmitGeneratedVariableUnbind(W, std::to_string(Slot) + "u", inIndent);
    for (const uint32 Slot : inPlan.SavedSlots)
    {
        EmitGeneratedVariableSetMove(W, std::to_string(Slot) + "u",
                                     "&environment_checkpoint_" + std::to_string(inPlan.Id) + "_slot_" + std::to_string(Slot),
                                     inIndent);
        W.Out << inIndent << "HTNAtom_Destroy(&environment_checkpoint_" << inPlan.Id << "_slot_" << Slot << ");\n";
    }
}

void EmitGeneratedCheckpointCommit(CodeWriter& W, const GeneratedCheckpointPlan& inPlan, const char* inIndent = "    ")
{
    W.Out << inIndent << "HTN_GENERATED_STRUCTURAL_EVENT(context, HTN_GENERATED_STRUCTURAL_CHECKPOINT_COMMIT);\n";
    for (const uint32 Slot : inPlan.SavedSlots)
        W.Out << inIndent << "HTNAtom_Destroy(&environment_checkpoint_" << inPlan.Id << "_slot_" << Slot << ");\n";
}

bool IsGeneratedAxiomChoiceSupported(const HTNCompilerIR& inBuilder, const uint32 inConditionIndex)
{
    if (inConditionIndex >= inBuilder.Conditions.size())
        return false;
    const ConditionRecord& Condition = inBuilder.Conditions[inConditionIndex];
    if (Condition.Kind != HTN_CONDITION_AXIOM)
        return false;
    const AxiomRecord* Axiom = FindAxiomRecord(inBuilder, Condition.Id);
    if (!Axiom || Axiom->Condition == kNoIndex || Axiom->Condition >= inBuilder.Conditions.size())
        return false;

    const ConditionRecord& Body = inBuilder.Conditions[Axiom->Condition];
    if (Body.Kind == HTN_CONDITION_FACT)
        return true;
    if (Body.Kind != HTN_CONDITION_AND)
        return false;
    for (uint32 I = 0; I < Body.ChildCount; ++I)
    {
        const uint32 Ref = Body.FirstChildRef + I;
        if (Ref >= inBuilder.ConditionChildRefs.size())
            return false;
        const uint32 Child = inBuilder.ConditionChildRefs[Ref];
        if (Child >= inBuilder.Conditions.size() || inBuilder.Conditions[Child].Kind != HTN_CONDITION_FACT)
            return false;
    }
    return true;
}

std::string GetGeneratedAxiomChoiceHelperName(const std::string& inDomainSymbol, const uint32 inConditionIndex)
{
    return inDomainSymbol + "_AXIOM_CHOICE_" + std::to_string(inConditionIndex);
}

std::string GetGeneratedFactChoiceHelperName(const std::string& inDomainSymbol, const uint32 inConditionIndex)
{
    return inDomainSymbol + "_FACT_CHOICE_" + std::to_string(inConditionIndex);
}

std::string GetGeneratedAxiomBeginHelperName(const std::string& inDomainSymbol, const uint32 inConditionIndex)
{
    return inDomainSymbol + "_BEGIN_AXIOM_" + std::to_string(inConditionIndex);
}

std::string GetGeneratedAxiomEndHelperName(const std::string& inDomainSymbol, const uint32 inConditionIndex)
{
    return inDomainSymbol + "_END_AXIOM_" + std::to_string(inConditionIndex);
}


std::string GetGeneratedAxiomScopeName(const uint32 inConditionIndex)
{
    return "axiom_scope_" + std::to_string(inConditionIndex);
}

uint32 CountGeneratedAxiomScopeSlots(const AxiomRecord& inAxiom)
{
    uint32 Count = 0u;
    for (uint64_t Word : inAxiom.VariableSlotMask)
    {
        while (Word != 0u)
        {
            ++Count;
            Word &= Word - 1u;
        }
    }
    return Count;
}

const ValueRecord* ResolveGeneratedStaticValue(const HTNCompilerIR& inBuilder, uint32 inValueIndex)
{
    // Constants are compile-time aliases in generated code. Collapse them now
    // so fact checks can emit a typed literal instead of asking a generic helper to
    // resolve metadata on every row.
    for (uint32 Depth = 0u; Depth < 16u; ++Depth)
    {
        if (inValueIndex >= inBuilder.Values.size())
            return nullptr;
        const ValueRecord& Value = inBuilder.Values[inValueIndex];
        if (Value.Kind != HTNIRValueKind::Constant)
            return &Value;
        const auto It = std::find_if(inBuilder.Constants.begin(), inBuilder.Constants.end(),
            [&Value](const ConstantRecord& Constant) { return Constant.Id == Value.Text; });
        if (It == inBuilder.Constants.end())
            return nullptr;
        inValueIndex = It->Value;
    }
    return nullptr;
}

bool TryEvaluateStaticBuiltinComparison(const HTNCompilerIR& inBuilder, const ConditionRecord& inCondition, bool& outResult)
{
    if (inCondition.Kind != HTN_CONDITION_BUILTIN_COMPARISON || inCondition.ArgumentCount != 2u)
        return false;

    const ValueRecord* Left = ResolveGeneratedStaticValue(inBuilder, inCondition.FirstArgument);
    const ValueRecord* Right = ResolveGeneratedStaticValue(inBuilder, inCondition.FirstArgument + 1u);
    if (!Left || !Right ||
        Left->Kind == HTNIRValueKind::Variable || Right->Kind == HTNIRValueKind::Variable ||
        Left->Kind == HTNIRValueKind::Arithmetic || Right->Kind == HTNIRValueKind::Arithmetic)
        return false;

    const bool LeftNumeric = Left->AtomType == HTN_ATOM_TYPE_INT || Left->AtomType == HTN_ATOM_TYPE_FLOAT;
    const bool RightNumeric = Right->AtomType == HTN_ATOM_TYPE_INT || Right->AtomType == HTN_ATOM_TYPE_FLOAT;
    const auto NumericValue = [](const ValueRecord& inValue) -> double {
        return inValue.AtomType == HTN_ATOM_TYPE_INT ? static_cast<double>(inValue.IntValue) : static_cast<double>(inValue.FloatValue);
    };

    if (inCondition.Id == 0u || inCondition.Id == 1u)
    {
        bool Equal = false;
        if (LeftNumeric && RightNumeric)
            Equal = NumericValue(*Left) == NumericValue(*Right);
        else if (Left->AtomType == Right->AtomType)
        {
            switch (Left->AtomType)
            {
            case HTN_ATOM_TYPE_BOOL: Equal = Left->BoolValue == Right->BoolValue; break;
            case HTN_ATOM_TYPE_STRING:
            case HTN_ATOM_TYPE_SYMBOL: Equal = Left->Text == Right->Text; break;
            default: return false;
            }
        }
        else
            Equal = false;
        outResult = inCondition.Id == 0u ? Equal : !Equal;
        return true;
    }

    if (!LeftNumeric || !RightNumeric)
        return false;
    const double L = NumericValue(*Left);
    const double R = NumericValue(*Right);
    switch (inCondition.Id)
    {
    case HTN_BUILTIN_COMPARE_LESS: outResult = L < R; return true;
    case HTN_BUILTIN_COMPARE_LESS_EQUAL: outResult = L <= R; return true;
    case HTN_BUILTIN_COMPARE_GREATER: outResult = L > R; return true;
    case HTN_BUILTIN_COMPARE_GREATER_EQUAL: outResult = L >= R; return true;
    default: return false;
    }
}

std::string FormatCFloatLiteral(const float inValue)
{
    // A C floating suffix may only follow a floating constant.  Streaming a
    // whole-valued float normally produces "0"/"1", and appending 'f' would
    // therefore generate the invalid tokens "0f"/"1f" (MSVC C2059).
    // Use enough precision to round-trip the float and force a decimal point
    // when the representation has neither a decimal point nor an exponent.
    std::ostringstream Out;
    Out.imbue(std::locale::classic());
    Out << std::setprecision(std::numeric_limits<float>::max_digits10) << inValue;
    std::string Text = Out.str();
    if (Text.find('.') == std::string::npos &&
        Text.find('e') == std::string::npos &&
        Text.find('E') == std::string::npos)
    {
        Text += ".0";
    }
    Text += 'f';
    return Text;
}

std::string BuildGeneratedValueAtomReference(const HTNCompilerIR& inBuilder,
                                             const uint32 inValueIndex,
                                             const std::string& inDomainSymbol)
{
    if (inValueIndex >= inBuilder.Values.size())
    {
        inBuilder.SetError("Generated value reference is out of range");
        return "NULL";
    }

    const ValueRecord& Value = inBuilder.Values[inValueIndex];
    if (Value.Kind == HTNIRValueKind::Variable)
    {
        // Any-argument variables intentionally have no generated variable slot. In atom
        // contexts they represent an unbound value: comparisons fail and fact
        // cursors fall back to an unindexed scan, matching the previous generated behavior
        // materialization semantics without re-introducing value-kind dispatch.
        if (Value.VariableSlot == kNoIndex)
            return "NULL";
        return "HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, " +
               std::to_string(Value.VariableSlot) + "u)";
    }

    if (Value.StaticValueIndex == kNoIndex)
    {
        inBuilder.SetError("Generated static value has no prepared-value index");
        return "NULL";
    }
    return "&" + inDomainSymbol + "_PREPARED(context)->values[" +
           std::to_string(Value.StaticValueIndex) + "u]";
}

std::string EmitGeneratedArithmeticValue(CodeWriter& W,
                                         const HTNCompilerIR& B,
                                         const ValueRecord& inValue,
                                         const std::string& inDomainSymbol,
                                         const std::string& inBaseName,
                                         const std::string& inIndent,
                                         uint32& ioTemporary)
{
    if (inValue.Kind == HTNIRValueKind::Variable)
    {
        if (inValue.VariableSlot == kNoIndex) return "NULL";
        return "HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, " +
            std::to_string(inValue.VariableSlot) + "u)";
    }
    if (inValue.Kind == HTNIRValueKind::Constant)
    {
        const auto It = std::find_if(B.Constants.begin(), B.Constants.end(),
            [&inValue](const ConstantRecord& Constant) { return Constant.Id == inValue.Text; });
        return It == B.Constants.end() ? "NULL" :
            BuildGeneratedValueAtomReference(B, It->Value, inDomainSymbol);
    }
    if (inValue.Kind != HTNIRValueKind::Arithmetic)
    {
        const std::string Name = inBaseName + "_literal_" + std::to_string(ioTemporary++);
        W.Out << inIndent << "HTNAtom " << Name << ";\n";
        W.Out << inIndent << "HTNAtom_Init(&" << Name << ");\n";
        if (inValue.AtomType == HTN_ATOM_TYPE_INT)
            W.Out << inIndent << "HTNAtom_SetInt(&" << Name << ", " << inValue.IntValue << ");\n";
        else if (inValue.AtomType == HTN_ATOM_TYPE_FLOAT)
            W.Out << inIndent << "HTNAtom_SetFloat(&" << Name << ", " << FormatCFloatLiteral(inValue.FloatValue) << ");\n";
        return "&" + Name;
    }
    if (inValue.ArithmeticExpression >= B.ArithmeticExpressions.size())
        return "NULL";

    const HTNIRArithmeticExpression& Expression = B.ArithmeticExpressions[inValue.ArithmeticExpression];
    const std::string Name = inBaseName + "_expression_" + std::to_string(ioTemporary++);
    std::vector<std::string> Operands;
    Operands.reserve(Expression.Operands.size());
    for (const ValueRecord& Operand : Expression.Operands)
        Operands.push_back(EmitGeneratedArithmeticValue(W, B, Operand, inDomainSymbol, inBaseName, inIndent, ioTemporary));
    W.Out << inIndent << "const HTNAtom* " << Name << "_operands[" << std::max<size_t>(1u, Operands.size()) << "u] = {";
    for (size_t I = 0; I < Operands.size(); ++I)
        W.Out << (I == 0u ? "" : ", ") << Operands[I];
    if (Operands.empty()) W.Out << "NULL";
    W.Out << "};\n";
    W.Out << inIndent << "HTNAtom " << Name << ";\n";
    W.Out << inIndent << "HTNAtom_Init(&" << Name << ");\n";
    W.Out << inIndent << "const int " << Name << "_valid = " << inDomainSymbol << "_EVALUATE_ARITHMETIC("
          << Name << "_operands, " << Operands.size() << "u, "
          << static_cast<uint32>(Expression.Operator) << "u, &" << Name << ");\n";
    return "(" + Name + "_valid ? &" + Name + " : NULL)";
}


enum class GeneratedAxiomParameterDirection
{
    Input,
    Output,
    InputOutput
};

GeneratedAxiomParameterDirection GetGeneratedAxiomParameterDirection(const HTNCompilerIR& inBuilder,
                                                                     const ValueRecord& inParameter)
{
    if (inParameter.Kind != HTNIRValueKind::Variable ||
        inParameter.VariableSlot == kNoIndex ||
        inParameter.Text >= inBuilder.Strings.Values.size())
    {
        inBuilder.SetError("Generated axiom parameter has no compile-time variable slot");
        return GeneratedAxiomParameterDirection::Input;
    }

    const std::string& Name = inBuilder.Strings.Values[inParameter.Text];
    if (Name.rfind("out_", 0) == 0)
        return GeneratedAxiomParameterDirection::Output;
    if (Name.rfind("io_", 0) == 0)
        return GeneratedAxiomParameterDirection::InputOutput;
    return GeneratedAxiomParameterDirection::Input;
}

bool GeneratedAxiomParameterIsInput(const GeneratedAxiomParameterDirection inDirection)
{
    return inDirection != GeneratedAxiomParameterDirection::Output;
}

bool GeneratedAxiomParameterIsOutput(const GeneratedAxiomParameterDirection inDirection)
{
    return inDirection != GeneratedAxiomParameterDirection::Input;
}

void EmitGeneratedAxiomBegin(CodeWriter& W,
                             const HTNCompilerIR& B,
                             const ConditionRecord& inCondition,
                             const uint32 inConditionIndex,
                             const std::string& inDomainSymbol)
{
    if (inCondition.ResolvedIndex == kNoIndex || inCondition.ResolvedIndex >= B.Axioms.size())
    {
        B.SetError("Generated axiom scope has no resolved axiom");
        return;
    }

    const uint32 SavedValueCapacity = CountGeneratedAxiomScopeSlots(B.Axioms[inCondition.ResolvedIndex]);
    const uint32 StorageCapacity = std::max<uint32>(SavedValueCapacity, 1u);
    const std::string ScopeName = GetGeneratedAxiomScopeName(inConditionIndex);
    W.Out << "    HTNAtom " << ScopeName << "_values[" << StorageCapacity << "u];\n";
    W.Out << "    uint8_t " << ScopeName << "_bound[" << StorageCapacity << "u];\n";
    W.Out << "    " << inDomainSymbol << "_AXIOM_SCOPE " << ScopeName << " = { "
          << ScopeName << "_values, " << ScopeName << "_bound, 0u };\n";
    W.Out << "    " << GetGeneratedAxiomBeginHelperName(inDomainSymbol, inConditionIndex)
          << "(context, &" << ScopeName << ");\n";
}

void EmitGeneratedAxiomEndCall(CodeWriter& W,
                               const HTNCompilerIR& B,
                               const ConditionRecord& inCondition,
                               const uint32 inConditionIndex,
                               const std::string& inDomainSymbol,
                               const std::string& inSucceededExpression,
                               const std::string& inPrefix,
                               const std::string& inSuffix)
{
    (void)B;
    (void)inCondition;
    W.Out << inPrefix << GetGeneratedAxiomEndHelperName(inDomainSymbol, inConditionIndex)
          << "(context, " << inSucceededExpression << ", &" << GetGeneratedAxiomScopeName(inConditionIndex)
          << ")" << inSuffix;
}

std::string BuildGeneratedStaticFactMatch(const HTNCompilerIR& inBuilder,
                                          const uint32 inValueIndex,
                                          const std::string& inDomainSymbol,
                                          const std::string& inArguments,
                                          const uint32 inArgument)
{
    const ValueRecord* Value = ResolveGeneratedStaticValue(inBuilder, inValueIndex);
    if (!Value || Value->Kind == HTNIRValueKind::Variable)
    {
        inBuilder.SetError("Static fact argument could not be resolved at translation time");
        return "0";
    }

    std::ostringstream Out;
    Out << "HTNAtom_Equals(" << inArguments << "[" << inArgument << "u], "
        << BuildGeneratedValueAtomReference(inBuilder, inValueIndex, inDomainSymbol) << ")";
    return Out.str();
}

std::string BuildGeneratedFactCursorBegin(const HTNCompilerIR& inBuilder,
                                          const ConditionRecord& inCondition,
                                          const std::string& inCursorName)
{
    if (inCondition.ResolvedIndex == kNoIndex)
    {
        inBuilder.SetError("Generated fact has no compile-time fact slot");
        return {};
    }

    std::ostringstream Out;
    Out << "HTNWorldState_BeginGeneratedFactRowCursor(HTN_GENERATED_EXECUTION(context)->fact_slots["
        << inCondition.ResolvedIndex << "u], " << inCondition.ArgumentCount << "u, &" << inCursorName << ")";
    return Out.str();
}


void EmitGeneratedFactChoiceHelper(CodeWriter& W, const HTNCompilerIR& B,
                                   const uint32 inConditionIndex,
                                   const std::string& inDomainSymbol)
{
    if (inConditionIndex >= B.Conditions.size()) { B.SetError("Generated fact choice condition index is out of range"); return; }
    const ConditionRecord& Condition = B.Conditions[inConditionIndex];
    if (Condition.Kind != HTN_CONDITION_FACT) { B.SetError("Attempted to emit a fact choice helper for a non-fact condition"); return; }

    W.Out << "static int " << GetGeneratedFactChoiceHelperName(inDomainSymbol, inConditionIndex)
          << "(const HTNGeneratedPlannerContext* context, uint32_t target_solution)\n{\n";
    W.Out << "    HTNGeneratedFactRowCursor fact_cursor;\n";
    if (Condition.ArgumentCount > 0u)
        W.Out << "    const HTNAtom* fact_arguments[" << Condition.ArgumentCount << "u];\n";
    W.Out << "    uint32_t solution_index = 0u;\n";
    W.Out << "    HTN_GENERATED_STRUCTURAL_EVENT(context, HTN_GENERATED_STRUCTURAL_FACT_QUERY);\n";
    W.Out << "    HTN_GENERATED_STRUCTURAL_EVENT(context, target_solution == 0u ? HTN_GENERATED_STRUCTURAL_FACT_CHOICE_POINT : HTN_GENERATED_STRUCTURAL_FACT_CHOICE_RETRY);\n";
    W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_FACT_CURSOR_SETUP);\n";
    W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_FACT);\n";
    W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_WORLDSTATE_COUNT);\n";
    if (Condition.ResolvedIndex == kNoIndex) { B.SetError("Generated fact choice has no compile-time fact slot"); return; }
    W.Out << "    " << BuildGeneratedFactCursorBegin(B, Condition, "fact_cursor") << ";\n";
    W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_WORLDSTATE_COUNT);\n";
    W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_FACT);\n";
    W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_FACT_CURSOR_SETUP);\n";
    W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_FACT_SCAN_UNIFY);\n";
    W.Out << "    while (HTNWorldState_NextGeneratedFactRow(&fact_cursor, "
          << (Condition.ArgumentCount > 0u ? "fact_arguments" : "NULL") << ")) {\n";
    W.Out << "        HTN_GENERATED_STRUCTURAL_EVENT(context, HTN_GENERATED_STRUCTURAL_FACT_ROW_TESTED);\n";

    std::unordered_map<uint32, uint32> FirstVariableArgument;
    for (uint32 I = 0; I < Condition.ArgumentCount; ++I)
    {
        const uint32 ValueIndex = Condition.FirstArgument + I;
        if (ValueIndex >= B.Values.size())
            continue;
        const ValueRecord& Value = B.Values[ValueIndex];
        if (Value.Kind == HTNIRValueKind::Variable && Value.VariableSlot == kNoIndex)
        {
            continue;
        }
        if (Value.Kind == HTNIRValueKind::Variable)
        {
            W.Out << "        { const HTNAtom* variable_atom = HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, "
                  << Value.VariableSlot << "u); if (variable_atom && !HTNAtom_Equals(fact_arguments["
                  << I << "u], variable_atom)) continue; }\n";
        }
        else
        {
            W.Out << "        if (!(" << BuildGeneratedStaticFactMatch(B, ValueIndex, inDomainSymbol, "fact_arguments", I)
                  << ")) continue;\n";
        }
        if (Value.Kind == HTNIRValueKind::Variable && Value.VariableSlot != kNoIndex)
        {
            const auto [It, Inserted] = FirstVariableArgument.emplace(Value.Text, I);
            if (!Inserted)
            {
                W.Out << "        if (!HTNAtom_Equals(fact_arguments["
                      << It->second << "u], fact_arguments[" << I << "u])) continue;\n";
            }
        }
    }

    W.Out << "        HTN_GENERATED_STRUCTURAL_EVENT(context, HTN_GENERATED_STRUCTURAL_FACT_ROW_MATCHED);\n";
    W.Out << "        if (solution_index++ != target_solution) continue;\n";
    for (const auto& Pair : FirstVariableArgument)
    {
        const uint32 Argument = Pair.second;
        const uint32 ValueIndex = Condition.FirstArgument + Argument;
        const uint32 VariableSlot = B.Values[ValueIndex].VariableSlot;
        W.Out << "        if (!HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, " << VariableSlot
              << "u) && HTNAtom_IsBound(fact_arguments[" << Argument << "u])) {\n";
        EmitGeneratedVariableSetCopy(W, std::to_string(VariableSlot) + "u",
                                     "fact_arguments[" + std::to_string(Argument) + "u]", "            ");
        W.Out << "        }\n";
    }
    W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_FACT_SCAN_UNIFY);\n";
    W.Out << "        return 1;\n";
    W.Out << "    }\n";
    W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_FACT_SCAN_UNIFY);\n";
    W.Out << "    return 0;\n}\n\n";
}

void EmitDirectDeterministicFact(CodeWriter& W, const HTNCompilerIR& B, uint32 inCondition,
                                 const BoundVariableSet& inBound,
                                 uint32 inSuccess, uint32 inFailure,
                                 const std::string& inDomainSymbol);

void EmitAxiomFactChoiceSequence(CodeWriter& W, const HTNCompilerIR& B,
                                 const std::vector<uint32>& inFacts,
                                 const uint32 inFactOffset,
                                 const BoundVariableSet& inBound,
                                 const uint32 inFailure,
                                 const uint32 inRetryAfterSolution,
                                 const std::vector<GeneratedCheckpointPlan>& inActiveChoiceCheckpoints,
                                 const uint32 inAxiomCondition,
                                 const uint32 inAxiomBodyCondition,
                                 const std::string& inDomainSymbol)
{
    if (inFactOffset >= inFacts.size())
    {
        W.Out << "    if (solution_index++ == target_solution) {\n";
        if (!inActiveChoiceCheckpoints.empty())
            W.Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        for (auto It = inActiveChoiceCheckpoints.rbegin(); It != inActiveChoiceCheckpoints.rend(); ++It)
            EmitGeneratedCheckpointCommit(W, *It, "        ");
        if (!inActiveChoiceCheckpoints.empty())
            W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        W.Out << "        HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inAxiomBodyCondition << "u, 1, 0);\n";
        const ConditionRecord& AxiomCall = B.Conditions[inAxiomCondition];
        EmitGeneratedAxiomEndCall(W, B, AxiomCall, inAxiomCondition, inDomainSymbol,
                                  "1",
                                  "        return ", ";\n");
        W.Out << "    }\n";
        W.Out << "    goto " << W.Label(inRetryAfterSolution) << ";\n";
        return;
    }

    const uint32 Fact = inFacts[inFactOffset];
    const ConditionAnalysis FactAnalysis = AnalyzeCondition(B, Fact, inBound);
    if (FactAnalysis.MayProduceMultipleSolutions)
    {
        const uint32 Retry = W.NewLabel();
        const uint32 Checkpoint = W.NewLabel();
        const GeneratedCheckpointPlan ChoiceCheckpointPlan =
            BuildGeneratedFactSuffixCheckpointPlan(B, Checkpoint, inFacts, inFactOffset, inBound);
        EmitGeneratedCheckpointDeclarations(W, ChoiceCheckpointPlan);
        W.Out << "    fact_choice_cursor_" << Fact << " = 0u;\n";
        W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        EmitGeneratedCheckpointPush(W, ChoiceCheckpointPlan);
        W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        W.Out << W.Label(Retry) << ":\n";
        if (B.RuntimeBacktrackingSupport == HTNGeneratedRuntimeBacktrackingSupport::Enabled)
        {
            W.Out << "    if (fact_choice_cursor_" << Fact << " != 0u && (context->backtracking_mode & HTN_BACKTRACKING_FACTS_AND_AXIOMS) == 0) {\n";
            W.Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
            EmitGeneratedCheckpointRollback(W, ChoiceCheckpointPlan, "        ");
            W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
            W.Out << "        goto " << W.Label(inFailure) << ";\n";
            W.Out << "    }\n";
        }
        W.Out << "    if (fact_choice_cursor_" << Fact << " != 0u) {\n";
        W.Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        EmitGeneratedCheckpointRollback(W, ChoiceCheckpointPlan, "        ");
        EmitGeneratedCheckpointPush(W, ChoiceCheckpointPlan, "        ");
        W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        W.Out << "    }\n";
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << Fact << "u, 1);\n";
        W.Out << "    if (!" << GetGeneratedFactChoiceHelperName(inDomainSymbol, Fact) << "(context, fact_choice_cursor_" << Fact << "++)) {\n";
        W.Out << "        HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << Fact << "u, 0, 1);\n";
        W.Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        EmitGeneratedCheckpointRollback(W, ChoiceCheckpointPlan, "        ");
        W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        W.Out << "        goto " << W.Label(inFailure) << ";\n";
        W.Out << "    }\n";
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << Fact << "u, 1, 1);\n";
        std::vector<GeneratedCheckpointPlan> ActiveChoiceCheckpoints = inActiveChoiceCheckpoints;
        ActiveChoiceCheckpoints.push_back(ChoiceCheckpointPlan);
        EmitAxiomFactChoiceSequence(W, B, inFacts, inFactOffset + 1u,
                                    FactAnalysis.BoundAfter, Retry, Retry,
                                    ActiveChoiceCheckpoints, inAxiomCondition, inAxiomBodyCondition, inDomainSymbol);
        return;
    }

    const uint32 FactSucceeded = W.NewLabel();
    EmitDirectDeterministicFact(W, B, Fact, inBound, FactSucceeded, inFailure, inDomainSymbol);
    W.Out << W.Label(FactSucceeded) << ":\n";
    EmitAxiomFactChoiceSequence(W, B, inFacts, inFactOffset + 1u,
                                FactAnalysis.BoundAfter, inFailure, inRetryAfterSolution,
                                inActiveChoiceCheckpoints, inAxiomCondition, inAxiomBodyCondition, inDomainSymbol);
}

void EmitGeneratedAxiomChoiceHelper(CodeWriter& W, const HTNCompilerIR& B,
                                    const uint32 inConditionIndex,
                                    const std::string& inDomainSymbol)
{
    const ConditionRecord& Condition = B.Conditions[inConditionIndex];
    const AxiomRecord* Axiom = FindAxiomRecord(B, Condition.Id);
    if (!Axiom || !IsGeneratedAxiomChoiceSupported(B, inConditionIndex)) { B.SetError("Multi-solution axiom cannot yet be lowered without a generic condition evaluator at condition " + std::to_string(inConditionIndex)); return; }

    const ConditionRecord& Body = B.Conditions[Axiom->Condition];
    std::vector<uint32> Facts;
    if (Body.Kind == HTN_CONDITION_FACT)
        Facts.emplace_back(Axiom->Condition);
    else
    {
        Facts.reserve(Body.ChildCount);
        for (uint32 I = 0; I < Body.ChildCount; ++I)
            Facts.emplace_back(B.ConditionChildRefs[Body.FirstChildRef + I]);
    }

    BoundVariableSet AxiomBound;
    for (uint32 I = 0; I < Axiom->ParameterCount; ++I)
    {
        const ValueRecord& Parameter = B.Values[Axiom->FirstParameter + I];
        if (Parameter.Kind != HTNIRValueKind::Variable || Parameter.Text >= B.Strings.Values.size())
            continue;
        const std::string& Name = B.Strings.Values[Parameter.Text];
        if (Name.rfind("out_", 0) != 0)
            AxiomBound.insert(Parameter.Text);
    }

    const uint32 Failure = W.NewLabel();
    W.Out << "static int " << GetGeneratedAxiomChoiceHelperName(inDomainSymbol, inConditionIndex)
          << "(const HTNGeneratedPlannerContext* context, uint32_t target_solution)\n{\n";
    W.Out << "    uint32_t solution_index = 0u;\n";
    for (const uint32 Fact : Facts)
    {
        const ConditionRecord& FactCondition = B.Conditions[Fact];
        bool HasVariable = false;
        for (uint32 I = 0; I < FactCondition.ArgumentCount; ++I)
        {
            const uint32 ValueIndex = FactCondition.FirstArgument + I;
            if (ValueIndex < B.Values.size() && B.Values[ValueIndex].Kind == HTNIRValueKind::Variable) { HasVariable = true; break; }
        }
        if (HasVariable)
        {
            W.Out << "    uint32_t fact_choice_cursor_" << Fact << " = 0u;\n";
            W.Out << "    (void)fact_choice_cursor_" << Fact << ";\n";
        }
    }
    W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
    EmitGeneratedAxiomBegin(W, B, Condition, inConditionIndex, inDomainSymbol);
    W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
    W.Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << Axiom->Condition << "u, 0);\n";
    EmitAxiomFactChoiceSequence(W, B, Facts, 0u, AxiomBound, Failure, Failure, {},
                                inConditionIndex, Axiom->Condition, inDomainSymbol);
    W.Out << W.Label(Failure) << ":\n";
    W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << Axiom->Condition << "u, 0, 0);\n";
    W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
    EmitGeneratedAxiomEndCall(W, B, Condition, inConditionIndex, inDomainSymbol, "0", "    (void)", ";\n");
    W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
    W.Out << "    return 0;\n}\n\n";
}

void EmitDirectDeterministicFact(CodeWriter& W, const HTNCompilerIR& B, const uint32 inCondition,
                                 const BoundVariableSet& inBound,
                                 const uint32 inSuccess, const uint32 inFailure,
                                 const std::string& inDomainSymbol)
{
    if (inCondition >= B.Conditions.size()) { B.SetError("Generated fact condition index is out of range"); return; }
    const ConditionRecord& Condition = B.Conditions[inCondition];
    if (Condition.Kind != HTN_CONDITION_FACT) { B.SetError("Attempted to emit direct fact code for a non-fact condition"); return; }


    W.Out << "    {\n";
    W.Out << "        HTNGeneratedFactRowCursor fact_cursor;\n";
    if (Condition.ArgumentCount > 0u)
        W.Out << "        const HTNAtom* fact_arguments[" << Condition.ArgumentCount << "u];\n";
    W.Out << "        int fact_matched = 0;\n";
    W.Out << "        HTN_GENERATED_STRUCTURAL_EVENT(context, HTN_GENERATED_STRUCTURAL_FACT_QUERY);\n";
    W.Out << "        HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0);\n";
    W.Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_FACT_CURSOR_SETUP);\n";
    W.Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_FACT);\n";
    W.Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_WORLDSTATE_COUNT);\n";
    W.Out << "        " << BuildGeneratedFactCursorBegin(B, Condition, "fact_cursor") << ";\n";
    W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_WORLDSTATE_COUNT);\n";
    W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_FACT);\n";
    W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_FACT_CURSOR_SETUP);\n";
    W.Out << "        {\n";
    W.Out << "            HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_FACT_SCAN_UNIFY);\n";
    W.Out << "            while (HTNWorldState_NextGeneratedFactRow(&fact_cursor, "
          << (Condition.ArgumentCount > 0u ? "fact_arguments" : "NULL") << ")) {\n";
    W.Out << "                HTN_GENERATED_STRUCTURAL_EVENT(context, HTN_GENERATED_STRUCTURAL_FACT_ROW_TESTED);\n";

    std::unordered_map<uint32, uint32> FirstUnboundVariableArgument;
    for (uint32 I = 0; I < Condition.ArgumentCount; ++I)
    {
        const uint32 ValueIndex = Condition.FirstArgument + I;
        if (ValueIndex >= B.Values.size())
            continue;
        const ValueRecord& Value = B.Values[ValueIndex];
        if (Value.Kind == HTNIRValueKind::Variable && Value.VariableSlot == kNoIndex)
        {
            continue;
        }
        if (Value.Kind == HTNIRValueKind::Variable && inBound.find(Value.Text) == inBound.end())
        {
            const auto [It, Inserted] = FirstUnboundVariableArgument.emplace(Value.Text, I);
            if (!Inserted)
            {
                W.Out << "                if (!HTNAtom_Equals(fact_arguments["
                      << It->second << "u], fact_arguments[" << I << "u])) continue;\n";
            }
            continue;
        }
        if (Value.Kind == HTNIRValueKind::Variable)
        {
            W.Out << "                if (!HTNAtom_Equals(fact_arguments["
                  << I << "u], HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, "
                  << Value.VariableSlot << "u))) continue;\n";
        }
        else
        {
            W.Out << "                if (!(" << BuildGeneratedStaticFactMatch(B, ValueIndex, inDomainSymbol, "fact_arguments", I)
                  << ")) continue;\n";
        }
    }

    W.Out << "                HTN_GENERATED_STRUCTURAL_EVENT(context, HTN_GENERATED_STRUCTURAL_FACT_ROW_MATCHED);\n";
    for (const auto& Pair : FirstUnboundVariableArgument)
    {
        const uint32 Argument = Pair.second;
        const uint32 ValueIndex = Condition.FirstArgument + Argument;
        EmitGeneratedSetCopyIfChanged(W, B.Values[ValueIndex].VariableSlot,
                                      "fact_arguments[" + std::to_string(Argument) + "u]", "                ");
    }
    W.Out << "                fact_matched = 1;\n";
    W.Out << "                break;\n";
    W.Out << "            }\n";
    W.Out << "            HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_FACT_SCAN_UNIFY);\n";
    W.Out << "        }\n";
    W.Out << "        HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION"
          << ", " << inCondition << "u, fact_matched, 0);\n";
    W.Out << "        if (fact_matched) goto " << W.Label(inSuccess) << ";\n";
    W.Out << "        goto " << W.Label(inFailure) << ";\n";
    W.Out << "    }\n";
}

void EmitGeneratedConditionLeaf(CodeWriter& W, const HTNCompilerIR& B, const uint32 inCondition,
                              const BoundVariableSet& inBound,
                              const uint32 inSuccess, const uint32 inFailure,
                              const std::string& inDomainSymbol)
{
    if (inCondition >= B.Conditions.size()) { B.SetError("Generated condition index is out of range"); return; }

    const ConditionRecord& Condition = B.Conditions[inCondition];
    W.DomainExpressionComment(Condition.DomainExpression);
    if (Condition.Kind == HTN_CONDITION_BUILTIN_COMPARISON)
    {
        if (Condition.ArgumentCount != 2u) { B.SetError("Built-in comparison must contain exactly two operands"); return; }

        bool StaticResult = false;
        if (TryEvaluateStaticBuiltinComparison(B, Condition, StaticResult))
        {
            W.Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0);\n";
            W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition
                  << "u, " << (StaticResult ? "1" : "0") << ", 0);\n";
            W.Out << "    goto " << W.Label(StaticResult ? inSuccess : inFailure) << ";\n";
            return;
        }


        W.Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition
              << "u, 0);\n";

        uint32 ArithmeticTemporary = 0u;
        const ValueRecord& LeftValue = B.Values[Condition.FirstArgument];
        const ValueRecord& RightValue = B.Values[Condition.FirstArgument + 1u];
        const std::string LeftReference = LeftValue.Kind == HTNIRValueKind::Arithmetic
            ? EmitGeneratedArithmeticValue(W, B, LeftValue, inDomainSymbol,
                                           "comparison_" + std::to_string(inCondition) + "_left", "    ", ArithmeticTemporary)
            : BuildGeneratedValueAtomReference(B, Condition.FirstArgument, inDomainSymbol);
        const std::string RightReference = RightValue.Kind == HTNIRValueKind::Arithmetic
            ? EmitGeneratedArithmeticValue(W, B, RightValue, inDomainSymbol,
                                           "comparison_" + std::to_string(inCondition) + "_right", "    ", ArithmeticTemporary)
            : BuildGeneratedValueAtomReference(B, Condition.FirstArgument + 1u, inDomainSymbol);
        W.Out << "    if (" << inDomainSymbol << "_COMPARE_ATOMS("
              << LeftReference << ", " << RightReference << ", "
              << BuiltinComparisonOperatorCName(Condition.Id) << ")) {\n";


        W.Out << "        HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition
              << "u, 1, 0);\n";

        W.Out << "        goto " << W.Label(inSuccess) << ";\n";
        W.Out << "    }\n";


        W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition
              << "u, 0, 0);\n";

        W.Out << "    goto " << W.Label(inFailure) << ";\n";
        return;
    }
    if (Condition.Kind == HTN_CONDITION_BUILTIN_LIST_SPLIT)
    {
        if (Condition.ArgumentCount != 3u)
        {
            B.SetError("Built-in list split must contain exactly three arguments");
            return;
        }

        const ValueRecord& ElementOutput = B.Values[Condition.FirstArgument + 1u];
        const ValueRecord& RemainderOutput = B.Values[Condition.FirstArgument + 2u];
        const std::string ListReference = BuildGeneratedValueAtomReference(B, Condition.FirstArgument, inDomainSymbol);
        const std::string ElementReference = BuildGeneratedValueAtomReference(B, Condition.FirstArgument + 1u, inDomainSymbol);
        const std::string RemainderReference = BuildGeneratedValueAtomReference(B, Condition.FirstArgument + 2u, inDomainSymbol);
        const char* Direction = Condition.Id == static_cast<uint32>(HTNIRListSplitOperation::Back)
            ? "HTN_ATOM_LIST_SPLIT_BACK"
            : "HTN_ATOM_LIST_SPLIT_FRONT";

        W.Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0);\n";
        W.Out << "    {\n";
        W.Out << "        const HTNAtom* split_list_value = " << ListReference << ";\n";
        W.Out << "        HTNAtom split_element;\n";
        W.Out << "        HTNAtom split_remainder;\n";
        W.Out << "        int split_valid = 0;\n";
        W.Out << "        HTNAtom_Init(&split_element);\n";
        W.Out << "        HTNAtom_Init(&split_remainder);\n";
        W.Out << "        if (split_list_value && split_list_value->type == HTN_ATOM_TYPE_LIST &&\n";
        W.Out << "            HTNAtomList_Split(&split_list_value->value.list_value, " << Direction
              << ", &split_element, &split_remainder)) {\n";
        W.Out << "            split_valid = 1;\n";

        if (ElementOutput.Kind == HTNIRValueKind::Variable && ElementOutput.VariableSlot != kNoIndex)
        {
            W.Out << "            { const HTNAtom* existing = HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, "
                  << ElementOutput.VariableSlot << "u); if (existing && !HTNAtom_Equals(existing, &split_element)) split_valid = 0; }\n";
        }
        else
        {
            W.Out << "            if (!" << ElementReference << " || !HTNAtom_Equals(" << ElementReference
                  << ", &split_element)) split_valid = 0;\n";
        }

        if (RemainderOutput.Kind == HTNIRValueKind::Variable && RemainderOutput.VariableSlot != kNoIndex)
        {
            W.Out << "            { const HTNAtom* existing = HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, "
                  << RemainderOutput.VariableSlot << "u); if (existing && !HTNAtom_Equals(existing, &split_remainder)) split_valid = 0; }\n";
        }
        else
        {
            W.Out << "            if (!" << RemainderReference << " || !HTNAtom_Equals(" << RemainderReference
                  << ", &split_remainder)) split_valid = 0;\n";
        }

        if (ElementOutput.Kind == HTNIRValueKind::Variable && RemainderOutput.Kind == HTNIRValueKind::Variable &&
            ElementOutput.VariableSlot != kNoIndex && ElementOutput.VariableSlot == RemainderOutput.VariableSlot)
        {
            W.Out << "            if (!HTNGeneratedVariables_IsBound(&HTN_GENERATED_EXECUTION(context)->variables, "
                  << ElementOutput.VariableSlot << "u) && !HTNAtom_Equals(&split_element, &split_remainder)) split_valid = 0;\n";
        }

        W.Out << "            if (split_valid) {\n";
        if (ElementOutput.Kind == HTNIRValueKind::Variable && ElementOutput.VariableSlot != kNoIndex)
        {
            W.Out << "                if (!HTNGeneratedVariables_IsBound(&HTN_GENERATED_EXECUTION(context)->variables, "
                  << ElementOutput.VariableSlot << "u)) {\n";
            EmitGeneratedVariableSetCopy(W, std::to_string(ElementOutput.VariableSlot) + "u", "&split_element", "                    ");
            W.Out << "                }\n";
        }
        if (RemainderOutput.Kind == HTNIRValueKind::Variable && RemainderOutput.VariableSlot != kNoIndex)
        {
            W.Out << "                if (!HTNGeneratedVariables_IsBound(&HTN_GENERATED_EXECUTION(context)->variables, "
                  << RemainderOutput.VariableSlot << "u)) {\n";
            EmitGeneratedVariableSetCopy(W, std::to_string(RemainderOutput.VariableSlot) + "u", "&split_remainder", "                    ");
            W.Out << "                }\n";
        }
        W.Out << "            }\n";
        W.Out << "        }\n";
        W.Out << "        HTNAtom_Destroy(&split_element);\n";
        W.Out << "        HTNAtom_Destroy(&split_remainder);\n";
        W.Out << "        HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, split_valid, 0);\n";
        W.Out << "        if (split_valid) goto " << W.Label(inSuccess) << ";\n";
        W.Out << "        goto " << W.Label(inFailure) << ";\n";
        W.Out << "    }\n";
        return;
    }
    if (Condition.Kind == HTN_CONDITION_FACT)
    {
        EmitDirectDeterministicFact(W, B, inCondition, inBound, inSuccess, inFailure, inDomainSymbol);
        return;
    }

    W.Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0);\n";
    W.Out << "    {\n";
    if (Condition.ArgumentCount > 0u)
    {
        W.Out << "        const HTNAtom* call_arguments[" << Condition.ArgumentCount << "u] = {";
        for (uint32 I = 0u; I < Condition.ArgumentCount; ++I)
        {
            if (I > 0u)
                W.Out << ", ";
            W.Out << BuildGeneratedValueAtomReference(B, Condition.FirstArgument + I, inDomainSymbol);
        }
        W.Out << "};\n";
    }
    const std::string Args = Condition.ArgumentCount == 0u ? "0" : "call_arguments";
    switch (Condition.Kind)
    {
    case HTN_CONDITION_CALL:
    {
        if (Condition.ResolvedIndex >= B.CallTermStringIds.size()) { B.SetError("Generated callterm condition has invalid callterm slot"); return; }
        W.Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CALL_CONTROL);\n";
        W.Out << "        HTNAtom call_result;\n";
        W.Out << "        HTNAtom_Init(&call_result);\n";
        W.Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CALLTERM);\n";
        W.Out << "        const int call_has_result = HTNCallTermRegistry_InvokeGeneratedCallTerm(context->callterm_binding_context, &HTN_GENERATED_EXECUTION(context)->callterm_slots["
              << Condition.ResolvedIndex << "u], " << Args << ", " << Condition.ArgumentCount << "u, &call_result);\n";
        W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CALLTERM);\n";
        W.Out << "        { const uint64_t fact_storage_generation = HTNWorldState_GetFactStorageGeneration(context->world_state);\n";
        W.Out << "          if (HTN_GENERATED_EXECUTION(context)->fact_storage_generation != fact_storage_generation) {\n";
        W.Out << "              if (!" << inDomainSymbol << "_PREPARE_FACTS(HTN_GENERATED_EXECUTION(context)->fact_slots, context->world_state, context->prepared_storage)) {\n";
        W.Out << "                  HTNAtom_Destroy(&call_result);\n";
        W.Out << "                  HTN_GENERATED_EXECUTION(context)->failure_state = HTN_DECOMPOSITION_PREPARATION_FAILED;\n";
        W.Out << "                  HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0, 0);\n";
        W.Out << "                  HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CALL_CONTROL);\n";
        W.Out << "                  return 0;\n";
        W.Out << "              }\n";
        W.Out << "              HTN_GENERATED_EXECUTION(context)->fact_storage_generation = fact_storage_generation;\n";
        W.Out << "          } }\n";
        W.Out << "        const int condition_result = call_has_result && call_result.type == HTN_ATOM_TYPE_BOOL && call_result.value.bool_value != 0u;\n";
        W.Out << "        HTNAtom_Destroy(&call_result);\n";
        W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CALL_CONTROL);\n";
        break;
    }
    case HTN_CONDITION_CALL_BIND:
    {
        if (Condition.ResolvedIndex >= B.CallTermStringIds.size()) { B.SetError("Generated call-bind condition has invalid callterm slot"); return; }
        if (Condition.OutputValue == kNoIndex || Condition.OutputValue >= B.Values.size() || B.Values[Condition.OutputValue].Kind != HTNIRValueKind::Variable) { B.SetError("Generated call-bind output is not a variable"); return; }
        const ValueRecord& Output = B.Values[Condition.OutputValue];
        W.Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CALL_CONTROL);\n";
        W.Out << "        int condition_result = HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, " << Output.VariableSlot << "u) == NULL;\n";
        W.Out << "        if (condition_result) {\n";
        W.Out << "            HTNAtom call_result;\n";
        W.Out << "            HTNAtom_Init(&call_result);\n";
        W.Out << "            HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CALLTERM);\n";
        W.Out << "            condition_result = HTNCallTermRegistry_InvokeGeneratedCallTerm(context->callterm_binding_context, &HTN_GENERATED_EXECUTION(context)->callterm_slots["
              << Condition.ResolvedIndex << "u], " << Args << ", " << Condition.ArgumentCount << "u, &call_result);\n";
        W.Out << "            HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CALLTERM);\n";
        W.Out << "            { const uint64_t fact_storage_generation = HTNWorldState_GetFactStorageGeneration(context->world_state);\n";
        W.Out << "              if (HTN_GENERATED_EXECUTION(context)->fact_storage_generation != fact_storage_generation) {\n";
        W.Out << "                  if (!" << inDomainSymbol << "_PREPARE_FACTS(HTN_GENERATED_EXECUTION(context)->fact_slots, context->world_state, context->prepared_storage)) {\n";
        W.Out << "                      HTNAtom_Destroy(&call_result);\n";
        W.Out << "                      HTN_GENERATED_EXECUTION(context)->failure_state = HTN_DECOMPOSITION_PREPARATION_FAILED;\n";
        W.Out << "                      HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0, 0);\n";
        W.Out << "                      HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CALL_CONTROL);\n";
        W.Out << "                      return 0;\n";
        W.Out << "                  }\n";
        W.Out << "                  HTN_GENERATED_EXECUTION(context)->fact_storage_generation = fact_storage_generation;\n";
        W.Out << "              } }\n";
        W.Out << "            if (condition_result) ";
        EmitGeneratedSetMoveIfChanged(W, Output.VariableSlot, "&call_result", "");
        W.Out << "            HTNAtom_Destroy(&call_result);\n";
        W.Out << "        }\n";
        W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CALL_CONTROL);\n";
        break;
    }
    default:
        B.SetError("HTNTranslator attempted to emit an unsupported generated-condition fallback for condition " + std::to_string(inCondition));
        return;
    }
    W.Out << "        HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION"
          << ", " << inCondition << "u, condition_result, 0);\n";
    W.Out << "        if (condition_result) goto " << W.Label(inSuccess) << ";\n";
    W.Out << "        goto " << W.Label(inFailure) << ";\n";
    W.Out << "    }\n";
}

void EmitLoweredCondition(CodeWriter& W, const HTNCompilerIR& B, const uint32 inCondition,
                          const BoundVariableSet& inBound,
                          const uint32 inSuccess, const uint32 inFailure,
                          const std::string& inDomainSymbol);

void EmitLoweredAndSequence(CodeWriter& W, const HTNCompilerIR& B, const ConditionRecord& inAndCondition,
                           const uint32 inChildOffset, const BoundVariableSet& inBound,
                           const uint32 inSuccess, const uint32 inFailure,
                           const std::string& inDomainSymbol)
{
    if (inChildOffset >= inAndCondition.ChildCount)
    {
        W.Out << "    goto " << W.Label(inSuccess) << ";\n";
        return;
    }

    const uint32 Ref = inAndCondition.FirstChildRef + inChildOffset;
    if (Ref >= B.ConditionChildRefs.size())
    {
        W.Out << "    goto " << W.Label(inFailure) << ";\n";
        return;
    }

    const uint32 Child = B.ConditionChildRefs[Ref];
    const ConditionAnalysis ChildAnalysis = AnalyzeCondition(B, Child, inBound);

    if (ChildAnalysis.MayProduceMultipleSolutions)
    {
        const uint32 Retry = W.NewLabel();
        const uint32 Continue = W.NewLabel();
        const uint32 ChildSucceeded = W.NewLabel();
        const uint32 Checkpoint = W.NewLabel();

        W.DomainExpressionComment(B.Conditions[Child].DomainExpression);
        const bool IsFactChoice = Child < B.Conditions.size() && B.Conditions[Child].Kind == HTN_CONDITION_FACT;
        const bool IsAxiomChoice = Child < B.Conditions.size() && B.Conditions[Child].Kind == HTN_CONDITION_AXIOM;
        GeneratedCheckpointPlan ChoiceCheckpointPlan;
        if (IsFactChoice)
        {
            ChoiceCheckpointPlan = BuildGeneratedAndSuffixCheckpointPlan(B, Checkpoint, inAndCondition, inChildOffset, inBound);
            EmitGeneratedCheckpointDeclarations(W, ChoiceCheckpointPlan);
            W.Out << "    fact_choice_cursor_" << Child << " = 0u;\n";
            W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
            EmitGeneratedCheckpointPush(W, ChoiceCheckpointPlan);
            W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        }
        else if (IsAxiomChoice)
        {
            if (!IsGeneratedAxiomChoiceSupported(B, Child))
            {
                B.SetError("Multi-solution axiom cannot be lowered without a generic condition evaluator at condition " + std::to_string(Child));
                return;
            }
            W.Out << "    axiom_choice_cursor_" << Child << " = 0u;\n";
        }
        else
        {
            B.SetError("Choice-producing condition cannot yet be lowered without a generic condition evaluator at condition " + std::to_string(Child)); return;
        }
        W.Out << "    goto " << W.Label(Retry) << ";\n";
        W.Out << W.Label(Retry) << ":\n";
        if (IsFactChoice)
        {
            if (B.RuntimeBacktrackingSupport == HTNGeneratedRuntimeBacktrackingSupport::Enabled)
            {
                W.Out << "    if (fact_choice_cursor_" << Child << " != 0u && (context->backtracking_mode & HTN_BACKTRACKING_FACTS_AND_AXIOMS) == 0) {\n";
                W.Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
                EmitGeneratedCheckpointRollback(W, ChoiceCheckpointPlan, "        ");
                W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
                W.Out << "        goto " << W.Label(inFailure) << ";\n";
                W.Out << "    }\n";
            }
            W.Out << "    if (fact_choice_cursor_" << Child << " != 0u) {\n";
            W.Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
            EmitGeneratedCheckpointRollback(W, ChoiceCheckpointPlan, "        ");
            EmitGeneratedCheckpointPush(W, ChoiceCheckpointPlan, "        ");
            W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
            W.Out << "    }\n";
            W.Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << Child << "u, 1);\n";
            W.Out << "    if (!" << GetGeneratedFactChoiceHelperName(inDomainSymbol, Child) << "(context, fact_choice_cursor_" << Child << "++)) {\n";
            W.Out << "        HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << Child << "u, 0, 1);\n";
            W.Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
            EmitGeneratedCheckpointRollback(W, ChoiceCheckpointPlan, "        ");
            W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
            W.Out << "        goto " << W.Label(inFailure) << ";\n";
            W.Out << "    }\n";
            W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << Child << "u, 1, 1);\n";
        }
        else
        {
            if (B.RuntimeBacktrackingSupport == HTNGeneratedRuntimeBacktrackingSupport::Enabled)
                W.Out << "    if (axiom_choice_cursor_" << Child << " != 0u && (context->backtracking_mode & HTN_BACKTRACKING_FACTS_AND_AXIOMS) == 0) goto " << W.Label(inFailure) << ";\n";
            // The axiom call itself is not the visual choice point. Its generator
            // facts inside the axiom body are.
            // Keeping the axiom regular prevents it from filtering older decomposition
            // steps from its children when the latest alternative is expanded.
            W.Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << Child << "u, 0);\n";
            W.Out << "    if (!" << GetGeneratedAxiomChoiceHelperName(inDomainSymbol, Child) << "(context, axiom_choice_cursor_" << Child << "++)) { "
                  << "HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << Child << "u, 0, 0); goto " << W.Label(inFailure) << "; }\n";
            W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << Child << "u, 1, 0);\n";
        }
        W.Out << "    goto " << W.Label(Continue) << ";\n";
        W.Out << W.Label(Continue) << ":\n";

        EmitLoweredAndSequence(W, B, inAndCondition, inChildOffset + 1u,
            ChildAnalysis.BoundAfter, ChildSucceeded, Retry, inDomainSymbol);

        W.Out << W.Label(ChildSucceeded) << ":\n";
        if (IsFactChoice)
        {
            W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
            EmitGeneratedCheckpointCommit(W, ChoiceCheckpointPlan);
            W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        }
        W.Out << "    goto " << W.Label(inSuccess) << ";\n";
        return;
    }

    const uint32 ChildSuccess = W.NewLabel();
    EmitLoweredCondition(W, B, Child, inBound, ChildSuccess, inFailure, inDomainSymbol);
    W.Out << W.Label(ChildSuccess) << ":\n";
    EmitLoweredAndSequence(W, B, inAndCondition, inChildOffset + 1u,
        ChildAnalysis.BoundAfter, inSuccess, inFailure, inDomainSymbol);
}

void EmitLoweredCondition(CodeWriter& W, const HTNCompilerIR& B, const uint32 inCondition,
                          const BoundVariableSet& inBound,
                          const uint32 inSuccess, const uint32 inFailure,
                          const std::string& inDomainSymbol)
{
    if (inCondition == kNoIndex)
    {
        W.Out << "    goto " << W.Label(inSuccess) << ";\n";
        return;
    }

    const ConditionRecord& C = B.Conditions[inCondition];
    const ConditionAnalysis Analysis = AnalyzeCondition(B, inCondition, inBound);

    // Multi-solution axioms are also generated. The helper replays the axiom
    // body from its local frame and returns the requested solution ordinal;
    // enclosing generated ANDs increment the ordinal when they backtrack.
    if (C.Kind == HTN_CONDITION_AXIOM && Analysis.MayProduceMultipleSolutions)
    {
        if (!IsGeneratedAxiomChoiceSupported(B, inCondition))
        {
            B.SetError("Multi-solution axiom cannot be lowered without a generic condition evaluator at condition " + std::to_string(inCondition));
            return;
        }
        W.DomainExpressionComment(C.DomainExpression);
        // The generator inside the axiom is the choice point. Keep the axiom
        // condition node regular so its
        // descendants can expose every backtracking step independently.
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0);\n";
        W.Out << "    if (!" << GetGeneratedAxiomChoiceHelperName(inDomainSymbol, inCondition) << "(context, 0u)) {\n";
        W.Out << "        HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0, 0);\n";
        W.Out << "        goto " << W.Label(inFailure) << ";\n";
        W.Out << "    }\n";
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 1, 0);\n";
        W.Out << "    goto " << W.Label(inSuccess) << ";\n";
        return;
    }

    // Deterministic axioms are expanded into their generated body while the
    // Generated execution only manages the axiom-local variable frame and out/io propagation.
    if (C.Kind == HTN_CONDITION_AXIOM && Analysis.CanLowerToCFG)
    {
        const AxiomRecord* Axiom = FindAxiomRecord(B, C.Id);
        if (!Axiom)
        {
            EmitGeneratedConditionLeaf(W, B, inCondition, inBound, inSuccess, inFailure, inDomainSymbol);
            return;
        }

        BoundVariableSet AxiomBound;
        for (uint32 I = 0; I < Axiom->ParameterCount; ++I)
        {
            const ValueRecord& Parameter = B.Values[Axiom->FirstParameter + I];
            if (Parameter.Kind != HTNIRValueKind::Variable || Parameter.Text >= B.Strings.Values.size())
                continue;
            const std::string& Name = B.Strings.Values[Parameter.Text];
            if (Name.rfind("out_", 0) != 0)
                AxiomBound.insert(Parameter.Text);
        }

        const uint32 BodySuccess = W.NewLabel();
        const uint32 BodyFailure = W.NewLabel();
        const uint32 PropagationFailure = W.NewLabel();
        W.DomainExpressionComment(C.DomainExpression);
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0);\n";
        W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
        EmitGeneratedAxiomBegin(W, B, C, inCondition, inDomainSymbol);
        W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
        EmitLoweredCondition(W, B, Axiom->Condition, AxiomBound, BodySuccess, BodyFailure, inDomainSymbol);
        W.Out << W.Label(BodyFailure) << ":\n";
        W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
        EmitGeneratedAxiomEndCall(W, B, C, inCondition, inDomainSymbol, "0", "    (void)", ";\n");
        W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0, 0);\n";
        W.Out << "    goto " << W.Label(inFailure) << ";\n";
        W.Out << W.Label(BodySuccess) << ":\n";
        W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
        EmitGeneratedAxiomEndCall(W, B, C, inCondition, inDomainSymbol,
                                  "1",
                                  "    if (!", ") {\n");
        W.Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
        W.Out << "        goto " << W.Label(PropagationFailure) << ";\n";
        W.Out << "    }\n";
        W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL);\n";
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 1, 0);\n";
        W.Out << "    goto " << W.Label(inSuccess) << ";\n";
        W.Out << W.Label(PropagationFailure) << ":\n";
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0, 0);\n";
        W.Out << "    goto " << W.Label(inFailure) << ";\n";
        return;
    }

    // Facts and callterms are the only remaining generated leaves. Every logical
    // composite and axiom must have been lowered to generated control flow.
    if (C.Kind == HTN_CONDITION_FACT || C.Kind == HTN_CONDITION_CALL || C.Kind == HTN_CONDITION_CALL_BIND ||
        C.Kind == HTN_CONDITION_BUILTIN_COMPARISON || C.Kind == HTN_CONDITION_BUILTIN_LIST_SPLIT)
    {
        EmitGeneratedConditionLeaf(W, B, inCondition, inBound, inSuccess, inFailure, inDomainSymbol);
        return;
    }
    if (!Analysis.CanLowerToCFG)
    {
        B.SetError("Condition cannot be lowered to generated CFG without a generic condition evaluator: " + std::to_string(inCondition));
        return;
    }

    W.Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0);\n";

    if (C.Kind == HTN_CONDITION_AND) // AND
    {
        const uint32 LocalSuccess = W.NewLabel();
        const uint32 LocalFailure = W.NewLabel();
        const uint32 Checkpoint = Analysis.MayBindVariables ? W.NewLabel() : 0u;
        GeneratedCheckpointPlan CheckpointPlan;
        if (Analysis.MayBindVariables)
        {
            CheckpointPlan = BuildGeneratedConditionCheckpointPlan(B, Checkpoint, inCondition, inBound);
            EmitGeneratedCheckpointDeclarations(W, CheckpointPlan);
            W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
            EmitGeneratedCheckpointPush(W, CheckpointPlan);
            W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        }

        EmitLoweredAndSequence(W, B, C, 0u, inBound, LocalSuccess, LocalFailure,
            inDomainSymbol);

        W.Out << W.Label(LocalFailure) << ":\n";
        if (Analysis.MayBindVariables)
        {
            W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
            EmitGeneratedCheckpointRollback(W, CheckpointPlan);
            W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        }
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0, 0);\n";
        W.Out << "    goto " << W.Label(inFailure) << ";\n";

        W.Out << W.Label(LocalSuccess) << ":\n";
        if (Analysis.MayBindVariables)
        {
            W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
            EmitGeneratedCheckpointCommit(W, CheckpointPlan);
            W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        }
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 1, 0);\n";
        W.Out << "    goto " << W.Label(inSuccess) << ";\n";
        return;
    }

    if (C.Kind == HTN_CONDITION_OR || C.Kind == HTN_CONDITION_ALT) // OR / pure-no-bind ALT
    {
        const uint32 LocalSuccess = W.NewLabel();
        const uint32 LocalFailure = W.NewLabel();
        for (uint32 I = 0; I < C.ChildCount; ++I)
        {
            const uint32 Ref = C.FirstChildRef + I;
            const uint32 Child = B.ConditionChildRefs[Ref];
            const uint32 ChildSuccess = W.NewLabel();
            const uint32 ChildFailure = (I + 1u == C.ChildCount) ? LocalFailure : W.NewLabel();

            // Give every alternative its own success bridge. This is deliberately
            // explicit rather than targeting LocalSuccess directly: nested generated
            // axioms emit their own local labels while unwinding the axiom frame, and
            // the bridge guarantees that a successful alternative leaves the OR/ALT
            // immediately instead of ever falling through into the next alternative.
            EmitLoweredCondition(W, B, Child, inBound, ChildSuccess, ChildFailure, inDomainSymbol);
            W.Out << W.Label(ChildSuccess) << ":\n";
            W.Out << "    goto " << W.Label(LocalSuccess) << ";\n";
            if (I + 1u != C.ChildCount)
                W.Out << W.Label(ChildFailure) << ":\n";
        }
        if (C.ChildCount == 0u)
            W.Out << "    goto " << W.Label(LocalFailure) << ";\n";
        W.Out << W.Label(LocalFailure) << ":\n";
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0, 0);\n";
        W.Out << "    goto " << W.Label(inFailure) << ";\n";
        W.Out << W.Label(LocalSuccess) << ":\n";
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 1, 0);\n";
        W.Out << "    goto " << W.Label(inSuccess) << ";\n";
        return;
    }

    if (C.Kind == HTN_CONDITION_NOT) // NOT
    {
        const uint32 ChildSuccess = W.NewLabel();
        const uint32 ChildFailure = W.NewLabel();
        const uint32 LocalSuccess = W.NewLabel();
        const uint32 LocalFailure = W.NewLabel();
        const uint32 Checkpoint = W.NewLabel();
        const GeneratedCheckpointPlan CheckpointPlan = BuildGeneratedConditionCheckpointPlan(B, Checkpoint, inCondition, inBound);
        EmitGeneratedCheckpointDeclarations(W, CheckpointPlan);
        W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        EmitGeneratedCheckpointPush(W, CheckpointPlan);
        W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        if (C.ChildCount == 0u)
            W.Out << "    goto " << W.Label(ChildFailure) << ";\n";
        else
        {
            const uint32 Child = B.ConditionChildRefs[C.FirstChildRef];
            EmitLoweredCondition(W, B, Child, inBound, ChildSuccess, ChildFailure, inDomainSymbol);
        }
        W.Out << W.Label(ChildSuccess) << ":\n";
        W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        EmitGeneratedCheckpointRollback(W, CheckpointPlan);

        W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        W.Out << "    goto " << W.Label(LocalFailure) << ";\n";
        W.Out << W.Label(ChildFailure) << ":\n";
        W.Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        EmitGeneratedCheckpointRollback(W, CheckpointPlan);

        W.Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK);\n";
        W.Out << "    goto " << W.Label(LocalSuccess) << ";\n";
        W.Out << W.Label(LocalFailure) << ":\n";
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 0, 0);\n";
        W.Out << "    goto " << W.Label(inFailure) << ";\n";
        W.Out << W.Label(LocalSuccess) << ":\n";
        W.Out << "    HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, &" << inDomainSymbol << "_PLANNER_DEFINITION, " << inCondition << "u, 1, 0);\n";
        W.Out << "    goto " << W.Label(inSuccess) << ";\n";
        return;
    }

    EmitGeneratedConditionLeaf(W, B, inCondition, inBound, inSuccess, inFailure, inDomainSymbol);
}

void EmitCondition(CodeWriter& W, const HTNCompilerIR& B, uint32 inCondition, uint32 inSuccess, uint32 inFailure,
                   const BoundVariableSet& inInitiallyBound,
                   const std::string& inDomainSymbol)
{
    if (inCondition == kNoIndex)
    {
        W.Out << "    goto " << W.Label(inSuccess) << ";\n";
        return;
    }

    EmitLoweredCondition(W, B, inCondition, inInitiallyBound, inSuccess, inFailure, inDomainSymbol);

    // Keep the existing branch-level trace callback behaviour: one result for
    // the complete branch condition, regardless of whether it was lowered.
    // The actual callback is emitted at the success/failure labels by the
    // branch generator so it also covers compiled control flow.
}

template<typename T, typename Writer>
void WriteArray(std::ostringstream& out, const char* type, const std::string& name, const std::vector<T>& values, Writer writer)
{
    const size_t Count = values.empty() ? 1u : values.size();
    out << "static const " << type << " " << name << "[" << Count << "] = {\n";
    if (values.empty()) out << "    {0}\n";
    else for (const auto& V : values) { out << "    "; writer(out,V); out << ",\n"; }
    out << "};\n\n";
}

std::string MakeSource(const HTNCompilerIR& B, const std::string& Prefix, const std::string& EntryPointName,
                       const std::string& SourceFile, const std::vector<std::string>& LinkedSourceFiles,
                       const HTNGeneratedBacktrackingPolicy inBacktrackingPolicy,
                       const HTNGeneratedRuntimeBacktrackingSupport inRuntimeBacktrackingSupport,
                       const uint32 inBacktrackingCapacity)
{
    CodeWriter W;
    auto& Out = W.Out;
    const std::string DomainSymbol = Prefix + "_DOMAIN";
    Out << "/* Generated by HTNTranslator. Do not edit. */\n";
    Out << "/* Source domain: " << EscapeCString(SourceFile) << " */\n";
    if (LinkedSourceFiles.size() > 1u)
    {
        Out << "/* Linked domain sources:\n";
        for (const std::string& LinkedSource : LinkedSourceFiles)
            Out << " *   " << EscapeCString(LinkedSource) << "\n";
        Out << " */\n";
    }
    Out << "#include \"Translator/HTNGeneratedPlanner.h\"\n";
    if (inBacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedWithOverflow)
        Out << "#include \"Translator/HTNGeneratedBacktracking.h\"\n";
    Out << "#include \"Translator/HTNGeneratedProfiling.h\"\n";
    Out << "#include \"Translator/HTNGeneratedDebug.h\"\n";
    Out << "#include \"Core/HtnSymbolGenerated.h\"\n";
    Out << "#include \"WorldState/HTNGeneratedWorldState.h\"\n";
    Out << "#include \"Translator/HTNCallTermBridge.h\"\n";
    Out << "#include <float.h>\n";
    Out << "\n";
    Out << "/* Generated control flow intentionally contains paths a compiler may prove unreachable. */\n";
    Out << "/* Keep warning suppression local so warnings-as-errors remain active for hand-written code. */\n";
    Out << "#if defined(_MSC_VER)\n";
    Out << "#pragma warning(push)\n";
    Out << "#pragma warning(disable: 4702) /* unreachable code */\n";
    Out << "#pragma warning(disable: 4102) /* unreferenced label */\n";
    Out << "#elif defined(__clang__)\n";
    Out << "#pragma clang diagnostic push\n";
    Out << "#pragma clang diagnostic ignored \"-Wunreachable-code\"\n";
    Out << "#pragma clang diagnostic ignored \"-Wunused-label\"\n";
    Out << "#elif defined(__GNUC__)\n";
    Out << "#pragma GCC diagnostic push\n";
    Out << "#pragma GCC diagnostic ignored \"-Wunreachable-code\"\n";
    Out << "#pragma GCC diagnostic ignored \"-Wunused-label\"\n";
    Out << "#endif\n\n";
    Out << "#define HTN_NO_INDEX HTN_GENERATED_NO_INDEX\n\n";

    // Callterm evaluation may invalidate the generated fact-slot cache before
    // the emitted method/branch functions run. Those functions can therefore
    // refresh the slots, so the prepare helper needs a declaration before them.
    Out << "static int " << DomainSymbol << "_PREPARE_FACTS(const void** fact_slots, HTNWorldState* world_state, const void* prepared_storage);\n\n";

    Out << "#ifdef HTN_DEBUG_DECOMPOSITION\n";
    const size_t DebugStringCount = B.Strings.Values.empty() ? 1u : B.Strings.Values.size();
    Out << "static const char* const " << Prefix << "_DEBUG_STRINGS[" << DebugStringCount << "] = {\n";
    if (B.Strings.Values.empty()) Out << "    \"\"\n"; else for (const auto& S : B.Strings.Values) Out << "    \"" << EscapeCString(S) << "\",\n";
    Out << "};\n\n";
    WriteArray(Out,"HTNGeneratedDebugValue",Prefix+"_DEBUG_VALUES",B.Values,[](auto& O,const auto& V){
        uint32 Flags = 0u;
        if (V.Kind == HTNIRValueKind::Variable && V.DebugAsVariable) Flags |= HTN_GENERATED_DEBUG_VALUE_FLAG_VARIABLE;
        if (V.Kind == HTNIRValueKind::Literal && V.AtomType == HTN_ATOM_TYPE_STRING) Flags |= HTN_GENERATED_DEBUG_VALUE_FLAG_STRING_LITERAL;
        if (V.Kind == HTNIRValueKind::Variable && !V.DebugAsVariable && V.DebugText != V.Text) Flags |= HTN_GENERATED_DEBUG_VALUE_FLAG_CALL_EXPRESSION;
        O<<"{"<<Flags<<"u,"<<V.DebugText<<"u,"<<V.Text<<"u,"<<V.SourceLine<<"u,"<<(V.VariableSlot==kNoIndex?"HTN_NO_INDEX":std::to_string(V.VariableSlot)+"u")<<"}";
    });
    const size_t DebugVariableSlotCount=B.VariableStringIds.empty()?1u:B.VariableStringIds.size();
    Out << "static const uint32_t "<<Prefix<<"_DEBUG_VARIABLE_STRING_IDS["<<DebugVariableSlotCount<<"] = {";
    if(B.VariableStringIds.empty()) Out<<"0u"; else for(size_t I=0;I<B.VariableStringIds.size();++I){if(I)Out<<",";Out<<B.VariableStringIds[I]<<"u";} Out<<"};\n\n";
    WriteArray(Out,"HTNGeneratedDebugCondition",Prefix+"_DEBUG_CONDITIONS",B.Conditions,[](auto& O,const auto& V){O<<"{"<<ConditionKindCName(V.Kind)<<","<<(V.Id==kNoIndex?"HTN_NO_INDEX":(V.Kind==HTN_CONDITION_BUILTIN_COMPARISON?std::string(BuiltinComparisonOperatorCName(V.Id)):std::to_string(V.Id)+"u"))<<","<<V.FirstArgument<<"u,"<<V.ArgumentCount<<"u,"<<V.FirstChildRef<<"u,"<<V.ChildCount<<"u,"<<(V.OutputValue==kNoIndex?"HTN_NO_INDEX":std::to_string(V.OutputValue)+"u")<<","<<(V.ResolvedIndex==kNoIndex?"HTN_NO_INDEX":std::to_string(V.ResolvedIndex)+"u")<<","<<V.SourceLine<<"u}";});
    const size_t DebugConditionChildCount=B.ConditionChildRefs.empty()?1u:B.ConditionChildRefs.size();
    Out << "static const uint32_t "<<Prefix<<"_DEBUG_CONDITION_CHILD_REFS["<<DebugConditionChildCount<<"] = {";
    if(B.ConditionChildRefs.empty()) Out<<"0u"; else for(size_t I=0;I<B.ConditionChildRefs.size();++I){if(I)Out<<",";Out<<B.ConditionChildRefs[I]<<"u";} Out<<"};\n\n";
    WriteArray(Out,"HTNGeneratedDebugTask",Prefix+"_DEBUG_TASKS",B.Tasks,[&B](auto& O,const auto& V){
        std::string PlanStepHeadStringId = "HTN_GENERATED_NO_INDEX";
        if (V.PlanStepHeadSymbolSlot != kNoIndex)
        {
            if (V.PlanStepHeadSymbolSlot >= B.PreparedSymbolStringIds.size()) { B.SetError("Generated plan-step prepared-symbol slot is out of range"); return; }
            PlanStepHeadStringId = std::to_string(B.PreparedSymbolStringIds[V.PlanStepHeadSymbolSlot]) + "u";
        }
        O<<"{"<<TaskKindCName(V.Kind)<<","<<V.Id<<"u,"<<V.FirstArgument<<"u,"<<V.ArgumentCount<<"u,"<<V.SourceLine<<"u,"<<PlanStepHeadStringId<<"}";
    });
    WriteArray(Out,"HTNGeneratedDebugBranch",Prefix+"_DEBUG_BRANCHES",B.Branches,[](auto& O,const auto& V){O<<"{"<<V.Id<<"u,"<<(V.Condition==kNoIndex?"HTN_NO_INDEX":std::to_string(V.Condition)+"u")<<","<<V.FirstTask<<"u,"<<V.TaskCount<<"u,"<<V.SourceLine<<"u}";});
    WriteArray(Out,"HTNGeneratedDebugMethod",Prefix+"_DEBUG_METHODS",B.Methods,[](auto& O,const auto& V)
    {
        O<<"{"<<V.Id<<"u,"<<V.FirstParameter<<"u,"<<V.ParameterCount<<"u,"<<V.FirstBranch<<"u,"<<V.BranchCount<<"u,"<<V.SourceLine<<"u,{";
        for (size_t Word = 0u; Word < V.VariableSlotMask.size(); ++Word) { if (Word != 0u) O << ","; O << V.VariableSlotMask[Word] << "ull"; }
        O << "}}";
    });
    WriteArray(Out,"HTNGeneratedDebugAxiom",Prefix+"_DEBUG_AXIOMS",B.Axioms,[](auto& O,const auto& V)
    {
        O<<"{"<<V.Id<<"u,"<<V.FirstParameter<<"u,"<<V.ParameterCount<<"u,"<<(V.Condition==kNoIndex?"HTN_NO_INDEX":std::to_string(V.Condition)+"u")<<","<<V.SourceLine<<"u,{";
        for (size_t Word = 0u; Word < V.VariableSlotMask.size(); ++Word) { if (Word != 0u) O << ","; O << V.VariableSlotMask[Word] << "ull"; }
        O << "}}";
    });
    WriteArray(Out,"HTNGeneratedDebugConstant",Prefix+"_DEBUG_CONSTANTS",B.Constants,[](auto& O,const auto& V){O<<"{"<<V.GroupId<<"u,"<<V.Id<<"u,"<<V.Value<<"u,"<<V.SourceLine<<"u}";});
    const size_t DebugSourceFileCount = B.SourceFiles.empty() ? 1u : B.SourceFiles.size();
    Out << "static const char* const " << Prefix << "_DEBUG_SOURCE_FILES[" << DebugSourceFileCount << "] = {\n";
    if (B.SourceFiles.empty()) Out << "    \"" << EscapeCString(SourceFile) << "\"\n";
    else for (const auto& File : B.SourceFiles) Out << "    \"" << EscapeCString(File) << "\",\n";
    Out << "};\n\n";
    auto WriteSources = [&Out, &Prefix](const char* Name, const auto& Records)
    {
        WriteArray(Out, "HTNGeneratedDebugSourceRange", Prefix + Name, Records, [](auto& O, const auto& V)
        {
            const auto& S = V.Source;
            O << "{" << S.FileIndex << "u," << S.Range.Begin.Line << "u," << S.Range.Begin.Column
              << "u," << S.Range.End.Line << "u," << S.Range.End.Column << "u}";
        });
    };
    WriteSources("_DEBUG_VALUE_SOURCES", B.Values);
    WriteSources("_DEBUG_CONDITION_SOURCES", B.Conditions);
    WriteSources("_DEBUG_TASK_SOURCES", B.Tasks);
    WriteSources("_DEBUG_BRANCH_SOURCES", B.Branches);
    WriteSources("_DEBUG_METHOD_SOURCES", B.Methods);
    WriteSources("_DEBUG_AXIOM_SOURCES", B.Axioms);
    WriteSources("_DEBUG_CONSTANT_SOURCES", B.Constants);
    Out << "static const HTNGeneratedDebugMetadata " << Prefix << "_DEBUG_METADATA = {\n";
    Out << "    \"" << EscapeCString(SourceFile) << "\",\n";
    Out << "    " << Prefix << "_DEBUG_STRINGS," << B.Strings.Values.size() << "u,\n";
    Out << "    " << Prefix << "_DEBUG_VALUES," << B.Values.size() << "u,\n";
    Out << "    " << Prefix << "_DEBUG_VARIABLE_STRING_IDS," << B.VariableStringIds.size() << "u,\n";
    Out << "    " << Prefix << "_DEBUG_CONDITIONS," << B.Conditions.size() << "u,\n";
    Out << "    " << Prefix << "_DEBUG_CONDITION_CHILD_REFS," << B.ConditionChildRefs.size() << "u,\n";
    Out << "    " << Prefix << "_DEBUG_TASKS," << B.Tasks.size() << "u,\n";
    Out << "    " << Prefix << "_DEBUG_BRANCHES," << B.Branches.size() << "u,\n";
    Out << "    " << Prefix << "_DEBUG_METHODS," << B.Methods.size() << "u,\n";
    Out << "    " << Prefix << "_DEBUG_AXIOMS," << B.Axioms.size() << "u,\n";
    Out << "    " << Prefix << "_DEBUG_CONSTANTS," << B.Constants.size() << "u,\n";
    Out << "    " << B.CallTermStringIds.size() << "u," << B.FactStringIds.size() << "u,\n";
    Out << "    " << Prefix << "_DEBUG_SOURCE_FILES," << DebugSourceFileCount << "u,\n";
    Out << "    " << Prefix << "_DEBUG_VALUE_SOURCES,\n";
    Out << "    " << Prefix << "_DEBUG_CONDITION_SOURCES,\n";
    Out << "    " << Prefix << "_DEBUG_TASK_SOURCES,\n";
    Out << "    " << Prefix << "_DEBUG_BRANCH_SOURCES,\n";
    Out << "    " << Prefix << "_DEBUG_METHOD_SOURCES,\n";
    Out << "    " << Prefix << "_DEBUG_AXIOM_SOURCES,\n";
    Out << "    " << Prefix << "_DEBUG_CONSTANT_SOURCES\n";
    Out << "};\n";
    Out << "#endif\n\n";
    // Prepared storage is fully domain-specific and opaque to generic C++. Literal
    // strings and list topology are non-owning generated data; initialization only
    // interns symbols and wires generated storage.
    const size_t GeneratedPreparedSymbolStorageCount = std::max<size_t>(1u, B.PreparedSymbolStringIds.size());
    const size_t GeneratedPreparedValueStorageCount = std::max<size_t>(1u, B.StaticValues.size());
    const size_t GeneratedPreparedListElementStorageCount = std::max<size_t>(1u, B.ListElements.size());
    const size_t GeneratedPreparedListNodeStorageCount = std::max<size_t>(1u, B.ListChildRefs.size());

    Out << "typedef struct " << Prefix << "_PREPARED_STORAGE\n{\n";
    Out << "    const HtnSymbol* symbols[" << GeneratedPreparedSymbolStorageCount << "u];\n";
    Out << "    HTNAtom values[" << GeneratedPreparedValueStorageCount << "u];\n";
    Out << "    HTNAtom list_elements[" << GeneratedPreparedListElementStorageCount << "u];\n";
    Out << "    HTNAtomNode list_nodes[" << GeneratedPreparedListNodeStorageCount << "u];\n";
    Out << "} " << Prefix << "_PREPARED_STORAGE;\n\n";
    Out << "#define " << DomainSymbol << "_PREPARED(context) ((const " << Prefix << "_PREPARED_STORAGE*)((context)->prepared_storage))\n\n";

    Out << "static void " << Prefix << "_DESTROY_PREPARED_STORAGE(void* raw_storage)\n{\n";
    Out << "    " << Prefix << "_PREPARED_STORAGE* storage = (" << Prefix << "_PREPARED_STORAGE*)raw_storage;\n";
    if (!B.StaticValues.empty())
        Out << "    HTNAtom_DestroyRange(storage->values, " << B.StaticValues.size() << "u);\n";
    if (!B.ListElements.empty())
        Out << "    HTNAtom_DestroyRange(storage->list_elements, " << B.ListElements.size() << "u);\n";
    if (B.StaticValues.empty() && B.ListElements.empty())
        Out << "    (void)storage;\n";
    Out << "}\n\n";

    // Static atoms are initialized by generated C rather than described by metadata
    // that generic host code would interpret. Generated prepared strings borrow
    // their C literals, and generated list atoms point at generated HTNAtomNode arrays.
    const bool HasPreparedAtoms = !B.StaticValues.empty() || !B.ListElements.empty();
    const bool HasPreparedBindings = !B.PreparedSymbolStringIds.empty();
    const bool HasPreparedInitialization = HasPreparedAtoms || HasPreparedBindings;
    const bool NeedsPreparedNode = !B.ListChildRefs.empty();
    const bool NeedsPreparedSymbol =
        std::any_of(B.StaticValues.begin(), B.StaticValues.end(), [](const StaticValueRecord& V) { return V.AtomType == HTN_ATOM_TYPE_SYMBOL; }) ||
        std::any_of(B.ListElements.begin(), B.ListElements.end(), [](const ListElementRecord& V) { return V.AtomType == HTN_ATOM_TYPE_SYMBOL; });
    Out << "static int " << Prefix << "_INITIALIZE_PREPARED_STORAGE(void* raw_storage)\n{\n";
    Out << "    " << Prefix << "_PREPARED_STORAGE* storage = (" << Prefix << "_PREPARED_STORAGE*)raw_storage;\n";
    if (HasPreparedAtoms) Out << "    HTNAtom* atom;\n";
    if (NeedsPreparedNode) Out << "    HTNAtomNode* node;\n";
    if (NeedsPreparedSymbol) Out << "    const void* symbol;\n";
    if (HasPreparedInitialization) Out << "\n";

    // Intern everything before initializing HTNAtom storage. If interning fails,
    // generic C++ can free the raw block directly without any generated cleanup.
    for (size_t SymbolSlot = 0u; SymbolSlot < B.PreparedSymbolStringIds.size(); ++SymbolSlot)
    {
        const uint32 StringId = B.PreparedSymbolStringIds[SymbolSlot];
        if (StringId >= B.Strings.Values.size()) { B.SetError("Generated prepared-symbol string id is out of range"); return {}; }
        Out << "    storage->symbols[" << SymbolSlot << "u] = HtnSymbol_InternGenerated(\"" << EscapeCString(B.Strings.Values[StringId]) << "\");\n";
        Out << "    if (!storage->symbols[" << SymbolSlot << "u]) return 0;\n";
    }
    if (HasPreparedBindings && HasPreparedAtoms) Out << "\n";

    if (!B.StaticValues.empty())
        Out << "    HTNAtom_InitRange(storage->values, " << B.StaticValues.size() << "u);\n";
    if (!B.ListElements.empty())
        Out << "    HTNAtom_InitRange(storage->list_elements, " << B.ListElements.size() << "u);\n";
    if (HasPreparedAtoms) Out << "\n";


    for (size_t ReverseIndex = B.ListElements.size(); ReverseIndex > 0u; --ReverseIndex)
    {
        const size_t ElementIndex = ReverseIndex - 1u;
        const ListElementRecord& V = B.ListElements[ElementIndex];
        Out << "    atom = &storage->list_elements[" << ElementIndex << "u];\n";
        switch (V.AtomType)
        {
        case HTN_ATOM_TYPE_BOOL:
            Out << "    atom->value.bool_value = " << V.BoolValue << "u;\n";
            Out << "    atom->type = HTN_ATOM_TYPE_BOOL;\n";
            break;
        case HTN_ATOM_TYPE_INT:
            Out << "    atom->value.int_value = " << V.IntValue << ";\n";
            Out << "    atom->type = HTN_ATOM_TYPE_INT;\n";
            break;
        case HTN_ATOM_TYPE_FLOAT:
            Out << "    atom->value.float_value = " << FormatCFloatLiteral(V.FloatValue) << ";\n";
            Out << "    atom->type = HTN_ATOM_TYPE_FLOAT;\n";
            break;
        case HTN_ATOM_TYPE_STRING:
            if (V.Text >= B.Strings.Values.size()) { B.SetError("Generated list string id is out of range"); return {}; }
            Out << "    atom->value.string_value.data = (char*)\"" << EscapeCString(B.Strings.Values[V.Text]) << "\";\n";
            Out << "    atom->value.string_value.size = " << B.Strings.Values[V.Text].size() << "u;\n";
            Out << "    atom->value.string_value.capacity = HTN_ATOM_STRING_STATIC_CAPACITY;\n";
            Out << "    atom->type = HTN_ATOM_TYPE_STRING;\n";
            break;
        case HTN_ATOM_TYPE_SYMBOL:
            Out << "    symbol = storage->symbols[" << B.FindPreparedSymbolSlot(V.Text) << "u];\n";
            Out << "    atom->value.symbol_value = symbol;\n";
            Out << "    atom->type = HTN_ATOM_TYPE_SYMBOL;\n";
            break;
        case HTN_ATOM_TYPE_LIST:
            Out << "    atom->value.list_value.allocator = NULL;\n";
            if (V.ChildCount == 0u)
            {
                Out << "    atom->value.list_value.head_node = NULL;\n";
                Out << "    atom->value.list_value.tail_node = NULL;\n";
            }
            else
            {
                Out << "    atom->value.list_value.head_node = &storage->list_nodes[" << V.FirstChildRef << "u];\n";
                Out << "    atom->value.list_value.tail_node = &storage->list_nodes[" << (V.FirstChildRef + V.ChildCount - 1u) << "u];\n";
            }
            Out << "    atom->value.list_value.size = " << V.ChildCount << "u;\n";
            Out << "    atom->type = HTN_ATOM_TYPE_LIST;\n";
            for (uint32 ChildOffset = 0u; ChildOffset < V.ChildCount; ++ChildOffset)
            {
                const uint32 RefIndex = V.FirstChildRef + ChildOffset;
                if (RefIndex >= B.ListChildRefs.size()) { B.SetError("Generated list child reference is out of range"); return {}; }
                const uint32 ChildElementIndex = B.ListChildRefs[RefIndex];
                Out << "    node = &storage->list_nodes[" << RefIndex << "u];\n";
                Out << "    node->data = storage->list_elements[" << ChildElementIndex << "u];\n";
                if (ChildOffset + 1u < V.ChildCount)
                    Out << "    node->next_node = &storage->list_nodes[" << (RefIndex + 1u) << "u];\n";
                else
                    Out << "    node->next_node = NULL;\n";
                Out << "    node->allocation_cookie = NULL;\n";
            }
            break;
        case HTN_ATOM_TYPE_UNBOUND:
        default:
            // Translator semantic analysis should prevent this. Keep generated
            // cleanup correct if malformed intermediate data ever reaches here.
            if (!B.StaticValues.empty())
                Out << "    HTNAtom_DestroyRange(storage->values, " << B.StaticValues.size() << "u);\n";
            if (!B.ListElements.empty())
                Out << "    HTNAtom_DestroyRange(storage->list_elements, " << B.ListElements.size() << "u);\n";
            Out << "    return 0;\n";
            break;
        }
        Out << "\n";
    }
    for (size_t ValueIndex = 0u; ValueIndex < B.StaticValues.size(); ++ValueIndex)
    {
        const StaticValueRecord& V = B.StaticValues[ValueIndex];
        Out << "    atom = &storage->values[" << ValueIndex << "u];\n";
        switch (V.AtomType)
        {
        case HTN_ATOM_TYPE_BOOL:
            Out << "    atom->value.bool_value = " << V.BoolValue << "u;\n";
            Out << "    atom->type = HTN_ATOM_TYPE_BOOL;\n";
            break;
        case HTN_ATOM_TYPE_INT:
            Out << "    atom->value.int_value = " << V.IntValue << ";\n";
            Out << "    atom->type = HTN_ATOM_TYPE_INT;\n";
            break;
        case HTN_ATOM_TYPE_FLOAT:
            Out << "    atom->value.float_value = " << FormatCFloatLiteral(V.FloatValue) << ";\n";
            Out << "    atom->type = HTN_ATOM_TYPE_FLOAT;\n";
            break;
        case HTN_ATOM_TYPE_SYMBOL:
            Out << "    symbol = storage->symbols[" << B.FindPreparedSymbolSlot(V.Text) << "u];\n";
            Out << "    atom->value.symbol_value = symbol;\n";
            Out << "    atom->type = HTN_ATOM_TYPE_SYMBOL;\n";
            break;
        case HTN_ATOM_TYPE_LIST:
            if (V.ListElement == kNoIndex) { B.SetError("Generated static list has no prepared list element"); return {}; }
            Out << "    *atom = storage->list_elements[" << V.ListElement << "u];\n";
            break;
        case HTN_ATOM_TYPE_STRING:
        case HTN_ATOM_TYPE_UNBOUND:
        default:
            if (V.Text >= B.Strings.Values.size()) { B.SetError("Generated static string id is out of range"); return {}; }
            Out << "    atom->value.string_value.data = (char*)\"" << EscapeCString(B.Strings.Values[V.Text]) << "\";\n";
            Out << "    atom->value.string_value.size = " << B.Strings.Values[V.Text].size() << "u;\n";
            Out << "    atom->value.string_value.capacity = HTN_ATOM_STRING_STATIC_CAPACITY;\n";
            Out << "    atom->type = HTN_ATOM_TYPE_STRING;\n";
            break;
        }
        Out << "\n";
    }
    Out << "    return 1;\n}\n\n";
    Out << "static int " << DomainSymbol << "_PREPARE_FACTS(const void** fact_slots, HTNWorldState* world_state, const void* prepared_storage)\n{\n";
    if (B.FactStringIds.empty())
    {
        Out << "    (void)fact_slots;\n";
        Out << "    (void)world_state;\n";
        Out << "    (void)prepared_storage;\n";
    }
    else
    {
        for (size_t FactSlot = 0u; FactSlot < B.FactStringIds.size(); ++FactSlot)
        {
            const uint32 StringId = B.FactStringIds[FactSlot];
            if (StringId >= B.Strings.Values.size()) { B.SetError("Generated fact string id is out of range"); return {}; }
            Out << "    fact_slots[" << FactSlot << "u] = HTNWorldState_ResolveGeneratedFactTables(world_state, ((const " << Prefix << "_PREPARED_STORAGE*)prepared_storage)->symbols["
                << B.FindPreparedSymbolSlot(StringId) << "u]);\n";
            Out << "    if (!fact_slots[" << FactSlot << "u]) return 0;\n";
        }
    }
    Out << "    return 1;\n}\n\n";
    Out << "static int " << Prefix << "_PREPARE_CALLTERMS(HTNGeneratedCallTerm* callterm_slots, const HTNCallTermBindingContext* callterm_binding_context)\n{\n";
    if (B.CallTermStringIds.empty())
    {
        Out << "    (void)callterm_slots;\n";
        Out << "    (void)callterm_binding_context;\n";
    }
    else
    {
        for (size_t CallTermSlot = 0u; CallTermSlot < B.CallTermStringIds.size(); ++CallTermSlot)
        {
            const uint32 StringId = B.CallTermStringIds[CallTermSlot];
            if (StringId >= B.Strings.Values.size()) { B.SetError("Generated callterm string id is out of range"); return {}; }
            Out << "    callterm_slots[" << CallTermSlot << "u] = HTNCallTermRegistry_ResolveGeneratedCallTerm(callterm_binding_context, \""
                << EscapeCString(B.Strings.Values[StringId]) << "\");\n";
        }
    }
    Out << "    return 1;\n}\n\n";

    // Pending siblings all start from the branch-success environment. The first
    // task executes immediately, so it needs no restore snapshot. Each later task
    // only has to restore slots that its immediately preceding sibling may mutate.
    // Those mutation sets are compile-time-known: task call-expression outputs plus
    // the complete target-method variable mask for compound tasks.
    std::vector<std::vector<uint32>> TaskContinuationRestoreSlots(B.Tasks.size());
    const auto BuildTaskMutationMask = [&](const uint32 inTaskIndex)
    {
        std::array<uint64_t, HTN_GENERATED_VARIABLE_SLOT_MASK_WORDS> MutationMask{};
        if (inTaskIndex >= B.Tasks.size())
            return MutationMask;

        if (inTaskIndex < B.TaskCallExpressions.size())
        {
            for (const TaskCallExpressionRecord& Call : B.TaskCallExpressions[inTaskIndex])
                B.MarkVariableSlot(MutationMask, Call.OutputSlot);
        }

        const TaskRecord& Task = B.Tasks[inTaskIndex];
        if (Task.Kind == HTN_TASK_COMPOUND)
        {
            const int TargetMethodIndex = B.FindMethodByStringId(Task.Id);
            if (TargetMethodIndex >= 0)
            {
                const MethodRecord& TargetMethod = B.Methods[static_cast<size_t>(TargetMethodIndex)];
                for (size_t Word = 0u; Word < MutationMask.size(); ++Word)
                    MutationMask[Word] |= TargetMethod.VariableSlotMask[Word];
            }
        }
        return MutationMask;
    };

    size_t MaxContinuationRestoreSlots = 0u;
    for (const BranchRecord& Branch : B.Branches)
    {
        for (uint32 LocalTask = 1u; LocalTask < Branch.TaskCount; ++LocalTask)
        {
            const uint32 TaskIndex = Branch.FirstTask + LocalTask;
            const uint32 PreviousTaskIndex = TaskIndex - 1u;
            const auto MutationMask = BuildTaskMutationMask(PreviousTaskIndex);
            for (uint32 Word = 0u; Word < static_cast<uint32>(MutationMask.size()); ++Word)
            {
                uint64_t Bits = MutationMask[Word];
                while (Bits != 0u)
                {
                    const uint32 BitIndex = static_cast<uint32>(std::countr_zero(Bits));
                    TaskContinuationRestoreSlots[TaskIndex].push_back(Word * 64u + BitIndex);
                    Bits &= Bits - 1u;
                }
            }
            MaxContinuationRestoreSlots = std::max(
                MaxContinuationRestoreSlots,
                TaskContinuationRestoreSlots[TaskIndex].size());
        }
    }

    const size_t GeneratedVariableCount = B.VariableStringIds.size();
    const size_t GeneratedVariableStorageCount = std::max<size_t>(1u, GeneratedVariableCount);
    const size_t GeneratedBoundMaskWordCount = (GeneratedVariableCount + 63u) / 64u;
    const size_t GeneratedBoundMaskStorageWordCount = std::max<size_t>(1u, GeneratedBoundMaskWordCount);
    const size_t GeneratedFactSlotStorageCount = std::max<size_t>(1u, B.FactStringIds.size());
    const size_t GeneratedCallTermSlotStorageCount = std::max<size_t>(1u, B.CallTermStringIds.size());

    // BacktrackingCapacity limits pending continuations, not variables. A pending
    // continuation can only need the statically-known restore slots computed above,
    // so reserving one complete variable frame per continuation wastes substantial
    // execution storage without increasing the planner's actual capacity.
    const size_t RequestedSnapshotCapacity =
        static_cast<size_t>(inBacktrackingCapacity) * MaxContinuationRestoreSlots;
    const size_t GeneratedSnapshotCapacity =
        inBacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedCapacity
            ? RequestedSnapshotCapacity
            : std::min<size_t>(HTN_GENERATED_MAX_VARIABLE_SLOTS, RequestedSnapshotCapacity);
    const size_t GeneratedSnapshotStorageCount = std::max<size_t>(1u, GeneratedSnapshotCapacity);

    Out << "typedef struct " << Prefix << "_PENDING_CONTINUATION_ENTRY\n{\n";
    Out << "    HTNGeneratedTaskContinuationFn continuation;\n";
    Out << "    uint32_t snapshot_start;\n";
    Out << "    uint32_t snapshot_count;\n";
    Out << "    uint64_t variable_frame_id;\n";
    Out << "} " << Prefix << "_PENDING_CONTINUATION_ENTRY;\n\n";

    Out << "typedef struct " << Prefix << "_EXECUTION_STORAGE\n{\n";
    if (inBacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedWithOverflow)
        Out << "    HTNGeneratedBacktrackingOverflow* backtracking_overflow;\n";
#if defined(HTN_PROFILE_DETAILED) || defined(HTN_GENERATED_EXECUTION_PROFILING)
    Out << "    HTNGeneratedProfilingState* profiling;\n";
#endif
    Out << "    HTNGeneratedVariableStorage variables;\n";
    Out << "    const void* fact_prepared_storage;\n";
    Out << "    const HTNWorldState* fact_world_state;\n";
    Out << "    uint64_t fact_storage_generation;\n";
    Out << "    const void* callterm_prepared_storage;\n";
    Out << "    const HTNCallTermBindingContext* callterm_binding_context;\n";
    Out << "    HTNDecompositionStatus failure_state;\n";
    Out << "    uint64_t current_variable_frame_id;\n";
    Out << "    uint64_t next_variable_frame_id;\n";
    Out << "    uint32_t inline_pending_count;\n";
    Out << "    uint32_t inline_snapshot_count;\n";
    if (inBacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedWithOverflow)
        Out << "    uint32_t overflow_pending_count;\n";
    Out << "    uint32_t total_pending_count;\n";
    Out << "#if defined(HTN_GENERATED_EXECUTION_PROFILING)\n";
    Out << "    HTNGeneratedStructuralCounters* structural_counters;\n";
    Out << "#endif\n";
    Out << "    HTNAtom variable_values[" << GeneratedVariableStorageCount << "u];\n";
    Out << "    uint64_t variable_bound_mask[" << GeneratedBoundMaskStorageWordCount << "u];\n";
    Out << "    const void* fact_slots[" << GeneratedFactSlotStorageCount << "u];\n";
    Out << "    HTNGeneratedCallTerm callterm_slots[" << GeneratedCallTermSlotStorageCount << "u];\n";
    Out << "    " << Prefix << "_PENDING_CONTINUATION_ENTRY pending[" << inBacktrackingCapacity << "u];\n";
    Out << "    uint32_t snapshot_slots[" << GeneratedSnapshotStorageCount << "u];\n";
    Out << "    HTNAtom snapshot_values[" << GeneratedSnapshotStorageCount << "u];\n";
    Out << "} " << Prefix << "_EXECUTION_STORAGE;\n\n";

    // Axiom call scratch is a generated implementation detail. Generic host code
    // neither allocates nor interprets it.
    Out << "typedef struct " << DomainSymbol << "_AXIOM_SCOPE\n{\n";
    Out << "    HTNAtom* saved_values;\n";
    Out << "    uint8_t* saved_bound;\n";
    Out << "    uint64_t caller_frame_id;\n";
    Out << "} " << DomainSymbol << "_AXIOM_SCOPE;\n\n";

    Out << "#define HTN_GENERATED_EXECUTION(context) ((" << Prefix << "_EXECUTION_STORAGE*)((context)->execution_storage))\n";
    Out << "#define HTN_GENERATED_VARIABLES(context) (&HTN_GENERATED_EXECUTION(context)->variables)\n";
    Out << "#if defined(HTN_GENERATED_EXECUTION_PROFILING)\n";
    Out << "#define HTN_GENERATED_STRUCTURAL_COUNTERS(context) (HTN_GENERATED_EXECUTION(context)->structural_counters)\n";
    Out << "#endif\n";
#ifdef HTN_PROFILE_DETAILED
    Out << "#define HTN_GENERATED_PROFILING(context) (HTN_GENERATED_EXECUTION(context)->profiling)\n";
#endif
    Out << "\n";

    Out << "static int " << Prefix << "_INITIALIZE_EXECUTION_STORAGE(void* raw_storage)\n{\n";
    Out << "    " << Prefix << "_EXECUTION_STORAGE* storage = (" << Prefix << "_EXECUTION_STORAGE*)raw_storage;\n";
    if (inBacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedWithOverflow)
        Out << "    storage->backtracking_overflow = NULL;\n";
#if defined(HTN_PROFILE_DETAILED) || defined(HTN_GENERATED_EXECUTION_PROFILING)
    Out << "    storage->profiling = HTNGeneratedProfiling_Create();\n";
    Out << "    if (storage->profiling == NULL)\n";
    Out << "        return 0;\n";
#endif
    Out << "#if defined(HTN_GENERATED_EXECUTION_PROFILING)\n";
    Out << "    storage->structural_counters = NULL;\n";
    Out << "#endif\n";
    Out << "    storage->variables.values = storage->variable_values;\n";
    Out << "    storage->variables.bound_mask = storage->variable_bound_mask;\n";
    Out << "    storage->variables.value_count = " << GeneratedVariableCount << "u;\n";
    Out << "    HTNAtom_InitRange(storage->variable_values, " << GeneratedVariableCount << "u);\n";
    for (size_t Word = 0u; Word < GeneratedBoundMaskWordCount; ++Word)
        Out << "    storage->variable_bound_mask[" << Word << "u] = UINT64_C(0);\n";
    Out << "    storage->fact_prepared_storage = NULL;\n";
    Out << "    storage->fact_world_state = NULL;\n";
    Out << "    storage->fact_storage_generation = UINT64_C(0);\n";
    Out << "    storage->callterm_prepared_storage = NULL;\n";
    Out << "    storage->callterm_binding_context = NULL;\n";
    Out << "    storage->failure_state = HTN_DECOMPOSITION_NO_PLAN;\n";
    Out << "    storage->current_variable_frame_id = UINT64_C(1);\n";
    Out << "    storage->next_variable_frame_id = UINT64_C(2);\n";
    Out << "    storage->inline_pending_count = 0u;\n";
    Out << "    storage->inline_snapshot_count = 0u;\n";
    if (inBacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedWithOverflow)
        Out << "    storage->overflow_pending_count = 0u;\n";
    Out << "    storage->total_pending_count = 0u;\n";
    Out << "    HTNAtom_InitRange(storage->snapshot_values, " << GeneratedSnapshotCapacity << "u);\n";
    Out << "    return 1;\n";
    Out << "}\n\n";

    Out << "static void " << Prefix << "_DESTROY_EXECUTION_STORAGE(void* raw_storage)\n{\n";
    Out << "    if (raw_storage == NULL)\n";
    Out << "        return;\n";
    Out << "    " << Prefix << "_EXECUTION_STORAGE* storage = (" << Prefix << "_EXECUTION_STORAGE*)raw_storage;\n";
    if (inBacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedWithOverflow)
        Out << "    HTNGeneratedBacktracking_DestroyOverflow(storage->backtracking_overflow);\n";
#if defined(HTN_PROFILE_DETAILED) || defined(HTN_GENERATED_EXECUTION_PROFILING)
    Out << "    HTNGeneratedProfiling_Destroy(storage->profiling);\n";
#endif
    Out << "    HTNAtom_DestroyRange(storage->variable_values, " << GeneratedVariableCount << "u);\n";
    Out << "    HTNAtom_DestroyRange(storage->snapshot_values, " << GeneratedSnapshotCapacity << "u);\n";
    Out << "}\n\n";

#ifdef HTN_GENERATED_EXECUTION_PROFILING
    Out << "static HTNGeneratedProfilingState* " << Prefix << "_GET_EXECUTION_PROFILING(void* raw_storage)\n{\n";
    Out << "    return ((" << Prefix << "_EXECUTION_STORAGE*)raw_storage)->profiling;\n";
    Out << "}\n\n";
#endif

    // One immutable descriptor is enough: metadata, opaque-storage lifecycle and
    // entry point all describe the same generated planner. Keep the public entry
    // point declaration before the descriptor so its address can be stored here.
    Out << "#ifdef __cplusplus\nextern \"C\"\n#endif\n";
    Out << "HTNDecompositionStatus " << EntryPointName << "(const HTNGeneratedPlannerContext* context, const HTNAtom* call, int require_top_level, HTNAtom* out_result);\n\n";
    Out << "static const char* const " << Prefix << "_FACT_NAMES[] = {\n";
    if (B.FactStringIds.empty())
        Out << "    NULL\n";
    else
    {
        for (const uint32 StringId : B.FactStringIds)
        {
            if (StringId >= B.Strings.Values.size()) { B.SetError("Generated fact string id is out of range"); return {}; }
            Out << "    \"" << EscapeCString(B.Strings.Values[StringId]) << "\",\n";
        }
    }
    Out << "};\n\n";
    Out << "static const HTNGeneratedPlannerDefinition " << DomainSymbol << "_PLANNER_DEFINITION = {\n";
    Out << "    HTN_GENERATED_PLANNER_ABI_VERSION,\n";
    Out << "    " << (inRuntimeBacktrackingSupport == HTNGeneratedRuntimeBacktrackingSupport::Enabled
        ? "HTN_GENERATED_FEATURE_RUNTIME_BACKTRACKING"
        : "HTN_GENERATED_FEATURE_NONE") << ",\n";
    Out << "#ifdef HTN_DEBUG_DECOMPOSITION\n";
    Out << "    &" << Prefix << "_DEBUG_METADATA,\n";
    Out << "#endif\n";
    Out << "    sizeof(" << Prefix << "_PREPARED_STORAGE),\n";
    Out << "    " << Prefix << "_INITIALIZE_PREPARED_STORAGE,\n";
    Out << "    " << Prefix << "_DESTROY_PREPARED_STORAGE,\n";
    Out << "    sizeof(" << Prefix << "_EXECUTION_STORAGE),\n";
    Out << "    " << Prefix << "_INITIALIZE_EXECUTION_STORAGE,\n";
    Out << "    " << Prefix << "_DESTROY_EXECUTION_STORAGE";
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    Out << ",\n    " << Prefix << "_GET_EXECUTION_PROFILING";
#endif
    Out << ",\n    &" << EntryPointName << ",\n";
    Out << "    " << Prefix << "_FACT_NAMES,\n";
    Out << "    " << B.FactStringIds.size() << "u\n};\n\n";

    // Axiom call semantics are fully specialized here. Generated helpers own
    // the exact caller-slot save/restore sequence as well as input/output
    // materialization; the shared ABI exposes only constant-time opaque-state primitives.
    for (uint32 ConditionIndex = 0u; ConditionIndex < static_cast<uint32>(B.Conditions.size()); ++ConditionIndex)
    {
        const ConditionRecord& Condition = B.Conditions[ConditionIndex];
        if (Condition.Kind != HTN_CONDITION_AXIOM || Condition.ResolvedIndex == kNoIndex ||
            Condition.ResolvedIndex >= B.Axioms.size())
            continue;

        const AxiomRecord& Axiom = B.Axioms[Condition.ResolvedIndex];
        if (Axiom.ParameterCount != Condition.ArgumentCount) { B.SetError("Axiom call/parameter arity mismatch while emitting direct axiom helpers"); return {}; }

        Out << "static void " << GetGeneratedAxiomBeginHelperName(DomainSymbol, ConditionIndex)
            << "(const HTNGeneratedPlannerContext* context, " << DomainSymbol << "_AXIOM_SCOPE* axiom_scope)\n{\n";

        std::vector<uint32> CopiedInputs;
        for (uint32 I = 0u; I < Condition.ArgumentCount; ++I)
        {
            const ValueRecord& Parameter = B.Values[Axiom.FirstParameter + I];
            if (!GeneratedAxiomParameterIsInput(GetGeneratedAxiomParameterDirection(B, Parameter)))
                continue;

            const ValueRecord& Caller = B.Values[Condition.FirstArgument + I];
            Out << "    const HTNAtom* axiom_input_" << I << " = "
                << BuildGeneratedValueAtomReference(B, Condition.FirstArgument + I, DomainSymbol) << ";\n";

            if (Caller.Kind == HTNIRValueKind::Variable && Caller.VariableSlot != kNoIndex)
            {
                const uint32 Word = Caller.VariableSlot >> 6u;
                const uint64_t Bit = uint64_t{1} << (Caller.VariableSlot & 63u);
                if (Word < Axiom.VariableSlotMask.size() && (Axiom.VariableSlotMask[Word] & Bit) != 0u)
                {
                    CopiedInputs.push_back(I);
                    Out << "    HTNAtom axiom_input_copy_" << I << ";\n";
                    Out << "    int axiom_input_copied_" << I << " = 0;\n";
                    Out << "    if (axiom_input_" << I << ") {\n";
                    Out << "        HTNAtom_Copy(&axiom_input_copy_" << I << ", axiom_input_" << I << ");\n";
                    Out << "        axiom_input_" << I << " = &axiom_input_copy_" << I << ";\n";
                    Out << "        axiom_input_copied_" << I << " = 1;\n";
                    Out << "    }\n";
                }
            }
        }

        const uint32 AxiomScopeSlotCount = CountGeneratedAxiomScopeSlots(Axiom);
        Out << "    HTNAtom_InitRange(axiom_scope->saved_values, " << AxiomScopeSlotCount << "u);\n";
        uint32 AxiomScopeSlotIndex = 0u;
        for (uint32 Word = 0u; Word < static_cast<uint32>(Axiom.VariableSlotMask.size()); ++Word)
        {
            uint64_t Bits = Axiom.VariableSlotMask[Word];
            while (Bits != 0u)
            {
                const uint32 BitIndex = static_cast<uint32>(std::countr_zero(Bits));
                const uint32 Slot = Word * 64u + BitIndex;
                Out << "    axiom_scope->saved_bound[" << AxiomScopeSlotIndex << "u] = 0u;\n";
                Out << "    if (HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, " << Slot << "u)) {\n";
                Out << "        HTNAtom_AssignMove(&axiom_scope->saved_values[" << AxiomScopeSlotIndex
                    << "u], &HTN_GENERATED_EXECUTION(context)->variables.values[" << Slot << "u]);\n";
                EmitGeneratedVariableUnbind(W, std::to_string(Slot) + "u", "        ");
                Out << "        axiom_scope->saved_bound[" << AxiomScopeSlotIndex << "u] = 1u;\n";
                Out << "    }\n";
                ++AxiomScopeSlotIndex;
                Bits &= Bits - 1u;
            }
        }
        Out << "    axiom_scope->caller_frame_id = HTN_GENERATED_EXECUTION(context)->current_variable_frame_id;\n";
        Out << "    HTN_GENERATED_EXECUTION(context)->current_variable_frame_id = HTN_GENERATED_EXECUTION(context)->next_variable_frame_id++;\n";

        for (uint32 I = 0u; I < Condition.ArgumentCount; ++I)
        {
            const ValueRecord& Parameter = B.Values[Axiom.FirstParameter + I];
            if (!GeneratedAxiomParameterIsInput(GetGeneratedAxiomParameterDirection(B, Parameter)))
                continue;
            if (Parameter.VariableSlot == kNoIndex) { B.SetError("Generated axiom input parameter has no variable slot"); return {}; }
            Out << "    if (axiom_input_" << I << ") ";
            EmitGeneratedSetCopyIfChanged(W, Parameter.VariableSlot, "axiom_input_" + std::to_string(I), "");
        }

        for (const uint32 I : CopiedInputs)
            Out << "    if (axiom_input_copied_" << I << ") HTNAtom_Destroy(&axiom_input_copy_" << I << ");\n";

        Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_AXIOM(context, &" << DomainSymbol
            << "_PLANNER_DEFINITION, " << Condition.ResolvedIndex << "u);\n";
        Out << "}\n\n";

        Out << "static int " << GetGeneratedAxiomEndHelperName(DomainSymbol, ConditionIndex)
            << "(const HTNGeneratedPlannerContext* context, int succeeded, " << DomainSymbol << "_AXIOM_SCOPE* axiom_scope)\n{\n";
        Out << "    int valid = succeeded != 0;\n";

        for (uint32 I = 0u; I < Condition.ArgumentCount; ++I)
        {
            const ValueRecord& Parameter = B.Values[Axiom.FirstParameter + I];
            if (!GeneratedAxiomParameterIsOutput(GetGeneratedAxiomParameterDirection(B, Parameter)))
                continue;
            const ValueRecord& Caller = B.Values[Condition.FirstArgument + I];
            if (Parameter.VariableSlot == kNoIndex) { B.SetError("Generated axiom output parameter has no variable slot"); return {}; }

            Out << "    if (valid) {\n";
            Out << "        const HTNAtom* axiom_output_" << I
                << " = HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, " << Parameter.VariableSlot << "u);\n";
            Out << "        if (!axiom_output_" << I << " || !HTNAtom_IsBound(axiom_output_" << I << ")) valid = 0;\n";
            if (Caller.Kind != HTNIRValueKind::Variable)
            {
                Out << "        else if (!HTNAtom_Equals(axiom_output_" << I << ", "
                    << BuildGeneratedValueAtomReference(B, Condition.FirstArgument + I, DomainSymbol) << ")) valid = 0;\n";
            }
            Out << "    }\n";
        }

        std::vector<uint32> VariableOutputs;
        for (uint32 I = 0u; I < Condition.ArgumentCount; ++I)
        {
            const ValueRecord& Parameter = B.Values[Axiom.FirstParameter + I];
            const ValueRecord& Caller = B.Values[Condition.FirstArgument + I];
            if (!GeneratedAxiomParameterIsOutput(GetGeneratedAxiomParameterDirection(B, Parameter)) ||
                Caller.Kind != HTNIRValueKind::Variable)
                continue;
            if (Caller.VariableSlot == kNoIndex || Parameter.VariableSlot == kNoIndex) { B.SetError("Generated axiom variable output has no caller/parameter slot"); return {}; }

            VariableOutputs.push_back(I);
            Out << "    HTNAtom axiom_output_copy_" << I << ";\n";
            Out << "    int axiom_output_copied_" << I << " = 0;\n";
        }

        if (!VariableOutputs.empty())
        {
            Out << "    if (valid) {\n";
            for (const uint32 I : VariableOutputs)
            {
                const ValueRecord& Parameter = B.Values[Axiom.FirstParameter + I];
                Out << "        HTNAtom_Copy(&axiom_output_copy_" << I
                    << ", HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, " << Parameter.VariableSlot << "u));\n";
                Out << "        axiom_output_copied_" << I << " = 1;\n";
            }
            Out << "    }\n";
        }

        Out << "    HTN_GENERATED_EVENT_DEBUG_END_AXIOM(context, &" << DomainSymbol << "_PLANNER_DEFINITION, valid);\n";
        AxiomScopeSlotIndex = 0u;
        for (uint32 Word = 0u; Word < static_cast<uint32>(Axiom.VariableSlotMask.size()); ++Word)
        {
            uint64_t Bits = Axiom.VariableSlotMask[Word];
            while (Bits != 0u)
            {
                const uint32 BitIndex = static_cast<uint32>(std::countr_zero(Bits));
                const uint32 Slot = Word * 64u + BitIndex;
                EmitGeneratedVariableUnbind(W, std::to_string(Slot) + "u", "    ");
                Out << "    if (axiom_scope->saved_bound[" << AxiomScopeSlotIndex << "u]) {\n";
                EmitGeneratedVariableSetMove(W, std::to_string(Slot) + "u",
                                             "&axiom_scope->saved_values[" + std::to_string(AxiomScopeSlotIndex) + "u]", "        ");
                Out << "    }\n";
                ++AxiomScopeSlotIndex;
                Bits &= Bits - 1u;
            }
        }
        Out << "    HTNAtom_DestroyRange(axiom_scope->saved_values, " << AxiomScopeSlotCount << "u);\n";
        Out << "    HTN_GENERATED_EXECUTION(context)->current_variable_frame_id = axiom_scope->caller_frame_id;\n";

        if (!VariableOutputs.empty())
        {
            Out << "    if (valid) {\n";
            for (const uint32 I : VariableOutputs)
            {
                const ValueRecord& Caller = B.Values[Condition.FirstArgument + I];
                EmitGeneratedSetCopyIfChanged(W, Caller.VariableSlot,
                                              "&axiom_output_copy_" + std::to_string(I), "        ");
            }
            Out << "    }\n";
            for (const uint32 I : VariableOutputs)
                Out << "    if (axiom_output_copied_" << I << ") HTNAtom_Destroy(&axiom_output_copy_" << I << ");\n";
        }

        Out << "    return valid;\n";
        Out << "}\n\n";
    }

    bool NeedsBuiltinComparisonHelper = false;
    bool NeedsArithmeticHelper = false;
    for (const ConditionRecord& Condition : B.Conditions)
    {
        if (Condition.Kind != HTN_CONDITION_BUILTIN_COMPARISON)
            continue;
        if (Condition.ArgumentCount == 2u &&
            (B.Values[Condition.FirstArgument].Kind == HTNIRValueKind::Arithmetic ||
             B.Values[Condition.FirstArgument + 1u].Kind == HTNIRValueKind::Arithmetic))
            NeedsArithmeticHelper = true;
        bool StaticResult = false;
        if (!TryEvaluateStaticBuiltinComparison(B, Condition, StaticResult))
            NeedsBuiltinComparisonHelper = true;
    }
    if (NeedsArithmeticHelper)
    {
        Out << "static int " << DomainSymbol << "_EVALUATE_ARITHMETIC(const HTNAtom* const* operands, uint32_t count, uint32_t op, HTNAtom* result)\n{\n";
        Out << "    uint32_t i; int use_float = 0; int64_t integer_result = 0; double float_result = 0.0;\n";
        Out << "    if (!operands || !result || count == 0u) return 0;\n";
        Out << "    for (i = 0u; i < count; ++i) { if (!operands[i] || !HTNAtom_IsBound(operands[i]) || (operands[i]->type != HTN_ATOM_TYPE_INT && operands[i]->type != HTN_ATOM_TYPE_FLOAT)) return 0; if (operands[i]->type == HTN_ATOM_TYPE_FLOAT) use_float = 1; }\n";
        Out << "    if (op == 4u && (count != 2u || use_float)) return 0;\n";
        Out << "    if (op == 5u || op == 6u) { double value; if (count != 1u) return 0; if (!use_float) { const int64_t incremented = (int64_t)operands[0]->value.int_value + (op == 5u ? 1 : -1); if (incremented < INT32_MIN || incremented > INT32_MAX) return 0; HTNAtom_SetInt(result, (int32_t)incremented); return 1; } value = (double)operands[0]->value.float_value + (op == 5u ? 1.0 : -1.0); if (value != value || value < -(double)FLT_MAX || value > (double)FLT_MAX) return 0; HTNAtom_SetFloat(result, (float)value); return 1; }\n";
        Out << "    if (!use_float) {\n";
        Out << "        integer_result = op == 2u ? 1 : (int64_t)operands[0]->value.int_value;\n";
        Out << "        i = op == 2u ? 0u : 1u;\n";
        Out << "        if (op == 1u && count == 1u) { integer_result = -(int64_t)operands[0]->value.int_value; i = count; }\n";
        Out << "        for (; i < count; ++i) { const int64_t value = operands[i]->value.int_value; switch (op) { case 0u: integer_result += value; break; case 1u: integer_result -= value; break; case 2u: integer_result *= value; break; case 3u: if (value == 0) return 0; integer_result /= value; break; case 4u: if (value == 0) return 0; integer_result %= value; break; default: return 0; } if (integer_result < INT32_MIN || integer_result > INT32_MAX) return 0; }\n";
        Out << "        HTNAtom_SetInt(result, (int32_t)integer_result); return 1;\n";
        Out << "    }\n";
        Out << "    float_result = op == 2u ? 1.0 : (operands[0]->type == HTN_ATOM_TYPE_INT ? (double)operands[0]->value.int_value : (double)operands[0]->value.float_value);\n";
        Out << "    i = op == 2u ? 0u : 1u;\n";
        Out << "    if (op == 1u && count == 1u) { float_result = -float_result; i = count; }\n";
        Out << "    for (; i < count; ++i) { const double value = operands[i]->type == HTN_ATOM_TYPE_INT ? (double)operands[i]->value.int_value : (double)operands[i]->value.float_value; switch (op) { case 0u: float_result += value; break; case 1u: float_result -= value; break; case 2u: float_result *= value; break; case 3u: if (value == 0.0) return 0; float_result /= value; break; default: return 0; } if (float_result != float_result || float_result < -(double)FLT_MAX || float_result > (double)FLT_MAX) return 0; }\n";
        Out << "    HTNAtom_SetFloat(result, (float)float_result); return 1;\n";
        Out << "}\n\n";
    }
    if (NeedsBuiltinComparisonHelper)
    {
        Out << "static int " << DomainSymbol << "_COMPARE_ATOMS(const HTNAtom* left, const HTNAtom* right, HTNGeneratedBuiltinComparisonOperator op)\n{\n";
        Out << "    if (!left || !right || !HTNAtom_IsBound(left) || !HTNAtom_IsBound(right)) return 0;\n";
        Out << "    const int left_is_number = left->type == HTN_ATOM_TYPE_INT || left->type == HTN_ATOM_TYPE_FLOAT;\n";
        Out << "    const int right_is_number = right->type == HTN_ATOM_TYPE_INT || right->type == HTN_ATOM_TYPE_FLOAT;\n";
        Out << "    const double left_number = left->type == HTN_ATOM_TYPE_INT ? (double)left->value.int_value : (left->type == HTN_ATOM_TYPE_FLOAT ? (double)left->value.float_value : 0.0);\n";
        Out << "    const double right_number = right->type == HTN_ATOM_TYPE_INT ? (double)right->value.int_value : (right->type == HTN_ATOM_TYPE_FLOAT ? (double)right->value.float_value : 0.0);\n";
        Out << "    if (op == HTN_BUILTIN_COMPARE_EQUAL || op == HTN_BUILTIN_COMPARE_NOT_EQUAL) {\n";
        Out << "        const int equal = left_is_number && right_is_number ? left_number == right_number : HTNAtom_Equals(left, right);\n";
        Out << "        return op == HTN_BUILTIN_COMPARE_EQUAL ? equal : !equal;\n";
        Out << "    }\n";
        Out << "    if (!left_is_number || !right_is_number) return 0;\n";
        Out << "    switch (op) {\n";
        Out << "    case HTN_BUILTIN_COMPARE_LESS: return left_number < right_number;\n";
        Out << "    case HTN_BUILTIN_COMPARE_LESS_EQUAL: return left_number <= right_number;\n";
        Out << "    case HTN_BUILTIN_COMPARE_GREATER: return left_number > right_number;\n";
        Out << "    case HTN_BUILTIN_COMPARE_GREATER_EQUAL: return left_number >= right_number;\n";
        Out << "    default: return 0;\n";
        Out << "    }\n}\n\n";
    }

    // Multi-solution axiom iterators are generated as ordinary C helpers.
    // They intentionally live in generated execution storage so no generic
    // Fact choice helpers enumerate matching WorldState rows directly in generated C.
    // Emit one helper for each fact that can bind at least one variable; whether it
    // becomes a choice point at a particular callsite depends on the bound set there.
    for (uint32 ConditionIndex = 0; ConditionIndex < static_cast<uint32>(B.Conditions.size()); ++ConditionIndex)
    {
        const ConditionRecord& Condition = B.Conditions[ConditionIndex];
        if (Condition.Kind != HTN_CONDITION_FACT)
            continue;
        bool HasVariable = false;
        for (uint32 I = 0; I < Condition.ArgumentCount; ++I)
        {
            const uint32 ValueIndex = Condition.FirstArgument + I;
            if (ValueIndex < B.Values.size() && B.Values[ValueIndex].Kind == HTNIRValueKind::Variable)
            {
                HasVariable = true;
                break;
            }
        }
        if (HasVariable)
            EmitGeneratedFactChoiceHelper(W, B, ConditionIndex, DomainSymbol);
    }

    // Emit helpers only for axiom conditions referenced by compiled domains.
    for (uint32 ConditionIndex = 0; ConditionIndex < static_cast<uint32>(B.Conditions.size()); ++ConditionIndex)
    {
        if (B.Conditions[ConditionIndex].Kind != HTN_CONDITION_AXIOM)
            continue;
        const ConditionAnalysis AxiomAnalysis = AnalyzeCondition(B, ConditionIndex, BoundVariableSet{});
        if (!AxiomAnalysis.MayProduceMultipleSolutions)
            continue;
        EmitGeneratedAxiomChoiceHelper(W, B, ConditionIndex, DomainSymbol);
    }

    // Task identity and method targets are compile-time properties. Pending work
    // preserves the planning snapshot at each task, while each
    // entry stores its generated continuation directly: there is no task-id
    // switch/dispatcher in generated code.
    std::vector<std::string> MethodFunctions(B.Methods.size());
    std::vector<std::string> TaskFunctions(B.Tasks.size());
    for (size_t M = 0; M < B.Methods.size(); ++M)
        MethodFunctions[M] = Prefix + "_METHOD_" + std::to_string(M);
    for (size_t T = 0; T < B.Tasks.size(); ++T)
        TaskFunctions[T] = Prefix + "_TASK_" + std::to_string(T);

    // Callterm arguments are resolved directly to HTNAtom references at each
    // generated callsite. The generated path invokes the external binding over those atoms
    // and does not interpret argument kinds or own per-callterm argument scratch.

    // Compound arguments are resolved directly to HTNAtom references at each
    // generated callsite. Generated execution only preserves/copies those concrete values
    // before replacing the variable frame; it no longer interprets binding kinds.

    // Compile-time self-tail-call detection. A task record is an occurrence in one
    // branch, so it is safe to mark it when it is the final task of that branch
    // and resolves to the method that owns the branch. The dispatcher already
    // trampolines tasks, therefore lowering a self tail-call means reusing the
    // current logical variable frame rather than preserving a dead caller frame.
    std::vector<int> SelfTailMethodByTask(B.Tasks.size(), -1);
    for (size_t MethodIndex = 0; MethodIndex < B.Methods.size(); ++MethodIndex)
    {
        const MethodRecord& Method = B.Methods[MethodIndex];
        for (uint32 LocalBranch = 0u; LocalBranch < Method.BranchCount; ++LocalBranch)
        {
            const BranchRecord& Branch = B.Branches[Method.FirstBranch + LocalBranch];
            if (Branch.TaskCount == 0u)
                continue;
            const uint32 TailTaskIndex = Branch.FirstTask + Branch.TaskCount - 1u;
            if (TailTaskIndex >= B.Tasks.size())
                continue;
            const TaskRecord& TailTask = B.Tasks[TailTaskIndex];
            if (TailTask.Kind != HTN_TASK_COMPOUND)
                continue;
            const int TargetMethod = B.FindMethodByStringId(TailTask.Id);
            if (TargetMethod == static_cast<int>(MethodIndex))
                SelfTailMethodByTask[TailTaskIndex] = TargetMethod;
        }
    }

    // Keep the public definition accessor declaration explicit and stable for engine/build integration.
    Out << "#ifdef __cplusplus\nextern \"C\"\n#endif\n";
    Out << "HTN_GENERATED_MODULE_EXPORT const HTNGeneratedPlannerDefinition* " << EntryPointName << "_GetDefinition(void);\n\n";
    for (const std::string& Function : MethodFunctions)
        Out << "static int " << Function << "(const HTNGeneratedPlannerContext* context, HTNAtom* out_result);\n";
    for (const std::string& Function : TaskFunctions)
        Out << "static int " << Function << "(const HTNGeneratedPlannerContext* context, HTNAtom* out_result);\n";
    Out << "\n";

    for (size_t TaskIndex = 0u; TaskIndex < TaskContinuationRestoreSlots.size(); ++TaskIndex)
    {
        if (TaskContinuationRestoreSlots[TaskIndex].empty())
            continue;
        Out << "static const uint32_t " << Prefix << "_TASK_RESTORE_SLOTS_" << TaskIndex
            << "[" << TaskContinuationRestoreSlots[TaskIndex].size() << "u] = {";
        for (size_t I = 0u; I < TaskContinuationRestoreSlots[TaskIndex].size(); ++I)
        {
            if (I != 0u)
                Out << ", ";
            Out << TaskContinuationRestoreSlots[TaskIndex][I] << "u";
        }
        Out << "};\n";
    }

    // Per-task restore slot tables above are also used directly by the overflow fallback.

    // Pending continuation metadata and restore slot sets are compile-time-known.
    // Emit a branch-specialized inline push so the normal scheduling path stays in
    // generated C. The C++ bridge is only an arbitrary-depth overflow fallback.
    for (size_t BranchIndex = 0; BranchIndex < B.Branches.size(); ++BranchIndex)
    {
        const BranchRecord& Branch = B.Branches[BranchIndex];
        if (Branch.TaskCount == 0u)
            continue;

        size_t TotalRestoreSlots = 0u;
        for (uint32 TI = 0u; TI < Branch.TaskCount; ++TI)
        {
            const uint32 TaskIndex = Branch.FirstTask + (Branch.TaskCount - 1u - TI);
            TotalRestoreSlots += TaskContinuationRestoreSlots[TaskIndex].size();
        }

        Out << "static int " << Prefix << "_PUSH_BRANCH_CONTINUATIONS_" << BranchIndex
            << "(const HTNGeneratedPlannerContext* context)\n{\n";
        Out << "    " << Prefix << "_EXECUTION_STORAGE* storage = HTN_GENERATED_EXECUTION(context);\n";
        Out << "    const uint64_t variable_frame_id = HTN_GENERATED_EXECUTION(context)->current_variable_frame_id;\n";
        const bool StaticCapacityExceeded =
            Branch.TaskCount > inBacktrackingCapacity || TotalRestoreSlots > GeneratedSnapshotCapacity;

        if (StaticCapacityExceeded)
        {
            Out << "    {\n";
        }
        else
        {
            Out << "    if ("
                << (inBacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedWithOverflow ? "storage->overflow_pending_count != 0u || " : "")
                << "storage->inline_pending_count > " << inBacktrackingCapacity << "u - " << Branch.TaskCount << "u || "
                << "storage->inline_snapshot_count > " << GeneratedSnapshotCapacity << "u - " << TotalRestoreSlots << "u) {\n";
        }

        if (inBacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedCapacity)
        {
            Out << "        storage->failure_state = HTN_DECOMPOSITION_BACKTRACKING_CAPACITY_EXCEEDED;\n";
            Out << "        return 0;\n";
        }
        else
        {
            Out << "        if (!storage->backtracking_overflow) { storage->backtracking_overflow = HTNGeneratedBacktracking_CreateOverflow(); if (!storage->backtracking_overflow) { storage->failure_state = HTN_DECOMPOSITION_OUT_OF_MEMORY; return 0; } }\n";
            for (uint32 TI = 0u; TI < Branch.TaskCount; ++TI)
            {
                const uint32 TaskIndex = Branch.FirstTask + (Branch.TaskCount - 1u - TI);
                const auto& RestoreSlots = TaskContinuationRestoreSlots[TaskIndex];
                Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_PENDING_PUSH);\n";
                Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONTINUATION_TRAIL);\n";
                for (const uint32 Slot : RestoreSlots)
                {
                    Out << "        { const HTNAtom* snapshot_value = HTNGeneratedVariables_Get(&storage->variables, " << Slot << "u); "
                        << "if (!HTNGeneratedBacktracking_PushContinuationSnapshotOverflow(storage->backtracking_overflow, " << Slot << "u, snapshot_value)) { "
                        << "HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONTINUATION_TRAIL); "
                        << "storage->failure_state = HTN_DECOMPOSITION_OUT_OF_MEMORY; HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_PENDING_PUSH); return 0; } }\n";
                }
                Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONTINUATION_TRAIL);\n";
                Out << "        if (!HTNGeneratedBacktracking_PushPendingContinuationOverflow(storage->backtracking_overflow, variable_frame_id, &"
                    << TaskFunctions[TaskIndex] << ", " << RestoreSlots.size() << "u)) { storage->failure_state = HTN_DECOMPOSITION_OUT_OF_MEMORY; HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_PENDING_PUSH); return 0; }\n";
                Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_PENDING_PUSH);\n";
            }
            Out << "        storage->overflow_pending_count += " << Branch.TaskCount << "u;\n";
            Out << "        storage->total_pending_count += " << Branch.TaskCount << "u;\n";
            Out << "        return 1;\n";
        }
        Out << "    }\n";

        for (uint32 TI = 0u; TI < Branch.TaskCount; ++TI)
        {
            const uint32 TaskIndex = Branch.FirstTask + (Branch.TaskCount - 1u - TI);
            const auto& RestoreSlots = TaskContinuationRestoreSlots[TaskIndex];
            Out << "    {\n";
            Out << "        " << Prefix << "_PENDING_CONTINUATION_ENTRY* pending;\n";
            Out << "        const uint32_t snapshot_start = storage->inline_snapshot_count;\n";
            Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_PENDING_PUSH);\n";
            Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONTINUATION_TRAIL);\n";
            for (const uint32 Slot : RestoreSlots)
            {
                Out << "        storage->snapshot_slots[storage->inline_snapshot_count] = " << Slot << "u;\n";
                Out << "        { const HTNAtom* snapshot_value = HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, " << Slot << "u); "
                    << "if (snapshot_value) HTNAtom_AssignCopy(&storage->snapshot_values[storage->inline_snapshot_count], snapshot_value); "
                    << "else HTNAtom_Unbind(&storage->snapshot_values[storage->inline_snapshot_count]); }\n";
                Out << "        ++storage->inline_snapshot_count;\n";
            }
            Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONTINUATION_TRAIL);\n";
            Out << "        pending = &storage->pending[storage->inline_pending_count++];\n";
            Out << "        pending->continuation = &" << TaskFunctions[TaskIndex] << ";\n";
            Out << "        pending->snapshot_start = snapshot_start;\n";
            Out << "        pending->snapshot_count = " << RestoreSlots.size() << "u;\n";
            Out << "        pending->variable_frame_id = variable_frame_id;\n";
            Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_PENDING_PUSH);\n";
            Out << "    }\n";
        }
        Out << "    storage->total_pending_count += " << Branch.TaskCount << "u;\n";
        Out << "    return 1;\n";
        Out << "}\n";
    }
    Out << "\n";

    // Pop one pending continuation and restore the variable snapshot captured for it.
    // Methods use the same helper while validating a committed branch subtree, so a
    // child decomposition failure is observed before the parent method returns.
    Out << "static HTNGeneratedTaskContinuationFn " << Prefix << "_POP_PENDING_CONTINUATION(const HTNGeneratedPlannerContext* context)\n{\n";
    Out << "    " << Prefix << "_EXECUTION_STORAGE* storage = HTN_GENERATED_EXECUTION(context);\n";
    Out << "    HTNGeneratedTaskContinuationFn continuation = 0;\n";
    Out << "    if (storage->total_pending_count == 0u) return 0;\n";
    if (inBacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedWithOverflow)
    {
        Out << "    if (storage->overflow_pending_count != 0u) {\n";
        Out << "        uint32_t restore_snapshot_count = 0u;\n";
        Out << "        uint32_t restore_snapshot_index;\n";
        Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_PENDING_POP);\n";
        Out << "        continuation = HTNGeneratedBacktracking_PopPendingContinuationOverflow(storage->backtracking_overflow, &storage->current_variable_frame_id, &restore_snapshot_count);\n";
        Out << "        if (continuation) {\n";
        Out << "            HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONTINUATION_TRAIL);\n";
        Out << "            for (restore_snapshot_index = 0u; restore_snapshot_index < restore_snapshot_count; ++restore_snapshot_index) {\n";
        Out << "                HTNAtom snapshot_value;\n";
        Out << "                uint32_t variable_slot;\n";
        Out << "                HTNGeneratedBacktracking_PopContinuationSnapshotOverflow(storage->backtracking_overflow, &variable_slot, &snapshot_value);\n";
        Out << "                if (HTNAtom_IsBound(&snapshot_value)) {\n";
        EmitGeneratedVariableSetMove(W, "variable_slot", "&snapshot_value", "                    ");
        Out << "                } else {\n";
        EmitGeneratedVariableUnbind(W, "variable_slot", "                    ");
        Out << "                }\n";
        Out << "                HTNAtom_Destroy(&snapshot_value);\n";
        Out << "            }\n";
        Out << "            HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONTINUATION_TRAIL);\n";
        Out << "            --storage->overflow_pending_count;\n";
        Out << "            --storage->total_pending_count;\n";
        Out << "        }\n";
        Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_PENDING_POP);\n";
        Out << "        return continuation;\n";
        Out << "    }\n";
    }
    Out << "    {\n";
    Out << "        " << Prefix << "_PENDING_CONTINUATION_ENTRY* pending;\n";
    Out << "        uint32_t snapshot_index;\n";
    Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_PENDING_POP);\n";
    Out << "        pending = &storage->pending[--storage->inline_pending_count];\n";
    Out << "        HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONTINUATION_TRAIL);\n";
    Out << "        for (snapshot_index = pending->snapshot_start; snapshot_index < pending->snapshot_start + pending->snapshot_count; ++snapshot_index) {\n";
    Out << "            HTNAtom* snapshot_value = &storage->snapshot_values[snapshot_index];\n";
    Out << "            const uint32_t variable_slot = storage->snapshot_slots[snapshot_index];\n";
    Out << "            if (HTNAtom_IsBound(snapshot_value)) {\n";
    EmitGeneratedVariableSetMove(W, "variable_slot", "snapshot_value", "                ");
    Out << "            } else {\n";
    EmitGeneratedVariableUnbind(W, "variable_slot", "                ");
    Out << "            }\n";
    Out << "        }\n";
    Out << "        storage->inline_snapshot_count = pending->snapshot_start;\n";
    Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONTINUATION_TRAIL);\n";
    Out << "        storage->current_variable_frame_id = pending->variable_frame_id;\n";
    Out << "        continuation = pending->continuation;\n";
    Out << "        --storage->total_pending_count;\n";
    Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_PENDING_POP);\n";
    Out << "    }\n";
    Out << "    return continuation;\n";
    Out << "}\n\n";

    // A continuation executes one already-popped task. Compound continuations
    // only select/decompose their method and queue that method's children. The
    // dispatcher then consumes those children before older sibling snapshots,
    // exactly matching the old LIFO HTNTaskInstance behaviour.
    for (size_t T = 0; T < B.Tasks.size(); ++T)
    {
        const auto& Task = B.Tasks[T];
        Out << "static int " << TaskFunctions[T] << "(const HTNGeneratedPlannerContext* context, HTNAtom* out_result)\n{\n";
        Out << "    (void)context;\n";
        Out << "    (void)out_result;\n";
        Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_GENERATED_TASK);\n";
        Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_TASK(context, &" << DomainSymbol << "_PLANNER_DEFINITION, " << T << "u);\n";
        if (T < B.TaskCallExpressions.size())
        {
            for (size_t C = 0; C < B.TaskCallExpressions[T].size(); ++C)
            {
                const TaskCallExpressionRecord& Call = B.TaskCallExpressions[T][C];
                const std::string ArgumentsArray = "call_arguments_" + std::to_string(C);
                if (!Call.Arguments.empty())
                {
                    Out << "    const HTNAtom* " << ArgumentsArray << "[" << Call.Arguments.size() << "u] = {";
                    for (size_t I = 0u; I < Call.Arguments.size(); ++I)
                    {
                        if (I > 0u)
                            Out << ", ";
                        const ValueRecord& Argument = Call.Arguments[I];
                        if (Argument.Kind == HTNIRValueKind::Variable)
                        {
                            if (Argument.VariableSlot == kNoIndex)
                                Out << "NULL";
                            else
                                Out << "HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, " << Argument.VariableSlot << "u)";
                        }
                        else
                        {
                            if (Argument.StaticValueIndex == kNoIndex) { B.SetError("Generated task call argument has no prepared-value index"); return {}; }
                            Out << "&" << DomainSymbol << "_PREPARED(context)->values["
                                << Argument.StaticValueIndex << "u]";
                        }
                    }
                    Out << "};\n";
                }
                const std::string CallResult = "call_result_" + std::to_string(C);
                Out << "    HTNAtom " << CallResult << ";\n";
                Out << "    HTNAtom_Init(&" << CallResult << ");\n";
                const std::string CallSucceeded = "call_succeeded_" + std::to_string(C);
                if (Call.CallTermSlot >= B.CallTermStringIds.size()) { B.SetError("Generated task call expression has invalid callterm slot"); return {}; }
                W.DomainExpressionComment(Call.DomainExpression);
                Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CALLTERM);\n";
                Out << "    const int " << CallSucceeded << " = HTNCallTermRegistry_InvokeGeneratedCallTerm(context->callterm_binding_context, &HTN_GENERATED_EXECUTION(context)->callterm_slots["
                    << Call.CallTermSlot << "u], ";
                if (Call.Arguments.empty())
                    Out << "0, 0u";
                else
                    Out << ArgumentsArray << ", " << Call.Arguments.size() << "u";
                Out << ", &" << CallResult << ");\n";
                Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CALLTERM);\n";
                Out << "    { const uint64_t fact_storage_generation = HTNWorldState_GetFactStorageGeneration(context->world_state);\n";
                Out << "      if (HTN_GENERATED_EXECUTION(context)->fact_storage_generation != fact_storage_generation) {\n";
                Out << "          if (!" << DomainSymbol << "_PREPARE_FACTS(HTN_GENERATED_EXECUTION(context)->fact_slots, context->world_state, context->prepared_storage)) {\n";
                Out << "              HTNAtom_Destroy(&" << CallResult << ");\n";
                Out << "              HTN_GENERATED_EXECUTION(context)->failure_state = HTN_DECOMPOSITION_PREPARATION_FAILED;\n";
                Out << "              HTN_GENERATED_EVENT_DEBUG_END_TASK(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
                Out << "              HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_TASK);\n";
                Out << "              return 0;\n";
                Out << "          }\n";
                Out << "          HTN_GENERATED_EXECUTION(context)->fact_storage_generation = fact_storage_generation;\n";
                Out << "      } }\n";
                Out << "    if (!" << CallSucceeded << ") {\n";
                Out << "        HTNAtom_Destroy(&" << CallResult << ");\n";
                Out << "        HTN_GENERATED_EVENT_DEBUG_END_TASK(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
                Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_TASK);\n";
                Out << "        return 0;\n";
                Out << "    }\n";
                EmitGeneratedSetMoveIfChanged(W, Call.OutputSlot, "&" + CallResult, "    ");
                Out << "    HTNAtom_Destroy(&" << CallResult << ");\n";
            }
        }
        if (Task.Kind == HTN_TASK_PRIMITIVE || Task.Kind == HTN_TASK_DEFERRED)
        {
            if (Task.ArgumentCount > 0u)
            {
                Out << "    const HTNAtom* plan_step_arguments[" << Task.ArgumentCount << "u] = {";
                for (uint32 ArgumentIndex = 0u; ArgumentIndex < Task.ArgumentCount; ++ArgumentIndex)
                {
                    if (ArgumentIndex > 0u)
                        Out << ", ";
                    Out << BuildGeneratedValueAtomReference(B, Task.FirstArgument + ArgumentIndex, DomainSymbol);
                }
                Out << "};\n";
            }
            W.DomainExpressionComment(Task.DomainExpression);
            Out << "    {\n";
            Out << "        HTNAtom plan_step;\n";
            Out << "        if (!HTNAtom_CreateCallFromPointers(&plan_step, " << DomainSymbol << "_PREPARED(context)->symbols["
                << Task.PlanStepHeadSymbolSlot << "u], ";
            if (Task.ArgumentCount > 0u)
                Out << "plan_step_arguments, " << Task.ArgumentCount << "u";
            else
                Out << "NULL, 0u";
            Out << ")) {\n";
            Out << "            HTN_GENERATED_EXECUTION(context)->failure_state = HTN_DECOMPOSITION_OUT_OF_MEMORY;\n";
            Out << "            HTN_GENERATED_EVENT_DEBUG_END_TASK(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
            Out << "            HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_TASK);\n";
            Out << "            return 0;\n";
            Out << "        }\n";
            Out << "        if (!HTNAtom_PushBackListElementMove(out_result, &plan_step)) {\n";
            Out << "            HTNAtom_Destroy(&plan_step);\n";
            Out << "            HTN_GENERATED_EXECUTION(context)->failure_state = HTN_DECOMPOSITION_OUT_OF_MEMORY;\n";
            Out << "            HTN_GENERATED_EVENT_DEBUG_END_TASK(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
            Out << "            HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_TASK);\n";
            Out << "            return 0;\n";
            Out << "        }\n";
            Out << "        HTNAtom_Destroy(&plan_step);\n";
            Out << "    }\n";
            Out << "    HTN_GENERATED_EVENT_DEBUG_END_TASK(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 1);\n";
            Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_TASK);\n";
            Out << "    return 1;\n";
        }
        else
        {
            const int MethodIndex = B.FindMethodByStringId(Task.Id);
            if (MethodIndex < 0)
            {
                Out << "    HTN_GENERATED_EVENT_DEBUG_END_TASK(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
                Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_TASK);\n";
                Out << "    return 0; /* unresolved compound task */\n";
            }
            else
            {
                W.DomainExpressionComment(Task.DomainExpression);
                const bool IsSelfTailCall = SelfTailMethodByTask[T] == MethodIndex;
                const MethodRecord& TargetMethod = B.Methods[static_cast<size_t>(MethodIndex)];
                if (Task.ArgumentCount != TargetMethod.ParameterCount) { B.SetError("Generated compound task parameter count mismatch"); return {}; }

                for (uint32 ArgumentIndex = 0u; ArgumentIndex < Task.ArgumentCount; ++ArgumentIndex)
                {
                    Out << "    const HTNAtom* compound_argument_" << ArgumentIndex << " = "
                        << BuildGeneratedValueAtomReference(B, Task.FirstArgument + ArgumentIndex, DomainSymbol) << ";\n";
                }
                if (Task.ArgumentCount > 0u)
                {
                    Out << "    if (";
                    for (uint32 ArgumentIndex = 0u; ArgumentIndex < Task.ArgumentCount; ++ArgumentIndex)
                    {
                        if (ArgumentIndex > 0u)
                            Out << " || ";
                        Out << "!compound_argument_" << ArgumentIndex;
                    }
                    Out << ") {\n";
                    Out << "        HTN_GENERATED_EVENT_DEBUG_END_TASK(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
                    Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_TASK);\n";
                    Out << "        return 0;\n";
                    Out << "    }\n";
                }

                std::vector<uint32> CopiedArguments;
                for (uint32 ArgumentIndex = 0u; ArgumentIndex < Task.ArgumentCount; ++ArgumentIndex)
                {
                    const ValueRecord& Argument = B.Values[Task.FirstArgument + ArgumentIndex];
                    if (Argument.Kind != HTNIRValueKind::Variable || Argument.VariableSlot == kNoIndex)
                        continue;
                    const uint32 Word = Argument.VariableSlot >> 6u;
                    const uint64_t Bit = uint64_t{1} << (Argument.VariableSlot & 63u);
                    if ((TargetMethod.VariableSlotMask[Word] & Bit) == 0u)
                        continue;

                    CopiedArguments.push_back(ArgumentIndex);
                    Out << "    HTNAtom compound_argument_copy_" << ArgumentIndex << ";\n";
                    Out << "    HTNAtom_Copy(&compound_argument_copy_" << ArgumentIndex << ", compound_argument_" << ArgumentIndex << ");\n";
                    Out << "    compound_argument_" << ArgumentIndex << " = &compound_argument_copy_" << ArgumentIndex << ";\n";
                }

                for (uint32 Word = 0u; Word < static_cast<uint32>(TargetMethod.VariableSlotMask.size()); ++Word)
                {
                    uint64_t Bits = TargetMethod.VariableSlotMask[Word];
                    while (Bits != 0u)
                    {
                        const uint32 BitIndex = static_cast<uint32>(std::countr_zero(Bits));
                        const uint32 VariableSlot = Word * 64u + BitIndex;
                        if (IsSelfTailCall)
                            EmitGeneratedVariableUnbind(W, std::to_string(VariableSlot) + "u", "    ");
                        else
                        {
                            Out << "    if (HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, " << VariableSlot << "u)) {\n";
                            EmitGeneratedVariableUnbind(W, std::to_string(VariableSlot) + "u", "        ");
                            Out << "    }\n";
                        }
                        Bits &= Bits - 1u;
                    }
                }
                Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_ENTER_COMPOUND);\n";
                Out << "    HTN_GENERATED_EXECUTION(context)->current_variable_frame_id = HTN_GENERATED_EXECUTION(context)->next_variable_frame_id++;\n";
                Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_ENTER_COMPOUND);\n";

                for (uint32 ArgumentIndex = 0u; ArgumentIndex < Task.ArgumentCount; ++ArgumentIndex)
                {
                    const ValueRecord& Target = B.Values[TargetMethod.FirstParameter + ArgumentIndex];
                    if (Target.Kind != HTNIRValueKind::Variable || Target.VariableSlot == kNoIndex) { B.SetError("Generated compound target parameter has no variable slot"); return {}; }

                    if (IsSelfTailCall)
                    {
                        EmitGeneratedVariableSetCopy(W, std::to_string(Target.VariableSlot) + "u",
                                                     "compound_argument_" + std::to_string(ArgumentIndex), "    ");
                    }
                    else
                    {
                        EmitGeneratedSetCopyIfChanged(W, Target.VariableSlot,
                                                      "compound_argument_" + std::to_string(ArgumentIndex), "    ");
                    }
                }

                for (const uint32 ArgumentIndex : CopiedArguments)
                    Out << "    HTNAtom_Destroy(&compound_argument_copy_" << ArgumentIndex << ");\n";
                Out << "    { int task_result = " << MethodFunctions[static_cast<size_t>(MethodIndex)] << "(context, out_result);\n";
                Out << "      HTN_GENERATED_EVENT_DEBUG_END_TASK(context, &" << DomainSymbol << "_PLANNER_DEFINITION, task_result);\n";
                Out << "      HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_TASK);\n";
                Out << "      return task_result; }\n";
            }
        }
        Out << "}\n\n";
    }

    // Methods retain the same condition CFG and queue semantics as the known-good
    // generated planner. The only changed dispatch mechanism is task-id switch ->
    // compile-time continuation pointer.
    for (size_t M = 0; M < B.Methods.size(); ++M)
    {
        const auto& Method = B.Methods[M];
        BoundVariableSet MethodBoundVariables;
        for (uint32 PI = 0; PI < Method.ParameterCount; ++PI)
        {
            const ValueRecord& Parameter = B.Values[Method.FirstParameter + PI];
            if (Parameter.Kind == HTNIRValueKind::Variable)
                MethodBoundVariables.insert(Parameter.Text);
        }

        std::vector<uint32> MethodVariableSlots;
        for (uint32 Word = 0u; Word < static_cast<uint32>(Method.VariableSlotMask.size()); ++Word)
        {
            uint64_t Bits = Method.VariableSlotMask[Word];
            while (Bits != 0u)
            {
                const uint32 BitIndex = static_cast<uint32>(std::countr_zero(Bits));
                MethodVariableSlots.push_back(Word * 64u + BitIndex);
                Bits &= Bits - 1u;
            }
        }

        const uint32 MethodFailureLabel = W.NewLabel();
        std::vector<uint32> BranchLabels(Method.BranchCount);
        for (uint32 BI = 0; BI < Method.BranchCount; ++BI)
            BranchLabels[BI] = W.NewLabel();

        Out << "static int " << MethodFunctions[M] << "(const HTNGeneratedPlannerContext* context, HTNAtom* out_result)\n{\n";
        Out << "    (void)context;\n";
        Out << "    (void)out_result;\n";
        Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_GENERATED_METHOD);\n";
        // Choice cursors belong to this method invocation. Only conditions reachable
        // from this method's branches need storage; declaring cursors for the whole
        // domain needlessly inflates every generated stack frame.
        std::vector<uint32> MethodFactChoiceCursors;
        std::vector<uint32> MethodAxiomChoiceCursors;
        for (uint32 BI = 0u; BI < Method.BranchCount; ++BI)
        {
            CollectGeneratedChoiceCursors(B, B.Branches[Method.FirstBranch + BI].Condition,
                                          MethodBoundVariables,
                                          MethodFactChoiceCursors,
                                          MethodAxiomChoiceCursors);
        }

        for (const uint32 ConditionIndex : MethodFactChoiceCursors)
        {
            Out << "    uint32_t fact_choice_cursor_" << ConditionIndex << " = 0u;\n";
            Out << "    (void)fact_choice_cursor_" << ConditionIndex << ";\n";
        }
        for (const uint32 ConditionIndex : MethodAxiomChoiceCursors)
        {
            Out << "    uint32_t axiom_choice_cursor_" << ConditionIndex << " = 0u;\n";
            Out << "    (void)axiom_choice_cursor_" << ConditionIndex << ";\n";
        }

        Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_METHOD(context, &" << DomainSymbol << "_PLANNER_DEFINITION, " << M << "u);\n";
        if (Method.BranchCount == 0u)
        {
            Out << "    HTN_GENERATED_EVENT_DEBUG_END_METHOD(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
            Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_METHOD);\n";
            Out << "    return 0;\n";
            Out << "}\n\n";
            continue;
        }
        Out << "    goto " << W.Label(BranchLabels[0]) << ";\n\n";

        for (uint32 BI = 0; BI < Method.BranchCount; ++BI)
        {
            const uint32 BIndex = Method.FirstBranch + BI;
            const auto& Branch = B.Branches[BIndex];
            const uint32 BranchSuccess = W.NewLabel();
            const uint32 BranchFailedDebug = W.NewLabel();
            const uint32 BranchFailure = (BI + 1u < Method.BranchCount) ? BranchLabels[BI + 1u] : MethodFailureLabel;
            const bool CanRetryNextBranch = BI + 1u < Method.BranchCount && Branch.TaskCount != 0u;
            const size_t MethodSnapshotCount = MethodVariableSlots.size();
            const size_t MethodSnapshotStorageCount = std::max<size_t>(1u, MethodSnapshotCount);

            Out << W.Label(BranchLabels[BI]) << ":\n";
            Out << "    ;\n";
            if (CanRetryNextBranch)
            {
                Out << "    HTNAtom branch_retry_values_" << BIndex << "[" << MethodSnapshotStorageCount << "u];\n";
                if (MethodSnapshotCount != 0u)
                    Out << "    uint8_t branch_retry_bound_" << BIndex << "[" << MethodSnapshotStorageCount << "u] = {0};\n";
                Out << "    const int32_t branch_retry_plan_size_" << BIndex << " = HTNAtom_GetListSize(out_result);\n";
                Out << "    const uint32_t branch_retry_pending_base_" << BIndex << " = HTN_GENERATED_EXECUTION(context)->total_pending_count;\n";
                Out << "    const uint64_t branch_retry_frame_" << BIndex << " = HTN_GENERATED_EXECUTION(context)->current_variable_frame_id;\n";
                Out << "    HTNAtom_InitRange(branch_retry_values_" << BIndex << ", " << MethodSnapshotCount << "u);\n";
                for (size_t SnapshotIndex = 0u; SnapshotIndex < MethodVariableSlots.size(); ++SnapshotIndex)
                {
                    const uint32 Slot = MethodVariableSlots[SnapshotIndex];
                    Out << "    { const HTNAtom* branch_retry_value = HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, " << Slot << "u);\n";
                    Out << "      if (branch_retry_value) { if (!HTNAtom_AssignCopy(&branch_retry_values_" << BIndex << "[" << SnapshotIndex << "u], branch_retry_value)) { HTNAtom_DestroyRange(branch_retry_values_" << BIndex << ", " << MethodSnapshotCount << "u); HTN_GENERATED_EXECUTION(context)->failure_state = HTN_DECOMPOSITION_OUT_OF_MEMORY; HTN_GENERATED_EVENT_DEBUG_END_METHOD(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0); HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_METHOD); return 0; } branch_retry_bound_" << BIndex << "[" << SnapshotIndex << "u] = 1u; } }\n";
                }
            }
            Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_GENERATED_BRANCH);\n";
            Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_BRANCH_SETUP);\n";
            Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_BRANCH(context, &" << DomainSymbol << "_PLANNER_DEFINITION, " << BIndex << "u);\n";
            Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_BRANCH_SETUP);\n";
            Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_BRANCH_CONDITION_CFG);\n";
            EmitCondition(W, B, Branch.Condition, BranchSuccess, BranchFailedDebug, MethodBoundVariables, DomainSymbol);

            Out << W.Label(BranchFailedDebug) << ":\n";
            Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_BRANCH_CONDITION_CFG);\n";
            if (CanRetryNextBranch)
                Out << "    HTNAtom_DestroyRange(branch_retry_values_" << BIndex << ", " << MethodSnapshotCount << "u);\n";
            Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_BRANCH_RETRY);\n";
            Out << "    HTN_GENERATED_EVENT_DEBUG_END_BRANCH(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
            Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_BRANCH_RETRY);\n";
            Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_BRANCH);\n";
            Out << "    goto " << W.Label(BranchFailure) << ";\n\n";

            Out << W.Label(BranchSuccess) << ":\n";
            Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_BRANCH_CONDITION_CFG);\n";
            Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_BRANCH_TASK_SCHEDULING);\n";
            if (Branch.TaskCount != 0u)
            {
                Out << "    if (!" << Prefix << "_PUSH_BRANCH_CONTINUATIONS_" << BIndex << "(context)) {\n";
                if (CanRetryNextBranch)
                    Out << "        HTNAtom_DestroyRange(branch_retry_values_" << BIndex << ", " << MethodSnapshotCount << "u);\n";
                Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_BRANCH_TASK_SCHEDULING);\n";
                Out << "        HTN_GENERATED_EVENT_DEBUG_END_BRANCH(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
                Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_BRANCH);\n";
                Out << "        HTN_GENERATED_EVENT_DEBUG_END_METHOD(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
                Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_METHOD);\n";
                Out << "        return 0;\n";
                Out << "    }\n";
                for (uint32 TI = 0; TI < Branch.TaskCount; ++TI)
                {
                    const uint32 TaskIndex = Branch.FirstTask + (Branch.TaskCount - 1u - TI);
                    Out << "    HTN_GENERATED_EVENT_DEBUG_CAPTURE_PENDING_TASK(context, " << TaskIndex << "u);\n";
                }
            }
            Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_BRANCH_TASK_SCHEDULING);\n";

            if (Branch.TaskCount != 0u)
            {
                const std::string PendingBase = CanRetryNextBranch
                    ? "branch_retry_pending_base_" + std::to_string(BIndex)
                    : "0u";
                Out << "    while (HTN_GENERATED_EXECUTION(context)->total_pending_count > " << PendingBase << ") {\n";
                Out << "        HTNGeneratedTaskContinuationFn branch_continuation = " << Prefix << "_POP_PENDING_CONTINUATION(context);\n";
                Out << "        if (!branch_continuation || !branch_continuation(context, out_result)) {\n";
                if (CanRetryNextBranch)
                {
                    Out << "            if (HTN_GENERATED_EXECUTION(context)->failure_state != HTN_DECOMPOSITION_NO_PLAN) { HTNAtom_DestroyRange(branch_retry_values_" << BIndex << ", " << MethodSnapshotCount << "u); HTN_GENERATED_EVENT_DEBUG_END_BRANCH(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0); HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_BRANCH); HTN_GENERATED_EVENT_DEBUG_END_METHOD(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0); HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_METHOD); return 0; }\n";
                    if (inRuntimeBacktrackingSupport == HTNGeneratedRuntimeBacktrackingSupport::Enabled)
                        Out << "            if ((context->backtracking_mode & HTN_BACKTRACKING_BRANCHES) == 0) { HTNAtom_DestroyRange(branch_retry_values_" << BIndex << ", " << MethodSnapshotCount << "u); HTN_GENERATED_EVENT_DEBUG_END_BRANCH(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0); HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_BRANCH); HTN_GENERATED_EVENT_DEBUG_END_METHOD(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0); HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_METHOD); return 0; }\n";
                    Out << "            while (HTN_GENERATED_EXECUTION(context)->total_pending_count > branch_retry_pending_base_" << BIndex << ") (void)" << Prefix << "_POP_PENDING_CONTINUATION(context);\n";
                    Out << "            while (HTNAtom_GetListSize(out_result) > branch_retry_plan_size_" << BIndex << ") (void)HTNAtomList_RemoveAt(&out_result->value.list_value, (uint32_t)(HTNAtom_GetListSize(out_result) - 1));\n";
                    for (size_t SnapshotIndex = 0u; SnapshotIndex < MethodVariableSlots.size(); ++SnapshotIndex)
                    {
                        const uint32 Slot = MethodVariableSlots[SnapshotIndex];
                        Out << "            if (branch_retry_bound_" << BIndex << "[" << SnapshotIndex << "u]) {\n";
                        EmitGeneratedVariableSetMove(W, std::to_string(Slot) + "u", "&branch_retry_values_" + std::to_string(BIndex) + "[" + std::to_string(SnapshotIndex) + "u]", "                ");
                        Out << "            } else {\n";
                        EmitGeneratedVariableUnbind(W, std::to_string(Slot) + "u", "                ");
                        Out << "            }\n";
                    }
                    Out << "            HTN_GENERATED_EXECUTION(context)->current_variable_frame_id = branch_retry_frame_" << BIndex << ";\n";
                    Out << "            HTNAtom_DestroyRange(branch_retry_values_" << BIndex << ", " << MethodSnapshotCount << "u);\n";
                    Out << "            HTN_GENERATED_EVENT_DEBUG_END_BRANCH(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
                    Out << "            HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_BRANCH);\n";
                    Out << "            goto " << W.Label(BranchFailure) << ";\n";
                }
                else
                {
                    Out << "            HTN_GENERATED_EVENT_DEBUG_END_BRANCH(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
                    Out << "            HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_BRANCH);\n";
                    Out << "            HTN_GENERATED_EVENT_DEBUG_END_METHOD(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
                    Out << "            HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_METHOD);\n";
                    Out << "            return 0;\n";
                }
                Out << "        }\n";
                Out << "    }\n";
            }

            if (CanRetryNextBranch)
                Out << "    HTNAtom_DestroyRange(branch_retry_values_" << BIndex << ", " << MethodSnapshotCount << "u);\n";
            Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_BRANCH_COMMIT);\n";
            Out << "    HTN_GENERATED_EVENT_DEBUG_END_BRANCH(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 1);\n";
            Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_BRANCH_COMMIT);\n";
            Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_BRANCH);\n";
            Out << "    HTN_GENERATED_EVENT_DEBUG_END_METHOD(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 1);\n";
            Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_METHOD);\n";
            Out << "    return 1;\n\n";
        }

        Out << W.Label(MethodFailureLabel) << ":\n";
        Out << "    HTN_GENERATED_EVENT_DEBUG_END_METHOD(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
        Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_METHOD);\n";
        Out << "    return 0;\n";
        Out << "}\n\n";
    }

    Out << "#ifdef __cplusplus\nextern \"C\"\n#endif\n";
    Out << "HTNDecompositionStatus " << EntryPointName << "(const HTNGeneratedPlannerContext* context, const HTNAtom* call, int require_top_level, HTNAtom* out_result)\n{\n";
    Out << "    HTNGeneratedTaskContinuationFn pending_continuation = 0;\n";
    Out << "    int result = 0;\n";
    Out << "    uint32_t entry_method = HTN_NO_INDEX;\n";
    Out << "    int fact_slots_prepared = 0;\n";
    Out << "    int callterm_slots_prepared = 0;\n";
    Out << "    uint64_t fact_storage_generation = 0u;\n";
    Out << "    const HTNAtom* call_head = 0;\n";
    Out << "    uint32_t call_argument_count = 0u;\n";
    Out << "    if (!out_result) return HTN_DECOMPOSITION_INVALID_CONTEXT;\n";
    Out << "    HTNAtom_Init(out_result);\n";
    Out << "    HTNAtom_SetEmptyList(out_result);\n";
    Out << "    (void)out_result;\n";
    Out << "    (void)require_top_level;\n";
    Out << "    if (!context || !context->execution_storage || !context->prepared_storage) return HTN_DECOMPOSITION_INVALID_CONTEXT;\n";
    Out << "    if (!context->world_state || !context->callterm_binding_context) return HTN_DECOMPOSITION_INVALID_CONTEXT;\n";
    Out << "    if (!call || HTNAtom_GetType(call) != HTN_ATOM_TYPE_LIST || HTNAtom_GetListSize(call) < 1) return HTN_DECOMPOSITION_INVALID_CALL;\n";
    Out << "    call_head = HTNAtom_GetListElement(call, 0u);\n";
    Out << "    if (!call_head || HTNAtom_GetType(call_head) != HTN_ATOM_TYPE_SYMBOL) return HTN_DECOMPOSITION_INVALID_CALL;\n";
    Out << "    call_argument_count = (uint32_t)(HTNAtom_GetListSize(call) - 1);\n";
    Out << "    HTN_GENERATED_EXECUTION(context)->failure_state = HTN_DECOMPOSITION_NO_PLAN;\n";
    Out << "    fact_storage_generation = HTNWorldState_GetFactStorageGeneration(context->world_state);\n";
    Out << "    fact_slots_prepared = HTN_GENERATED_EXECUTION(context)->fact_prepared_storage == context->prepared_storage && HTN_GENERATED_EXECUTION(context)->fact_world_state == context->world_state && HTN_GENERATED_EXECUTION(context)->fact_storage_generation == fact_storage_generation;\n";
    Out << "    callterm_slots_prepared = HTN_GENERATED_EXECUTION(context)->callterm_prepared_storage == context->prepared_storage && HTN_GENERATED_EXECUTION(context)->callterm_binding_context == context->callterm_binding_context;\n";
    Out << "    HTN_GENERATED_PREPARATION_BEGIN(HTN_GENERATED_EXECUTION(context)->profiling, fact_slots_prepared, callterm_slots_prepared);\n";
    Out << "#if defined(HTN_PROFILE_DETAILED) || defined(HTN_GENERATED_EXECUTION_PROFILING)\n";
    Out << "    HTNGeneratedProfiling_ResetExecution(HTN_GENERATED_EXECUTION(context)->profiling);\n";
    Out << "#if defined(HTN_GENERATED_EXECUTION_PROFILING)\n";
    Out << "    HTN_GENERATED_EXECUTION(context)->structural_counters = HTNGeneratedProfiling_GetStructuralCountersMutable(HTN_GENERATED_EXECUTION(context)->profiling);\n";
    Out << "#endif\n";
    Out << "#endif\n";
    Out << "    {\n";
    Out << "        uint32_t reset_index = 0u;\n";
    if (inBacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedWithOverflow)
    {
        Out << "        if (HTN_GENERATED_EXECUTION(context)->backtracking_overflow)\n";
        Out << "            HTNGeneratedBacktracking_ResetOverflow(HTN_GENERATED_EXECUTION(context)->backtracking_overflow);\n";
    }
    Out << "        for (reset_index = 0u; reset_index < HTN_GENERATED_EXECUTION(context)->inline_snapshot_count; ++reset_index)\n";
    Out << "            HTNAtom_Unbind(&HTN_GENERATED_EXECUTION(context)->snapshot_values[reset_index]);\n";
    Out << "        for (reset_index = 0u; reset_index < " << ((B.VariableStringIds.size() + 63u) / 64u) << "u; ++reset_index)\n";
    Out << "            HTN_GENERATED_EXECUTION(context)->variables.bound_mask[reset_index] = UINT64_C(0);\n";
    Out << "        HTN_GENERATED_EXECUTION(context)->inline_pending_count = 0u;\n";
    Out << "        HTN_GENERATED_EXECUTION(context)->inline_snapshot_count = 0u;\n";
    if (inBacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedWithOverflow)
        Out << "        HTN_GENERATED_EXECUTION(context)->overflow_pending_count = 0u;\n";
    Out << "        HTN_GENERATED_EXECUTION(context)->total_pending_count = 0u;\n";
    Out << "        HTN_GENERATED_EXECUTION(context)->current_variable_frame_id = UINT64_C(1);\n";
    Out << "        HTN_GENERATED_EXECUTION(context)->next_variable_frame_id = UINT64_C(2);\n";
    Out << "    }\n";
    Out << "    HTN_GENERATED_PREPARATION_MARK(HTN_GENERATED_EXECUTION(context)->profiling, HTN_GENERATED_PREPARATION_RESET);\n";
    Out << "    if (!fact_slots_prepared) {\n";
    Out << "        if (!" << DomainSymbol << "_PREPARE_FACTS(HTN_GENERATED_EXECUTION(context)->fact_slots, context->world_state, context->prepared_storage)) return HTN_DECOMPOSITION_PREPARATION_FAILED;\n";
    Out << "        HTN_GENERATED_EXECUTION(context)->fact_prepared_storage = context->prepared_storage;\n";
    Out << "        HTN_GENERATED_EXECUTION(context)->fact_world_state = context->world_state;\n";
    Out << "        HTN_GENERATED_EXECUTION(context)->fact_storage_generation = fact_storage_generation;\n";
    Out << "    }\n";
    Out << "    HTN_GENERATED_PREPARATION_MARK(HTN_GENERATED_EXECUTION(context)->profiling, HTN_GENERATED_PREPARATION_FACT_SLOTS);\n";
    Out << "    if (!callterm_slots_prepared) {\n";
    Out << "        if (!" << Prefix << "_PREPARE_CALLTERMS(HTN_GENERATED_EXECUTION(context)->callterm_slots, context->callterm_binding_context)) return HTN_DECOMPOSITION_PREPARATION_FAILED;\n";
    Out << "        HTN_GENERATED_EXECUTION(context)->callterm_prepared_storage = context->prepared_storage;\n";
    Out << "        HTN_GENERATED_EXECUTION(context)->callterm_binding_context = context->callterm_binding_context;\n";
    Out << "    }\n";
    Out << "    HTN_GENERATED_PREPARATION_MARK(HTN_GENERATED_EXECUTION(context)->profiling, HTN_GENERATED_PREPARATION_CALLTERM_SLOTS);\n";
    for (size_t M = 0; M < B.Methods.size(); ++M)
    {
        if (!B.Methods[M].IsExternallyDecomposable) continue;
        if (B.Methods[M].Id >= B.Strings.Values.size()) { B.SetError("Generated externally decomposable method string id is out of range"); return {}; }
        Out << "    if (entry_method == HTN_NO_INDEX && call_head->value.symbol_value == " << DomainSymbol << "_PREPARED(context)->symbols["
            << B.FindPreparedSymbolSlot(B.Methods[M].Id) << "u]) {\n";
        if (!B.Methods[M].IsTopLevel)
            Out << "        if (require_top_level) return HTN_DECOMPOSITION_INVALID_CALL;\n";
        Out << "        if (call_argument_count != " << B.Methods[M].ParameterCount << "u) return HTN_DECOMPOSITION_INVALID_CALL;\n";
        Out << "        entry_method = " << M << "u;\n";
        Out << "    }\n";
    }
    Out << "    HTN_GENERATED_PREPARATION_MARK(HTN_GENERATED_EXECUTION(context)->profiling, HTN_GENERATED_PREPARATION_TOP_LEVEL_METHOD);\n";
    Out << "    HTN_GENERATED_PREPARATION_MARK(HTN_GENERATED_EXECUTION(context)->profiling, HTN_GENERATED_PREPARATION_COMPLETE);\n";
    Out << "    if (entry_method == HTN_NO_INDEX) return HTN_DECOMPOSITION_INVALID_CALL;\n";
    Out << "    HTN_GENERATED_EVENT_DEBUG_BEGIN_PLAN(context, &" << DomainSymbol << "_PLANNER_DEFINITION, entry_method);\n";
    Out << "    HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_GENERATED_DISPATCH);\n";
    Out << "    switch (entry_method) {\n";
    for (size_t M = 0; M < B.Methods.size(); ++M)
    {
        const MethodRecord& EntryMethod = B.Methods[M];
        if (!EntryMethod.IsExternallyDecomposable)
            continue;
        Out << "    case " << M << "u:\n";
        for (uint32 ArgumentIndex = 0u; ArgumentIndex < EntryMethod.ParameterCount; ++ArgumentIndex)
        {
            const ValueRecord& Target = B.Values[EntryMethod.FirstParameter + ArgumentIndex];
            if (Target.Kind != HTNIRValueKind::Variable || Target.VariableSlot == kNoIndex) { B.SetError("Generated top-level method parameter has no variable slot"); return {}; }
            Out << "        { const HTNAtom* entry_argument = HTNAtom_GetListElement(call, " << (ArgumentIndex + 1u) << "u);\n";
            Out << "          if (!entry_argument || !HTNAtom_IsBound(entry_argument)) { HTNAtom_SetEmptyList(out_result); return HTN_DECOMPOSITION_INVALID_CALL; }\n";
            EmitGeneratedVariableSetCopy(W, std::to_string(Target.VariableSlot) + "u", "entry_argument", "          ");
            Out << "          if (!HTNGeneratedVariables_Get(&HTN_GENERATED_EXECUTION(context)->variables, " << Target.VariableSlot << "u)) { HTNAtom_SetEmptyList(out_result); return HTN_DECOMPOSITION_OUT_OF_MEMORY; } }\n";
        }
        Out << "        result = " << MethodFunctions[M] << "(context, out_result); break;\n";
    }
    Out << "    default: result = 0; break;\n";
    Out << "    }\n";
    Out << "    if (!result) {\n";
    Out << "        HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_DISPATCH);\n";
    Out << "        HTN_GENERATED_EVENT_DEBUG_END_PLAN(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
    Out << "        HTNAtom_SetEmptyList(out_result);\n";
    Out << "        return HTN_GENERATED_EXECUTION(context)->failure_state;\n";
    Out << "    }\n";
    Out << "    while (HTN_GENERATED_EXECUTION(context)->total_pending_count != 0u) {\n";
    Out << "        " << Prefix << "_EXECUTION_STORAGE* continuation_storage = HTN_GENERATED_EXECUTION(context);\n";
    if (inBacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedWithOverflow)
    {
    Out << "        if (continuation_storage->overflow_pending_count != 0u) {\n";
    Out << "            uint32_t restore_snapshot_count = 0u;\n";
    Out << "            uint32_t restore_snapshot_index;\n";
    Out << "            HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_PENDING_POP);\n";
    Out << "            pending_continuation = HTNGeneratedBacktracking_PopPendingContinuationOverflow(continuation_storage->backtracking_overflow, &continuation_storage->current_variable_frame_id, &restore_snapshot_count);\n";
    Out << "            if (pending_continuation) {\n";
    Out << "                HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONTINUATION_TRAIL);\n";
    Out << "                for (restore_snapshot_index = 0u; restore_snapshot_index < restore_snapshot_count; ++restore_snapshot_index) {\n";
    Out << "                    HTNAtom snapshot_value;\n";
    Out << "                    uint32_t variable_slot;\n";
    Out << "                    HTNGeneratedBacktracking_PopContinuationSnapshotOverflow(continuation_storage->backtracking_overflow, &variable_slot, &snapshot_value);\n";
    Out << "                    if (HTNAtom_IsBound(&snapshot_value)) {\n";
    EmitGeneratedVariableSetMove(W, "variable_slot", "&snapshot_value", "                        ");
    Out << "                    } else {\n";
    EmitGeneratedVariableUnbind(W, "variable_slot", "                        ");
    Out << "                    }\n";
    Out << "                    HTNAtom_Destroy(&snapshot_value);\n";
    Out << "                }\n";
    Out << "                HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONTINUATION_TRAIL);\n";
    Out << "                --continuation_storage->overflow_pending_count;\n";
    Out << "                --continuation_storage->total_pending_count;\n";
    Out << "            }\n";
    Out << "            HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_PENDING_POP);\n";
    Out << "        } else {\n";
    }
    else
    {
        Out << "        {\n";
    }
    Out << "            " << Prefix << "_PENDING_CONTINUATION_ENTRY* pending;\n";
    Out << "            uint32_t snapshot_index;\n";
    Out << "            HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_PENDING_POP);\n";
    Out << "            pending = &continuation_storage->pending[--continuation_storage->inline_pending_count];\n";
    Out << "            HTN_GENERATED_PROFILE_BEGIN(context, HTN_GENERATED_PROFILE_CONTINUATION_TRAIL);\n";
    Out << "            for (snapshot_index = pending->snapshot_start; snapshot_index < pending->snapshot_start + pending->snapshot_count; ++snapshot_index) {\n";
    Out << "                HTNAtom* snapshot_value = &continuation_storage->snapshot_values[snapshot_index];\n";
    Out << "                const uint32_t variable_slot = continuation_storage->snapshot_slots[snapshot_index];\n";
    Out << "                if (HTNAtom_IsBound(snapshot_value)) {\n";
    EmitGeneratedVariableSetMove(W, "variable_slot", "snapshot_value", "                    ");
    Out << "                } else {\n";
    EmitGeneratedVariableUnbind(W, "variable_slot", "                    ");
    Out << "                }\n";
    Out << "            }\n";
    Out << "            continuation_storage->inline_snapshot_count = pending->snapshot_start;\n";
    Out << "            HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_CONTINUATION_TRAIL);\n";
    Out << "            HTN_GENERATED_EXECUTION(context)->current_variable_frame_id = pending->variable_frame_id;\n";
    Out << "            pending_continuation = pending->continuation;\n";
    Out << "            --continuation_storage->total_pending_count;\n";
    Out << "            HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_PENDING_POP);\n";
    Out << "        }\n";
    Out << "        if (!pending_continuation) break;\n";
    Out << "        if (!pending_continuation(context, out_result)) {\n";
    Out << "            HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_DISPATCH);\n";
    Out << "            HTN_GENERATED_EVENT_DEBUG_END_PLAN(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 0);\n";
    Out << "            HTNAtom_SetEmptyList(out_result);\n";
    Out << "            return HTN_GENERATED_EXECUTION(context)->failure_state;\n";
    Out << "        }\n";
    Out << "    }\n";
    Out << "    HTN_GENERATED_PROFILE_END(context, HTN_GENERATED_PROFILE_GENERATED_DISPATCH);\n";
    Out << "    HTN_GENERATED_EVENT_DEBUG_END_PLAN(context, &" << DomainSymbol << "_PLANNER_DEFINITION, 1);\n";
    Out << "    return HTN_DECOMPOSITION_SUCCEEDED;\n";
    Out << "}\n\n";

    Out << "#ifdef __cplusplus\nextern \"C\"\n#endif\n";
    Out << "HTN_GENERATED_MODULE_EXPORT const HTNGeneratedPlannerDefinition* " << EntryPointName << "_GetDefinition(void)\n{\n";
    Out << "    return &" << DomainSymbol << "_PLANNER_DEFINITION;\n";
    Out << "}\n\n";
#ifdef HTN_PROFILE_DETAILED
    Out << "#undef HTN_GENERATED_PROFILING\n";
#endif
    Out << "#if defined(HTN_GENERATED_EXECUTION_PROFILING)\n";
    Out << "#undef HTN_GENERATED_STRUCTURAL_COUNTERS\n";
    Out << "#endif\n";
    Out << "#undef HTN_GENERATED_VARIABLES\n";
    Out << "#undef HTN_GENERATED_EXECUTION\n\n";

    Out << "#if defined(_MSC_VER)\n";
    Out << "#pragma warning(pop)\n";
    Out << "#elif defined(__clang__)\n";
    Out << "#pragma clang diagnostic pop\n";
    Out << "#elif defined(__GNUC__)\n";
    Out << "#pragma GCC diagnostic pop\n";
    Out << "#endif\n";
    return Out.str();
}


bool WriteTextFile(const std::string& inPath,const std::string& inText,std::string& outError)
{
    std::error_code EC; const std::filesystem::path P(inPath); if(P.has_parent_path())std::filesystem::create_directories(P.parent_path(),EC);
    if(EC){outError="Could not create output directory: "+EC.message();return false;}
    std::ofstream F(inPath,std::ios::binary|std::ios::trunc); if(!F){outError="Could not open output file: "+inPath;return false;} F<<inText;
    if(!F.good()){outError="Could not write output file: "+inPath;return false;} return true;
}
}

bool HTNCCodeGenerator::Generate(const HTNCompilerAST::Domain& inDomain,
                                 const HTNCCodeGeneratorOptions& inOptions,
                                 std::string& outError) const
{
    if(inOptions.OutputSourcePath.empty()){outError="Output source path must not be empty";return false;}
    if(inOptions.EntryPointName.empty()){outError="Entry point name must not be empty";return false;}
    if(inOptions.BacktrackingPolicy != HTNGeneratedBacktrackingPolicy::FixedWithOverflow &&
       inOptions.BacktrackingPolicy != HTNGeneratedBacktrackingPolicy::FixedCapacity)
    {
        outError="Unknown backtracking policy";
        return false;
    }
    if(inOptions.RuntimeBacktrackingSupport != HTNGeneratedRuntimeBacktrackingSupport::Disabled &&
       inOptions.RuntimeBacktrackingSupport != HTNGeneratedRuntimeBacktrackingSupport::Enabled)
    {
        outError="Unknown runtime backtracking support mode";
        return false;
    }
    if(inOptions.BacktrackingCapacity == 0u){outError="Backtracking capacity must be greater than zero";return false;}
    const auto IsStart=[](unsigned char C){return std::isalpha(C)||C=='_';}; const auto IsChar=[](unsigned char C){return std::isalnum(C)||C=='_';};
    if(!IsStart(static_cast<unsigned char>(inOptions.EntryPointName.front()))){outError="Entry point must be a valid C identifier: "+inOptions.EntryPointName;return false;}
    for(unsigned char C:inOptions.EntryPointName)if(!IsChar(C)){outError="Entry point must be a valid C identifier: "+inOptions.EntryPointName;return false;}

    HTNCompilerIR IR;
    const std::vector<std::string> SourceFiles = inOptions.LinkedSourceFiles.empty()
        ? std::vector<std::string>{inOptions.SourceFilePath.empty() ? "<domain>" : inOptions.SourceFilePath}
        : inOptions.LinkedSourceFiles;
    if (!HTNBuildCompilerIR(inDomain, SourceFiles,
                            inOptions.RuntimeBacktrackingSupport, IR, outError))
        return false;
    if (inOptions.BacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedCapacity &&
        !IR.VariableStringIds.empty() &&
        static_cast<size_t>(inOptions.BacktrackingCapacity) >
            std::numeric_limits<size_t>::max() / IR.VariableStringIds.size())
    {
        outError = "Fixed backtracking capacity is too large for this generated domain";
        return false;
    }
    const std::string Prefix="HTN_"+MakeIdentifier(IR.DomainId);
    const std::string SourceFile=inOptions.SourceFilePath.empty()?"<domain>":inOptions.SourceFilePath;
    const std::string GeneratedSource = MakeSource(IR,Prefix,inOptions.EntryPointName,SourceFile,inOptions.LinkedSourceFiles,
                                                   inOptions.BacktrackingPolicy,inOptions.RuntimeBacktrackingSupport,
                                                   inOptions.BacktrackingCapacity);
    if (IR.HasError())
    {
        outError = IR.GetError();
        return false;
    }
    return WriteTextFile(inOptions.OutputSourcePath, GeneratedSource, outError);
}
