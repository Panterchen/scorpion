#include "flaw_search.h"

#include "abstract_state.h"
#include "abstraction.h"
#include "flaw.h"
#include "shortest_paths.h"
#include "split_selector.h"
#include "regression_strategy.h"
#include "extension_strategy.h"
#include "transition_system.h"
#include "utils.h"
#include "../state_registry.h"


#include "../plugins/plugin.h"
#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../task_proxy.h"
#include "../utils/countdown_timer.h"
#include "../utils/memory.h"
#include "../utils/rng.h"

using namespace std;

namespace cartesian_abstractions {
int FlawSearch::get_abstract_state_id(const State &state) const {
    return abstraction.get_abstract_state_id(state);
}

Cost FlawSearch::get_h_value(int abstract_state_id) const {
    return shortest_paths.get_64bit_goal_distance(abstract_state_id);
}

OptimalTransitions FlawSearch::get_f_optimal_transitions(
    int abstract_state_id) const {
    return shortest_paths.get_optimal_transitions(
        abstraction, abstract_state_id);
}

void FlawSearch::add_flaw(int abs_id, const State &state) {
    assert(abstraction.get_state(abs_id).includes(state));

    if (log.is_at_least_debug()) {
        log << "Add flaw abs:" << abs_id << " conc:" << state.get_id() << endl;
    }

    // We limit the number of concrete states we consider per abstract state.
    // For a new abstract state (with a potentially unseen h-value),
    // this if-statement is never true.
    if (flawed_states.num_concrete_states(abs_id) >=
        max_concrete_states_per_abstract_state) {
        return;
    }

    Cost h = get_h_value(abs_id);
    if (pick_flawed_abstract_state == PickFlawedAbstractState::MIN_H) {
        if (best_flaw_h > h) {
            flawed_states.clear();
        }
        if (best_flaw_h >= h) {
            best_flaw_h = h;
            flawed_states.add_state(abs_id, state, h);
        }
    } else if (pick_flawed_abstract_state == PickFlawedAbstractState::MAX_H) {
        if (best_flaw_h < h) {
            flawed_states.clear();
        }
        if (best_flaw_h <= h) {
            best_flaw_h = h;
            flawed_states.add_state(abs_id, state, h);
        }
    } else {
        assert(
            pick_flawed_abstract_state == PickFlawedAbstractState::RANDOM ||
            pick_flawed_abstract_state == PickFlawedAbstractState::FIRST ||
            pick_flawed_abstract_state == PickFlawedAbstractState::BATCH_MIN_H);
        flawed_states.add_state(abs_id, state, h);
    }
}

void FlawSearch::initialize() {
    ++num_searches;
    last_refined_flawed_state = FlawedState::no_state;
    best_flaw_h = (pick_flawed_abstract_state == PickFlawedAbstractState::MAX_H)
                      ? 0
                      : INF_COSTS;
    assert(open_list.empty());
    assert(flawed_states.empty());
    state_registry = make_unique<StateRegistry>(task_proxy);
    search_space = make_unique<SearchSpace>(*state_registry, silent_log);
    const State &initial_state = state_registry->get_initial_state();
    SearchNode node = search_space->get_node(initial_state);
    node.open_initial();
    cached_abstract_state_ids = make_unique<PerStateInformation<int>>(MISSING);
    (*cached_abstract_state_ids)[initial_state] =
        abstraction.get_initial_state().get_id();
    open_list.push(initial_state.get_id());
}

SearchStatus FlawSearch::step() {
    if (open_list.empty()) {
        // Completely explored f-optimal state space.
        return FAILED;
    }
    StateID id = open_list.top();
    open_list.pop();
    State s = state_registry->lookup_state(id);
    SearchNode node = search_space->get_node(s);
    assert(!node.is_closed());
    node.close();
    assert(!node.is_dead_end());
    ++num_overall_expanded_concrete_states;

    if (task_properties::is_goal_state(task_proxy, s) &&
        pick_flawed_abstract_state != PickFlawedAbstractState::MAX_H) {
        return SOLVED;
    }

    bool found_flaw = false;
    int abs_id = (*cached_abstract_state_ids)[s];
    assert(abs_id == get_abstract_state_id(s));

    // Check for each transition if the operator is applicable or if there is a
    // deviation.
    for (auto &pair : get_f_optimal_transitions(abs_id)) {
        if (!utils::extra_memory_padding_is_reserved()) {
            return TIMEOUT;
        }

        int op_id = pair.first;
        const vector<int> &targets = pair.second;

        OperatorProxy op = task_proxy.get_operators()[op_id];

        if (!task_properties::is_applicable(op, s)) {
            // Applicability flaw
            if (!found_flaw) {
                add_flaw(abs_id, s);
                found_flaw = true;
            }
            if (pick_flawed_abstract_state == PickFlawedAbstractState::FIRST) {
                return FAILED;
            }
            continue;
        }

        State succ_state = state_registry->get_successor_state(s, op);
        SearchNode succ_node = search_space->get_node(succ_state);
        assert(!succ_node.is_dead_end());

        for (int target : targets) {
            if (!abstraction.get_state(target).includes(succ_state)) {
                // Deviation flaw
                if (!found_flaw) {
                    add_flaw(abs_id, s);
                    found_flaw = true;
                }
                if (pick_flawed_abstract_state ==
                    PickFlawedAbstractState::FIRST) {
                    return FAILED;
                }
            } else if (succ_node.is_new()) {
                // No flaw
                (*cached_abstract_state_ids)[succ_state] = target;
                succ_node.open_new_node(node, op, op.get_cost());
                open_list.push(succ_state.get_id());

                if (pick_flawed_abstract_state ==
                    PickFlawedAbstractState::FIRST) {
                    // Only consider one successor.
                    break;
                }
            }
        }
        if (pick_flawed_abstract_state == PickFlawedAbstractState::FIRST) {
            // Only consider one successor as in the legacy variant.
            break;
        }
    }
    return IN_PROGRESS;
}

static void add_split(vector<vector<Split>> &splits, Split &&new_split) {
    vector<Split> &var_splits = splits[new_split.var_id];
    bool is_duplicate = false;
    for (auto &old_split : var_splits) {
        if (old_split == new_split) {
            is_duplicate = true;
            old_split.count += new_split.count;
            break;
        }
    }
    if (!is_duplicate) {
        var_splits.push_back(move(new_split));
    }
}

static vector<int> get_unaffected_variables(
    const OperatorProxy &op, int num_variables) {
    vector<bool> affected(num_variables);
    for (EffectProxy effect : op.get_effects()) {
        FactPair fact = effect.get_fact().get_pair();
        affected[fact.var] = true;
    }
    for (FactProxy precondition : op.get_preconditions()) {
        FactPair fact = precondition.get_pair();
        affected[fact.var] = true;
    }
    vector<int> unaffected_vars;
    unaffected_vars.reserve(num_variables);
    for (int var = 0; var < num_variables; ++var) {
        if (!affected[var]) {
            unaffected_vars.push_back(var);
        }
    }
    return unaffected_vars;
}

struct FactPairHash {
    size_t operator()(FactPair fact) const {
        utils::HashState hash_state;
        hash_state.feed(fact.var);
        hash_state.feed(fact.value);
        return hash_state.get_hash64();
    }
};

using CompactFactMap = phmap::flat_hash_map<FactPair, int, FactPairHash>;

static void get_deviation_splits(
    const AbstractState &abs_state, const CompactFactMap &fact_count,
    const AbstractState &target_abs_state, const vector<int> &domain_sizes,
    vector<vector<Split>> &splits, TaskProxy task, int op_id,
    RegressionStrategyInstance &regression_strategy_instance) {
    /*
      For each fact in the concrete state that is not contained in the
      target abstract state, loop over all values in the domain of the
      corresponding variable. The values that are in both the current and
      the target abstract state are the "wanted" ones, i.e., the ones that
      we want to split off. This test can be specialized for applicability and
      deviation flaws. Here, we consider deviation flaws.

      Let the desired abstract transition be (a, o, t) and the deviation be
      (a, o, b). We distinguish three cases for each basic variable v:

      pre(o)[v] defined: no split possible since o is applicable in s.
      pre(o)[v] undefined, eff(o)[v] defined: no split possible since regression
      adds whole domain.
      pre(o)[v] and eff(o)[v] undefined: if s[v] \notin t[v],
      wanted = intersect(a[v], b[v]).

      For derived variables v we distinguish two cases (in naive regression):
      pre(o)[v] defined: no split possible since o is applicable in s.
      else: regr(t, o)[v] is approximated as the entire domain of v, so
      wanted = intersect(a[v], domain(v)) = a[v]
    */
    for (auto &[fact, count] : fact_count) {
        assert(count > 0);
        int var = fact.var;
        if (!target_abs_state.contains(var, fact.value)) {
            if (!task.get_variables()[var].is_derived()) { // non-derived case
                // Note: we could precompute the "wanted" vector, but not the split.
                vector<int> wanted;
                for (int value = 0; value < domain_sizes[var]; ++value) {
                    if (abs_state.contains(var, value) &&
                        target_abs_state.contains(var, value)) {
                        wanted.push_back(value);
                        }
                }
                assert(!wanted.empty());
                add_split(
                    splits,
                    Split(
                        abs_state.get_id(), var, fact.value, move(wanted), count));
            } else { // derived variable
                bool found_split_on_var = false;
                if (abs_state.get_cartesian_set().count(var) > 1) {
                    vector<int> wanted = regression_strategy_instance.get_wanted_values(abs_state, target_abs_state, var, op_id);
                    if (wanted.empty()) {
                        /* With composed regression, the extended regression of t under op can
                         * determine a derived variable value that is incompatible with a[v],
                         * giving an empty intersection. This means a cannot reach t via op
                         * for this derived variable — skip this split candidate.
                         * (With naive regression this cannot happen since wanted = a[v].)
                         * It should never happen for basic variables.
                         */
                        OperatorProxy op_proxy = task.get_operators()[op_id];
                        std::cout << "Operator ID: " << op_id << " Name: " << op_proxy.get_name() << "\n    Preconditions: " << std::endl;
                        for (auto pre : op_proxy.get_preconditions()) {
                            std::cout << "        Variable: " << pre.get_var_id() << ", Value: " << pre.get_value() << std::endl;
                        }
                        std::cout << "    Effects:" << std::endl;
                        for (auto eff : op_proxy.get_effects()) {
                            std::cout << "        Variable: " << eff.get_fact().get_var_id() << ", Value: " << eff.get_fact().get_value() << std::endl;
                        }
                        assert(task.get_variables()[var].is_derived());
                        continue;
                    }
                    assert(!wanted.empty());
                    /* For derived variables, it can happen that the wanted vector is not
                     * empty, but that it contains a single value and that this is the
                     * only value in the abstract state 'a' we want to split. So we skip
                     * derived variables with non-empty wanted vectors where |a[v]| = 1
                    */
                    if (wanted.size() < static_cast<size_t>(abs_state.get_cartesian_set().count(var))) {
                        // remove degenerate splits on derived variables
                        add_split(splits, Split(abs_state.get_id(), var, fact.value,
                                                move(wanted), count));
                        found_split_on_var = true;
                    }
                }
                if (!found_split_on_var) {
                    // TODO: calculate split on basic variables that var depends on
                    // TODO: pass in the variable dependencies for var
                    VariableDependencies variable_dependencies(task);
                    vector<AxiomRule> rules = variable_dependencies.get_rules(var, fact.value);
                    vector<int> dep_vars;

                }
            }
        }
    }
}

// TODO: Add comment about split considering multiple transitions.
unique_ptr<Split> FlawSearch::create_split(
    const vector<StateID> &state_ids, int abstract_state_id) {
    compute_splits_timer.resume();
    const AbstractState &abstract_state =
        abstraction.get_state(abstract_state_id);

    if (log.is_at_least_debug()) {
        log << endl;
        log << "Create split for abstract state " << abstract_state_id
            << " and " << state_ids.size() << " concrete states." << endl;
	}


    vector<vector<Split>> splits(task_proxy.get_variables().size());
    for (auto &pair : get_f_optimal_transitions(abstract_state_id)) {
        int op_id = pair.first;
        const vector<int> &targets = pair.second;
        OperatorProxy op = task_proxy.get_operators()[op_id];

        vector<State> states;
        states.reserve(state_ids.size());
        for (const StateID &state_id : state_ids) {
            states.push_back(state_registry->lookup_state(state_id));
            assert(abstract_state.includes(states.back()));
        }

        vector<bool> applicable(states.size(), true);
        for (FactPair fact : abstraction.get_preconditions(op_id)) {
            vector<int> state_value_count(domain_sizes[fact.var], 0);
            for (size_t i = 0; i < states.size(); ++i) {
                const State &state = states[i];
                int state_value = state[fact.var].get_value();
                if (state_value != fact.value) {
                    // Applicability flaw
                    applicable[i] = false;
                    ++state_value_count[state_value];
                }
            }
            for (int value = 0; value < domain_sizes[fact.var]; ++value) {
                if (state_value_count[value] > 0) {
                    assert(value != fact.value);
                    //cout << "WANTED fact value " << fact.value << endl;
                    add_split(
                        splits, Split(
                                    abstract_state_id, fact.var, value,
                                    {fact.value}, state_value_count[value]));
                }
            }
        }

        int num_vars = domain_sizes.size();
        vector<int> unaffected_variables =
            get_unaffected_variables(op, num_vars);

        phmap::flat_hash_map<int, CompactFactMap> fact_count_by_target;
        for (size_t i = 0; i < states.size(); ++i) {
            if (!applicable[i]) {
                continue;
            }
            const State &state = states[i];
            assert(task_properties::is_applicable(op, state));
            State succ_state = state_registry->get_successor_state(state, op);
            bool target_hit = false;
            for (int target : targets) {
                if (!utils::extra_memory_padding_is_reserved()) {
                    return nullptr;
                }

                // At most one of the f-optimal targets can include the
                // successor state.
                if (!target_hit &&
                    abstraction.get_state(target).includes(succ_state)) {
                    // No flaw
                    target_hit = true;
                } else {
                    // Deviation flaw
                    assert(target != get_abstract_state_id(succ_state));
                    auto pos = fact_count_by_target.find(target);
                    if (pos == fact_count_by_target.end()) {
                        pos = fact_count_by_target.emplace_hint(
                            pos, target, CompactFactMap{});
                    }
                    CompactFactMap &fact_count = pos->second;
                    for (int var : unaffected_variables) {
                        int state_value = state[var].get_value();
                        ++fact_count[FactPair(var, state_value)];
                    }
                }
            }
        }

        for (const auto &[target, fact_count] : fact_count_by_target) {
            regression_strategy_instance->prepare(
                abstraction.get_state(target).get_cartesian_set(), op_id);
            get_deviation_splits(
                abstract_state, fact_count, abstraction.get_state(target),
                domain_sizes, splits, task_proxy,
                op_id, *regression_strategy_instance);
        }
    }

    int num_splits = 0;
    for (auto &var_splits : splits) {
        num_splits += var_splits.size();
    }
    if (log.is_at_least_debug()) {
        log << "Unique splits: " << num_splits << endl;
    }
    compute_splits_timer.stop();

    if (num_splits == 0) {
        
        return  nullptr;
    }

    pick_split_timer.resume();
    Split split = split_selector.pick_split(abstract_state, move(splits), rng);
    pick_split_timer.stop();
    return make_unique<Split>(move(split));
}

FlawedState FlawSearch::get_next_usable_flawed_state(
    std::unordered_set<int> unsplittable_abstract_states) {
    FlawedState flawed_state = get_flawed_state_with_min_h();
    while (flawed_state != FlawedState::no_state  &&
        unsplittable_abstract_states.count(flawed_state.abs_id)) {
        flawed_state = get_flawed_state_with_min_h();
    }
    return flawed_state;
}

SearchStatus FlawSearch::search_for_flaws(
    const utils::CountdownTimer &cegar_timer) {
    flaw_search_timer.resume();
    if (log.is_at_least_debug()) {
        log << "Search for flaws" << endl;
    }
    initialize();
    int num_expansions_in_prev_searches = num_overall_expanded_concrete_states;
    SearchStatus search_status = IN_PROGRESS;
    while (search_status == IN_PROGRESS) {
        if (cegar_timer.is_expired()) {
            search_status = TIMEOUT;
            break;
        }

        int current_num_expanded_states = num_overall_expanded_concrete_states -
                                          num_expansions_in_prev_searches;
        // To remain complete, only take the expansions limit into account once
        // at least one flaw has been found.
        if (current_num_expanded_states >= max_state_expansions &&
            flawed_states.num_abstract_states() > 0) {
            log << "Expansion limit reached with flaws." << endl;
            search_status = FAILED;
            break;
        }
        search_status = step();
    }
    // Clear open list.
    stack<StateID>().swap(open_list);

    int current_num_expanded_states =
        num_overall_expanded_concrete_states - num_expansions_in_prev_searches;
    max_expanded_concrete_states =
        max(max_expanded_concrete_states, current_num_expanded_states);
    if (log.is_at_least_debug()) {
        log << "Flaw search expanded " << current_num_expanded_states
            << " states." << endl;
    }

    /* For MAX_H, we don't return SOLVED when hitting a goal state. So if MAX_H
       fails to find a single flaw, we adapt the search status here. */
    if (pick_flawed_abstract_state == PickFlawedAbstractState::MAX_H &&
        search_status == FAILED && flawed_states.num_abstract_states() == 0) {
        search_status = SOLVED;
    }

    flaw_search_timer.stop();
    return search_status;
}

unique_ptr<Split> FlawSearch::get_single_split(
    const utils::CountdownTimer &cegar_timer) {
    auto search_status = search_for_flaws(cegar_timer);

    if (search_status == TIMEOUT)
        return nullptr;

    if (search_status == FAILED) {
        assert(!flawed_states.empty());

        FlawedState flawed_state =
            flawed_states.pop_random_flawed_state_and_clear(rng);
        StateID state_id = *rng.choose(flawed_state.concrete_states);

        if (log.is_at_least_debug()) {
            vector<OperatorID> trace = search_space->trace_path(
                task_proxy,
                successor_generator::g_successor_generators[task_proxy],
                state_registry->lookup_state(state_id));
            vector<string> operator_names;
            operator_names.reserve(trace.size());
            for (OperatorID op_id : trace) {
                operator_names.push_back(
                    task_proxy.get_operators()[op_id].get_name());
            }
            log << "Path (without last operator): " << operator_names << endl;
        }

        return create_split({state_id}, flawed_state.abs_id);
    }
    assert(search_status == SOLVED);
    return nullptr;
}

FlawedState FlawSearch::get_flawed_state_with_min_h() {
    while (!flawed_states.empty()) {
        FlawedState flawed_state = flawed_states.pop_flawed_state_with_min_h();
        Cost old_h = flawed_state.h;
        int abs_id = flawed_state.abs_id;
        assert(get_h_value(abs_id) >= old_h);
        if (get_h_value(abs_id) == old_h) {
            if (log.is_at_least_debug()) {
                log << "Reuse flawed state: " << abs_id << endl;
            }
            return flawed_state;
        } else {
            if (log.is_at_least_debug()) {
                log << "Ignore flawed state with increased f value: " << abs_id
                    << endl;
            }
        }
    }
    // The f value increased for all states.
    return FlawedState::no_state;
}

unique_ptr<Split> FlawSearch::get_min_h_batch_split(
    const utils::CountdownTimer &cegar_timer) {
    assert(pick_flawed_abstract_state == PickFlawedAbstractState::BATCH_MIN_H);

    // Recycle flaws of the last refined abstract state: re-evaluate all
    // concrete states that were in the last refined abstract state and
    // add them back to flawed_states if their h-value is unchanged.
    if (last_refined_flawed_state != FlawedState::no_state) {
        Cost old_h = last_refined_flawed_state.h;
        for (const StateID &state_id :
             last_refined_flawed_state.concrete_states) {
            State state = state_registry->lookup_state(state_id);
            assert(!task_properties::is_goal_state(task_proxy, state));
            int abs_id = get_abstract_state_id(state);
            if (get_h_value(abs_id) == old_h) {
                add_flaw(abs_id, state);
            }
        }
    }

    /*
    // TODO: remove this part if the alternate solution below performs stable
    if (task_properties::has_axioms(task_proxy)) {
        // Tracks abstract states for which create_split returned nullptr in
        // this round (i.e. no valid split could be found despite a flaw existing).
        // This happens with axioms when all split candidates are derived variables
        // whose wanted vectors are degenerate, i.e. the wanted vector equals the
        // current variable domain of the abstract state (naive regression).
        // For tasks without derived variables, this set always stays empty.
        // The set is reset after each call to search_for_flaws since the
        // flawed_states collection is freshly populated at that point.
        std::unordered_set<int> unsplittable_abstract_states;

        while (true) {
            // Try to get the next flawed abstract state with minimum h-value
            // from the current collection without running a new flaw search.
            FlawedState flawed_state = get_flawed_state_with_min_h();

            if (flawed_state == FlawedState::no_state) {
                // If we exhausted flawed_states and already encountered
                // unsplittable states this round, running search_for_flaws again
                // would find the same flaws since nothing has been refined —
                // this would cause an infinite loop. Return nullptr and let
                // the caller trigger a new refinement cycle.
                if (!unsplittable_abstract_states.empty()) {
                    last_refined_flawed_state = FlawedState::no_state;
                    return nullptr;
                }

                // flawed_states is empty and no unsplittable states were seen
                // this round — run a fresh flaw search to find new flaws.
                if (log.is_at_least_debug()) {
                    log << "No flawed state with min h found, search for flaws again." << endl;
                }
                SearchStatus search_status = search_for_flaws(cegar_timer);

                if (search_status == SearchStatus::TIMEOUT)
                    return nullptr;

                if (search_status == SearchStatus::SOLVED)
                    return nullptr;

                // Flaw search found flaws (FAILED status). Clear the unsplittable
                // set since we are starting a new round with a freshly populated
                // flawed_states — previously unsplittable states may now be
                // splittable after refinements elsewhere.
                unsplittable_abstract_states.clear();

                // Try to get a flawed state from the freshly populated set.
                // Can still return no_state if all found states have stale
                // h-values (get_flawed_state_with_min_h discards states whose
                // h-value has increased). Return nullptr and let the caller
                // handle the next cycle.
                flawed_state = get_flawed_state_with_min_h();
                if (flawed_state == FlawedState::no_state)
                    return nullptr;
            }

            // Skip abstract states that already failed to produce a split in
            // this round — they will not improve without a refinement step.
            // Re-pop from flawed_states by continuing the loop.
            if (unsplittable_abstract_states.count(flawed_state.abs_id)) {
                continue;
            }

            if (log.is_at_least_debug()) {
                log << "Use flawed state: " << flawed_state << endl;
            }

            unique_ptr<Split> split =
                create_split(flawed_state.concrete_states, flawed_state.abs_id);

            if (!utils::extra_memory_padding_is_reserved()) {
                return nullptr;
            }

            if (split) {
                // Valid split found — store the refined state for recycling
                // on the next call and return the split to the caller.
                last_refined_flawed_state = move(flawed_state);
                return split;
            } else {
                // create_split returned nullptr — no valid split could be found
                // for this abstract state despite a flaw existing. Mark it as
                // unsplittable for this round and try the next flawed state.
                unsplittable_abstract_states.insert(flawed_state.abs_id);
                last_refined_flawed_state = FlawedState::no_state;
            }
        }
    } else {
        FlawedState flawed_state = get_flawed_state_with_min_h();
        SearchStatus search_status = SearchStatus::FAILED;
        if (flawed_state == FlawedState::no_state) {
            search_status = search_for_flaws(cegar_timer);
            if (search_status == SearchStatus::FAILED) {
                flawed_state = get_flawed_state_with_min_h();
            }
        }

        if (search_status == SearchStatus::TIMEOUT)
            return nullptr;

        if (search_status == SearchStatus::FAILED) {
            // There are flaws to refine.
            assert(flawed_state != FlawedState::no_state);

            if (log.is_at_least_debug()) {
                log << "Use flawed state: " << flawed_state << endl;
            }

            unique_ptr<Split> split;
            split = create_split(flawed_state.concrete_states, flawed_state.abs_id);

            if (!utils::extra_memory_padding_is_reserved()) {
                return nullptr;
            }

            if (split) {
                last_refined_flawed_state = move(flawed_state);
            } else {
                last_refined_flawed_state = FlawedState::no_state;
                // We selected an abstract state without any flaws, so we try again.
                return get_min_h_batch_split(cegar_timer);
            }

            return split;
        }

        assert(search_status == SearchStatus::SOLVED);
        return nullptr;
    } */
    // TODO: test this implementation
    // Tracks abstract states for which create_split returned nullptr in
    // this round (i.e. no valid split could be found despite a flaw existing).
    // This happens with axioms when all split candidates are derived variables
    // whose wanted vectors are degenerate, i.e. the wanted vector equals the
    // current variable domain of the abstract state (naive regression).
    // For tasks without derived variables, this set always stays empty.
    // The set is reset after each call to search_for_flaws since the
    // flawed_states collection is freshly populated at that point.
    std::unordered_set<int> unsplittable_abs_states;
    while (true) {
        FlawedState flawed_state = get_next_usable_flawed_state(unsplittable_abs_states);
        if (flawed_state == FlawedState::no_state) {
            // Known pool (aside from already-unsplittable states) is
            // exhausted. Run a fresh flaw search — it may explore further
            // into the concrete state space and uncover new flaws.
            if (log.is_at_least_debug()) {
                log << "No usable flawed state found, search for flaws again." << endl;
            }
            SearchStatus search_status = search_for_flaws(cegar_timer);

            if (search_status == SearchStatus::TIMEOUT)
                return nullptr;

            if (search_status == SearchStatus::SOLVED)
                return nullptr;

            // FAILED: search added new flaws. But those new flaws might
            // themselves all belong to abstract states we already know are
            // unsplittable — check explicitly instead of assuming progress.
            flawed_state = get_next_usable_flawed_state(unsplittable_abs_states);

            if (flawed_state == FlawedState::no_state) {
                // A fresh search found nothing beyond already-known-unsplittable
                // states — no further progress possible this round, stop.
                last_refined_flawed_state = FlawedState::no_state;
                // std::cout<< "Kein valider Split für aktuelle FlawedStates gefunden"<<std::endl;
                return nullptr;
            }
        }
        if (log.is_at_least_debug()) {
            log << "Use flawed state: " << flawed_state << endl;
        }

        // std::cout<< "Angekommen!!!"<<std::endl;
        unique_ptr<Split> split =
            create_split(flawed_state.concrete_states, flawed_state.abs_id);

        if (!utils::extra_memory_padding_is_reserved()) {
            return nullptr;
        }

        if (split) {
            // Valid split found — store the refined state for recycling
            // on the next call and return the split to the caller.
            last_refined_flawed_state = move(flawed_state);
            // std::cout << "Split berechnet!!!" << std::endl;
            return split;
        } else {
            // create_split returned nullptr — no valid split could be
            // found for this abstract state despite a flaw existing. Mark
            // it as unsplittable for this round and try the next flawed
            // state (looping back to the top).
            // TODO: hier nicht in die unsplittable states packen, sondern
            // split anders berechnen... (Annahme aktuell: leerer wanted vector)
            // -> problem liegt in derived variable..., statt auf derived var
            // selbst zu splitten, auf einer der basic vars splitten von denen
            // derived var abhängt
            unsplittable_abs_states.insert(flawed_state.abs_id);
            last_refined_flawed_state = FlawedState::no_state;
        }
    }
}



FlawSearch::FlawSearch(
    const shared_ptr<AbstractTask> &task, const Abstraction &abstraction,
    const ShortestPaths &shortest_paths, utils::RandomNumberGenerator &rng,
    PickFlawedAbstractState pick_flawed_abstract_state, PickSplit pick_split,
    PickSplit tiebreak_split, int max_concrete_states_per_abstract_state,
    int max_state_expansions,
    const shared_ptr<ExtensionStrategy> &extension_strategy,
    const shared_ptr<RegressionStrategy> &regression_strategy,
    const shared_ptr<const VariableDependencies> &variable_dependencies,
    const utils::LogProxy &log)
    : task_proxy(*task),
      domain_sizes(get_domain_sizes(task_proxy)),
      abstraction(abstraction),
      shortest_paths(shortest_paths),
      split_selector(task, pick_split, tiebreak_split, log.is_at_least_debug()),
      rng(rng),
      pick_flawed_abstract_state(pick_flawed_abstract_state),
      regression_strategy_instance(regression_strategy->create(task_proxy,
          extension_strategy)),
      variable_dependencies(variable_dependencies),
      max_concrete_states_per_abstract_state(
          max_concrete_states_per_abstract_state),
      max_state_expansions(max_state_expansions),
      log(log),
      silent_log(utils::get_silent_log()),
      last_refined_flawed_state(FlawedState::no_state),
      best_flaw_h(
          (pick_flawed_abstract_state == PickFlawedAbstractState::MAX_H) ? 0
                                                                         : INF),
      num_searches(0),
      num_overall_expanded_concrete_states(0),
      max_expanded_concrete_states(0),
      flaw_search_timer(false),
      compute_splits_timer(false),
      pick_split_timer(false) {
}

unique_ptr<Split> FlawSearch::get_split(
    const utils::CountdownTimer &cegar_timer) {
    unique_ptr<Split> split;

    switch (pick_flawed_abstract_state) {
    case PickFlawedAbstractState::FIRST:
    case PickFlawedAbstractState::RANDOM:
    case PickFlawedAbstractState::MIN_H:
    case PickFlawedAbstractState::MAX_H:
        split = get_single_split(cegar_timer);
        break;
    case PickFlawedAbstractState::BATCH_MIN_H:
        split = get_min_h_batch_split(cegar_timer);
        break;
    default:
        log << "Invalid pick flaw strategy: "
            << static_cast<int>(pick_flawed_abstract_state) << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }

    if (split) {
        assert(
            (pick_flawed_abstract_state != PickFlawedAbstractState::MAX_H &&
             pick_flawed_abstract_state != PickFlawedAbstractState::MIN_H) ||
            best_flaw_h == get_h_value(split->abstract_state_id));
    }
    return split;
}

unique_ptr<Split> FlawSearch::get_split_legacy(const Solution &solution) {
    state_registry = make_unique<StateRegistry>(task_proxy);
    bool debug = log.is_at_least_debug();
    if (debug)
        log << "Check solution:" << endl;

    const AbstractState *abstract_state = &abstraction.get_initial_state();
    State concrete_state = state_registry->get_initial_state();
    assert(abstract_state->includes(concrete_state));

    if (debug)
        log << "  Initial abstract state: " << *abstract_state << endl;

    for (const Transition &step : solution) {
        OperatorProxy op = task_proxy.get_operators()[step.op_id];
        const AbstractState *next_abstract_state =
            &abstraction.get_state(step.target_id);
        if (task_properties::is_applicable(op, concrete_state)) {
            if (debug)
                log << "  Move to " << *next_abstract_state << " with "
                    << op.get_name() << endl;
            State next_concrete_state =
                state_registry->get_successor_state(concrete_state, op);
            if (!next_abstract_state->includes(next_concrete_state)) {
                if (debug)
                    log << "  Paths deviate." << endl;
                return create_split(
                    {concrete_state.get_id()}, abstract_state->get_id());
            }
            abstract_state = next_abstract_state;
            concrete_state = move(next_concrete_state);
        } else {
            if (debug)
                log << "  Operator not applicable: " << op.get_name() << endl;
            return create_split(
                {concrete_state.get_id()}, abstract_state->get_id());
        }
    }
    assert(abstraction.get_goals().count(abstract_state->get_id()));
    if (task_properties::is_goal_state(task_proxy, concrete_state)) {
        // We found a concrete solution.
        return nullptr;
    } else {
        if (debug)
            log << "  Goal test failed." << endl;
        return create_split(
            {concrete_state.get_id()}, abstract_state->get_id());
    }
}

void FlawSearch::print_statistics() const {
    int refinements = abstraction.get_num_states() - 1;
    int expansions = num_overall_expanded_concrete_states;
    log << "Flaw searches: " << num_searches << endl;
    log << "Expanded concrete states: " << expansions << endl;
    log << "Maximum expanded concrete states in single flaw search: "
        << max_expanded_concrete_states << endl;
    log << "Flaw search time: " << flaw_search_timer << endl;
    log << "Time for computing splits: " << compute_splits_timer << endl;
    log << "Time for selecting splits: " << pick_split_timer << endl;
    if (num_searches > 0) {
        log << "Average number of refinements per flaw search: "
            << refinements / static_cast<float>(num_searches) << endl;
        log << "Average number of expanded concrete states per flaw search: "
            << expansions / static_cast<float>(num_searches) << endl;
        log << "Average flaw search time: "
            << flaw_search_timer() / num_searches << endl;
    }
}

static plugins::TypedEnumPlugin<PickFlawedAbstractState> _enum_plugin({
    {"first",
     "Consider first encountered flawed abstract state and a random concrete state."},
    {"first_on_shortest_path",
     "Follow the arbitrary solution in the shortest path tree (no flaw search). "
     "Consider first encountered flawed abstract state and a random concrete state."},
    {"random",
     "Collect all flawed abstract states and then consider a random abstract state "
     "and a random concrete state."},
    {"min_h",
     "Collect all flawed abstract states and then consider a random abstract state "
     "with minimum h value and a random concrete state."},
    {"max_h",
     "Collect all flawed abstract states and then consider a random abstract state "
     "with maximum h value and a random concrete state."},
    {"batch_min_h",
     "Collect all flawed abstract states and iteratively refine them (by increasing "
     "h value). Only start a new flaw search once all remaining flawed abstract "
     "states are refined. For each abstract state consider all concrete states."},
});
}
