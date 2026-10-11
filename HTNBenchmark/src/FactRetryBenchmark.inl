// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

// Included by main.cpp to share its allocation probe and timing infrastructure.
extern "C" const HTNGeneratedPlannerDefinition* CreateFactRetryHTN_GetDefinition(void);

#if defined(HTN_DEBUG_DECOMPOSITION) && defined(HTN_GENERATED_EXECUTION_PROFILING)
// Content digest includes every captured node/binding, including unsuccessful and
// unstarted conditions. Useful for comparing debugger optimizations without dumping MBs.
static uint64_t FactRetrySnapshotDigest(const std::vector<HTNGeneratedDebugger::Node>& inNodes)
{
    uint64_t Hash = 14695981039346656037ull;
    const auto Add = [&](const std::string& Text) {
        for (unsigned char C : Text) { Hash ^= C; Hash *= 1099511628211ull; }
        Hash ^= 0xff; Hash *= 1099511628211ull;
    };
    for (const auto& Node : inNodes)
    {
        Add(std::to_string(Node.EventNodeId)); Add(std::to_string(Node.ParentEventNodeId));
        Add(std::to_string(Node.MetadataIndex)); Add(std::to_string(static_cast<int>(Node.Kind)));
        Add(Node.DisplayName); Add(Node.Source.DomainPath);
        Add(std::to_string(Node.Source.Line)); Add(std::to_string(Node.Source.Column));
        Add(std::to_string(Node.Source.EndLine)); Add(std::to_string(Node.Source.EndColumn));
        Add(std::to_string(Node.Started)); Add(std::to_string(Node.Completed)); Add(std::to_string(Node.Succeeded));
        for (const auto& Token : Node.TitleTokens)
        { Add(std::to_string(static_cast<int>(Token.Kind))); Add(Token.Text); Add(std::to_string(Token.SpaceBefore)); }
        for (const auto& Constant : Node.Constants) { Add(Constant.Name); Add(Constant.Value); }
        for (const auto* Values : {&Node.VariablesBefore, &Node.VariablesAfter})
        {
            Add(std::to_string(Values->size()));
            for (const auto& Value : *Values)
            { Add(std::to_string(Value.Slot)); Add(Value.Name); Add(Value.Value.ToString(true)); }
        }
        for (auto Word : Node.ScopeVariableMask) Add(std::to_string(Word));
        for (auto Child : Node.Children) Add(std::to_string(Child));
    }
    return Hash;
}
#endif

static int RunFactRetryBenchmark(int inSamples, int inMaximumDebugEvents)
{
#ifndef HTN_GENERATED_EXECUTION_PROFILING
    (void)inSamples; (void)inMaximumDebugEvents;
    std::cerr << "Use --generated-execution-profiling when generating the build.\n";
    return 1;
#else
    const auto* Definition = CreateFactRetryHTN_GetDefinition();
    std::cout << "Fact retry benchmark; warmups=2, samples=" << inSamples
              << ", shuffled seed=1729; timings and diagnostic/allocation passes are separate.\n"
              << "Preparation is the generated entry preparation (included in total); execution=total-preparation.\n"
              << "Debugger event/capture/search timings come from an extra instrumented pass; copy is a deep SDK-node snapshot.\n"
              << "events,order,debugger,total_ms,prepare_ms,execution_ms,copy_ms,rows,retries,nodes,new_calls,new_bytes,copy_new_calls,copy_new_bytes,debug_event_ms,capture_ms,search_ms,children_examined,captures,slots_examined,atoms_copied,lists_copied,snapshot_digest\n";
    for (int Count : {16, 64, 256, 1024})
    for (const char* Order : {"ascending", "descending", "shuffled"})
    for (bool DebugEnabled : {false, true})
    {
#ifndef HTN_DEBUG_DECOMPOSITION
        if (DebugEnabled) continue;
#endif
        if (DebugEnabled && Count > inMaximumDebugEvents)
        {
            std::cerr << "Skipping debugger " << Count << '/' << Order
                      << ": explicit benchmark memory cap; pass --fact-retries <samples> 1024 to include.\n";
            continue;
        }
        HTNDatabaseHook Database;
        HTNCallTermRegistry Registry;
        HTNPlannerHook Hook(Database.GetWorldState(), Registry);
        if (!Hook.SetGeneratedPlannerDefinition(Definition)) return 2;
        Database.GetWorldState().SetFactRegistry(&Hook.GetFactRegistry());
        std::vector<int> IDs;
        for (int I = 1; I <= Count; ++I) IDs.push_back(I);
        if (std::string(Order) == "descending") std::reverse(IDs.begin(), IDs.end());
        if (std::string(Order) == "shuffled") { std::mt19937 Random(1729); std::shuffle(IDs.begin(), IDs.end(), Random); }
        for (int ID : IDs)
            if (!Database.GetWorldState().WriteFact(HtnSymbol::sGetSymbol("event"), HtnSymbol::sGetSymbol("explosion"), ID, static_cast<float>(ID))) return 3;

        HTNGeneratedPlannerContext Context{};
        Context.world_state = &Database.GetWorldState();
        Context.callterm_binding_context = &Hook.GetCallTermBindingContext();
        Context.prepared_storage = Hook.GetGeneratedPreparedStorage();
        Context.execution_storage = ::operator new(Definition->execution_storage_size);
        if (!Definition->initialize_execution_storage(Context.execution_storage)) return 4;
        Context.backtracking_mode = HTN_BACKTRACKING_ALL;
        Context.callterm_error_policy = HTNCallTermErrorPolicy::FailSilently;
#ifdef HTN_DEBUG_DECOMPOSITION
        HTNGeneratedDebugger Debugger;
        Debugger.SetEnabled(DebugEnabled);
        Context.debugger = &Debugger; // Disabled instance must not allocate or capture.
        std::vector<HTNGeneratedDebugger::Node> Snapshot;
#endif
        HTNAtomOwner Call(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("select_event")));
        HTNAtomOwner Plan;
        auto* Profile = Definition->get_execution_profiling(Context.execution_storage);
        const auto Run = [&] {
            return Definition->decompose_call(&Context, Call.Get(), 1, Plan.Get());
        };
        const auto Reset = [&] {
            HTNAtom_Unbind(Plan.Get());
#ifdef HTN_DEBUG_DECOMPOSITION
            Debugger.Reset();
#endif
        };
        const auto Valid = [&](HTNDecompositionStatus Status) {
            return Status == HTN_DECOMPOSITION_SUCCEEDED && Plan.GetListSize() == 1 &&
                HTNAtom_GetListElement(&Plan.GetListElement(0), 1)->value.int_value == Count;
        };
        std::vector<double> Total, Preparation, Copies;
        for (int I = -2; I < inSamples; ++I)
        {
            Reset(); // Host cleanup is outside decomposition/copy time.
            const auto Start = Clock::now();
            const auto Status = Run();
            const double Milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - Start).count();
            if (!Valid(Status)) { std::cerr << "Wrong selected event\n"; std::abort(); }
            HTNGeneratedPreparationTimingBreakdown Timing{};
            HTNGeneratedProfiling_GetLastPreparationTiming(Profile, &Timing);
            double CopyMilliseconds = 0.0;
#ifdef HTN_DEBUG_DECOMPOSITION
            Snapshot.clear(); // Old history destruction is not included in copy time.
            if (DebugEnabled)
            {
                const auto CopyStart = Clock::now();
                Snapshot = Debugger.GetNodes();
                CopyMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - CopyStart).count();
            }
