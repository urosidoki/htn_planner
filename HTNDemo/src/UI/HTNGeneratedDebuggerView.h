// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#ifdef HTN_DEBUG_DECOMPOSITION

#include "UI/HTNGeneratedImGuiHelpers.h"
#include "Translator/HTNGeneratedDebugger.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>


// Presentation-only view for the generated/event debugger.
// The debug model lives in HTNGeneratedDebugger; this class owns only UI state.
namespace HTNImGuiHelpers = HTNGeneratedImGuiHelpers;

class HTNGeneratedDebuggerView final
{
public:
    enum class DisplayMode : std::uint8_t
    {
        Normal,
        Verbose
    };

    void ClearSelection()
    {
        mSelectedNodeId = HTN_GENERATED_NO_INDEX;
    }

    // Request that the next render restores the default execution-path expansion.
    // This is used after a new generated decomposition so stale ImGui tree state
    // from the previous run cannot leave the newly committed path collapsed.
    void ResetExpansionOnNextRender()
    {
        mResetExpansionOnNextRender = true;
    }

    void RenderDisplayModeSelector()
    {
        ImGui::SetNextItemWidth(110.0f);
        const char* Preview = mDisplayMode == DisplayMode::Verbose ? "Verbose" : "Normal";
        if (!ImGui::BeginCombo("View mode##GeneratedDebuggerDisplayMode", Preview))
            return;

        const bool IsNormal = mDisplayMode == DisplayMode::Normal;
        if (ImGui::Selectable("Normal", IsNormal))
        {
            mDisplayMode = DisplayMode::Normal;
            mResetExpansionOnNextRender = true;
        }
        if (IsNormal)
            ImGui::SetItemDefaultFocus();

        const bool IsVerbose = mDisplayMode == DisplayMode::Verbose;
        if (ImGui::Selectable("Verbose", IsVerbose))
        {
            mDisplayMode = DisplayMode::Verbose;
            mResetExpansionOnNextRender = true;
        }
        if (IsVerbose)
            ImGui::SetItemDefaultFocus();

        ImGui::EndCombo();
    }

    void RenderWatch(const HTNGeneratedDebugger& inDebugger) const
    {
        const HTNGeneratedDebugger::Node* Node = inDebugger.FindNode(mSelectedNodeId);
        if (!Node)
        {
            ImGui::TextDisabled("Select a generated event line.");
            return;
        }

        ImGui::Text("%s | %s", Node->DisplayName.c_str(),
            Node->Completed ? (Node->Succeeded ? "Success" : "Failure") : "Running");
        ImGui::SameLine();
        ImGui::TextDisabled("%s:%u", Node->Source.DomainPath.c_str(), Node->Source.Line);

        std::vector<std::string> VariableNames;
        VariableNames.reserve(Node->VariablesBefore.size() + Node->VariablesAfter.size());
        for (const auto& Variable : Node->VariablesBefore)
            VariableNames.emplace_back(Variable.Name);
        for (const auto& Variable : Node->VariablesAfter)
        {
            if (std::find(VariableNames.begin(), VariableNames.end(), Variable.Name) == VariableNames.end())
                VariableNames.emplace_back(Variable.Name);
        }
        std::sort(VariableNames.begin(), VariableNames.end());

        if (!Node->Constants.empty())
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Constants referenced by this event");
            for (const HTNGeneratedDebugger::Node::ConstantValue& Constant : Node->Constants)
            {
                ImGui::TextColored(HTNImGuiHelpers::GetDebuggerPalette().Argument, "%s", Constant.Name.c_str());
                ImGui::SameLine();
                ImGui::TextUnformatted("=");
                ImGui::SameLine();
                ImGui::TextUnformatted(Constant.Value.c_str());
            }
        }

        if (VariableNames.empty())
        {
            ImGui::Separator();
            ImGui::TextDisabled("No bound variables at this event.");
            return;
        }

