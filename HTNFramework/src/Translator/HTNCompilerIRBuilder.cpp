// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Translator/HTNCompilerIRBuilder.h"

#include "Translator/HTNCompilerAST.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
namespace AST = HTNCompilerAST;
constexpr uint32 kNoIndex = HTN_IR_NO_INDEX;
using StringTable = HTNIRStringTable;
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

HTNAtomType GetAtomType(const HTNAtom& inAtom)
{
    return HTNAtomGetType(inAtom);
}

const char* BuiltinListSplitOperationName(const uint32 inOperation)
{
    switch (inOperation)
    {
    case 0u: return "split_list";
    case 1u: return "split_list_front";
    case 2u: return "split_list_back";
    default: return "split_list";
    }
}

std::string FormatDomainValueExpression(const AST::Value& inNode)
{
    const HTNAtom& Value = inNode.GetValue();
    switch (inNode.GetExpressionType())
    {
    case AST::ValueKind::Identifier:
        return HTNAtomToString(Value, false);
    case AST::ValueKind::Literal:
        return HTNAtomToString(Value, true);
    case AST::ValueKind::Variable:
        return "?" + HTNAtomToString(Value, false);
    case AST::ValueKind::Constant:
        return "@" + HTNAtomToString(Value, false);
    case AST::ValueKind::Call:
    {
        std::string Result = "(call " + FormatDomainValueExpression(*inNode.GetIDNode());
        for (const auto& Argument : inNode.GetArgumentNodes())
            Result += " " + FormatDomainValueExpression(*Argument);
        Result += ")";
        return Result;
    }
    default:
        return HTNAtomToString(Value, true);
    }
}

std::string FormatDomainArguments(const std::vector<AST::ValuePtr>& inArguments)
{
    std::string Result;
    for (const auto& Argument : inArguments)
        Result += " " + FormatDomainValueExpression(*Argument);
    return Result;
}

std::string FormatDomainCondition(const AST::Condition& inNode)
{
    if (inNode.Kind == AST::ConditionKind::Fact)
        return "(" + FormatDomainValueExpression(*inNode.GetIDNode()) + FormatDomainArguments(inNode.GetArgumentNodes()) + ")";
    if (inNode.Kind == AST::ConditionKind::Axiom)
        return "(#" + FormatDomainValueExpression(*inNode.GetIDNode()) + FormatDomainArguments(inNode.GetArgumentNodes()) + ")";
    if (inNode.Kind == AST::ConditionKind::Call)
    {
        std::string Invocation = "(call " + FormatDomainValueExpression(*inNode.GetIDNode()) + FormatDomainArguments(inNode.GetArgumentNodes()) + ")";
        if (!inNode.HasOutputVariable())
            return Invocation;
        return "(" + FormatDomainValueExpression(*inNode.GetOutputVariableNode()) + " " + Invocation + ")";
    }
    if (inNode.Kind == AST::ConditionKind::Comparison)
    {
        static constexpr const char* Operators[] = {"==", "!=", "<", "<=", ">", ">="};
        const uint32 Operator = inNode.GetOperator();
        return "(" + std::string(Operator < 6u ? Operators[Operator] : "?") + " " +
            FormatDomainValueExpression(*inNode.GetLeftNode()) + " " +
            FormatDomainValueExpression(*inNode.GetRightNode()) + ")";
    }
    if (inNode.Kind == AST::ConditionKind::Split)
    {
        const std::string List = FormatDomainValueExpression(*inNode.GetListNode());
        const std::string Element = FormatDomainValueExpression(*inNode.GetElementNode());
        const std::string Remainder = FormatDomainValueExpression(*inNode.GetRemainderNode());
        if (inNode.GetOperation() == 2u)
            return "(" + std::string(BuiltinListSplitOperationName(inNode.GetOperation())) + " " + List + " " + Remainder + " " + Element + ")";
        return "(" + std::string(BuiltinListSplitOperationName(inNode.GetOperation())) + " " + List + " " + Element + " " + Remainder + ")";
    }

    const auto FormatComposite = [](const char* inName, const auto& inChildren)
    {
        std::string Result = "(" + std::string(inName);
        for (const auto& Child : inChildren)
            Result += " " + FormatDomainCondition(*Child);
        Result += ")";
        return Result;
    };
    if (inNode.Kind == AST::ConditionKind::And) return FormatComposite("and", inNode.Children);
    if (inNode.Kind == AST::ConditionKind::Or) return FormatComposite("or", inNode.Children);
    if (inNode.Kind == AST::ConditionKind::Alt) return FormatComposite("alt", inNode.Children);
    if (inNode.Kind == AST::ConditionKind::Not)
        return "(not " + FormatDomainCondition(*inNode.GetSubConditionNode()) + ")";
    return "<condition>";
}

std::string FormatDomainTask(const AST::Task& inNode)
{
    const bool IsPrimitive = inNode.Kind == AST::TaskKind::Primitive;
    const bool IsDeferred = inNode.Kind == AST::TaskKind::Deferred;
    return "(" + std::string(IsPrimitive ? "!" : (IsDeferred ? "#" : "")) + FormatDomainValueExpression(*inNode.GetIDNode()) +
        FormatDomainArguments(inNode.GetArgumentNodes()) + ")";
}

