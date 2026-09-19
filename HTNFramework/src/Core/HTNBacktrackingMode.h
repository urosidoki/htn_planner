// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

/*
 * Runtime backtracking policy for one HTN decomposition.
 *
 * This is intentionally independent from HTNGeneratedBacktrackingPolicy. The generated
 * backtracking policy selects how generated planners store continuation/snapshot state;
 * HTNBacktrackingMode selects which semantic forms of backtracking are allowed at runtime.
 * Changing this mode never requires regenerating a planner.
 */
typedef enum HTNBacktrackingMode
{
    /*
     * Disables all optional backtracking.
     *
     * Facts and axioms keep only their first solution. A method may still continue to the
     * next branch when a branch's own preconditions fail, but it does not reconsider a
     * later branch after a selected branch's child subtree has failed.
     */
    HTN_BACKTRACKING_NONE = 0,

    /*
     * Enables alternative solutions produced by facts and axioms.
     *
     * When a later condition or task invalidates the current binding, the planner may
     * restore the corresponding choice point and try another fact/axiom solution.
     */
    HTN_BACKTRACKING_FACTS_AND_AXIOMS = 1 << 0,

    /*
     * Enables hierarchical branch backtracking.
     *
     * When a selected branch's child subtree fails, the method may restore its pre-branch
     * state and try the next branch. Ordinary branch selection after a branch-precondition
     * failure is not controlled by this flag and remains available in every mode.
     */
    HTN_BACKTRACKING_BRANCHES = 1 << 1,

    /* Enables every supported form of runtime backtracking. This is the default mode. */
    HTN_BACKTRACKING_ALL =
        HTN_BACKTRACKING_FACTS_AND_AXIOMS |
        HTN_BACKTRACKING_BRANCHES
} HTNBacktrackingMode;