        if (ImGui::BeginTable("GeneratedEventWatchTable", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Variable");
            ImGui::TableSetupColumn("Before");
            ImGui::TableSetupColumn("After");
            ImGui::TableHeadersRow();

            for (const std::string& Name : VariableNames)
            {
                const auto* Before = FindVariable(Node->VariablesBefore, Name);
                const auto* After = FindVariable(Node->VariablesAfter, Name);
                const std::string BeforeText = Before ? Before->Value.ToString(true) : std::string("<unbound>");
                const std::string AfterText = After ? After->Value.ToString(true) : std::string("<unbound>");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(HTNImGuiHelpers::GetVariableColor(Name), "%s", Name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(BeforeText.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(AfterText.c_str());
            }
            ImGui::EndTable();
        }
    }

    void RenderTree(const HTNGeneratedDebugger& inDebugger, const bool inForceExecutionExpansion)
    {
        // Give this tree's raw numeric node IDs (EventNodeId + 1) their own ImGui ID
        // scope. Without this, those small integers can collide with the ID of any
        // other widget rendered in the same window (columns, child windows, buttons
        // built earlier in the frame), causing ImGui's persisted open/closed state
        // for a node to silently belong to something else instead. The dedicated
        // scope makes the persisted tree state stable across surrounding widgets.
        ImGui::PushID("GeneratedEventDebugger");
        const auto& Nodes = inDebugger.GetNodes();
        if (Nodes.empty())
        {
            ImGui::TextDisabled("Run generated code with Event Debugger enabled.");
            ImGui::PopID();
            return;
        }

        if (mSelectedNodeId != HTN_GENERATED_NO_INDEX && !inDebugger.FindNode(mSelectedNodeId))
            mSelectedNodeId = HTN_GENERATED_NO_INDEX;

        const bool ApplyExecutionExpansion = inForceExecutionExpansion ||
            mResetExpansionOnNextRender || mLastExpansionRevision != inDebugger.GetRevision();
        mExecutionExpansionIncomplete = false;
        if (ApplyExecutionExpansion)
        {
            mLastExpansionRevision = inDebugger.GetRevision();
            mExpandedBacktrackingChoices.clear();
        }

        for (const auto& Node : Nodes)
        {
            if (Node.ParentEventNodeId == HTN_GENERATED_NO_INDEX)
                RenderNode(inDebugger, Node.EventNodeId, ApplyExecutionExpansion);
        }

        if (ApplyExecutionExpansion)
        {
            // TreeNodeEx may observe the newly forced storage state one frame later.
            // Keep execution-path expansion active until every node that should be
            // open has actually reported itself open. This also lets newly visible
            // descendants (for example the committed backtracking candidate) receive
            // their own default expansion on the following frame.
            mResetExpansionOnNextRender = mExecutionExpansionIncomplete;
        }

        ImGui::PopID();
    }

private:
    static const HTNGeneratedDebugger::Node::VariableValue* FindVariable(
        const std::vector<HTNGeneratedDebugger::Node::VariableValue>& inVariables,
        const std::string& inName)
    {
        const auto It = std::find_if(inVariables.begin(), inVariables.end(),
            [&inName](const HTNGeneratedDebugger::Node::VariableValue& inVariable)
            {
                return inVariable.Name == inName;
            });
        return It != inVariables.end() ? &*It : nullptr;
    }

    static bool ContainsSuccessfulPath(const HTNGeneratedDebugger& inDebugger, const std::uint32_t inNodeId)
    {
        const HTNGeneratedDebugger::Node* Node = inDebugger.FindNode(inNodeId);
        if (!Node)
            return false;

        if (Node->Kind == HTNGeneratedDebugger::NodeKind::Branch && Node->Completed && !Node->Succeeded)
            return false;

        if (Node->Completed && Node->Succeeded)
            return true;

        for (const std::uint32_t Child : Node->Children)
        {
            if (ContainsSuccessfulPath(inDebugger, Child))
                return true;
        }
        return false;
    }

    struct VisibleChild
    {
        std::uint32_t EventNodeId = HTN_GENERATED_NO_INDEX;
        std::uint32_t AttemptCount = 1u;
        int ChoiceIndex = -1;
        bool ChoiceSucceeded = false;
        std::vector<std::uint32_t> ProjectedChildren;
    };

    struct ExpandedBacktrackingChoice
    {
        std::uint32_t ParentEventNodeId = HTN_GENERATED_NO_INDEX;
        std::uint32_t ChoiceEventNodeId = HTN_GENERATED_NO_INDEX;
    };

    std::uint32_t GetExpandedBacktrackingChoice(const std::uint32_t inParentEventNodeId) const
    {
        const auto It = std::find_if(mExpandedBacktrackingChoices.begin(), mExpandedBacktrackingChoices.end(),
            [inParentEventNodeId](const ExpandedBacktrackingChoice& inChoice)
            {
                return inChoice.ParentEventNodeId == inParentEventNodeId;
            });
        return It != mExpandedBacktrackingChoices.end() ? It->ChoiceEventNodeId : HTN_GENERATED_NO_INDEX;
    }

    void SetExpandedBacktrackingChoice(const std::uint32_t inParentEventNodeId, const std::uint32_t inChoiceEventNodeId)
    {
        const auto It = std::find_if(mExpandedBacktrackingChoices.begin(), mExpandedBacktrackingChoices.end(),
            [inParentEventNodeId](const ExpandedBacktrackingChoice& inChoice)
            {
                return inChoice.ParentEventNodeId == inParentEventNodeId;
            });

        if (inChoiceEventNodeId == HTN_GENERATED_NO_INDEX)
        {
            if (It != mExpandedBacktrackingChoices.end())
                mExpandedBacktrackingChoices.erase(It);
            return;
        }

        if (It != mExpandedBacktrackingChoices.end())
        {
            It->ChoiceEventNodeId = inChoiceEventNodeId;
            return;
        }

        mExpandedBacktrackingChoices.push_back({ inParentEventNodeId, inChoiceEventNodeId });
    }

    bool ShouldDisplayNode(const HTNGeneratedDebugger::Node& inNode) const
    {
        return mDisplayMode == DisplayMode::Verbose || inNode.Started;
    }

    static bool IsSameSemanticNode(const HTNGeneratedDebugger::Node& inLeft,
                                   const HTNGeneratedDebugger::Node& inRight)
    {
        return inLeft.MetadataIndex != HTN_GENERATED_NO_INDEX &&
            inLeft.Kind == inRight.Kind &&
            inLeft.MetadataIndex == inRight.MetadataIndex;
    }

    std::vector<std::uint32_t> BuildStartedChildIds(
        const HTNGeneratedDebugger& inDebugger,
        const HTNGeneratedDebugger::Node& inNode) const
    {
        std::vector<std::uint32_t> Result;
        Result.reserve(inNode.Children.size());
        for (const std::uint32_t ChildId : inNode.Children)
        {
            const HTNGeneratedDebugger::Node* Child = inDebugger.FindNode(ChildId);
            if (Child && ShouldDisplayNode(*Child))
                Result.push_back(ChildId);
        }
        return Result;
    }

    std::vector<VisibleChild> BuildVisibleChildren(
        const HTNGeneratedDebugger& inDebugger,
        const HTNGeneratedDebugger::Node& inNode) const
    {
        const std::vector<std::uint32_t> StartedChildren = BuildStartedChildIds(inDebugger, inNode);
        std::vector<VisibleChild> Result;
        Result.reserve(StartedChildren.size());

        if (mDisplayMode == DisplayMode::Verbose)
        {
            for (const std::uint32_t ChildId : StartedChildren)
                Result.push_back({ ChildId });
            return FlattenAxiomImplementationChildren(inDebugger, inNode, std::move(Result));
        }

        // Represent a fact that produced several valid bindings as a real choice
        // point: one numbered row per successful match.
        // Preserve that semantic information here instead of reducing it to
        // "retry xN". The events following each successful fact visit belong to
        // that candidate until the next visit of the same fact; project that range
        // below the candidate.
        for (std::size_t I = 0u; I < StartedChildren.size(); ++I)
        {
            const HTNGeneratedDebugger::Node* Child = inDebugger.FindNode(StartedChildren[I]);
            if (!Child)
                continue;

            if (Child->Kind == HTNGeneratedDebugger::NodeKind::Fact &&
                Child->MetadataIndex != HTN_GENERATED_NO_INDEX)
            {
                std::vector<std::size_t> Occurrences;
                std::uint32_t SuccessfulCount = 0u;
                for (std::size_t J = I; J < StartedChildren.size(); ++J)
                {
                    const HTNGeneratedDebugger::Node* Candidate = inDebugger.FindNode(StartedChildren[J]);
                    if (!Candidate || !IsSameSemanticNode(*Child, *Candidate))
                        continue;
                    Occurrences.push_back(J);
                    if (Candidate->Completed && Candidate->Succeeded)
                        ++SuccessfulCount;
                }

                if (SuccessfulCount > 1u)
                {
                    int ChoiceIndex = 0;
                    for (std::size_t O = 0u; O < Occurrences.size(); ++O)
                    {
                        const std::size_t Position = Occurrences[O];
                        const HTNGeneratedDebugger::Node* Attempt = inDebugger.FindNode(StartedChildren[Position]);
                        if (!Attempt || !Attempt->Completed || !Attempt->Succeeded)
                            continue;

                        const std::size_t RangeEnd = O + 1u < Occurrences.size()
                            ? Occurrences[O + 1u]
                            : StartedChildren.size();

                        VisibleChild Visible;
                        Visible.EventNodeId = StartedChildren[Position];
                        Visible.ChoiceIndex = ChoiceIndex++;
                        const bool IsLastChoice = static_cast<std::uint32_t>(ChoiceIndex) == SuccessfulCount;
                        // A successful fact match is only a candidate. If execution
                        // later comes back to this same fact, that candidate was
                        // rolled back by the enclosing decomposition and must be
                        // shown as failed, just like the legacy debugger. Only the
                        // final candidate is committed, and only when the owning
                        // condition itself ultimately succeeds.
                        Visible.ChoiceSucceeded = IsLastChoice && inNode.Completed && inNode.Succeeded;
                        for (std::size_t J = Position + 1u; J < RangeEnd; ++J)
                            Visible.ProjectedChildren.push_back(StartedChildren[J]);
                        Result.emplace_back(std::move(Visible));
                    }

                    // Everything from the first visit onward belongs to one of the
                    // alternatives above. This mirrors the legacy decomposition-step
                    // range: downstream siblings are visible under the selected
                    // choice rather than duplicated beside every candidate.
                    break;
                }
            }

            // Regular retries are execution history, not extra semantic structure.
            // Keep the latest visit in Normal mode. Actual variable-binding choice
            // points are handled explicitly above and therefore never end up here.
            if (Child->MetadataIndex != HTN_GENERATED_NO_INDEX)
            {
                const auto Existing = std::find_if(Result.begin(), Result.end(),
                    [&inDebugger, Child](const VisibleChild& inVisibleChild)
                    {
                        const HTNGeneratedDebugger::Node* ExistingNode = inDebugger.FindNode(inVisibleChild.EventNodeId);
                        return ExistingNode && IsSameSemanticNode(*ExistingNode, *Child);
                    });

                if (Existing != Result.end())
                {
                    Existing->EventNodeId = StartedChildren[I];
                    ++Existing->AttemptCount;
                    continue;
                }
            }

            Result.push_back({ StartedChildren[I] });
        }

        return FlattenAxiomImplementationChildren(inDebugger, inNode, std::move(Result));
    }

    std::vector<VisibleChild> FlattenAxiomImplementationChildren(
        const HTNGeneratedDebugger& inDebugger,
        const HTNGeneratedDebugger::Node& inNode,
        std::vector<VisibleChild> inChildren) const
    {
        // An axiom condition already represents the call site ("#axiom ...").
        // Generated execution also creates an implementation-scope Axiom node
        // underneath it so BeginAxiom/EndAxiom can capture runtime state. That
        // scope is useful internally but is redundant in the decomposition tree:
        // opening the call-site axiom should reveal its preconditions directly.
        //
        // Condition axioms have title tokens; the implementation scope created by
        // BeginAxiom intentionally has none. Keep the model untouched and flatten
        // only this presentation-only wrapper here.
        if (inNode.Kind != HTNGeneratedDebugger::NodeKind::Axiom || inNode.TitleTokens.empty())
            return inChildren;

        std::vector<VisibleChild> Result;
        for (VisibleChild& Child : inChildren)
        {
            const HTNGeneratedDebugger::Node* ChildNode = inDebugger.FindNode(Child.EventNodeId);
            if (!ChildNode ||
                ChildNode->Kind != HTNGeneratedDebugger::NodeKind::Axiom ||
                !ChildNode->TitleTokens.empty())
            {
                Result.emplace_back(std::move(Child));
                continue;
            }

            std::vector<VisibleChild> ImplementationChildren = BuildVisibleChildren(inDebugger, *ChildNode);
            for (VisibleChild& ImplementationChild : ImplementationChildren)
                Result.emplace_back(std::move(ImplementationChild));
        }

        return Result;
    }

    std::uint32_t FindVisibleChoiceCount(
        const HTNGeneratedDebugger& inDebugger,
        const HTNGeneratedDebugger::Node& inNode) const
    {
        if (mDisplayMode == DisplayMode::Verbose)
            return 0u;

        const std::vector<VisibleChild> Children = BuildVisibleChildren(inDebugger, inNode);
        const std::uint32_t DirectChoiceCount = static_cast<std::uint32_t>(std::count_if(
            Children.begin(), Children.end(),
            [](const VisibleChild& inChild)
            {
                return inChild.ChoiceIndex >= 0;
            }));
        if (DirectChoiceCount > 1u)
            return DirectChoiceCount;

        // A generated axiom/call can wrap the actual fact choice point in one or
        // more structural nodes (axiom, AND, etc.). Follow single semantic paths
        // so the compact retry label reports the number of alternatives that the
        // Normal view actually exposes, rather than the number of times the outer
        // call event happened to be re-entered.
        if (Children.size() == 1u && Children.front().ChoiceIndex < 0)
        {
            const HTNGeneratedDebugger::Node* Child = inDebugger.FindNode(Children.front().EventNodeId);
            if (Child)
                return FindVisibleChoiceCount(inDebugger, *Child);
        }

        return 0u;
    }

    std::uint32_t GetVisibleRetryCount(
        const HTNGeneratedDebugger& inDebugger,
        const HTNGeneratedDebugger::Node& inNode,
        const std::uint32_t inAttemptCount) const
    {
        if (inAttemptCount <= 1u)
            return inAttemptCount;

        const std::uint32_t ChoiceCount = FindVisibleChoiceCount(inDebugger, inNode);
        return ChoiceCount > 1u ? ChoiceCount : inAttemptCount;
    }

    void RenderTitle(const HTNGeneratedDebugger::Node& inNode,
                     const bool inVerbose,
                     bool& ioRowHovered,
                     const bool* inResultOverride = nullptr)
    {
        if (inNode.TitleTokens.empty())
            return;

        const bool HasResult = inResultOverride != nullptr || inNode.Completed;
        const bool Result = inResultOverride != nullptr ? *inResultOverride : inNode.Succeeded;
        const ImVec4 ResultColor = HasResult
            ? HTNImGuiHelpers::GetResultColor(Result)
            : HTNImGuiHelpers::GetDebuggerPalette().NoResult;

        for (const HTNGeneratedDebugger::Node::TitleToken& Token : inNode.TitleTokens)
        {
            ImGui::SameLine(0.0f, 4.0f);
            switch (Token.Kind)
            {
            case HTNGeneratedDebugger::Node::TitleTokenKind::Result:
                ImGui::TextColored(ResultColor, "%s", Token.Text.c_str());
                break;
            case HTNGeneratedDebugger::Node::TitleTokenKind::Variable:
            {
                const std::string VariableName = !Token.Text.empty() && Token.Text.front() == '?'
                    ? Token.Text.substr(1u)
                    : Token.Text;
                ImGui::TextColored(HTNImGuiHelpers::GetVariableColor(VariableName), "%s", Token.Text.c_str());
                break;
            }
            case HTNGeneratedDebugger::Node::TitleTokenKind::Constant:
            case HTNGeneratedDebugger::Node::TitleTokenKind::StringLiteral:
                ImGui::TextColored(HTNImGuiHelpers::GetDebuggerPalette().Argument, "%s", Token.Text.c_str());
                break;
            case HTNGeneratedDebugger::Node::TitleTokenKind::CallExpression:
                ImGui::TextColored(HTNImGuiHelpers::GetDebuggerPalette().CallExpression, "%s", Token.Text.c_str());
                break;
            case HTNGeneratedDebugger::Node::TitleTokenKind::Normal:
            default:
                ImGui::TextUnformatted(Token.Text.c_str());
                break;
            }
            ioRowHovered |= ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                mSelectedNodeId = inNode.EventNodeId;
        }

        if (inVerbose)
        {
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::TextDisabled("%s:%u", inNode.Source.DomainPath.c_str(), inNode.Source.Line);
            ioRowHovered |= ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                mSelectedNodeId = inNode.EventNodeId;
        }
    }

    static bool IsCollapsible(const HTNGeneratedDebugger::Node& inNode)
    {
        return inNode.Kind == HTNGeneratedDebugger::NodeKind::Branch ||
            inNode.Kind == HTNGeneratedDebugger::NodeKind::Axiom;
    }

    struct RenderNodeRequest
    {
        enum class Action : std::uint8_t
        {
            Render,
            TreePop,
            Unindent
        };

        Action PendingAction = Action::Render;
        std::uint32_t NodeId = HTN_GENERATED_NO_INDEX;
        std::uint32_t AttemptCount = 1u;
        int ChoiceIndex = -1;
        std::vector<std::uint32_t> ProjectedChildren;
        bool ChoiceSucceeded = false;
        std::uint32_t ChoiceParentEventNodeId = HTN_GENERATED_NO_INDEX;
    };

    void RenderNode(const HTNGeneratedDebugger& inDebugger,
                    const std::uint32_t inNodeId,
                    const bool inApplyExecutionExpansion,
                    const std::uint32_t inAttemptCount = 1u,
                    const int inChoiceIndex = -1,
                    const std::vector<std::uint32_t>* inProjectedChildren = nullptr,
                    const bool inChoiceSucceeded = false,
                    const std::uint32_t inChoiceParentEventNodeId = HTN_GENERATED_NO_INDEX)
    {
        // Rendering the debugger tree must not depend on the native call stack.
        // Deep HTN hierarchies are perfectly valid, and the previous recursive
        // implementation could overflow the thread stack simply by opening a
        // sufficiently deep successful path. Keep the traversal explicitly on
        // the heap instead.
        std::vector<RenderNodeRequest> RenderStack;
        RenderStack.reserve(128u);

        RenderNodeRequest Root;
        Root.NodeId = inNodeId;
        Root.AttemptCount = inAttemptCount;
        Root.ChoiceIndex = inChoiceIndex;
        if (inProjectedChildren)
            Root.ProjectedChildren = *inProjectedChildren;
        Root.ChoiceSucceeded = inChoiceSucceeded;
        Root.ChoiceParentEventNodeId = inChoiceParentEventNodeId;
        RenderStack.emplace_back(std::move(Root));

        while (!RenderStack.empty())
        {
            RenderNodeRequest Request = std::move(RenderStack.back());
            RenderStack.pop_back();

            if (Request.PendingAction == RenderNodeRequest::Action::TreePop)
            {
                ImGui::TreePop();
                continue;
            }

            if (Request.PendingAction == RenderNodeRequest::Action::Unindent)
            {
                ImGui::Unindent();
                continue;
            }

            const std::uint32_t NodeId = Request.NodeId;
            const std::uint32_t AttemptCount = Request.AttemptCount;
            const int ChoiceIndex = Request.ChoiceIndex;
            const bool ChoiceSucceeded = Request.ChoiceSucceeded;
            const std::uint32_t ChoiceParentEventNodeId = Request.ChoiceParentEventNodeId;

            const HTNGeneratedDebugger::Node* Node = inDebugger.FindNode(NodeId);
            if (!Node || !ShouldDisplayNode(*Node))
                continue;

            const bool IsVerbose = mDisplayMode == DisplayMode::Verbose;
            std::vector<VisibleChild> VisibleChildren = BuildVisibleChildren(inDebugger, *Node);
            if (!IsVerbose && !Request.ProjectedChildren.empty())
            {
                HTNGeneratedDebugger::Node ProjectionParent;
                ProjectionParent.Children = Request.ProjectedChildren;
                VisibleChildren = BuildVisibleChildren(inDebugger, ProjectionParent);
            }

            // Branches and axioms are regular semantic grouping points. A numbered
            // backtracking candidate is also a grouping point when it owns projected
            // execution below that candidate. The disclosure triangle belongs to
            // each candidate (0, 1, ...), not to
            // the parent AND/condition that contains the choice point.
            const bool Collapsible = !VisibleChildren.empty() &&
                (IsCollapsible(*Node) || (!IsVerbose && ChoiceIndex >= 0));
            ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth;
            if (mSelectedNodeId == NodeId)
                Flags |= ImGuiTreeNodeFlags_Selected;
            if (!Collapsible)
                Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

            const char* StatusText = Node->Completed ? (Node->Succeeded ? "[OK]" : "[FAIL]") : (Node->Started ? "[...]" : "[--]");
            const void* TreeNodeId = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(NodeId + 1u));
            bool DesiredOpen = false;

            if (inApplyExecutionExpansion && Collapsible)
            {
                bool OpenByDefault = false;
                if (!IsVerbose && ChoiceIndex >= 0)
                {
                    OpenByDefault = ChoiceSucceeded;
                }
                else if (!IsVerbose && Node->Kind == HTNGeneratedDebugger::NodeKind::Axiom)
                {
                    OpenByDefault = false;
                }
                else if (Node->Kind == HTNGeneratedDebugger::NodeKind::Branch)
                {
                    // A branch is part of the visible execution path unless it is a
                    // completed failed alternative. Do not infer this from descendants:
                    // backtracking can leave failed historical condition nodes below a
                    // branch that ultimately succeeds, which made the whole successful
                    // path appear collapsed after a fresh Run/Run Both.
                    OpenByDefault = !Node->Completed || Node->Succeeded;
                }
                else
                {
                    OpenByDefault = ContainsSuccessfulPath(inDebugger, NodeId);
                }

                DesiredOpen = OpenByDefault;

                // Force the exact persistent state used by the TreeNodeEx below.
                // Keep SetNextItemOpen as well: it makes the intent explicit and covers
                // the normal ImGui path, while the storage write guarantees that stale
                // per-ID state from an earlier render cannot win on this execution reset.
                const ImGuiID ImGuiNodeId = ImGui::GetID(TreeNodeId);
                ImGuiStorage* Storage = ImGui::GetStateStorage();
                Storage->SetInt(ImGuiNodeId, OpenByDefault ? 1 : 0);
                ImGui::SetNextItemOpen(OpenByDefault, ImGuiCond_Always);
            }
            else
            {
            }

            const bool HasColoredTitle = !Node->TitleTokens.empty();
            const bool FailedBranch = Node->Kind == HTNGeneratedDebugger::NodeKind::Branch &&
                Node->Completed && !Node->Succeeded;
            if (FailedBranch)
                ImGui::PushStyleColor(ImGuiCol_Text, HTNImGuiHelpers::GetDebuggerPalette().FailedBranch);

            const bool Open = HasColoredTitle
                ? ImGui::TreeNodeEx(TreeNodeId, Flags, "%s", IsVerbose ? StatusText : "")
                : ImGui::TreeNodeEx(TreeNodeId, Flags,
                    IsVerbose ? "%s %s" : "%s",
                    IsVerbose ? StatusText : Node->DisplayName.c_str(),
                    Node->DisplayName.c_str());

            if (FailedBranch)
                ImGui::PopStyleColor();

            if (inApplyExecutionExpansion && Collapsible && DesiredOpen && !Open)
            {
                mExecutionExpansionIncomplete = true;
            }

            if (!IsVerbose && ChoiceIndex >= 0 && ChoiceParentEventNodeId != HTN_GENERATED_NO_INDEX)
            {

                // While restoring the execution path, the parent has already selected
                // the committed candidate. TreeNodeEx can still report closed for one
                // frame after its persistent state is forced, so do not let that
                // transient result erase the parent's selection. Outside automatic
                // expansion this remains the user-interaction writer.
                if (!inApplyExecutionExpansion)
                {
                    if (Open)
                        SetExpandedBacktrackingChoice(ChoiceParentEventNodeId, NodeId);
                    else if (GetExpandedBacktrackingChoice(ChoiceParentEventNodeId) == NodeId)
                        SetExpandedBacktrackingChoice(ChoiceParentEventNodeId, HTN_GENERATED_NO_INDEX);
                }
            }

            bool RowHovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                mSelectedNodeId = NodeId;

            if (!IsVerbose && ChoiceIndex >= 0)
            {
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextDisabled("%d", ChoiceIndex);
                RowHovered |= ImGui::IsItemHovered();
            }

            const bool* ResultOverride = !IsVerbose && ChoiceIndex >= 0 ? &ChoiceSucceeded : nullptr;
            const bool EffectiveSucceeded = ResultOverride ? *ResultOverride : Node->Succeeded;
            if (HTNImGuiHelpers::GetDebuggerPalette().ShowFailedFactMarker &&
                Node->Kind == HTNGeneratedDebugger::NodeKind::Fact &&
                Node->Completed && !EffectiveSucceeded)
            {
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextColored(HTNImGuiHelpers::GetDebuggerPalette().Fail, "X");
                RowHovered |= ImGui::IsItemHovered();
                if (ImGui::IsItemClicked())
                    mSelectedNodeId = NodeId;
            }

            RenderTitle(*Node, IsVerbose, RowHovered, ResultOverride);

            const std::uint32_t RetryCount = !IsVerbose
                ? GetVisibleRetryCount(inDebugger, *Node, AttemptCount)
                : AttemptCount;
            if (!IsVerbose && RetryCount > 1u)
            {
                ImGui::SameLine(0.0f, 6.0f);
                ImGui::TextDisabled("retry x%u", RetryCount);
                RowHovered |= ImGui::IsItemHovered();
            }

            if (RowHovered)
                RenderTooltip(*Node, RetryCount);

            if (VisibleChildren.empty())
                continue;

            if (Collapsible)
            {
                if (!Open)
                    continue;

                const bool HasBacktrackingChoices = !IsVerbose && std::count_if(
                    VisibleChildren.begin(), VisibleChildren.end(),
                    [](const VisibleChild& inChild) { return inChild.ChoiceIndex >= 0; }) > 1;

                if (HasBacktrackingChoices && inApplyExecutionExpansion)
                {
                    const auto SuccessfulChoice = std::find_if(VisibleChildren.begin(), VisibleChildren.end(),
                        [](const VisibleChild& inChild) { return inChild.ChoiceIndex >= 0 && inChild.ChoiceSucceeded; });
                    SetExpandedBacktrackingChoice(NodeId, SuccessfulChoice != VisibleChildren.end()
                        ? SuccessfulChoice->EventNodeId
                        : HTN_GENERATED_NO_INDEX);
                }

                const std::uint32_t ExpandedChoice = HasBacktrackingChoices
                    ? GetExpandedBacktrackingChoice(NodeId)
                    : HTN_GENERATED_NO_INDEX;

                RenderNodeRequest Pop;
                Pop.PendingAction = RenderNodeRequest::Action::TreePop;
                RenderStack.emplace_back(std::move(Pop));

                for (auto It = VisibleChildren.rbegin(); It != VisibleChildren.rend(); ++It)
                {
                    const VisibleChild& Child = *It;
                    if (HasBacktrackingChoices && ExpandedChoice != HTN_GENERATED_NO_INDEX &&
                        Child.ChoiceIndex >= 0 && Child.EventNodeId != ExpandedChoice)
                    {
                        continue;
                    }

                    RenderNodeRequest ChildRequest;
                    ChildRequest.NodeId = Child.EventNodeId;
                    ChildRequest.AttemptCount = Child.AttemptCount;
                    ChildRequest.ChoiceIndex = Child.ChoiceIndex;
                    ChildRequest.ProjectedChildren = Child.ProjectedChildren;
                    ChildRequest.ChoiceSucceeded = Child.ChoiceSucceeded;
                    ChildRequest.ChoiceParentEventNodeId = HasBacktrackingChoices
                        ? NodeId
                        : HTN_GENERATED_NO_INDEX;
                    RenderStack.emplace_back(std::move(ChildRequest));
                }

                continue;
            }

            // Match the Guerrilla debugger hierarchy: only branches and axioms are
            // interactive grouping points. Every other node is always expanded.
            // Choice-point projections are semantic siblings of the fact row, so do
            // not add indentation for those projected children.
            const bool ProjectedChoice = !IsVerbose && ChoiceIndex >= 0;
            if (!ProjectedChoice)
                ImGui::Indent();

            const bool HasBacktrackingChoices = !IsVerbose && std::count_if(
                VisibleChildren.begin(), VisibleChildren.end(),
                [](const VisibleChild& inChild) { return inChild.ChoiceIndex >= 0; }) > 1;

            if (HasBacktrackingChoices && inApplyExecutionExpansion)
            {
                const auto SuccessfulChoice = std::find_if(VisibleChildren.begin(), VisibleChildren.end(),
                    [](const VisibleChild& inChild) { return inChild.ChoiceIndex >= 0 && inChild.ChoiceSucceeded; });
                SetExpandedBacktrackingChoice(NodeId, SuccessfulChoice != VisibleChildren.end()
                    ? SuccessfulChoice->EventNodeId
                    : HTN_GENERATED_NO_INDEX);
            }

            const std::uint32_t ExpandedChoice = HasBacktrackingChoices
                ? GetExpandedBacktrackingChoice(NodeId)
                : HTN_GENERATED_NO_INDEX;

            if (!ProjectedChoice)
            {
                RenderNodeRequest Unindent;
                Unindent.PendingAction = RenderNodeRequest::Action::Unindent;
                RenderStack.emplace_back(std::move(Unindent));
            }

            for (auto It = VisibleChildren.rbegin(); It != VisibleChildren.rend(); ++It)
            {
                const VisibleChild& Child = *It;
                if (HasBacktrackingChoices && ExpandedChoice != HTN_GENERATED_NO_INDEX &&
                    Child.ChoiceIndex >= 0 && Child.EventNodeId != ExpandedChoice)
                {
                    continue;
                }

                RenderNodeRequest ChildRequest;
                ChildRequest.NodeId = Child.EventNodeId;
                ChildRequest.AttemptCount = Child.AttemptCount;
                ChildRequest.ChoiceIndex = Child.ChoiceIndex;
                ChildRequest.ProjectedChildren = Child.ProjectedChildren;
                ChildRequest.ChoiceSucceeded = Child.ChoiceSucceeded;
                ChildRequest.ChoiceParentEventNodeId = HasBacktrackingChoices
                    ? NodeId
                    : HTN_GENERATED_NO_INDEX;
                RenderStack.emplace_back(std::move(ChildRequest));
            }
        }
    }

    static void RenderTooltip(const HTNGeneratedDebugger::Node& inNode, const std::uint32_t inAttemptCount)
    {
        ImGui::BeginTooltip();
        ImGui::Text("Source: %s:%u", inNode.Source.DomainPath.c_str(), inNode.Source.Line);
        ImGui::TextDisabled("Generated event node: %u", inNode.EventNodeId);
        if (inAttemptCount > 1u)
            ImGui::TextDisabled("Execution attempts: %u (backtracking/retry)", inAttemptCount);

        if (!inNode.Constants.empty())
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Constants referenced by this event");
            for (const HTNGeneratedDebugger::Node::ConstantValue& Constant : inNode.Constants)
            {
                ImGui::TextColored(HTNImGuiHelpers::GetDebuggerPalette().Argument, "%s", Constant.Name.c_str());
                ImGui::SameLine();
                ImGui::TextUnformatted("=");
                ImGui::SameLine();
                ImGui::TextUnformatted(Constant.Value.c_str());
            }
        }

        if (!inNode.VariablesBefore.empty() || !inNode.VariablesAfter.empty())
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Bound variables in this scope");

            std::vector<std::string> ScopedNames;
            for (const auto& Variable : inNode.VariablesBefore)
                ScopedNames.emplace_back(Variable.Name);
            for (const auto& Variable : inNode.VariablesAfter)
            {
                if (std::find(ScopedNames.begin(), ScopedNames.end(), Variable.Name) == ScopedNames.end())
                    ScopedNames.emplace_back(Variable.Name);
            }
            std::sort(ScopedNames.begin(), ScopedNames.end());

            for (const std::string& Name : ScopedNames)
            {
                const auto* Before = FindVariable(inNode.VariablesBefore, Name);
                const auto* After = FindVariable(inNode.VariablesAfter, Name);
                const auto* Value = After ? After : Before;
                if (!Value)
                    continue;

                ImGui::TextColored(HTNImGuiHelpers::GetVariableColor(Name), "?%s", Name.c_str());
                ImGui::SameLine();
                ImGui::TextUnformatted("=");
                ImGui::SameLine();
                const std::string ValueText = Value->Value.ToString(true);
                ImGui::TextUnformatted(ValueText.c_str());
            }
        }
        else
        {
            ImGui::Separator();
            ImGui::TextDisabled("No bound variables in this scope.");
        }

        ImGui::EndTooltip();
    }

    std::vector<ExpandedBacktrackingChoice> mExpandedBacktrackingChoices;
    std::uint32_t mSelectedNodeId = HTN_GENERATED_NO_INDEX;
    std::uint64_t mLastExpansionRevision = 0u;
    bool mResetExpansionOnNextRender = false;
    bool mExecutionExpansionIncomplete = false;
    DisplayMode mDisplayMode = DisplayMode::Normal;
};

#endif // HTN_DEBUG_DECOMPOSITION