HTNIRValueKind LowerValueKind(AST::ValueKind inKind)
{
    switch (inKind)
    {
    case AST::ValueKind::Identifier: return HTNIRValueKind::Identifier;
    case AST::ValueKind::Literal: return HTNIRValueKind::Literal;
    case AST::ValueKind::Variable: return HTNIRValueKind::Variable;
    case AST::ValueKind::Constant: return HTNIRValueKind::Constant;
    case AST::ValueKind::Call: return HTNIRValueKind::Call;
    }
    return HTNIRValueKind::Literal;
}

class Builder : public HTNCompilerIR
{
public:
    explicit Builder(const AST::Domain& inDomain) : Domain(inDomain) {}

    template <typename TRecord>
    void SetSource(TRecord& outRecord, const AST::Node& inNode) const
    {
        outRecord.Source.FileIndex = inNode.FileIndex;
        outRecord.Source.Range = inNode.GetSourceRange();
        outRecord.SourceLine = static_cast<uint32>(std::max(1, outRecord.Source.Range.Begin.Line));
    }

    void Build()
    {
        for (const auto& ConstantsNode : Domain.GetConstantsNodes())
        {
            const uint32 GroupId = Strings.Add(ConstantsNode->GetID());
            for (const auto& ConstantNode : ConstantsNode->GetConstantNodes())
            {
                ConstantRecord Record;
                Record.GroupId = GroupId;
                Record.Id = Strings.Add(ConstantNode->GetID());
                SetSource(Record, *ConstantNode);
                Record.Value = AddValue(*ConstantNode->GetValueNode());
                Constants.push_back(Record);
            }
        }

        for (const auto& AxiomNode : Domain.GetAxiomNodes())
        {
            AxiomRecord Record;
            Record.Id = Strings.Add(AxiomNode->GetID());
            SetSource(Record, *AxiomNode);
            Record.FirstParameter = static_cast<uint32>(Values.size());
            for (const auto& Parameter : AxiomNode->GetParameterNodes()) AddValue(*Parameter);
            Record.ParameterCount = static_cast<uint32>(Values.size()) - Record.FirstParameter;
            Record.Condition = AddCondition(AxiomNode->GetConditionNode());
            BuildAxiomVariableSlotMask(Record);
            Axioms.push_back(Record);
        }

        for (const auto& MethodNode : Domain.GetMethodNodes())
        {
            MethodRecord Record;
            Record.Id = Strings.Add(MethodNode->GetID());
            SetSource(Record, *MethodNode);
            Record.FirstParameter = static_cast<uint32>(Values.size());
            for (const auto& Parameter : MethodNode->GetParameterNodes()) AddValue(*Parameter);
            Record.ParameterCount = static_cast<uint32>(Values.size()) - Record.FirstParameter;
            Record.FirstBranch = static_cast<uint32>(Branches.size());
            Record.IsTopLevel = MethodNode->IsTopLevel() ? 1u : 0u;
            // Public top-level visibility and generated dispatchability are separate concepts.
            // Deferred-call targets will set IsExternallyDecomposable without becoming top-level methods.
            Record.IsExternallyDecomposable = Record.IsTopLevel;
            if (Record.IsExternallyDecomposable)
                AllocatePreparedSymbolSlot(Record.Id);

            for (const auto& BranchNode : MethodNode->GetBranchNodes())
            {
                BranchRecord Branch;
                Branch.Id = Strings.Add(BranchNode->GetID());
                SetSource(Branch, *BranchNode);
                Branch.Condition = AddCondition(BranchNode->GetPreConditionNode());
                Branch.FirstTask = static_cast<uint32>(Tasks.size());
                for (const auto& TaskNode : BranchNode->GetTaskNodes()) AddTask(*TaskNode);
                Branch.TaskCount = static_cast<uint32>(Tasks.size()) - Branch.FirstTask;
                Branches.push_back(Branch);
            }

            Record.BranchCount = static_cast<uint32>(Branches.size()) - Record.FirstBranch;
            BuildMethodVariableSlotMask(Record);
            Methods.push_back(Record);
        }

        // A method referenced through #call(...) must be reachable by the generated
        // decompose-call dispatch, but it remains semantically distinct from an explicit
        // top-level method. The linker has already resolved and validated these calls.
        for (const TaskRecord& Task : Tasks)
        {
            if (Task.Kind != HTN_TASK_DEFERRED)
                continue;

            for (MethodRecord& Method : Methods)
            {
                if (Method.Id != Task.Id)
                    continue;
                Method.IsExternallyDecomposable = 1u;
                AllocatePreparedSymbolSlot(Method.Id);
                break;
            }
        }
    }

    uint32 AllocateVariableSlot(const uint32 inStringId)
    {
        const auto It = VariableSlotByStringId.find(inStringId);
        if (It != VariableSlotByStringId.end())
            return It->second;
        const uint32 Slot = static_cast<uint32>(VariableStringIds.size());
        VariableStringIds.emplace_back(inStringId);
        VariableSlotByStringId.emplace(inStringId, Slot);
        return Slot;
    }

    uint32 AllocatePreparedSymbolSlot(const uint32 inStringId)
    {
        const auto Existing = PreparedSymbolSlotByStringId.find(inStringId);
        if (Existing != PreparedSymbolSlotByStringId.end())
            return Existing->second;
        const uint32 Slot = static_cast<uint32>(PreparedSymbolStringIds.size());
        PreparedSymbolStringIds.emplace_back(inStringId);
        PreparedSymbolSlotByStringId.emplace(inStringId, Slot);
        return Slot;
    }

