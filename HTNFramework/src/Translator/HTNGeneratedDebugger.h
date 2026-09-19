// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#ifdef HTN_DEBUG_DECOMPOSITION

#include "Translator/HTNGeneratedDebug.h"
#include "Core/HTNAtom.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

// Standalone event-based debugger for generated HTN execution.
// Deliberately depends only on generated metadata and generated event snapshots.
class HTNGeneratedDebugger
{
public:
    enum class NodeKind : std::uint8_t
    {
        Plan,
        Method,
        Branch,
        Fact,
        Axiom,
        And,
        Or,
        Alt,
        Not,
        Call,
        CallBind,
        BuiltinComparison,
        BuiltinListSplit,
        Task,
        UnknownCondition
    };

    struct SourceLocation
    {
        std::string DomainPath;
        std::uint32_t Line = 0u;
        std::uint32_t Column = 0u;
        std::uint32_t EndLine = 0u;
        std::uint32_t EndColumn = 0u;
    };

    struct Node
    {
        std::uint32_t EventNodeId = 0u;
        std::uint32_t MetadataIndex = HTN_GENERATED_NO_INDEX;
        std::uint32_t ParentEventNodeId = HTN_GENERATED_NO_INDEX;
        NodeKind Kind = NodeKind::UnknownCondition;
        SourceLocation Source;
        std::string DisplayName;
        enum class TitleTokenKind : std::uint8_t
        {
            Normal,
            Result,
            Variable,
            Constant,
            StringLiteral,
            CallExpression
        };

        struct TitleToken
        {
            TitleTokenKind Kind = TitleTokenKind::Normal;
            std::string Text;
        };

        struct VariableValue
        {
            std::uint32_t Slot = HTN_GENERATED_NO_INDEX;
            std::string Name;
            HTNAtomOwner Value;
        };

        struct ConstantValue
        {
            std::string Name;
            std::string Value;
        };

        bool Started = false;
        bool Completed = false;
        bool Succeeded = false;
        std::vector<TitleToken> TitleTokens;
        std::vector<ConstantValue> Constants;
        std::vector<VariableValue> VariablesBefore;
        std::vector<VariableValue> VariablesAfter;
        std::vector<std::uint64_t> ScopeVariableMask;
        std::vector<std::uint32_t> Children;
    };

    void SetEnabled(const bool inEnabled)
    {
        mEnabled = inEnabled;
        if (!mEnabled)
            Reset();
    }

    bool IsEnabled() const { return mEnabled; }

    void Reset(const char* inDomainPath = nullptr)
    {
        mDomainPath = inDomainPath ? inDomainPath : std::string();
        mNodes.clear();
        mOpenNodes.clear();
        mPendingTasks.clear();
        ++mRevision;
    }

    const std::string& GetDomainPath() const { return mDomainPath; }
    std::uint64_t GetRevision() const { return mRevision; }
    const std::vector<Node>& GetNodes() const { return mNodes; }

    const Node* FindNode(const std::uint32_t inEventNodeId) const
    {
        return inEventNodeId < mNodes.size() ? &mNodes[inEventNodeId] : nullptr;
    }

    void BeginPlan(const HTNGeneratedPlannerDefinition* inDomain, const std::uint32_t inMethodIndex, const HTNAtom* inValues = nullptr, const std::uint64_t* inBoundMask = nullptr, const std::uint32_t inSlotCount = 0u)
    {
        if (!mEnabled || !inDomain || !inDomain->debug_metadata || inMethodIndex >= inDomain->debug_metadata->method_count) return;
        const HTNGeneratedDebugMethod& DebugMethod = inDomain->debug_metadata->methods[inMethodIndex];
        BeginNode(NodeKind::Plan, inMethodIndex, DebugMethod.source_line, ResolveString(inDomain, DebugMethod.id, "plan"));
        ApplySourceLocation(mNodes.back(), inDomain, inDomain->debug_metadata->method_sources, inMethodIndex, inDomain->debug_metadata->method_count);
        SetCurrentNodeScopeMask(DebugMethod.variable_slot_mask);
        CaptureCurrentVariables(inDomain, inValues, inBoundMask, inSlotCount, true);
    }

    void EndPlan(const HTNGeneratedPlannerDefinition* inDomain, const HTNAtom* inValues, const std::uint64_t* inBoundMask, const std::uint32_t inSlotCount, const bool inResult) { EndNode(inDomain, inValues, inBoundMask, inSlotCount, inResult); }

    void BeginMethod(const HTNGeneratedPlannerDefinition* inDomain, const std::uint32_t inMethodIndex, const HTNAtom* inValues = nullptr, const std::uint64_t* inBoundMask = nullptr, const std::uint32_t inSlotCount = 0u)
    {
        if (!mEnabled || !inDomain || !inDomain->debug_metadata || inMethodIndex >= inDomain->debug_metadata->method_count) return;
        const HTNGeneratedDebugMethod& DebugMethod = inDomain->debug_metadata->methods[inMethodIndex];
        BeginNode(NodeKind::Method, inMethodIndex, DebugMethod.source_line, ResolveString(inDomain, DebugMethod.id, "method"));
        ApplySourceLocation(mNodes.back(), inDomain, inDomain->debug_metadata->method_sources, inMethodIndex, inDomain->debug_metadata->method_count);
        SetCurrentNodeScopeMask(DebugMethod.variable_slot_mask);
        CaptureCurrentVariables(inDomain, inValues, inBoundMask, inSlotCount, true);
    }

