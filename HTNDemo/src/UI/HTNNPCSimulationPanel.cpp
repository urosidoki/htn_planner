// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "UI/HTNNPCSimulationPanel.h"

#include "AI/AIHTNDemoWandererAgent.h"
#include "World/DemoWanderer.h"
#include "imgui.h"

#ifdef HTN_DEBUG_DECOMPOSITION
#include "UI/HTNGeneratedDebuggerView.h"
#include "UI/HTNWorldStateView.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>


struct HTNNPCDecompositionViewState
{
    ImGuiTextFilter WorldStateFilter;

#ifdef HTN_DEBUG_DECOMPOSITION
    HTNGeneratedDebuggerView GeneratedView;
    HTNGeneratedDebugger CapturedDebugger;
    bool HasCapture = false;
    std::uint32_t CapturedNPCId = 0u;
    std::uint64_t CapturedPlanCount = 0u;
    float CapturedAgeSeconds = 0.0f;
#endif
};

HTNNPCSimulationPanel::HTNNPCSimulationPanel(
    const HTNGeneratedPlannerDefinition* inWandererDefinition,
    const HTNCallTermRegistry& inCallTermRegistry)
    : mWandererDefinition(inWandererDefinition)
    , mCallTermRegistry(inCallTermRegistry)
    , mDecompositionViewState(std::make_unique<HTNNPCDecompositionViewState>())
{
    SpawnNPCs(8);
}

HTNNPCSimulationPanel::~HTNNPCSimulationPanel() = default;

#ifdef HTN_HOT_RELOAD_DEMO
void HTNNPCSimulationPanel::ReleaseGeneratedPlanner()
{
    for (const auto& NPC : mNPCs)
        NPC->ReleaseGeneratedPlanner();
    mWandererDefinition = nullptr;
    mDecompositionViewState = std::make_unique<HTNNPCDecompositionViewState>();
}

bool HTNNPCSimulationPanel::AttachGeneratedPlanner(const HTNGeneratedPlannerDefinition* inDefinition)
{
    if (!inDefinition)
        return false;
    mWandererDefinition = inDefinition;
    if (mNPCs.empty() && mNextNPCId == 1u)
    {
        SpawnNPCs(8);
        return std::all_of(mNPCs.begin(), mNPCs.end(), [](const auto& NPC) { return NPC->IsInitialized(); });
    }
    for (const auto& NPC : mNPCs)
        if (!NPC->AttachGeneratedPlanner(inDefinition))
            return false; // Caller releases the whole batch before rollback/unload.
    return true;
}
#endif

void HTNNPCSimulationPanel::Update(const float inDeltaTime)
{
    if (!mRunning)
        return;

    const float DeltaTime = std::max(0.0f, inDeltaTime) * mTimeScale;
    mSimulationAgeSeconds += DeltaTime;

    for (const std::unique_ptr<AIHTNDemoWandererAgent>& NPC : mNPCs)
    {
        if (NPC)
            NPC->Update(DeltaTime);
    }
}

