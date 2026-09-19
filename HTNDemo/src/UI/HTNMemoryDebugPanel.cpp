// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#include "UI/HTNMemoryDebugPanel.h"

#include "imgui.h"

#include <cstdint>
#include <cstdio>

#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
#define HTN_MEMORY_DEBUG_VIEW
#endif

namespace
{
#ifdef HTN_MEMORY_DEBUG_VIEW
void FormatBytes(const std::uint64_t inBytes, char* outBuffer, const std::size_t inBufferSize)
{
    if (inBytes >= 1024u * 1024u)
        std::snprintf(outBuffer, inBufferSize, "%.2f MB", static_cast<double>(inBytes) / (1024.0 * 1024.0));
    else if (inBytes >= 1024u)
        std::snprintf(outBuffer, inBufferSize, "%.2f KB", static_cast<double>(inBytes) / 1024.0);
    else
        std::snprintf(outBuffer, inBufferSize, "%llu B", static_cast<unsigned long long>(inBytes));
}

void DrawCountRow(const char* inLabel, const std::uint64_t inValue)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(inLabel);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%llu", static_cast<unsigned long long>(inValue));
}

void DrawBytesRow(const char* inLabel, const std::uint64_t inValue)
{
    char Buffer[64];
    FormatBytes(inValue, Buffer, sizeof(Buffer));
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(inLabel);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(Buffer);
}

void DrawSignedDelta(const char* inLabel, const std::int64_t inValue)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(inLabel);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%+lld", static_cast<long long>(inValue));
}

bool BeginStatsTable(const char* inId)
{
    if (!ImGui::BeginTable(inId, 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        return false;

    ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch, 0.68f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.32f);
    return true;
}
#endif
}

void HTNMemoryDebugPanel::Render()
{
#ifdef HTN_MEMORY_DEBUG_VIEW
    const HTNMemoryDebugStats Stats = HTNMemoryDebug_GetStats();

    if (ImGui::Button("Capture baseline"))
    {
        mBaseline = Stats;
        mHasBaseline = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset peaks"))
        HTNMemoryDebug_ResetPeaks();

    ImGui::SameLine();
    ImGui::TextDisabled("Owned-resource diagnostics require HTN_MEMORY_ATOM_DIAGNOSTICS.");

    ImGui::SeparatorText("Strings owned by HTNAtom");
    if (BeginStatsTable("HTNAtomStringMemoryStats"))
    {
    DrawCountRow("Live heap strings", Stats.liveHeapStrings);
    DrawBytesRow("Live heap string bytes", Stats.heapStringBytes);
    DrawBytesRow("Peak heap string bytes", Stats.peakHeapStringBytes);
    DrawCountRow("Heap string allocations", Stats.heapStringAllocations);
    DrawCountRow("Heap string frees", Stats.heapStringFrees);
    ImGui::EndTable();
    }

    ImGui::SeparatorText("HTNAtomList allocators");
    if (BeginStatsTable("HTNAtomListMemoryStats"))
    {
    DrawCountRow("Live list nodes", Stats.liveListNodes);
    DrawCountRow("Peak live list nodes", Stats.peakLiveListNodes);
    DrawCountRow("new/delete live nodes", Stats.newDeleteListNodes);
    DrawCountRow("Pooled live nodes", Stats.pooledListNodes);
    DrawCountRow("Pooled capacity nodes", Stats.pooledCapacityNodes);
    DrawCountRow("Pooled allocators", Stats.pooledAllocatorCount);
    DrawBytesRow("Live list node payload", Stats.listNodeStorageBytes);
    DrawBytesRow("Pool reserved bytes", Stats.pooledReservedBytes);
    DrawCountRow("Node allocations", Stats.listNodeAllocations);
    DrawCountRow("Node deallocations", Stats.listNodeDeallocations);
    if (Stats.pooledCapacityNodes > 0u)
    {
        const double Utilization = 100.0 * static_cast<double>(Stats.pooledListNodes) / static_cast<double>(Stats.pooledCapacityNodes);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Pool utilization");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.2f%%", Utilization);
    }
    ImGui::EndTable();
    }

    ImGui::SeparatorText("Tracked memory");
    if (BeginStatsTable("HTNTrackedMemoryStats"))
    {
    DrawBytesRow("Tracked dynamic/pool total", Stats.trackedBytes);
    DrawCountRow("sizeof(HTNAtom)", Stats.atomSize);
    DrawCountRow("sizeof(HTNAtomList)", Stats.atomListSize);
    DrawCountRow("sizeof(HTNAtomNode)", Stats.atomNodeSize);
    ImGui::EndTable();
    }
    ImGui::TextDisabled("Tracked dynamic/pool total = heap strings + pool backing storage + live new/delete list nodes. General allocator overhead is not included.");

    ImGui::SeparatorText("Lifetime / leak diagnostics");
    if (!mHasBaseline)
    {
        ImGui::TextWrapped("Capture a baseline once the demo is in a stable state. Live resources are expected while the demo is running; a non-zero live count alone is not a leak.");
    }
    else
    {
        if (BeginStatsTable("HTNMemoryBaselineDelta"))
        {
        DrawSignedDelta("Heap strings delta", static_cast<std::int64_t>(Stats.liveHeapStrings) - static_cast<std::int64_t>(mBaseline.liveHeapStrings));
        DrawSignedDelta("Heap string bytes delta", static_cast<std::int64_t>(Stats.heapStringBytes) - static_cast<std::int64_t>(mBaseline.heapStringBytes));
        DrawSignedDelta("Live list nodes delta", static_cast<std::int64_t>(Stats.liveListNodes) - static_cast<std::int64_t>(mBaseline.liveListNodes));
        DrawSignedDelta("Pool reserved bytes delta", static_cast<std::int64_t>(Stats.pooledReservedBytes) - static_cast<std::int64_t>(mBaseline.pooledReservedBytes));
        ImGui::EndTable();
        }

        const bool ReturnedToBaseline = Stats.liveHeapStrings == mBaseline.liveHeapStrings &&
            Stats.heapStringBytes == mBaseline.heapStringBytes &&
            Stats.liveListNodes == mBaseline.liveListNodes &&
            Stats.pooledReservedBytes == mBaseline.pooledReservedBytes;

        if (ReturnedToBaseline)
            ImGui::Text("Baseline status: returned to baseline.");
        else
            ImGui::TextWrapped("Baseline status: resources differ from the captured baseline. This is a leak candidate only if the demo has returned to the same logical state.");
    }
#else
    ImGui::TextDisabled("HTN memory diagnostics require HTN_MEMORY_ATOM_DIAGNOSTICS.");
#endif
}