    void EndMethod(const HTNGeneratedPlannerDefinition* inDomain, const HTNAtom* inValues, const std::uint64_t* inBoundMask, const std::uint32_t inSlotCount, const bool inResult) { EndNode(inDomain, inValues, inBoundMask, inSlotCount, inResult); }

    void BeginBranch(const HTNGeneratedPlannerDefinition* inDomain, const std::uint32_t inBranchIndex, const HTNAtom* inValues = nullptr, const std::uint64_t* inBoundMask = nullptr, const std::uint32_t inSlotCount = 0u)
    {
        if (!mEnabled || !inDomain || !inDomain->debug_metadata || !inDomain->debug_metadata->branches || inBranchIndex >= inDomain->debug_metadata->branch_count) return;
        const HTNGeneratedDebugBranch& Branch = inDomain->debug_metadata->branches[inBranchIndex];
        BeginNode(NodeKind::Branch, inBranchIndex, Branch.source_line, ResolveString(inDomain, Branch.id, "branch"));
        ApplySourceLocation(mNodes.back(), inDomain, inDomain->debug_metadata->branch_sources, inBranchIndex, inDomain->debug_metadata->branch_count);
        BuildBranchScopeMask(inDomain, Branch, mNodes.back().ScopeVariableMask);
        AddParentMethodParametersToScope(inDomain, mNodes.back());
        CaptureCurrentVariables(inDomain, inValues, inBoundMask, inSlotCount, true);
    }

    void EndBranch(const HTNGeneratedPlannerDefinition* inDomain, const HTNAtom* inValues, const std::uint64_t* inBoundMask, const std::uint32_t inSlotCount, const bool inResult) { EndNode(inDomain, inValues, inBoundMask, inSlotCount, inResult); }

    void CapturePendingTask(const std::uint32_t inTaskIndex)
    {
        if (!mEnabled) return;
        PendingTask Pending;
        Pending.MetadataIndex = inTaskIndex;
        Pending.ParentEventNodeId = mOpenNodes.empty() ? HTN_GENERATED_NO_INDEX : mOpenNodes.back();
        mPendingTasks.push_back(Pending);
    }

    void BeginTask(const HTNGeneratedPlannerDefinition* inDomain, const std::uint32_t inTaskIndex, const HTNAtom* inValues = nullptr, const std::uint64_t* inBoundMask = nullptr, const std::uint32_t inSlotCount = 0u)
    {
        if (!mEnabled || !inDomain || !inDomain->debug_metadata || !inDomain->debug_metadata->tasks || inTaskIndex >= inDomain->debug_metadata->task_count) return;
        const HTNGeneratedDebugTask& Task = inDomain->debug_metadata->tasks[inTaskIndex];
        std::uint32_t ParentOverride = HTN_GENERATED_NO_INDEX;
        for (auto It = mPendingTasks.rbegin(); It != mPendingTasks.rend(); ++It)
        {
            if (It->MetadataIndex == inTaskIndex)
            {
                ParentOverride = It->ParentEventNodeId;
                mPendingTasks.erase(std::next(It).base());
                break;
            }
        }
        BeginNode(NodeKind::Task, inTaskIndex, Task.source_line, ResolveString(inDomain, Task.id, Task.kind == HTN_TASK_PRIMITIVE ? "primitive" : "compound"), ParentOverride);
        ApplySourceLocation(mNodes.back(), inDomain, inDomain->debug_metadata->task_sources, inTaskIndex, inDomain->debug_metadata->task_count);
        BuildTaskTitleTokens(inDomain, Task, mNodes.back());
        CaptureCurrentVariables(inDomain, inValues, inBoundMask, inSlotCount, true);
    }

    void EndTask(const HTNGeneratedPlannerDefinition* inDomain, const HTNAtom* inValues, const std::uint64_t* inBoundMask, const std::uint32_t inSlotCount, const bool inResult) { EndNode(inDomain, inValues, inBoundMask, inSlotCount, inResult); }

    void BeginAxiom(const HTNGeneratedPlannerDefinition* inDomain, const std::uint32_t inAxiomIndex, const HTNAtom* inValues = nullptr, const std::uint64_t* inBoundMask = nullptr, const std::uint32_t inSlotCount = 0u)
    {
        if (!mEnabled || !inDomain || !inDomain->debug_metadata || !inDomain->debug_metadata->axioms || inAxiomIndex >= inDomain->debug_metadata->axiom_count) return;
        const HTNGeneratedDebugAxiom& DebugAxiom = inDomain->debug_metadata->axioms[inAxiomIndex];
        BeginNode(NodeKind::Axiom, inAxiomIndex, DebugAxiom.source_line, ResolveString(inDomain, DebugAxiom.id, "axiom"));
        ApplySourceLocation(mNodes.back(), inDomain, inDomain->debug_metadata->axiom_sources, inAxiomIndex, inDomain->debug_metadata->axiom_count);
        SetCurrentNodeScopeMask(DebugAxiom.variable_slot_mask);
        CaptureCurrentVariables(inDomain, inValues, inBoundMask, inSlotCount, true);
    }