void HTNNPCSimulationPanel::Render()
{
    RenderToolbar();
    ImGui::Separator();

    constexpr float VerticalSplitterWidth = 7.0f;
    constexpr float HorizontalSplitterHeight = 7.0f;
    constexpr float MinimumLeftPaneWidth = 430.0f;
    constexpr float MinimumInspectorWidth = 300.0f;
    constexpr float MinimumNPCListHeight = 150.0f;
    constexpr float MinimumWorldMapHeight = 160.0f;

    const ImVec2 AvailableSize = ImGui::GetContentRegionAvail();

    // Left/right split. Keep the width persistent, but always clamp it to the
    // currently available window size so resizing the parent cannot leave an
    // unusable pane behind.
    if (mLeftPaneWidth <= 0.0f)
        mLeftPaneWidth = AvailableSize.x * 0.52f;

    const float MaximumLeftPaneWidth = std::max(
        MinimumLeftPaneWidth,
        AvailableSize.x - MinimumInspectorWidth - VerticalSplitterWidth);
    mLeftPaneWidth = std::clamp(
        mLeftPaneWidth,
        std::min(MinimumLeftPaneWidth, MaximumLeftPaneWidth),
        MaximumLeftPaneWidth);

    ImGui::BeginChild("NPCSimulationList", ImVec2(mLeftPaneWidth, 0.0f), true);

    // Top/bottom split inside the left pane: NPC table above, world map below.
    // The world map itself fits its complete grid into whatever rectangle this
    // splitter leaves available.
    const float LeftPaneHeight = ImGui::GetContentRegionAvail().y;
    if (mNPCListPanelHeight <= 0.0f)
        mNPCListPanelHeight = LeftPaneHeight * 0.42f;

    const float MaximumNPCListHeight = std::max(
        MinimumNPCListHeight,
        LeftPaneHeight - MinimumWorldMapHeight - HorizontalSplitterHeight);
    mNPCListPanelHeight = std::clamp(
        mNPCListPanelHeight,
        std::min(MinimumNPCListHeight, MaximumNPCListHeight),
        MaximumNPCListHeight);

    ImGui::BeginChild("NPCSimulationTablePane", ImVec2(0.0f, mNPCListPanelHeight), false);
    RenderNPCList();
    ImGui::EndChild();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.22f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.38f, 0.42f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.48f, 0.48f, 0.52f, 1.0f));
    ImGui::Button("##NPCSimulationHorizontalSplitter", ImVec2(-1.0f, HorizontalSplitterHeight));
    ImGui::PopStyleColor(3);

    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    if (ImGui::IsItemActive())
    {
        mNPCListPanelHeight = std::clamp(
            mNPCListPanelHeight + ImGui::GetIO().MouseDelta.y,
            std::min(MinimumNPCListHeight, MaximumNPCListHeight),
            MaximumNPCListHeight);
    }

    ImGui::BeginChild("NPCSimulationWorldMapPane", ImVec2(0.0f, 0.0f), false);
    RenderWorldMap();
    ImGui::EndChild();

    ImGui::EndChild();

    ImGui::SameLine(0.0f, 0.0f);

    // Left/right draggable splitter. An InvisibleButton gives us the larger
    // hit target without forcing button visuals into the layout; draw the
    // actual separator ourselves so it remains subtle.
    const ImVec2 VerticalSplitterOrigin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(
        "##NPCSimulationVerticalSplitter",
        ImVec2(VerticalSplitterWidth, AvailableSize.y));

    const bool VerticalSplitterHovered = ImGui::IsItemHovered();
    const bool VerticalSplitterActive = ImGui::IsItemActive();
    if (VerticalSplitterHovered || VerticalSplitterActive)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    const ImU32 VerticalSplitterColor = ImGui::GetColorU32(
        VerticalSplitterActive
            ? ImGuiCol_SeparatorActive
            : (VerticalSplitterHovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(VerticalSplitterOrigin.x + VerticalSplitterWidth * 0.5f - 0.5f, VerticalSplitterOrigin.y),
        ImVec2(VerticalSplitterOrigin.x + VerticalSplitterWidth * 0.5f + 0.5f, VerticalSplitterOrigin.y + AvailableSize.y),
        VerticalSplitterColor);

    if (VerticalSplitterActive)
    {
        mLeftPaneWidth = std::clamp(
            mLeftPaneWidth + ImGui::GetIO().MouseDelta.x,
            std::min(MinimumLeftPaneWidth, MaximumLeftPaneWidth),
            MaximumLeftPaneWidth);
    }

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("NPCSimulationInspector", ImVec2(0.0f, 0.0f), true);
    RenderSelectedNPC();
    ImGui::EndChild();
}

void HTNNPCSimulationPanel::SpawnNPC()
{
    if (!mWandererDefinition)
        return;

    const std::size_t InitialWaypoint = static_cast<std::size_t>(mNextNPCId - 1u) % DemoWanderer::GetWaypointCount();
    auto NPC = std::make_unique<AIHTNDemoWandererAgent>(
        mNextNPCId++, mWandererDefinition, InitialWaypoint, mTerrain, mCallTermRegistry);
    (void)NPC->Initialize();
    mNPCs.emplace_back(std::move(NPC));

    if (mSelectedNPCIndex < 0)
        mSelectedNPCIndex = 0;
}

void HTNNPCSimulationPanel::SpawnNPCs(const int inCount)
{
    for (int Index = 0; Index < inCount; ++Index)
        SpawnNPC();
}

void HTNNPCSimulationPanel::RemoveSelectedNPC()
{
    if (mSelectedNPCIndex < 0 || static_cast<std::size_t>(mSelectedNPCIndex) >= mNPCs.size())
        return;

    mNPCs.erase(mNPCs.begin() + mSelectedNPCIndex);
    if (mNPCs.empty())
        mSelectedNPCIndex = -1;
    else if (static_cast<std::size_t>(mSelectedNPCIndex) >= mNPCs.size())
        mSelectedNPCIndex = static_cast<int>(mNPCs.size() - 1u);
}

