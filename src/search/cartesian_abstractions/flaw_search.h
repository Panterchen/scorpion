#ifndef CARTESIAN_ABSTRACTIONS_FLAW_SEARCH_H
#define CARTESIAN_ABSTRACTIONS_FLAW_SEARCH_H

#include "flaw.h"
#include "split_selector.h"
#include "types.h"

// Needed for SearchStatus enum.
#include "../search_algorithm.h"

#include "../utils/logging.h"
#include "../utils/timer.h"

#include <stack>

#include "regression_strategy.h"
#include "extension_strategy.h"
#include "utils.h"

namespace utils {
class CountdownTimer;
class LogProxy;
class RandomNumberGenerator;
}

namespace cartesian_abstractions {
class Abstraction;
class ShortestPaths;
class ExtensionStrategy;
class VariableDependencies;

// Variants from ICAPS 2022 paper (in order): FIRST, MIN_H, MAX_H, MIN_H,
// BATCH_MIN_H. See bottom of .cc file for documentation.
enum class PickFlawedAbstractState {
    FIRST,
    FIRST_ON_SHORTEST_PATH,
    RANDOM,
    MIN_H,
    MAX_H,
    BATCH_MIN_H
};

// Reasons why no split is returned, to better differentiate termination condition
// in CEGAR::CEGAR() instead of interpreting nullptr as SOLVED.
enum class StopReason {
    NONE,
    SOLVED,
    TIMEOUT,
    MEMORY_LIMIT,
    REFINEMENT_STALLED,
};

struct FactPairHash {
    size_t operator()(FactPair fact) const {
        utils::HashState hash_state;
        hash_state.feed(fact.var);
        hash_state.feed(fact.value);
        return hash_state.get_hash64();
    }
};

using CompactFactMap = phmap::flat_hash_map<FactPair, int, FactPairHash>;

class FlawSearch {
    TaskProxy task_proxy;
    const std::vector<int> domain_sizes;
    const Abstraction &abstraction;
    const ShortestPaths &shortest_paths;
    const SplitSelector split_selector;
    utils::RandomNumberGenerator &rng;
    const PickFlawedAbstractState pick_flawed_abstract_state;
    const std::shared_ptr<ExtensionStrategy> extension_strategy;
    const std::unique_ptr<RegressionStrategyInstance> regression_strategy_instance;
    const std::shared_ptr<const VariableDependencies> variable_dependencies;
    const int max_concrete_states_per_abstract_state;
    const int max_state_expansions;
    mutable utils::LogProxy log;
    mutable utils::LogProxy silent_log; // For concrete search space.

    static const int MISSING = -1;

    // Search data
    std::stack<StateID> open_list;
    std::unique_ptr<StateRegistry> state_registry;
    std::unique_ptr<SearchSpace> search_space;
    std::unique_ptr<PerStateInformation<int>> cached_abstract_state_ids;

    // Flaw data
    FlawedState last_refined_flawed_state;
    Cost best_flaw_h;
    FlawedStates flawed_states;

    // Statistics
    int num_searches;
    int num_overall_expanded_concrete_states;
    int max_expanded_concrete_states;
    utils::Timer flaw_search_timer;
    utils::Timer compute_splits_timer;
    utils::Timer pick_split_timer;

    // Termination Condition
    StopReason last_stop_reason = StopReason::NONE;

    // Rescue Splits
    int consecutive_rescue_splits = 0;
    static const int MAX_CONSECUTIVE_RESCUE_SPLITS = 20;

    int get_abstract_state_id(const State &state) const;
    Cost get_h_value(int abstract_state_id) const;
    void add_flaw(int abs_id, const State &state);
    OptimalTransitions get_f_optimal_transitions(int abstract_state_id) const;

    void initialize();
    SearchStatus step();
    SearchStatus search_for_flaws(const utils::CountdownTimer &cegar_timer);

    bool add_candidates_for_basic_targets(
        const std::vector<AxiomSplitTarget> &targets, int count,
        const AbstractState &abs_state, std::vector<std::vector<Split>> &splits,
        const AbstractState &target_abs_state) const;

    void add_axiom_fallback_candidates(
        const AbstractState &abs_state, const AbstractState &target_abs_state,
        int var, int bad_value, int count,
        std::vector<std::vector<Split>> &splits) const;

    void get_deviation_splits(
        const AbstractState &abs_state, const CompactFactMap &fact_count,
        const AbstractState &target_abs_state, int op_id,
        std::vector<std::vector<Split>> &splits) const;

    std::unique_ptr<Split> create_split(
        const std::vector<StateID> &state_ids, int abstract_state_id);

    FlawedState get_next_usable_flawed_state(
        std::unordered_set<int> unsplittable_abstract_states);

    FlawedState get_flawed_state_with_min_h();
    std::unique_ptr<Split> get_single_split(
        const utils::CountdownTimer &cegar_timer);
    std::unique_ptr<Split> get_min_h_batch_split(
        const utils::CountdownTimer &cegar_timer);

public:
    FlawSearch(
        const std::shared_ptr<AbstractTask> &task,
        const Abstraction &abstraction, const ShortestPaths &shortest_paths,
        utils::RandomNumberGenerator &rng,
        PickFlawedAbstractState pick_flawed_abstract_state,
        PickSplit pick_split, PickSplit tiebreak_split,
        int max_concrete_states_per_abstract_state, int max_state_expansions,
        const std::shared_ptr<ExtensionStrategy> &extension_strategy,
        const std::shared_ptr<RegressionStrategy> &regression_strategy,
        const std::shared_ptr<const VariableDependencies> &variable_dependencies,
        const utils::LogProxy &log);

    std::unique_ptr<Split> get_split(const utils::CountdownTimer &cegar_timer);
    std::unique_ptr<Split> get_split_legacy(const Solution &solution);

    StopReason get_last_stop_reason() const { return last_stop_reason; }

    void print_statistics() const;
};
}

#endif