    void EndAxiom(const HTNGeneratedPlannerDefinition* inDomain, const HTNAtom* inValues, const std::uint64_t* inBoundMask, const std::uint32_t inSlotCount, const bool inResult) { EndNode(inDomain, inValues, inBoundMask, inSlotCount, inResult); }

    void BeginCondition(const HTNGeneratedPlannerDefinition* inDomain, const std::uint32_t inConditionIndex, const HTNAtom* inValues = nullptr, const std::uint64_t* inBoundMask = nullptr, const std::uint32_t inSlotCount = 0u)
    {
        if (!mEnabled || !inDomain || !inDomain->debug_metadata || !inDomain->debug_metadata->conditions || inConditionIndex >= inDomain->debug_metadata->condition_count) return;

        // Composite conditions pre-create their complete metadata subtree so the
        // debugger can always show every precondition, including terms skipped by
        // short-circuiting. When execution reaches one of those terms, reuse the
        // pending metadata node instead of creating a duplicate runtime node.
        const std::uint32_t Parent = mOpenNodes.empty() ? HTN_GENERATED_NO_INDEX : mOpenNodes.back();
        const std::uint32_t Existing = FindPendingConditionChild(Parent, inConditionIndex);
        if (Existing != HTN_GENERATED_NO_INDEX)
        {
            mNodes[Existing].Started = true;
            mOpenNodes.push_back(Existing);
        }
        else
        {
            const HTNGeneratedDebugCondition& Condition = inDomain->debug_metadata->conditions[inConditionIndex];
            const char* DisplayName = Condition.kind == HTN_CONDITION_BUILTIN_COMPARISON
                ? BuiltinComparisonName(Condition.id)
                : ResolveString(inDomain, Condition.id, ConditionName(Condition.kind));
            BeginNode(ConditionKind(Condition.kind), inConditionIndex, Condition.source_line, DisplayName);
            Node& RuntimeNode = mNodes.back();
            ApplySourceLocation(RuntimeNode, inDomain, inDomain->debug_metadata->condition_sources, inConditionIndex, inDomain->debug_metadata->condition_count);
            RuntimeNode.Started = true;
            BuildConditionTitleTokens(inDomain, Condition, RuntimeNode);
            if (Condition.kind == HTN_CONDITION_NOT)
            {
                if (RuntimeNode.ScopeVariableMask.empty())
                    RuntimeNode.ScopeVariableMask.assign(HTN_GENERATED_VARIABLE_SLOT_MASK_WORDS, 0u);
                CollectConditionScopeSlots(inDomain, inConditionIndex, RuntimeNode.ScopeVariableMask);
            }
            CreateConditionMetadataChildren(inDomain, RuntimeNode.EventNodeId, Condition);
        }

        CaptureCurrentVariables(inDomain, inValues, inBoundMask, inSlotCount, true);
    }

    void EndCondition(const HTNGeneratedPlannerDefinition* inDomain, const HTNAtom* inValues, const std::uint64_t* inBoundMask, const std::uint32_t inSlotCount, const bool inResult) { EndNode(inDomain, inValues, inBoundMask, inSlotCount, inResult); }

private:
    static void ApplySourceLocation(Node& ioNode, const HTNGeneratedPlannerDefinition* inDomain,
                                    const HTNGeneratedDebugSourceRange* inRanges,
                                    const std::uint32_t inIndex, const std::uint32_t inCount)
    {
        if (!inDomain || !inDomain->debug_metadata || !inRanges || inIndex >= inCount) return;
        const HTNGeneratedDebugMetadata& Metadata = *inDomain->debug_metadata;
        const HTNGeneratedDebugSourceRange& Range = inRanges[inIndex];
        if (Metadata.source_files && Range.source_file_index < Metadata.source_file_count && Metadata.source_files[Range.source_file_index])
            ioNode.Source.DomainPath = Metadata.source_files[Range.source_file_index];
        ioNode.Source.Line = Range.begin_line;
        ioNode.Source.Column = Range.begin_column;
        ioNode.Source.EndLine = Range.end_line;
        ioNode.Source.EndColumn = Range.end_column;
    }

    static const char* ResolveString(const HTNGeneratedPlannerDefinition* inDomain, const std::uint32_t inStringId, const char* inFallback)
    {
        if (inDomain && inDomain->debug_metadata && inDomain->debug_metadata->strings &&
            inStringId < inDomain->debug_metadata->string_count && inDomain->debug_metadata->strings[inStringId])
            return inDomain->debug_metadata->strings[inStringId];
        return inFallback;
    }