void HTNNPCSimulationPanel::ResetSimulation()
{
    mNPCs.clear();
    mNextNPCId = 1u;
    mSelectedNPCIndex = -1;
    mSimulationAgeSeconds = 0.0f;
    SpawnNPCs(8);
}

void HTNNPCSimulationPanel::RenderToolbar()
{
    if (ImGui::Button(mRunning ? "Pause" : "Resume"))
        mRunning = !mRunning;

    ImGui::SameLine();
    if (ImGui::Button("Spawn NPC"))
        SpawnNPC();

    ImGui::SameLine();
    if (ImGui::Button("Spawn 10"))
        SpawnNPCs(10);

    ImGui::SameLine();
    if (mSelectedNPCIndex < 0)
        ImGui::BeginDisabled();
    if (ImGui::Button("Remove selected"))
        RemoveSelectedNPC();
    if (mSelectedNPCIndex < 0)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Reset"))
        ResetSimulation();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::SliderFloat("Time scale", &mTimeScale, 0.1f, 8.0f, "%.1fx", ImGuiSliderFlags_Logarithmic);

    std::uint64_t TotalPlans = 0u;
    std::uint64_t TotalTasks = 0u;
    double TotalPlannerMilliseconds = 0.0;
    const AIHTNDemoWandererAgent* MostExpensivePlanNPC = nullptr;
    for (const std::unique_ptr<AIHTNDemoWandererAgent>& NPC : mNPCs)
    {
        if (!NPC)
            continue;
        TotalPlans += NPC->GetPlanCount();
        TotalTasks += NPC->GetCompletedTaskCount();
        TotalPlannerMilliseconds += NPC->GetTotalPlannerMilliseconds();

        if (!MostExpensivePlanNPC || NPC->GetMaxPlannerMilliseconds() > MostExpensivePlanNPC->GetMaxPlannerMilliseconds())
            MostExpensivePlanNPC = NPC.get();
    }

    ImGui::Text("Simulation %.1fs | NPCs %llu | plans %llu | completed tasks %llu | planner total %.2f ms",
        mSimulationAgeSeconds,
        static_cast<unsigned long long>(mNPCs.size()),
        static_cast<unsigned long long>(TotalPlans),
        static_cast<unsigned long long>(TotalTasks),
        TotalPlannerMilliseconds);

    if (MostExpensivePlanNPC && MostExpensivePlanNPC->GetMaxPlannerPlanIndex() > 0u)
    {
        ImGui::Text("Worst decomposition: %.4f ms | NPC #%u | plan #%llu | age %.2fs | primitive tasks %llu | %s",
            MostExpensivePlanNPC->GetMaxPlannerMilliseconds(),
            MostExpensivePlanNPC->GetId(),
            static_cast<unsigned long long>(MostExpensivePlanNPC->GetMaxPlannerPlanIndex()),
            MostExpensivePlanNPC->GetMaxPlannerAgeSeconds(),
            static_cast<unsigned long long>(MostExpensivePlanNPC->GetMaxPlannerPrimitiveTaskCount()),
            MostExpensivePlanNPC->DidMaxPlannerPlanSucceed() ? "success" : "failed");
    }
    ImGui::TextDisabled("Each NPC keeps its planner hook, generated execution storage, world state, pathfinder and executor alive for its entire lifetime.");
}