    uint32 AllocatePlanStepSymbolSlot(const char inPrefix, const std::string& inTaskId)
    {
        const uint32 StringId = Strings.Add(std::string(1u, inPrefix) + inTaskId);
        return AllocatePreparedSymbolSlot(StringId);
    }

    uint32 AllocateCallTermSlot(const uint32 inStringId)
    {
        const auto Existing = CallTermSlotByStringId.find(inStringId);
        if (Existing != CallTermSlotByStringId.end())
            return Existing->second;
        const uint32 Slot = static_cast<uint32>(CallTermStringIds.size());
        CallTermStringIds.emplace_back(inStringId);
        CallTermSlotByStringId.emplace(inStringId, Slot);
        return Slot;
    }

    ValueRecord MakeValueRecord(const AST::Value& inNode)
    {
        ValueRecord Record;
        Record.Kind = LowerValueKind(inNode.GetExpressionType());
        Record.Text = Strings.Add(HTNAtomToString(inNode.GetValue(), false));
        // Keep the original domain expression for debugger metadata. Runtime/prepared
        // values may resolve constants to their value, but the debugger should show
        // what the programmer wrote (for example @split_list_input).
        Record.DebugText = Strings.Add(FormatDomainValueExpression(inNode));
        Record.AtomType = GetAtomType(inNode.GetValue());
        if (Record.AtomType == HTN_ATOM_TYPE_BOOL) Record.BoolValue = HTNAtomGetValue<bool>(inNode.GetValue()) ? 1u : 0u;
        else if (Record.AtomType == HTN_ATOM_TYPE_INT) Record.IntValue = HTNAtomGetValue<int32>(inNode.GetValue());
        else if (Record.AtomType == HTN_ATOM_TYPE_FLOAT) Record.FloatValue = HTNAtomGetValue<float>(inNode.GetValue());
        else if (Record.AtomType == HTN_ATOM_TYPE_LIST) Record.ListElement = AddListElement(inNode.GetValue());
        else if (Record.AtomType == HTN_ATOM_TYPE_SYMBOL) AllocatePreparedSymbolSlot(Record.Text);
        SetSource(Record, inNode);
        if (Record.Kind == HTNIRValueKind::Variable && Record.Text < Strings.Values.size() &&
            !Strings.Values[Record.Text].starts_with("any_"))
        {
            Record.VariableSlot = AllocateVariableSlot(Record.Text);
        }
        return Record;
    }

    uint32 AddListElement(const HTNAtom& inAtom)
    {
        const uint32 Index = static_cast<uint32>(ListElements.size());
        ListElements.emplace_back();

        ListElementRecord Record;
        Record.AtomType = GetAtomType(inAtom);
        Record.Text = Strings.Add(HTNAtomToString(inAtom, false));
        if (Record.AtomType == HTN_ATOM_TYPE_BOOL) Record.BoolValue = HTNAtomGetValue<bool>(inAtom) ? 1u : 0u;
        else if (Record.AtomType == HTN_ATOM_TYPE_INT) Record.IntValue = HTNAtomGetValue<int32>(inAtom);
        else if (Record.AtomType == HTN_ATOM_TYPE_FLOAT) Record.FloatValue = HTNAtomGetValue<float>(inAtom);
        else if (Record.AtomType == HTN_ATOM_TYPE_LIST)
        {
            Record.FirstChildRef = static_cast<uint32>(ListChildRefs.size());
            Record.ChildCount = static_cast<uint32>(HTNAtomGetListSize(inAtom));
            ListChildRefs.resize(ListChildRefs.size() + Record.ChildCount);
            for (uint32 ChildIndex = 0; ChildIndex < Record.ChildCount; ++ChildIndex)
                ListChildRefs[Record.FirstChildRef + ChildIndex] = AddListElement(HTNAtomGetListElement(inAtom, ChildIndex));
        }
        else if (Record.AtomType == HTN_ATOM_TYPE_SYMBOL)
        {
            AllocatePreparedSymbolSlot(Record.Text);
        }
        ListElements[Index] = Record;
        return Index;
    }

    uint32 AddValue(const AST::Value& inNode)
    {
        const uint32 Index = static_cast<uint32>(Values.size());
        Values.push_back(MakeValueRecord(inNode));
        return Index;
    }

    ValueRecord BuildTaskArgument(const AST::Value& inNode,
                                  std::vector<TaskCallExpressionRecord>& ioCalls)
    {
        if (inNode.Kind == AST::ValueKind::Call)
        {
            TaskCallExpressionRecord CallRecord;
            CallRecord.DomainExpression = FormatDomainValueExpression(inNode);
            CallRecord.Id = Strings.Add(HTNAtomToString(inNode.GetIDNode()->GetValue(), false));
            CallRecord.CallTermSlot = AllocateCallTermSlot(CallRecord.Id);
            SetSource(CallRecord, inNode);
            CallRecord.Arguments.reserve(inNode.GetArgumentNodes().size());
            for (const auto& Argument : inNode.GetArgumentNodes())
                CallRecord.Arguments.emplace_back(BuildTaskArgument(*Argument, ioCalls));

            const uint32 DebugExpressionStringId = Strings.Add(CallRecord.DomainExpression);
            const std::string HiddenName = "__task_call_result_" + std::to_string(SyntheticTaskCallCount++);
            const uint32 HiddenStringId = Strings.Add(HiddenName);
            CallRecord.OutputSlot = AllocateVariableSlot(HiddenStringId);
            ioCalls.emplace_back(std::move(CallRecord));

            ValueRecord Result;
            Result.Kind = HTNIRValueKind::Variable;
            Result.Text = HiddenStringId;
            Result.DebugText = DebugExpressionStringId;
            Result.VariableSlot = ioCalls.back().OutputSlot;
            SetSource(Result, inNode);
            Result.DebugAsVariable = false;
            return Result;
        }
        return MakeValueRecord(inNode);
    }