    static NodeKind ConditionKind(const std::uint32_t inKind)
    {
        switch (inKind)
        {
        case HTN_CONDITION_FACT: return NodeKind::Fact;
        case HTN_CONDITION_AXIOM: return NodeKind::Axiom;
        case HTN_CONDITION_AND: return NodeKind::And;
        case HTN_CONDITION_OR: return NodeKind::Or;
        case HTN_CONDITION_ALT: return NodeKind::Alt;
        case HTN_CONDITION_NOT: return NodeKind::Not;
        case HTN_CONDITION_CALL: return NodeKind::Call;
        case HTN_CONDITION_CALL_BIND: return NodeKind::CallBind;
        case HTN_CONDITION_BUILTIN_COMPARISON: return NodeKind::BuiltinComparison;
        case HTN_CONDITION_BUILTIN_LIST_SPLIT: return NodeKind::BuiltinListSplit;
        default: return NodeKind::UnknownCondition;
        }
    }


    static const char* BuiltinComparisonName(const std::uint32_t inOperator)
    {
        switch (inOperator)
        {
        case HTN_BUILTIN_COMPARE_EQUAL: return "==";
        case HTN_BUILTIN_COMPARE_NOT_EQUAL: return "!=";
        case HTN_BUILTIN_COMPARE_LESS: return "<";
        case HTN_BUILTIN_COMPARE_LESS_EQUAL: return "<=";
        case HTN_BUILTIN_COMPARE_GREATER: return ">";
        case HTN_BUILTIN_COMPARE_GREATER_EQUAL: return ">=";
        default: return "comparison";
        }
    }


    static const char* BuiltinListSplitName(const std::uint32_t inOperation)
    {
        switch (inOperation)
        {
        case HTN_BUILTIN_LIST_SPLIT: return "split_list";
        case HTN_BUILTIN_LIST_SPLIT_FRONT: return "split_list_front";
        case HTN_BUILTIN_LIST_SPLIT_BACK: return "split_list_back";
        default: return "split_list";
        }
    }

    static const char* ConditionName(const std::uint32_t inKind)
    {
        switch (inKind)
        {
        case HTN_CONDITION_AND: return "and";
        case HTN_CONDITION_OR: return "or";
        case HTN_CONDITION_ALT: return "alt";
        case HTN_CONDITION_NOT: return "not";
        case HTN_CONDITION_CALL: return "call";
        case HTN_CONDITION_CALL_BIND: return "call-bind";
        case HTN_CONDITION_BUILTIN_COMPARISON: return "comparison";
        case HTN_CONDITION_BUILTIN_LIST_SPLIT: return "split_list";
        case HTN_CONDITION_AXIOM: return "axiom";
        case HTN_CONDITION_FACT: return "fact";
        default: return "condition";
        }
    }

    static void AddTitleToken(Node& ioNode, const Node::TitleTokenKind inKind, const std::string& inText)
    {
        if (inText.empty())
            return;
        Node::TitleToken Token;
        Token.Kind = inKind;
        Token.Text = inText;
        ioNode.TitleTokens.emplace_back(std::move(Token));
    }

    static void AddConstantValue(Node& ioNode, const std::string& inName, const std::string& inValue)
    {
        const auto Existing = std::find_if(
            ioNode.Constants.begin(),
            ioNode.Constants.end(),
            [&inName](const Node::ConstantValue& inConstant)
            {
                return inConstant.Name == inName;
            });
        if (Existing != ioNode.Constants.end())
            return;

        Node::ConstantValue Constant;
        Constant.Name = inName;
        Constant.Value = inValue;
        ioNode.Constants.emplace_back(std::move(Constant));
    }

    static void AddValueTitleToken(const HTNGeneratedPlannerDefinition* inDomain, const std::uint32_t inValueIndex, Node& ioNode)
    {
        if (!inDomain || !inDomain->debug_metadata || !inDomain->debug_metadata->values || inValueIndex >= inDomain->debug_metadata->value_count)
        {
            AddTitleToken(ioNode, Node::TitleTokenKind::Normal, "<invalid>");
            return;
        }

        const HTNGeneratedDebugValue& Value = inDomain->debug_metadata->values[inValueIndex];
        // Debug metadata stores the original domain expression rather than the
        // resolved runtime value. This preserves names such as @split_list_input
        // and also keeps source punctuation such as '?' and string quotes intact.
        const std::string Text = ResolveString(inDomain, Value.text, "?");
        if ((Value.flags & HTN_GENERATED_DEBUG_VALUE_FLAG_VARIABLE) != 0u)
        {
            AddTitleToken(ioNode, Node::TitleTokenKind::Variable, Text);
            return;
        }

        if (!Text.empty() && Text.front() == '@')
        {
            AddTitleToken(ioNode, Node::TitleTokenKind::Constant, Text);
            AddConstantValue(ioNode, Text, ResolveString(inDomain, Value.resolved_text, "<unresolved>"));
            return;
        }

        if ((Value.flags & HTN_GENERATED_DEBUG_VALUE_FLAG_STRING_LITERAL) != 0u)
        {
            AddTitleToken(ioNode, Node::TitleTokenKind::StringLiteral, Text);
            return;
        }

        if ((Value.flags & HTN_GENERATED_DEBUG_VALUE_FLAG_CALL_EXPRESSION) != 0u)
        {
            AddTitleToken(ioNode, Node::TitleTokenKind::CallExpression, Text);
            return;
        }

        AddTitleToken(ioNode, Node::TitleTokenKind::Normal, Text);
    }