#endif
            if (I >= 0) { Total.push_back(Milliseconds); Preparation.push_back(Timing.total_milliseconds); Copies.push_back(CopyMilliseconds); }
        }
        LifecyclePhase ExecutionAllocations, CopyAllocations;
        Reset();
        HTNDecompositionStatus Status;
        MeasureLifecyclePhase(ExecutionAllocations, true, [&] { Status = Run(); });
        if (!Valid(Status)) std::abort();
        uint64_t Nodes = 0, Digest = 0;
        uint64_t EventNs = 0, CaptureNs = 0, SearchNs = 0, Children = 0, Captures = 0, Slots = 0, Atoms = 0, Lists = 0;
#ifdef HTN_DEBUG_DECOMPOSITION
        Snapshot.clear();
        if (DebugEnabled)
            MeasureLifecyclePhase(CopyAllocations, true, [&] { Snapshot = Debugger.GetNodes(); });
        Reset();
        Debugger.SetPerformanceCountersEnabled(true);
        if (!Valid(Run())) std::abort();
        const auto& Stats = Debugger.GetPerformanceCounters();
        Nodes = Stats.NodesCreated; EventNs = Stats.EventNanoseconds; CaptureNs = Stats.CaptureNanoseconds;
        SearchNs = Stats.SearchNanoseconds; Children = Stats.ChildrenExamined; Captures = Stats.Captures;
        Slots = Stats.SlotsExamined; Atoms = Stats.AtomsCopied; Lists = Stats.ListsCopied;
        Digest = FactRetrySnapshotDigest(Debugger.GetNodes());
        if (!DebugEnabled && (Nodes || Captures || !Debugger.GetNodes().empty())) std::abort();
#endif
        HTNGeneratedStructuralCounters Counters{};
        HTNGeneratedProfiling_GetStructuralCounters(Profile, &Counters);
        const auto Median = [](std::vector<double>& Values) { std::sort(Values.begin(), Values.end()); return Values[Values.size() / 2]; };
        const double TotalMs = Median(Total), PrepareMs = Median(Preparation);
        std::cout << Count << ',' << Order << ',' << DebugEnabled << std::fixed << std::setprecision(6)
                  << ',' << TotalMs << ',' << PrepareMs << ',' << TotalMs - PrepareMs << ',' << Median(Copies)
                  << ',' << Counters.events[HTN_GENERATED_STRUCTURAL_FACT_ROW_TESTED]
                  << ',' << Counters.events[HTN_GENERATED_STRUCTURAL_FACT_CHOICE_RETRY]
                  << ',' << Nodes << ',' << ExecutionAllocations.Allocations << ',' << ExecutionAllocations.Bytes
                  << ',' << CopyAllocations.Allocations << ',' << CopyAllocations.Bytes
                  << ',' << EventNs / 1e6 << ',' << CaptureNs / 1e6 << ',' << SearchNs / 1e6
                  << ',' << Children << ',' << Captures << ',' << Slots << ',' << Atoms << ',' << Lists << ',' << Digest << std::endl;
        Definition->destroy_execution_storage(Context.execution_storage);
        ::operator delete(Context.execution_storage);
    }
    return 0;
#endif
}