    uint32 AllocateFactSlot(const uint32 inStringId)
    {
        const auto Existing = FactSlotByStringId.find(inStringId);
        if (Existing != FactSlotByStringId.end())
            return Existing->second;
        const uint32 Slot = static_cast<uint32>(FactStringIds.size());
        FactStringIds.push_back(inStringId);
        FactSlotByStringId.emplace(inStringId, Slot);
        AllocatePreparedSymbolSlot(inStringId);
        return Slot;
    }

    uint32 AddCondition(const AST::ConditionPtr& inNode)
    {
        if (!inNode) return kNoIndex;

        ConditionRecord Record;
        Record.DomainExpression = FormatDomainCondition(*inNode);
        SetSource(Record, *inNode);
        const uint32 Index = static_cast<uint32>(Conditions.size());
        Conditions.push_back(Record);

        const auto& Children = inNode->Children;
        if (inNode->Kind == AST::ConditionKind::Fact)
        {
            Record.Kind = HTN_CONDITION_FACT;
            const std::string Id = HTNAtomToString(inNode->GetIDNode()->GetValue(), false);
            Record.Id = Strings.Add(Id);
            Record.ResolvedIndex = AllocateFactSlot(Record.Id);
            Record.FirstArgument = static_cast<uint32>(Values.size());
            for (const auto& Argument : inNode->GetArgumentNodes()) AddValue(*Argument);
            Record.ArgumentCount = static_cast<uint32>(Values.size()) - Record.FirstArgument;
        }
        else if (inNode->Kind == AST::ConditionKind::Axiom)
        {
            Record.Kind = HTN_CONDITION_AXIOM;
            const std::string Id = HTNAtomToString(inNode->GetIDNode()->GetValue(), false);
            Record.Id = Strings.Add(Id);
            Record.FirstArgument = static_cast<uint32>(Values.size());
            for (const auto& Argument : inNode->GetArgumentNodes()) AddValue(*Argument);
            Record.ArgumentCount = static_cast<uint32>(Values.size()) - Record.FirstArgument;
        }
        else if (inNode->Kind == AST::ConditionKind::Call)
        {
            Record.Kind = inNode->HasOutputVariable() ? HTN_CONDITION_CALL_BIND : HTN_CONDITION_CALL;
            const std::string Id = HTNAtomToString(inNode->GetIDNode()->GetValue(), false);
            Record.Id = Strings.Add(Id);
            Record.ResolvedIndex = AllocateCallTermSlot(Record.Id);
            if (inNode->HasOutputVariable())
                Record.OutputValue = AddValue(*inNode->GetOutputVariableNode());
            Record.FirstArgument = static_cast<uint32>(Values.size());
            for (const auto& Argument : inNode->GetArgumentNodes()) AddValue(*Argument);
            Record.ArgumentCount = static_cast<uint32>(Values.size()) - Record.FirstArgument;
        }
        else if (inNode->Kind == AST::ConditionKind::Comparison)
        {
            Record.Kind = HTN_CONDITION_BUILTIN_COMPARISON;
            Record.Id = inNode->GetOperator();
            Record.FirstArgument = static_cast<uint32>(Values.size());
            AddValue(*inNode->GetLeftNode());
            AddValue(*inNode->GetRightNode());
            Record.ArgumentCount = 2u;
        }
        else if (inNode->Kind == AST::ConditionKind::Split)
        {
            Record.Kind = HTN_CONDITION_BUILTIN_LIST_SPLIT;
            Record.Id = inNode->GetOperation();
            Record.FirstArgument = static_cast<uint32>(Values.size());
            AddValue(*inNode->GetListNode());
            AddValue(*inNode->GetElementNode());
            AddValue(*inNode->GetRemainderNode());
            Record.ArgumentCount = 3u;
        }
        else if (inNode->Kind == AST::ConditionKind::And) Record.Kind=HTN_CONDITION_AND;
        else if (inNode->Kind == AST::ConditionKind::Or) Record.Kind=HTN_CONDITION_OR;
        else if (inNode->Kind == AST::ConditionKind::Alt) Record.Kind=HTN_CONDITION_ALT;
        else if (inNode->Kind == AST::ConditionKind::Not) Record.Kind=HTN_CONDITION_NOT;

        std::vector<uint32> ChildIndices;
        for (const auto& Child : Children) ChildIndices.push_back(AddCondition(Child));
        Record.FirstChildRef = static_cast<uint32>(ConditionChildRefs.size());
        ConditionChildRefs.insert(ConditionChildRefs.end(), ChildIndices.begin(), ChildIndices.end());
        Record.ChildCount = static_cast<uint32>(ChildIndices.size());

        Conditions[Index] = Record;
        return Index;
    }