    static void BuildTaskTitleTokens(const HTNGeneratedPlannerDefinition* inDomain,
                                     const HTNGeneratedDebugTask& inTask,
                                     Node& ioNode)
    {
        ioNode.TitleTokens.clear();

        std::string Head;
        if ((inTask.kind == HTN_TASK_PRIMITIVE || inTask.kind == HTN_TASK_DEFERRED) &&
            inTask.plan_step_head_string_id != HTN_GENERATED_NO_INDEX)
        {
            Head = ResolveString(inDomain, inTask.plan_step_head_string_id,
                                 inTask.kind == HTN_TASK_PRIMITIVE ? "!primitive" : "#deferred");
        }
        else
        {
            Head = ResolveString(inDomain, inTask.id, inTask.kind == HTN_TASK_PRIMITIVE ? "!primitive" : "compound");
            if (inTask.kind == HTN_TASK_PRIMITIVE && (Head.empty() || Head.front() != '!'))
                Head.insert(Head.begin(), '!');
        }

        AddTitleToken(ioNode, Node::TitleTokenKind::Normal, Head);
        for (std::uint32_t I = 0u; I < inTask.argument_count; ++I)
            AddValueTitleToken(inDomain, inTask.first_argument + I, ioNode);

        ioNode.DisplayName.clear();
        for (const Node::TitleToken& Token : ioNode.TitleTokens)
        {
            if (!ioNode.DisplayName.empty())
                ioNode.DisplayName += ' ';
            ioNode.DisplayName += Token.Text;
        }
    }

    static void BuildConditionTitleTokens(const HTNGeneratedPlannerDefinition* inDomain,
                                          const HTNGeneratedDebugCondition& inCondition,
                                          Node& ioNode)
    {
        ioNode.TitleTokens.clear();
        switch (inCondition.kind)
        {
        case HTN_CONDITION_FACT:
            AddTitleToken(ioNode, Node::TitleTokenKind::Result, ResolveString(inDomain, inCondition.id, "fact"));
            break;
        case HTN_CONDITION_AXIOM:
            AddTitleToken(ioNode, Node::TitleTokenKind::Result,
                          std::string("#") + ResolveString(inDomain, inCondition.id, "axiom"));
            break;
        case HTN_CONDITION_CALL:
            AddTitleToken(ioNode, Node::TitleTokenKind::Result, "call");
            AddTitleToken(ioNode, Node::TitleTokenKind::Result, ResolveString(inDomain, inCondition.id, "call"));
            break;
        case HTN_CONDITION_CALL_BIND:
            if (inCondition.output_value != HTN_GENERATED_NO_INDEX)
                AddValueTitleToken(inDomain, inCondition.output_value, ioNode);
            AddTitleToken(ioNode, Node::TitleTokenKind::Result, "call");
            AddTitleToken(ioNode, Node::TitleTokenKind::Result, ResolveString(inDomain, inCondition.id, "call"));
            break;
        case HTN_CONDITION_BUILTIN_COMPARISON:
            AddTitleToken(ioNode, Node::TitleTokenKind::Result, BuiltinComparisonName(inCondition.id));
            break;
        case HTN_CONDITION_BUILTIN_LIST_SPLIT:
            AddTitleToken(ioNode, Node::TitleTokenKind::Result, BuiltinListSplitName(inCondition.id));
            break;
        case HTN_CONDITION_AND:
        case HTN_CONDITION_OR:
        case HTN_CONDITION_ALT:
        case HTN_CONDITION_NOT:
            AddTitleToken(ioNode, Node::TitleTokenKind::Result, ConditionName(inCondition.kind));
            break;
        default:
            AddTitleToken(ioNode, Node::TitleTokenKind::Result, ConditionName(inCondition.kind));
            break;
        }

        if (inCondition.kind == HTN_CONDITION_FACT ||
            inCondition.kind == HTN_CONDITION_AXIOM ||
            inCondition.kind == HTN_CONDITION_CALL ||
            inCondition.kind == HTN_CONDITION_CALL_BIND ||
            inCondition.kind == HTN_CONDITION_BUILTIN_COMPARISON ||
            inCondition.kind == HTN_CONDITION_BUILTIN_LIST_SPLIT)
        {
            if (inCondition.kind == HTN_CONDITION_BUILTIN_LIST_SPLIT &&
                inCondition.id == HTN_BUILTIN_LIST_SPLIT_BACK &&
                inCondition.argument_count == 3u)
            {
                AddValueTitleToken(inDomain, inCondition.first_argument, ioNode);
                AddValueTitleToken(inDomain, inCondition.first_argument + 2u, ioNode);
                AddValueTitleToken(inDomain, inCondition.first_argument + 1u, ioNode);
            }
            else
            {
                for (std::uint32_t I = 0u; I < inCondition.argument_count; ++I)
                    AddValueTitleToken(inDomain, inCondition.first_argument + I, ioNode);
            }
        }

        ioNode.DisplayName.clear();
        for (const Node::TitleToken& Token : ioNode.TitleTokens)
        {
            if (!ioNode.DisplayName.empty())
                ioNode.DisplayName += ' ';
            ioNode.DisplayName += Token.Text;
        }
    }