void HTNNPCSimulationPanel::RenderNPCList()
{
    ImGui::TextUnformatted("Persistent NPCs");

    const float TableHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);

    if (ImGui::BeginTable("NPCSimulationTable", 8,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
        ImVec2(0.0f, TableHeight)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("NPC", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Destination", ImGuiTableColumnFlags_WidthFixed, 85.0f);
        ImGui::TableSetupColumn("Current task", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Plans", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Journeys", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableHeadersRow();

        for (std::size_t Index = 0u; Index < mNPCs.size(); ++Index)
        {
            const AIHTNDemoWandererAgent& NPC = *mNPCs[Index];
            const DemoWanderer& Wanderer = NPC.GetWanderer();
            const Cell& Location = Wanderer.GetCurrentLocation();
            const Cell& Destination = Wanderer.GetDestination();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            char Label[32];
            std::snprintf(Label, sizeof(Label), "#%u", NPC.GetId());
            const bool Selected = static_cast<int>(Index) == mSelectedNPCIndex;
            if (ImGui::Selectable(Label, Selected, ImGuiSelectableFlags_SpanAllColumns))
                mSelectedNPCIndex = static_cast<int>(Index);

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.1fs", NPC.GetAgeSeconds());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(Wanderer.GetStateName());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("(%d,%d)", Location.X, Location.Y);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("(%d,%d)", Destination.X, Destination.Y);
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(NPC.GetCurrentTaskName());
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%llu", static_cast<unsigned long long>(NPC.GetPlanCount()));
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%llu", static_cast<unsigned long long>(Wanderer.GetJourneyCount()));
        }

        ImGui::EndTable();
    }
}

void HTNNPCSimulationPanel::RenderWorldMap()
{
    ImGui::Text("World Map (%dx%d shared terrain)", mTerrain.GetWidth(), mTerrain.GetHeight());

    const ImU32 WalkableColor = ImGui::GetColorU32(ImVec4(0.16f, 0.17f, 0.19f, 1.0f));
    const ImU32 BlockedColor = ImGui::GetColorU32(ImVec4(0.18f, 0.29f, 0.45f, 1.0f));
    const ImU32 GridColor = ImGui::GetColorU32(ImVec4(0.34f, 0.35f, 0.37f, 0.85f));
    const ImU32 NPCColor = ImGui::GetColorU32(ImVec4(0.25f, 0.62f, 0.32f, 1.0f));
    const ImU32 NPCSelectedColor = ImGui::GetColorU32(ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
    const ImU32 InteractableColor = ImGui::GetColorU32(ImVec4(0.88f, 0.63f, 0.15f, 1.0f));
    const ImU32 SelectedPathColor = ImGui::GetColorU32(ImVec4(0.20f, 0.78f, 0.95f, 1.0f));
    const ImU32 TextColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 MutedTextColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    const auto DrawLegendEntry = [](const char* inLabel, const ImU32 inColor)
    {
        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        const ImVec2 P = ImGui::GetCursorScreenPos();
        DrawList->AddRectFilled(P, ImVec2(P.x + 12.0f, P.y + 12.0f), inColor);
        ImGui::Dummy(ImVec2(16.0f, 12.0f));
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::TextUnformatted(inLabel);
    };

    DrawLegendEntry("Walkable", WalkableColor);
    ImGui::SameLine();
    DrawLegendEntry("Blocked", BlockedColor);
    ImGui::SameLine();
    DrawLegendEntry("NPC", NPCColor);
    ImGui::SameLine();
    DrawLegendEntry("Interactable", InteractableColor);
    ImGui::SameLine();
    DrawLegendEntry("Selected path", SelectedPathColor);

    constexpr float AxisMargin = 24.0f;
    constexpr float BottomMargin = 20.0f;

    // The canvas always consumes the complete map pane.  Cell size is derived
    // from that rectangle, so resizing the splitter (or the whole window)
    // immediately scales the grid while preserving its aspect ratio.
    ImVec2 CanvasSize = ImGui::GetContentRegionAvail();
    CanvasSize.x = std::max(1.0f, CanvasSize.x);
    CanvasSize.y = std::max(1.0f, CanvasSize.y);

    const ImVec2 CanvasOrigin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("NPCWorldMapCanvas", CanvasSize);

    const float WidthForGrid = std::max(1.0f, CanvasSize.x - AxisMargin);
    const float HeightForGrid = std::max(1.0f, CanvasSize.y - BottomMargin);
    const float CellSize = std::max(0.25f, std::min(
        WidthForGrid / static_cast<float>(std::max(1, mTerrain.GetWidth())),
        HeightForGrid / static_cast<float>(std::max(1, mTerrain.GetHeight()))));

    const float GridWidthPixels = CellSize * static_cast<float>(mTerrain.GetWidth());
    const float GridHeightPixels = CellSize * static_cast<float>(mTerrain.GetHeight());
    const float HorizontalSlack = std::max(0.0f, WidthForGrid - GridWidthPixels);
    const float VerticalSlack = std::max(0.0f, HeightForGrid - GridHeightPixels);
    const ImVec2 GridOrigin(
        CanvasOrigin.x + AxisMargin + HorizontalSlack * 0.5f,
        CanvasOrigin.y + VerticalSlack * 0.5f);
    const bool CanvasHovered = ImGui::IsItemHovered();
    const bool CanvasClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const ImVec2 MousePosition = ImGui::GetIO().MousePos;
    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    DrawList->PushClipRect(
        CanvasOrigin,
        ImVec2(CanvasOrigin.x + CanvasSize.x, CanvasOrigin.y + CanvasSize.y),
        true);

    const auto CellTopLeft = [&](const Cell& inCell)
    {
        const int RenderY = mTerrain.GetHeight() - 1 - inCell.Y;
        return ImVec2(
            GridOrigin.x + static_cast<float>(inCell.X) * CellSize,
            GridOrigin.y + static_cast<float>(RenderY) * CellSize);
    };

    const auto CellCenter = [&](const Cell& inCell)
    {
        const ImVec2 P = CellTopLeft(inCell);
        return ImVec2(P.x + CellSize * 0.5f, P.y + CellSize * 0.5f);
    };

    for (int Y = 0; Y < mTerrain.GetHeight(); ++Y)
    {
        for (int X = 0; X < mTerrain.GetWidth(); ++X)
        {
            const Cell GridCell{X, Y};
            const ImVec2 Min = CellTopLeft(GridCell);
            const ImVec2 Max(Min.x + CellSize, Min.y + CellSize);
            DrawList->AddRectFilled(Min, Max, mTerrain.IsBlocked(GridCell) ? BlockedColor : WalkableColor);
            DrawList->AddRect(Min, Max, GridColor);
        }
    }

    // Avoid unreadable overlapping axis labels when the map is squeezed.
    const int AxisLabelStep =
        CellSize >= 16.0f ? 1 :
        CellSize >= 8.0f ? 2 :
        CellSize >= 4.0f ? 4 : 0;

    if (AxisLabelStep > 0)
    {
        for (int X = 0; X < mTerrain.GetWidth(); X += AxisLabelStep)
        {
            char Label[8];
            std::snprintf(Label, sizeof(Label), "%d", X);
            const ImVec2 TextSize = ImGui::CalcTextSize(Label);
            const float XCenter = GridOrigin.x + (static_cast<float>(X) + 0.5f) * CellSize;
            DrawList->AddText(ImVec2(XCenter - TextSize.x * 0.5f, GridOrigin.y + GridHeightPixels + 2.0f), MutedTextColor, Label);
        }

        for (int Y = 0; Y < mTerrain.GetHeight(); Y += AxisLabelStep)
        {
            char Label[8];
            std::snprintf(Label, sizeof(Label), "%d", Y);
            const ImVec2 TextSize = ImGui::CalcTextSize(Label);
            const ImVec2 Center = CellCenter(Cell{0, Y});
            DrawList->AddText(ImVec2(GridOrigin.x - TextSize.x - 5.0f, Center.y - TextSize.y * 0.5f), MutedTextColor, Label);
        }
    }

    // Draw the selected NPC's actual remaining resolved A* path. This is the
    // same path published by its pathfinder daemon to the HTN world state, not
    // a second UI-only pathfinding query.
    if (mSelectedNPCIndex >= 0 &&
        static_cast<std::size_t>(mSelectedNPCIndex) < mNPCs.size())
    {
        const AIHTNDemoWandererAgent& SelectedNPC = *mNPCs[static_cast<std::size_t>(mSelectedNPCIndex)];
        const DemoWanderer& SelectedWanderer = SelectedNPC.GetWanderer();

        std::vector<Cell> NavigationPath;
        if (SelectedNPC.GetCurrentNavigationPath(NavigationPath) && NavigationPath.size() >= 2u)
        {
            std::vector<ImVec2> PathPoints;
            PathPoints.reserve(NavigationPath.size());
            for (const Cell& PathCell : NavigationPath)
                PathPoints.push_back(CellCenter(PathCell));

            DrawList->AddPolyline(
                PathPoints.data(),
                static_cast<int>(PathPoints.size()),
                SelectedPathColor,
                ImDrawFlags_None,
                std::max(2.0f, CellSize * 0.12f));

            const float PointRadius = std::max(2.0f, CellSize * 0.08f);
            for (const ImVec2& Point : PathPoints)
                DrawList->AddCircleFilled(Point, PointRadius, SelectedPathColor, 10);
        }

        // Destination is useful even while the asynchronous path query is still
        // in progress and no resolved polyline is available yet.
        const ImVec2 DestinationCenter = CellCenter(SelectedWanderer.GetDestination());
        DrawList->AddCircle(
            DestinationCenter,
            std::max(5.0f, CellSize * 0.32f),
            SelectedPathColor,
            16,
            std::max(2.0f, CellSize * 0.08f));
    }

    for (const DemoGridInteractable& Interactable : mTerrain.GetInteractables())
    {
        const ImVec2 Center = CellCenter(Interactable.Location);
        const float Radius = std::max(3.0f, CellSize * 0.22f);
        const ImVec2 Top(Center.x, Center.y - Radius);
        const ImVec2 Right(Center.x + Radius, Center.y);
        const ImVec2 Bottom(Center.x, Center.y + Radius);
        const ImVec2 Left(Center.x - Radius, Center.y);
        DrawList->AddQuadFilled(Top, Right, Bottom, Left, InteractableColor);

        if (CellSize >= 24.0f)
        {
            const char* TypeName = DemoGridTerrain::GetInteractableTypeName(Interactable.Type);
            const char Marker = TypeName && TypeName[0] ? TypeName[0] : 'I';
            char MarkerText[2] = {Marker, '\0'};
            const ImVec2 MarkerSize = ImGui::CalcTextSize(MarkerText);
            DrawList->AddText(ImVec2(Center.x - MarkerSize.x * 0.5f, Center.y - MarkerSize.y * 0.5f), TextColor, MarkerText);
        }

        if (CanvasHovered)
        {
            const float DX = MousePosition.x - Center.x;
            const float DY = MousePosition.y - Center.y;
            if ((DX * DX + DY * DY) <= (Radius + 4.0f) * (Radius + 4.0f))
            {
                ImGui::BeginTooltip();
                ImGui::Text("%s (%s)", DemoGridTerrain::GetInteractableTypeName(Interactable.Type), Interactable.Id.c_str());
                ImGui::Text("Location: (%d,%d)", Interactable.Location.X, Interactable.Location.Y);
                ImGui::Text("Context task: %s", Interactable.ContextAnimation);
                ImGui::Text("Usage time: %.2f s", Interactable.UsageTimeSeconds);
                ImGui::EndTooltip();
            }
        }
    }

    struct RenderedNPC
    {
        std::size_t Index = 0u;
        ImVec2 Center;
        float Radius = 0.0f;
    };
    std::vector<RenderedNPC> RenderedNPCs;
    RenderedNPCs.reserve(mNPCs.size());

    for (std::size_t Index = 0u; Index < mNPCs.size(); ++Index)
    {
        const AIHTNDemoWandererAgent& NPC = *mNPCs[Index];
        const Cell& Location = NPC.GetWanderer().GetCurrentLocation();
        ImVec2 Center = CellCenter(Location);

        int SameCellOrdinal = 0;
        int SameCellCount = 0;
        for (std::size_t OtherIndex = 0u; OtherIndex < mNPCs.size(); ++OtherIndex)
        {
            if (mNPCs[OtherIndex]->GetWanderer().GetCurrentLocation() == Location)
            {
                if (OtherIndex < Index)
                    ++SameCellOrdinal;
                ++SameCellCount;
            }
        }

        if (SameCellCount > 1)
        {
            constexpr float TwoPi = 6.28318530718f;
            const float Angle = TwoPi * static_cast<float>(SameCellOrdinal) / static_cast<float>(SameCellCount);
            const float Offset = CellSize * 0.20f;
            Center.x += std::cos(Angle) * Offset;
            Center.y += std::sin(Angle) * Offset;
        }

        const float Radius = std::max(5.0f, CellSize * (SameCellCount > 1 ? 0.20f : 0.27f));
        const bool Selected = static_cast<int>(Index) == mSelectedNPCIndex;
        DrawList->AddCircleFilled(Center, Radius, NPCColor, 20);
        DrawList->AddCircle(Center, Radius, Selected ? NPCSelectedColor : GridColor, 20, Selected ? 2.5f : 1.0f);

        char IdText[16];
        std::snprintf(IdText, sizeof(IdText), "#%u", NPC.GetId());
        const ImVec2 IdSize = ImGui::CalcTextSize(IdText);
        DrawList->AddText(ImVec2(Center.x - IdSize.x * 0.5f, Center.y - IdSize.y * 0.5f), TextColor, IdText);

        const char* TaskName = NPC.GetCurrentTaskName();
        const ImVec2 TaskSize = ImGui::CalcTextSize(TaskName);
        const ImVec2 TaskMin(Center.x - TaskSize.x * 0.5f - 2.0f, Center.y + Radius + 1.0f);
        const ImVec2 TaskMax(TaskMin.x + TaskSize.x + 4.0f, TaskMin.y + TaskSize.y + 2.0f);
        DrawList->AddRectFilled(TaskMin, TaskMax, ImGui::GetColorU32(ImVec4(0.05f, 0.05f, 0.06f, 0.82f)), 2.0f);
        DrawList->AddText(ImVec2(TaskMin.x + 2.0f, TaskMin.y + 1.0f), TextColor, TaskName);

        RenderedNPCs.push_back({Index, Center, Radius});
    }

    if (CanvasHovered)
    {
        for (auto It = RenderedNPCs.rbegin(); It != RenderedNPCs.rend(); ++It)
        {
            const float DX = MousePosition.x - It->Center.x;
            const float DY = MousePosition.y - It->Center.y;
            if ((DX * DX + DY * DY) > (It->Radius + 3.0f) * (It->Radius + 3.0f))
                continue;

            const AIHTNDemoWandererAgent& NPC = *mNPCs[It->Index];
            const DemoWanderer& Wanderer = NPC.GetWanderer();
            ImGui::BeginTooltip();
            ImGui::Text("NPC #%u", NPC.GetId());
            ImGui::Text("Task: %s", NPC.GetCurrentTaskName());
            ImGui::Text("State: %s", Wanderer.GetStateName());
            ImGui::Text("Location: (%d,%d)", Wanderer.GetCurrentLocation().X, Wanderer.GetCurrentLocation().Y);
            ImGui::Text("Destination: (%d,%d)", Wanderer.GetDestination().X, Wanderer.GetDestination().Y);
            ImGui::Text("Move speed: %.2f cells/s", Wanderer.GetMoveSpeedCellsPerSecond());
            ImGui::EndTooltip();

            if (CanvasClicked)
                mSelectedNPCIndex = static_cast<int>(It->Index);
            break;
        }
    }

    DrawList->PopClipRect();
}

void HTNNPCSimulationPanel::RenderSelectedNPC()
{
    if (mSelectedNPCIndex < 0 || static_cast<std::size_t>(mSelectedNPCIndex) >= mNPCs.size())
    {
        ImGui::TextDisabled("Select an NPC to inspect its lifetime.");
        return;
    }

    const AIHTNDemoWandererAgent& NPC = *mNPCs[static_cast<std::size_t>(mSelectedNPCIndex)];
    const DemoWanderer& Wanderer = NPC.GetWanderer();
    const Cell& Location = Wanderer.GetCurrentLocation();
    const Cell& Destination = Wanderer.GetDestination();

    ImGui::Text("NPC #%u", NPC.GetId());
    ImGui::SameLine();
    ImGui::TextDisabled("age %.2fs", NPC.GetAgeSeconds());
    ImGui::Text("%s | (%d,%d) -> (%d,%d) | speed %.2f cells/s | journeys %llu",
        Wanderer.GetStateName(),
        Location.X, Location.Y,
        Destination.X, Destination.Y,
        Wanderer.GetMoveSpeedCellsPerSecond(),
        static_cast<unsigned long long>(Wanderer.GetJourneyCount()));

    if (const char* Animation = Wanderer.GetCurrentContextAnimationName())
        ImGui::Text("Context: %s", Animation);

    ImGui::Text("Planning: %s | plans %llu | last %.4f ms | max %.4f ms",
        NPC.DidLastPlanSucceed() ? "success" : "pending/failure",
        static_cast<unsigned long long>(NPC.GetPlanCount()),
        NPC.GetLastPlannerMilliseconds(),
        NPC.GetMaxPlannerMilliseconds());

    ImGui::Text("Current task: %s | remaining %.2fs",
        NPC.GetCurrentTaskName(), NPC.GetCurrentTaskRemainingSeconds());

    ImGui::Separator();

    if (ImGui::BeginTabBar("SelectedNPCInspectorTabs"))
    {
        if (ImGui::BeginTabItem("World State"))
        {
            RenderSelectedNPCWorldState(NPC);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Current Plan"))
        {
            RenderSelectedNPCPlan(NPC);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Decomposition"))
        {
            RenderSelectedNPCDecomposition(NPC);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("History"))
        {
            RenderSelectedNPCHistory(NPC);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

void HTNNPCSimulationPanel::RenderSelectedNPCWorldState(const AIHTNDemoWandererAgent& inNPC)
{
#ifdef HTN_DEBUG_DECOMPOSITION
    HTNNPCDecompositionViewState& ViewState = *mDecompositionViewState;
    ViewState.WorldStateFilter.Draw("Filter");
    ImGui::SameLine();
    ImGui::TextDisabled("Live world state for NPC #%u", inNPC.GetId());

    ImGui::BeginChild("SelectedNPCWorldState", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    RenderHTNWorldState(inNPC.GetWorldState(), ViewState.WorldStateFilter);
    ImGui::EndChild();
#else
    (void)inNPC;
    ImGui::TextDisabled("World-state inspection is compiled out in this configuration. Build with HTN_DEBUG_DECOMPOSITION.");
#endif
}

void HTNNPCSimulationPanel::RenderSelectedNPCPlan(const AIHTNDemoWandererAgent& inNPC)
{
    const std::vector<HTNAtomOwner>& Plan = inNPC.GetCurrentPlan();

    ImGui::Text("%llu primitive task(s) | current index %llu",
        static_cast<unsigned long long>(Plan.size()),
        static_cast<unsigned long long>(inNPC.GetCurrentTaskIndex()));

    ImGui::BeginChild("SelectedNPCPlan", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (Plan.empty())
    {
        ImGui::TextDisabled("No primitive plan queued; planner will run/re-run shortly.");
    }
    else
    {
        for (std::size_t Index = 0u; Index < Plan.size(); ++Index)
        {
            const bool IsCurrentTask = Index == inNPC.GetCurrentTaskIndex();
            const std::string TaskText = inNPC.FormatTaskForDisplay(Plan[Index]);
            ImGui::Text("%c [%02llu] %s",
                IsCurrentTask ? '>' : ' ',
                static_cast<unsigned long long>(Index),
                TaskText.c_str());

            if (IsCurrentTask)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(executing, %.2fs remaining)", inNPC.GetCurrentTaskRemainingSeconds());
            }
        }
    }
    ImGui::EndChild();
}

void HTNNPCSimulationPanel::RenderSelectedNPCDecomposition(const AIHTNDemoWandererAgent& inNPC)
{
#ifdef HTN_DEBUG_DECOMPOSITION
    HTNNPCDecompositionViewState& ViewState = *mDecompositionViewState;
    if (ViewState.HasCapture && ViewState.CapturedNPCId != inNPC.GetId())
    {
        ViewState.HasCapture = false;
        ViewState.GeneratedView.ClearSelection();
    }

    if (ImGui::Button("Capture generated decomposition") && inNPC.GetPlanCount() > 0u)
    {
        ViewState.CapturedDebugger = inNPC.GetLastGeneratedDebugger();
        ViewState.CapturedNPCId = inNPC.GetId();
        ViewState.CapturedPlanCount = inNPC.GetPlanCount();
        ViewState.CapturedAgeSeconds = inNPC.GetAgeSeconds();
        ViewState.HasCapture = true;
        ViewState.GeneratedView.ClearSelection();
        ViewState.GeneratedView.ResetExpansionOnNextRender();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Capture the latest generated event tree without running the planner again.");

    ImGui::SameLine();
    if (ViewState.HasCapture)
        ImGui::TextDisabled("plan #%llu at %.2fs%s",
            static_cast<unsigned long long>(ViewState.CapturedPlanCount),
            ViewState.CapturedAgeSeconds,
            ViewState.CapturedPlanCount == inNPC.GetPlanCount() ? "" : " (NPC has replanned)");
    else
        ImGui::TextDisabled("No generated decomposition captured");

    if (!ViewState.HasCapture)
        return;

    ViewState.GeneratedView.RenderDisplayModeSelector();
    ImGui::Separator();
    ImGui::BeginChild("SelectedNPCGeneratedEvents", ImVec2(0.0f, 220.0f), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ViewState.GeneratedView.RenderTree(ViewState.CapturedDebugger, false);
    ImGui::EndChild();
    ViewState.GeneratedView.RenderWatch(ViewState.CapturedDebugger);
    ImGui::Separator();
#endif
    ImGui::TextDisabled("Current generated plan");
    const auto& Plan = inNPC.GetCurrentPlan();
    if (Plan.empty())
    {
        ImGui::TextDisabled("No active plan");
        return;
    }
    ImGui::BeginChild("SelectedNPCDecomposition", ImVec2(0.0f, 0.0f), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (std::size_t Index = 0; Index < Plan.size(); ++Index)
        ImGui::Text("%zu: %s", Index, inNPC.FormatTaskForDisplay(Plan[Index]).c_str());
    ImGui::EndChild();
}

void HTNNPCSimulationPanel::RenderSelectedNPCHistory(const AIHTNDemoWandererAgent& inNPC)
{
    ImGui::BeginChild("SelectedNPCHistory", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    const auto& History = inNPC.GetHistory();
    for (auto It = History.rbegin(); It != History.rend(); ++It)
        ImGui::Text("[%6.2fs] %s", It->AgeSeconds, It->Text.c_str());
    ImGui::EndChild();
}