    void CollectConditionVariableSlots(const uint32 inConditionIndex,
                                       std::array<uint64_t, HTN_GENERATED_VARIABLE_SLOT_MASK_WORDS>& ioMask) const
    {
        if (inConditionIndex == kNoIndex || inConditionIndex >= Conditions.size())
            return;

        const ConditionRecord& Condition = Conditions[inConditionIndex];
        for (uint32 I = 0u; I < Condition.ArgumentCount; ++I)
            MarkVariableSlot(ioMask, Values[Condition.FirstArgument + I]);
        if (Condition.OutputValue != kNoIndex)
            MarkVariableSlot(ioMask, Values[Condition.OutputValue]);

        for (uint32 I = 0u; I < Condition.ChildCount; ++I)
            CollectConditionVariableSlots(ConditionChildRefs[Condition.FirstChildRef + I], ioMask);
    }

    void BuildAxiomVariableSlotMask(AxiomRecord& ioAxiom) const
    {
        for (uint32 I = 0u; I < ioAxiom.ParameterCount; ++I)
            MarkVariableSlot(ioAxiom.VariableSlotMask, Values[ioAxiom.FirstParameter + I]);
        CollectConditionVariableSlots(ioAxiom.Condition, ioAxiom.VariableSlotMask);
    }

    void CollectTaskVariableSlots(const uint32 inTaskIndex,
                                  std::array<uint64_t, HTN_GENERATED_VARIABLE_SLOT_MASK_WORDS>& ioMask) const
    {
        if (inTaskIndex >= Tasks.size())
            return;

        const TaskRecord& Task = Tasks[inTaskIndex];
        for (uint32 I = 0u; I < Task.ArgumentCount; ++I)
            MarkVariableSlot(ioMask, Values[Task.FirstArgument + I]);

        if (inTaskIndex >= TaskCallExpressions.size())
            return;

        for (const TaskCallExpressionRecord& Call : TaskCallExpressions[inTaskIndex])
        {
            MarkVariableSlot(ioMask, Call.OutputSlot);
            for (const ValueRecord& Argument : Call.Arguments)
                MarkVariableSlot(ioMask, Argument);
        }
    }

    void BuildMethodVariableSlotMask(MethodRecord& ioMethod) const
    {
        // A compound call gets a fresh logical variable frame, but translator
        // variable slots are domain-global. The method mask identifies exactly
        // which slots this method can observe or mutate, allowing generated entry
        // to preserve only caller values that can actually collide with it.
        for (uint32 I = 0u; I < ioMethod.ParameterCount; ++I)
            MarkVariableSlot(ioMethod.VariableSlotMask, Values[ioMethod.FirstParameter + I]);

        for (uint32 LocalBranch = 0u; LocalBranch < ioMethod.BranchCount; ++LocalBranch)
        {
            const BranchRecord& Branch = Branches[ioMethod.FirstBranch + LocalBranch];
            CollectConditionVariableSlots(Branch.Condition, ioMethod.VariableSlotMask);
            for (uint32 LocalTask = 0u; LocalTask < Branch.TaskCount; ++LocalTask)
                CollectTaskVariableSlots(Branch.FirstTask + LocalTask, ioMethod.VariableSlotMask);
        }
    }

    void AddTask(const AST::Task& inNode)
    {
        TaskRecord Record;
        Record.DomainExpression = FormatDomainTask(inNode);
        Record.Kind = inNode.Kind == AST::TaskKind::Primitive ? HTN_TASK_PRIMITIVE
            : (inNode.Kind == AST::TaskKind::Deferred ? HTN_TASK_DEFERRED : HTN_TASK_COMPOUND);
        const std::string Id = HTNAtomToString(inNode.GetIDNode()->GetValue(), false);
        Record.Id = Strings.Add(Id);
        if (Record.Kind == HTN_TASK_PRIMITIVE)
            Record.PlanStepHeadSymbolSlot = AllocatePlanStepSymbolSlot('!', Id);
        else if (Record.Kind == HTN_TASK_DEFERRED)
            Record.PlanStepHeadSymbolSlot = AllocatePlanStepSymbolSlot('#', Id);
        SetSource(Record, inNode);

        std::vector<TaskCallExpressionRecord> Calls;
        std::vector<ValueRecord> Arguments;
        Arguments.reserve(inNode.GetArgumentNodes().size());
        for (const auto& Argument : inNode.GetArgumentNodes())
            Arguments.emplace_back(BuildTaskArgument(*Argument, Calls));

        Record.FirstArgument = static_cast<uint32>(Values.size());
        Values.insert(Values.end(), Arguments.begin(), Arguments.end());
        Record.ArgumentCount = static_cast<uint32>(Arguments.size());
        Tasks.push_back(Record);
        TaskCallExpressions.emplace_back(std::move(Calls));
    }

    const AST::Domain& Domain;
};