    std::uint32_t FindPendingConditionChild(const std::uint32_t inParentEventNodeId, const std::uint32_t inConditionIndex) const
    {
        if (inParentEventNodeId == HTN_GENERATED_NO_INDEX || inParentEventNodeId >= mNodes.size())
            return HTN_GENERATED_NO_INDEX;

        for (const std::uint32_t ChildId : mNodes[inParentEventNodeId].Children)
        {
            if (ChildId >= mNodes.size())
                continue;
            const Node& Child = mNodes[ChildId];
            if (!Child.Started && Child.MetadataIndex == inConditionIndex && IsConditionNodeKind(Child.Kind))
                return ChildId;
        }
        return HTN_GENERATED_NO_INDEX;
    }

    static bool IsConditionNodeKind(const NodeKind inKind)
    {
        switch (inKind)
        {
        case NodeKind::Fact:
        case NodeKind::Axiom:
        case NodeKind::And:
        case NodeKind::Or:
        case NodeKind::Alt:
        case NodeKind::Not:
        case NodeKind::Call:
        case NodeKind::CallBind:
        case NodeKind::BuiltinComparison:
        case NodeKind::BuiltinListSplit:
        case NodeKind::UnknownCondition:
            return true;
        default:
            return false;
        }
    }

    std::uint32_t CreateConditionMetadataNode(const HTNGeneratedPlannerDefinition* inDomain,
                                              const std::uint32_t inConditionIndex,
                                              const std::uint32_t inParentEventNodeId)
    {
        if (!inDomain || !inDomain->debug_metadata || !inDomain->debug_metadata->conditions || inConditionIndex >= inDomain->debug_metadata->condition_count)
            return HTN_GENERATED_NO_INDEX;

        const HTNGeneratedDebugCondition& Condition = inDomain->debug_metadata->conditions[inConditionIndex];
        Node NewNode;
        NewNode.EventNodeId = static_cast<std::uint32_t>(mNodes.size());
        NewNode.MetadataIndex = inConditionIndex;
        NewNode.ParentEventNodeId = inParentEventNodeId;
        NewNode.Kind = ConditionKind(Condition.kind);
        NewNode.Started = false;
        NewNode.Source.DomainPath = mDomainPath;
        NewNode.Source.Line = Condition.source_line;
        NewNode.Source.EndLine = Condition.source_line;
        ApplySourceLocation(NewNode, inDomain, inDomain->debug_metadata->condition_sources, inConditionIndex, inDomain->debug_metadata->condition_count);
        NewNode.DisplayName = Condition.kind == HTN_CONDITION_BUILTIN_COMPARISON
            ? BuiltinComparisonName(Condition.id)
            : ResolveString(inDomain, Condition.id, ConditionName(Condition.kind));
        BuildConditionTitleTokens(inDomain, Condition, NewNode);

        if (inParentEventNodeId != HTN_GENERATED_NO_INDEX && inParentEventNodeId < mNodes.size())
        {
            NewNode.ScopeVariableMask = mNodes[inParentEventNodeId].ScopeVariableMask;
            mNodes[inParentEventNodeId].Children.push_back(NewNode.EventNodeId);
        }

        if (Condition.kind == HTN_CONDITION_NOT)
        {
            if (NewNode.ScopeVariableMask.empty())
                NewNode.ScopeVariableMask.assign(HTN_GENERATED_VARIABLE_SLOT_MASK_WORDS, 0u);
            CollectConditionScopeSlots(inDomain, inConditionIndex, NewNode.ScopeVariableMask);
        }

        mNodes.push_back(std::move(NewNode));
        const std::uint32_t NewNodeId = static_cast<std::uint32_t>(mNodes.size() - 1u);
        CreateConditionMetadataChildren(inDomain, NewNodeId, Condition);
        return NewNodeId;
    }

    void CreateConditionMetadataChildren(const HTNGeneratedPlannerDefinition* inDomain,
                                         const std::uint32_t inParentEventNodeId,
                                         const HTNGeneratedDebugCondition& inCondition)
    {
        if (!inDomain || !inDomain->debug_metadata || !inDomain->debug_metadata->condition_child_refs || inCondition.child_count == 0u)
            return;

        for (std::uint32_t I = 0u; I < inCondition.child_count; ++I)
        {
            const std::uint32_t Ref = inCondition.first_child_ref + I;
            if (Ref >= inDomain->debug_metadata->condition_child_ref_count)
                continue;
            const std::uint32_t ChildCondition = inDomain->debug_metadata->condition_child_refs[Ref];
            CreateConditionMetadataNode(inDomain, ChildCondition, inParentEventNodeId);
        }
    }