bool ResolveCompileTimeReferences(Builder& ioBuilder)
{
    // Generated values must never retain a constant indirection. Constants are
    // aliases known by the translator, so copy the terminal literal/identifier
    // into every use site once and remove the metadata lookup entirely.
    const std::vector<ValueRecord> OriginalValues = ioBuilder.Values;
    const auto ResolveFromOriginal = [&](uint32 inValueIndex) -> const ValueRecord*
    {
        for (uint32 Depth = 0u; Depth < 64u; ++Depth)
        {
            if (inValueIndex >= OriginalValues.size())
                return nullptr;
            const ValueRecord& Value = OriginalValues[inValueIndex];
            if (Value.Kind != HTNIRValueKind::Constant)
                return &Value;
            const auto It = std::find_if(ioBuilder.Constants.begin(), ioBuilder.Constants.end(),
                [&Value](const ConstantRecord& Constant) { return Constant.Id == Value.Text; });
            if (It == ioBuilder.Constants.end())
                return nullptr;
            inValueIndex = It->Value;
        }
        return nullptr;
    };

    for (uint32 ValueIndex = 0u; ValueIndex < static_cast<uint32>(ioBuilder.Values.size()); ++ValueIndex)
    {
        ValueRecord& Value = ioBuilder.Values[ValueIndex];
        if (Value.Kind != HTNIRValueKind::Constant)
            continue;
        const ValueRecord* Resolved = ResolveFromOriginal(ValueIndex);
        if (!Resolved || Resolved->Kind == HTNIRValueKind::Variable || Resolved->Kind == HTNIRValueKind::Constant)
        {
            ioBuilder.SetError("Generated constant could not be resolved to a static value at translation time");
            return false;
        }
        const uint32 SourceLine = Value.SourceLine;
        const HTNIRSourceLocation Source = Value.Source;
        const uint32 DebugText = Value.DebugText;
        Value = *Resolved;
        Value.DebugText = DebugText;
        Value.SourceLine = SourceLine;
        Value.Source = Source;
        Value.VariableSlot = kNoIndex;
    }

    // Task call expressions keep their argument ValueRecords outside Values because
    // nested calls are lowered dependency-first. Resolve constant aliases there too
    // so every generated callterm argument can become a direct HTNAtom reference.
    for (auto& TaskCalls : ioBuilder.TaskCallExpressions)
    {
        for (TaskCallExpressionRecord& Call : TaskCalls)
        {
            for (ValueRecord& Value : Call.Arguments)
            {
                if (Value.Kind != HTNIRValueKind::Constant)
                    continue;
                const auto ConstantIt = std::find_if(ioBuilder.Constants.begin(), ioBuilder.Constants.end(),
                    [&Value](const ConstantRecord& Constant) { return Constant.Id == Value.Text; });
                if (ConstantIt == ioBuilder.Constants.end())
                {
                    ioBuilder.SetError("Generated task call constant could not be resolved at translation time");
                    return false;
                }
                const ValueRecord* Resolved = ResolveFromOriginal(ConstantIt->Value);
                if (!Resolved || Resolved->Kind == HTNIRValueKind::Variable || Resolved->Kind == HTNIRValueKind::Constant)
                {
                    ioBuilder.SetError("Generated task call constant could not be resolved to a static value at translation time");
                    return false;
                }
                const uint32 SourceLine = Value.SourceLine;
                const HTNIRSourceLocation Source = Value.Source;
                const uint32 DebugText = Value.DebugText;
                Value = *Resolved;
                Value.DebugText = DebugText;
                Value.SourceLine = SourceLine;
                Value.Source = Source;
                Value.VariableSlot = kNoIndex;
            }
        }
    }

    ioBuilder.StaticValues.clear();
    size_t TaskCallArgumentCount = 0u;
    for (const auto& TaskCalls : ioBuilder.TaskCallExpressions)
        for (const TaskCallExpressionRecord& Call : TaskCalls)
            TaskCallArgumentCount += Call.Arguments.size();
    ioBuilder.StaticValues.reserve(ioBuilder.Values.size() + TaskCallArgumentCount);

    const auto AllocateStaticValue = [&ioBuilder](ValueRecord& Value)
    {
        Value.StaticValueIndex = kNoIndex;
        if (Value.Kind == HTNIRValueKind::Variable)
            return;
        Value.StaticValueIndex = static_cast<uint32>(ioBuilder.StaticValues.size());
        StaticValueRecord StaticValue;
        StaticValue.Text = Value.Text;
        StaticValue.AtomType = Value.AtomType;
        StaticValue.IntValue = Value.IntValue;
        StaticValue.FloatValue = Value.FloatValue;
        StaticValue.BoolValue = Value.BoolValue;
        StaticValue.ListElement = Value.ListElement;
        ioBuilder.StaticValues.emplace_back(StaticValue);
    };

    for (ValueRecord& Value : ioBuilder.Values)
        AllocateStaticValue(Value);
    for (auto& TaskCalls : ioBuilder.TaskCallExpressions)
        for (TaskCallExpressionRecord& Call : TaskCalls)
            for (ValueRecord& Value : Call.Arguments)
                AllocateStaticValue(Value);

    // Axiom names are also compile-time references. Store the exact axiom index
    // on the generated condition so generated axiom scope code never scans metadata.
    for (ConditionRecord& Condition : ioBuilder.Conditions)
    {
        if (Condition.Kind != HTN_CONDITION_AXIOM)
            continue;
        const auto AxiomIt = std::find_if(ioBuilder.Axioms.begin(), ioBuilder.Axioms.end(),
            [&Condition](const AxiomRecord& Axiom) { return Axiom.Id == Condition.Id; });
        if (AxiomIt == ioBuilder.Axioms.end())
        {
            ioBuilder.SetError("Generated axiom reference could not be resolved at translation time");
            return false;
        }
        Condition.ResolvedIndex = static_cast<uint32>(std::distance(ioBuilder.Axioms.begin(), AxiomIt));
    }
    return true;
}

bool StartsWithText(const std::string& inText, const char* inPrefix)
{
    return inText.rfind(inPrefix, 0) == 0;
}

bool ValidateCallTermCondition(const HTNCompilerIR& inBuilder, uint32 inConditionIndex,
                               std::unordered_map<uint32, bool>& ioMayBeBound, std::string& outError)
{
    if (inConditionIndex == kNoIndex || inConditionIndex >= inBuilder.Conditions.size())
        return true;

    const ConditionRecord& Condition = inBuilder.Conditions[inConditionIndex];
    const auto BindVariablesInArguments = [&]()
    {
        for (uint32 I = 0; I < Condition.ArgumentCount; ++I)
        {
            const ValueRecord& V = inBuilder.Values[Condition.FirstArgument + I];
            if (V.Kind == HTNIRValueKind::Variable)
                ioMayBeBound[V.Text] = true;
        }
    };

    if (Condition.Kind == HTN_CONDITION_FACT) // fact
    {
        BindVariablesInArguments();
        return true;
    }

    if (Condition.Kind == HTN_CONDITION_AXIOM) // axiom
    {
        const AxiomRecord* Axiom = nullptr;
        for (const AxiomRecord& Candidate : inBuilder.Axioms)
            if (Candidate.Id == Condition.Id) { Axiom = &Candidate; break; }
        if (!Axiom)
            return true;
        const uint32 Count = std::min(Axiom->ParameterCount, Condition.ArgumentCount);
        for (uint32 I = 0; I < Count; ++I)
        {
            const ValueRecord& Parameter = inBuilder.Values[Axiom->FirstParameter + I];
            const std::string& Name = inBuilder.Strings.Values[Parameter.Text];
            if (!StartsWithText(Name, "out_") && !StartsWithText(Name, "io_"))
                continue;
            const ValueRecord& Caller = inBuilder.Values[Condition.FirstArgument + I];
            if (Caller.Kind == HTNIRValueKind::Variable)
                ioMayBeBound[Caller.Text] = true;
        }
        return true;
    }

    if (Condition.Kind == HTN_CONDITION_BUILTIN_LIST_SPLIT)
    {
        for (uint32 I = 1u; I < Condition.ArgumentCount; ++I)
        {
            const ValueRecord& Output = inBuilder.Values[Condition.FirstArgument + I];
            if (Output.Kind == HTNIRValueKind::Variable)
                ioMayBeBound[Output.Text] = true;
        }
        return true;
    }

    if (Condition.Kind == HTN_CONDITION_CALL_BIND) // callterm with return binding
    {
        if (Condition.OutputValue == kNoIndex || Condition.OutputValue >= inBuilder.Values.size())
            return true;
        const ValueRecord& Output = inBuilder.Values[Condition.OutputValue];
        if (Output.Kind != HTNIRValueKind::Variable)
            return true;
        if (ioMayBeBound.find(Output.Text) != ioMayBeBound.end())
        {
            const std::string& Variable = inBuilder.Strings.Values[Output.Text];
            const std::string& CallTerm = Condition.Id < inBuilder.Strings.Values.size() ? inBuilder.Strings.Values[Condition.Id] : std::string("<unknown>");
            outError = "Callterm output variable '?" + Variable + "' may already be bound before call '" + CallTerm + "'";
            if (Condition.SourceLine != 0u)
                outError += " at domain line " + std::to_string(Condition.SourceLine);
            return false;
        }
        ioMayBeBound[Output.Text] = true;
        return true;
    }

    if (Condition.Kind == HTN_CONDITION_AND) // and: sequential
    {
        for (uint32 I = 0; I < Condition.ChildCount; ++I)
        {
            const uint32 Ref = Condition.FirstChildRef + I;
            if (Ref >= inBuilder.ConditionChildRefs.size() ||
                !ValidateCallTermCondition(inBuilder, inBuilder.ConditionChildRefs[Ref], ioMayBeBound, outError))
                return false;
        }
        return true;
    }

    if (Condition.Kind == HTN_CONDITION_OR || Condition.Kind == HTN_CONDITION_ALT) // or / alt: any branch may bind
    {
        std::unordered_map<uint32, bool> Union = ioMayBeBound;
        for (uint32 I = 0; I < Condition.ChildCount; ++I)
        {
            const uint32 Ref = Condition.FirstChildRef + I;
            if (Ref >= inBuilder.ConditionChildRefs.size())
                continue;
            auto BranchBindings = ioMayBeBound;
            if (!ValidateCallTermCondition(inBuilder, inBuilder.ConditionChildRefs[Ref], BranchBindings, outError))
                return false;
            for (const auto& [Key, Value] : BranchBindings)
                if (Value) Union[Key] = true;
        }
        ioMayBeBound = std::move(Union);
        return true;
    }

    // NOT does not export variable bindings. The callterm's world-state side effects,
    // if any, are deliberately persistent, but that is separate from variable binding.
    if (Condition.Kind == HTN_CONDITION_NOT)
    {
        for (uint32 I = 0; I < Condition.ChildCount; ++I)
        {
            const uint32 Ref = Condition.FirstChildRef + I;
            if (Ref >= inBuilder.ConditionChildRefs.size())
                continue;
            auto LocalBindings = ioMayBeBound;
            if (!ValidateCallTermCondition(inBuilder, inBuilder.ConditionChildRefs[Ref], LocalBindings, outError))
                return false;
        }
    }
    return true;
}