    void BeginNode(const NodeKind inKind, const std::uint32_t inMetadataIndex, const std::uint32_t inLine, const char* inName, const std::uint32_t inParentOverride = HTN_GENERATED_NO_INDEX)
    {
        Node NewNode;
        NewNode.EventNodeId = static_cast<std::uint32_t>(mNodes.size());
        NewNode.MetadataIndex = inMetadataIndex;
        NewNode.ParentEventNodeId = inParentOverride != HTN_GENERATED_NO_INDEX ? inParentOverride : (mOpenNodes.empty() ? HTN_GENERATED_NO_INDEX : mOpenNodes.back());
        NewNode.Kind = inKind;
        NewNode.Started = true;
        NewNode.Source.DomainPath = mDomainPath;
        NewNode.Source.Line = inLine;
        NewNode.Source.EndLine = inLine;
        NewNode.DisplayName = inName ? inName : "";

        if (NewNode.ParentEventNodeId != HTN_GENERATED_NO_INDEX && NewNode.ParentEventNodeId < mNodes.size())
        {
            NewNode.ScopeVariableMask = mNodes[NewNode.ParentEventNodeId].ScopeVariableMask;
            mNodes[NewNode.ParentEventNodeId].Children.push_back(NewNode.EventNodeId);
        }

        mNodes.push_back(std::move(NewNode));
        mOpenNodes.push_back(static_cast<std::uint32_t>(mNodes.size() - 1u));
    }

    void SetCurrentNodeScopeMask(const std::uint64_t* inMask)
    {
        if (mOpenNodes.empty() || !inMask)
            return;
        Node& Current = mNodes[mOpenNodes.back()];
        Current.ScopeVariableMask.assign(inMask, inMask + HTN_GENERATED_VARIABLE_SLOT_MASK_WORDS);
    }

    static void MarkValueSlot(const HTNGeneratedPlannerDefinition* inDomain, const std::uint32_t inValueIndex, std::vector<std::uint64_t>& ioMask)
    {
        if (!inDomain || !inDomain->debug_metadata || !inDomain->debug_metadata->values || inValueIndex >= inDomain->debug_metadata->value_count)
            return;
        const HTNGeneratedDebugValue& Value = inDomain->debug_metadata->values[inValueIndex];
        if ((Value.flags & HTN_GENERATED_DEBUG_VALUE_FLAG_VARIABLE) == 0u || Value.variable_slot == HTN_GENERATED_NO_INDEX)
            return;
        const std::uint32_t Word = Value.variable_slot >> 6u;
        if (Word >= ioMask.size())
            return;
        ioMask[Word] |= (std::uint64_t{1} << (Value.variable_slot & 63u));
    }

    static void CollectConditionScopeSlots(const HTNGeneratedPlannerDefinition* inDomain, const std::uint32_t inConditionIndex, std::vector<std::uint64_t>& ioMask)
    {
        if (!inDomain || !inDomain->debug_metadata || !inDomain->debug_metadata->conditions || inConditionIndex >= inDomain->debug_metadata->condition_count)
            return;
        const HTNGeneratedDebugCondition& Condition = inDomain->debug_metadata->conditions[inConditionIndex];
        for (std::uint32_t I = 0u; I < Condition.argument_count; ++I)
            MarkValueSlot(inDomain, Condition.first_argument + I, ioMask);
        if (Condition.output_value != HTN_GENERATED_NO_INDEX)
            MarkValueSlot(inDomain, Condition.output_value, ioMask);
        if (!inDomain->debug_metadata->condition_child_refs)
            return;
        for (std::uint32_t I = 0u; I < Condition.child_count; ++I)
        {
            const std::uint32_t Ref = Condition.first_child_ref + I;
            if (Ref < inDomain->debug_metadata->condition_child_ref_count)
                CollectConditionScopeSlots(inDomain, inDomain->debug_metadata->condition_child_refs[Ref], ioMask);
        }
    }

    static void CollectExportedConditionScopeSlots(const HTNGeneratedPlannerDefinition* inDomain, const std::uint32_t inConditionIndex, std::vector<std::uint64_t>& ioMask)
    {
        if (!inDomain || !inDomain->debug_metadata || !inDomain->debug_metadata->conditions || inConditionIndex >= inDomain->debug_metadata->condition_count)
            return;

        const HTNGeneratedDebugCondition& Condition = inDomain->debug_metadata->conditions[inConditionIndex];
        for (std::uint32_t I = 0u; I < Condition.argument_count; ++I)
            MarkValueSlot(inDomain, Condition.first_argument + I, ioMask);
        if (Condition.output_value != HTN_GENERATED_NO_INDEX)
            MarkValueSlot(inDomain, Condition.output_value, ioMask);

        // Bindings created while evaluating NOT are local to NOT. They are useful
        // while inspecting its child subtree, but they must never become part of
        // the enclosing branch/watch scope.
        if (Condition.kind == HTN_CONDITION_NOT || !inDomain->debug_metadata->condition_child_refs)
            return;

        for (std::uint32_t I = 0u; I < Condition.child_count; ++I)
        {
            const std::uint32_t Ref = Condition.first_child_ref + I;
            if (Ref < inDomain->debug_metadata->condition_child_ref_count)
                CollectExportedConditionScopeSlots(inDomain, inDomain->debug_metadata->condition_child_refs[Ref], ioMask);
        }
    }