bool ValidateGeneratedKinds(const HTNCompilerIR& inBuilder, std::string& outError)
{
    for (const ConditionRecord& Condition : inBuilder.Conditions)
    {
        switch (Condition.Kind)
        {
        case HTN_CONDITION_FACT:
        case HTN_CONDITION_AXIOM:
        case HTN_CONDITION_AND:
        case HTN_CONDITION_OR:
        case HTN_CONDITION_ALT:
        case HTN_CONDITION_NOT:
        case HTN_CONDITION_CALL:
        case HTN_CONDITION_CALL_BIND:
            break;
        case HTN_CONDITION_BUILTIN_LIST_SPLIT:
            if (Condition.Id > 2u)
            {
                outError = "Invalid generated built-in list split operation";
                return false;
            }
            break;
        case HTN_CONDITION_BUILTIN_COMPARISON:
            switch (Condition.Id)
            {
            case HTN_BUILTIN_COMPARE_EQUAL:
            case HTN_BUILTIN_COMPARE_NOT_EQUAL:
            case HTN_BUILTIN_COMPARE_LESS:
            case HTN_BUILTIN_COMPARE_LESS_EQUAL:
            case HTN_BUILTIN_COMPARE_GREATER:
            case HTN_BUILTIN_COMPARE_GREATER_EQUAL:
                break;
            default:
                outError = "Invalid generated built-in comparison operator";
                return false;
            }
            break;
        default:
            outError = "Invalid generated condition kind";
            return false;
        }
    }

    for (const TaskRecord& Task : inBuilder.Tasks)
    {
        if (Task.Kind != HTN_TASK_COMPOUND && Task.Kind != HTN_TASK_PRIMITIVE && Task.Kind != HTN_TASK_DEFERRED)
        {
            outError = "Invalid generated task kind";
            return false;
        }
    }
    return true;
}

bool ValidateCallTermBindings(const HTNCompilerIR& inBuilder, std::string& outError)
{
    for (const MethodRecord& Method : inBuilder.Methods)
    {
        std::unordered_map<uint32, bool> Initial;
        for (uint32 I = 0; I < Method.ParameterCount; ++I)
        {
            const ValueRecord& Parameter = inBuilder.Values[Method.FirstParameter + I];
            if (Parameter.Kind == HTNIRValueKind::Variable)
                Initial[Parameter.Text] = true;
        }
        for (uint32 I = 0; I < Method.BranchCount; ++I)
        {
            const BranchRecord& Branch = inBuilder.Branches[Method.FirstBranch + I];
            auto Bindings = Initial;
            if (!ValidateCallTermCondition(inBuilder, Branch.Condition, Bindings, outError))
                return false;
        }
    }

    for (const AxiomRecord& Axiom : inBuilder.Axioms)
    {
        std::unordered_map<uint32, bool> Initial;
        for (uint32 I = 0; I < Axiom.ParameterCount; ++I)
        {
            const ValueRecord& Parameter = inBuilder.Values[Axiom.FirstParameter + I];
            if (Parameter.Kind != HTNIRValueKind::Variable)
                continue;
            const std::string& Name = inBuilder.Strings.Values[Parameter.Text];
            if (!StartsWithText(Name, "out_"))
                Initial[Parameter.Text] = true;
        }
        if (!ValidateCallTermCondition(inBuilder, Axiom.Condition, Initial, outError))
            return false;
    }
    return true;
}


} // namespace

bool HTNBuildCompilerIR(const AST::Domain& inDomain,
                        const std::vector<std::string>& inSourceFiles,
                        HTNGeneratedRuntimeBacktrackingSupport inRuntimeBacktrackingSupport,
                        HTNCompilerIR& outIR,
                        std::string& outError)
{
    // Methods accept input parameters only. Reject invalid signatures before
    // lowering calls into IR.
    for (const auto& Method : inDomain.GetMethodNodes())
    {
        for (const auto& Parameter : Method->GetParameterNodes())
        {
            const std::string& Name = HTNAtomGetValue<std::string>(Parameter->GetValue());
            if (!Name.starts_with("inp_"))
            {
                outError = "Method '" + Method->GetID() + "' parameter '?" + Name + "' must use the inp_ prefix";
                return false;
            }
        }
    }
    for (const auto& Axiom : inDomain.GetAxiomNodes())
    {
        for (const auto& Parameter : Axiom->GetParameterNodes())
        {
            const std::string& Name = HTNAtomGetValue<std::string>(Parameter->GetValue());
            if (!Name.starts_with("inp_") &&
                !Name.starts_with("out_") &&
                !Name.starts_with("io_"))
            {
                outError = "Axiom '" + Axiom->GetID() + "' parameter '?" + Name +
                           "' must use an inp_, out_ or io_ prefix";
                return false;
            }
        }
    }
    Builder B(inDomain);
    B.DomainId = inDomain.GetID();
    B.SourceFiles = inSourceFiles;
    B.RuntimeBacktrackingSupport = inRuntimeBacktrackingSupport;
    B.Build();
    if (!ResolveCompileTimeReferences(B))
    {
        outError = B.GetError();
        return false;
    }
    if (!ValidateGeneratedKinds(B, outError) || !ValidateCallTermBindings(B, outError))
        return false;
    if (B.VariableStringIds.size() > HTN_GENERATED_MAX_VARIABLE_SLOTS)
    {
        outError = "Generated domain requires " + std::to_string(B.VariableStringIds.size()) +
                   " variable slots, but HTN_GENERATED_MAX_VARIABLE_SLOTS is " +
                   std::to_string(HTN_GENERATED_MAX_VARIABLE_SLOTS);
        return false;
    }
    outIR = std::move(static_cast<HTNCompilerIR&>(B));
    return true;
}