    static void BuildBranchScopeMask(const HTNGeneratedPlannerDefinition* inDomain, const HTNGeneratedDebugBranch& inBranch, std::vector<std::uint64_t>& outMask)
    {
        outMask.assign(HTN_GENERATED_VARIABLE_SLOT_MASK_WORDS, 0u);
        CollectExportedConditionScopeSlots(inDomain, inBranch.condition, outMask);
        if (!inDomain || !inDomain->debug_metadata || !inDomain->debug_metadata->tasks || !inDomain->debug_metadata->values)
            return;
        for (std::uint32_t I = 0u; I < inBranch.task_count; ++I)
        {
            const std::uint32_t TaskIndex = inBranch.first_task + I;
            if (TaskIndex >= inDomain->debug_metadata->task_count)
                continue;
            const HTNGeneratedDebugTask& Task = inDomain->debug_metadata->tasks[TaskIndex];
            for (std::uint32_t A = 0u; A < Task.argument_count; ++A)
                MarkValueSlot(inDomain, Task.first_argument + A, outMask);
        }
    }

    void AddParentMethodParametersToScope(const HTNGeneratedPlannerDefinition* inDomain, Node& ioNode) const
    {
        if (!inDomain || !inDomain->debug_metadata || !inDomain->debug_metadata->methods || ioNode.ParentEventNodeId == HTN_GENERATED_NO_INDEX ||
            ioNode.ParentEventNodeId >= mNodes.size())
            return;
        const Node& Parent = mNodes[ioNode.ParentEventNodeId];
        if ((Parent.Kind != NodeKind::Method && Parent.Kind != NodeKind::Plan) || Parent.MetadataIndex >= inDomain->debug_metadata->method_count)
            return;
        const HTNGeneratedDebugMethod& Method = inDomain->debug_metadata->methods[Parent.MetadataIndex];
        for (std::uint32_t I = 0u; I < Method.parameter_count; ++I)
            MarkValueSlot(inDomain, Method.first_parameter + I, ioNode.ScopeVariableMask);
    }

    void CaptureCurrentVariables(const HTNGeneratedPlannerDefinition* inDomain, const HTNAtom* inValues,
                                 const std::uint64_t* inBoundMask, const std::uint32_t inSlotCount,
                                 const bool inBefore)
    {
        if (!mEnabled || mOpenNodes.empty() || !inDomain || !inDomain->debug_metadata || !inValues || !inBoundMask || !inDomain->debug_metadata->variable_string_ids)
            return;

        Node& Current = mNodes[mOpenNodes.back()];
        std::vector<Node::VariableValue>& Out = inBefore ? Current.VariablesBefore : Current.VariablesAfter;
        Out.clear();
        const std::uint32_t SlotCount = std::min(inSlotCount, inDomain->debug_metadata->variable_slot_count);
        Out.reserve(SlotCount);
        for (std::uint32_t Slot = 0u; Slot < SlotCount; ++Slot)
        {
            const std::uint64_t Mask = std::uint64_t{1} << (Slot & 63u);
            if ((inBoundMask[Slot >> 6u] & Mask) == 0u)
                continue;
            if (!Current.ScopeVariableMask.empty())
            {
                const std::uint32_t Word = Slot >> 6u;
                if (Word >= Current.ScopeVariableMask.size() || (Current.ScopeVariableMask[Word] & Mask) == 0u)
                    continue;
            }

            Node::VariableValue Variable;
            Variable.Slot = Slot;
            Variable.Name = ResolveString(inDomain, inDomain->debug_metadata->variable_string_ids[Slot], "?");
            Variable.Value = inValues[Slot];
            Out.emplace_back(std::move(Variable));
        }
    }

    void EndNode(const HTNGeneratedPlannerDefinition* inDomain, const HTNAtom* inValues,
                 const std::uint64_t* inBoundMask, const std::uint32_t inSlotCount,
                 const bool inResult)
    {
        if (!mEnabled || mOpenNodes.empty()) return;
        CaptureCurrentVariables(inDomain, inValues, inBoundMask, inSlotCount, false);
        const std::uint32_t NodeId = mOpenNodes.back();
        mOpenNodes.pop_back();
        Node& Current = mNodes[NodeId];
        Current.Completed = true;
        Current.Succeeded = inResult;
    }

    struct PendingTask { std::uint32_t MetadataIndex = HTN_GENERATED_NO_INDEX; std::uint32_t ParentEventNodeId = HTN_GENERATED_NO_INDEX; };

    bool mEnabled = false;
    std::string mDomainPath;
    std::vector<Node> mNodes;
    std::vector<std::uint32_t> mOpenNodes;
    std::vector<PendingTask> mPendingTasks;
    std::uint64_t mRevision = 0u;
};

#endif // HTN_DEBUG_DECOMPOSITION
