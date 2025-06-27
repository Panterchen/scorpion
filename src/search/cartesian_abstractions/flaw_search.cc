#include "flaw_search.h"

#include "abstraction.h"
#include "abstract_state.h"
#include "flaw.h"
#include "shortest_paths.h"
#include "split_selector.h"
#include "transition_system.h"
#include "utils.h"

#include "../plugins/plugin.h"
#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
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

OptimalTransitions FlawSearch::get_f_optimal_transitions(int abstract_state_id) const {
    return shortest_paths.get_optimal_transitions(abstraction, abstract_state_id);
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
        assert(pick_flawed_abstract_state == PickFlawedAbstractState::RANDOM
               || pick_flawed_abstract_state == PickFlawedAbstractState::FIRST
               || pick_flawed_abstract_state == PickFlawedAbstractState::BATCH_MIN_H);
        flawed_states.add_state(abs_id, state, h);
    }
}

void FlawSearch::initialize() {
    ++num_searches;
    last_refined_flawed_state = FlawedState::no_state;
    best_flaw_h = (pick_flawed_abstract_state == PickFlawedAbstractState::MAX_H) ? 0 : INF_COSTS;
    assert(open_list.empty());
    assert(flawed_states.empty());
    state_registry = make_unique<StateRegistry>(task_proxy);
    search_space = make_unique<SearchSpace>(*state_registry, silent_log);
    const State &initial_state = state_registry->get_initial_state();
    SearchNode node = search_space->get_node(initial_state);
    node.open_initial();
    cached_abstract_state_ids = make_unique<PerStateInformation<int>>(MISSING);
    (*cached_abstract_state_ids)[initial_state] = abstraction.get_initial_state().get_id();
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

    // Check for each transition if the operator is applicable or if there is a deviation.
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
                if (pick_flawed_abstract_state == PickFlawedAbstractState::FIRST) {
                    return FAILED;
                }
            } else if (succ_node.is_new()) {
                // No flaw
                (*cached_abstract_state_ids)[succ_state] = target;
                succ_node.open_new_node(node, op, op.get_cost());
                open_list.push(succ_state.get_id());

                if (pick_flawed_abstract_state == PickFlawedAbstractState::FIRST) {
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
    const OperatorProxy &op, int num_variables,
	const AbstractState &state) {
    vector<bool> affected(num_variables);
	vector<bool> conditionally_affected(num_variables);
    affected.assign(num_variables, false);
    conditionally_affected.assign(num_variables, false);
	for (EffectProxy effect : op.get_effects()) {
		bool conds_always_satisfied = true; // check if effect has to be applied
		for (const FactProxy &cond : effect.get_conditions()) {
			if (!state.contains(cond.get_pair().var, cond.get_pair().value) ||
                state.count(cond.get_pair().var) > 1) {
                conds_always_satisfied = false;
                break;
            }
		}
		FactPair fact = effect.get_fact().get_pair();
		if (conds_always_satisfied) { //If the conditions have to be satisfied in this state,
			affected[fact.var] = true; // the effect is applied and the variable is affected.
		} else { // the condition is not necessarily affected
			conditionally_affected[fact.var] = true; // the variable is only conditionally affected.
		}
    }
	for (FactProxy precondition : op.get_preconditions()) {
    	FactPair fact = precondition.get_pair();
		if (!conditionally_affected[fact.var]) { // if the variable is not conditionally affected
        	affected[fact.var] = true; // it is by the precondition.
    	}
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

static void update_value_counts(const AbstractState &abs_state,
								const vector<State> &deviation_states,
    							const AbstractState &target_abs_state,
								const OperatorProxy &op,
								const vector<int> &unaffected_variables,
								vector<vector<int>> &state_value_count,
								vector<vector<int>> &cond_value_count,
								vector<vector<vector<bool>>> &cond_value_wanted,
								const vector<int> &domain_sizes) {
    /*
        For the transition (a, o, b) with the abstract state a = abs_state, operator o = op,
        target b = target_abs_state, we iterate over all concrete states in deviation_states that are contained in a,
        but their o-successor is not contained in b.
        We check all variables that are not necessarily affected by the operator o in the abstract state a.
        Either these variables are entirely unaffected by the operator o, or they are only conditionally affected.
        In state_value_count, we count for each variable and each value in its domain how many deviating states have
            this value if the variable is unaffected and if it its domain in abs_state can be split.
        In cond_value_count, we count for each variable and value in its domain how many deviating states have this
            value on the variable if the variable has a condition in the operator where the conditional effect
            changes one of our unaffected variables.
        In cond_value_wanted, we mark for each var / value pair where cond_value_count[var][value] > 0
            which values are already in abs_state and that we also want to keep in the abstract state.
            We use this to determine the wanted list for the split later in get_deviation_splits().
    */
	int num_vars = domain_sizes.size();
	vector<vector<EffectProxy>> effects_by_variable; // store effects that possible affect each variable
	state_value_count.clear(); // counter for deviations on unchanged facts
	cond_value_count.clear(); // counter for deviations on condition facts
	cond_value_wanted.clear(); // mark which values are wanted for conditions causing deviations
	for (int var = 0; var < num_vars; ++var) {
        state_value_count.emplace_back(domain_sizes[var], 0);
        cond_value_count.emplace_back(domain_sizes[var], 0);
        cond_value_wanted.emplace_back();
        cond_value_wanted[var].reserve(domain_sizes[var]);
        for (int i = 0; i < domain_sizes[var]; ++i) {
            cond_value_wanted[var].emplace_back(domain_sizes[var], false);
        }
		effects_by_variable.emplace_back();
    }
	// find effects that are relevant for unaffected variables
	for (int var : unaffected_variables) {
		for (EffectProxy eff : op.get_effects()) {
			if (eff.get_fact().get_variable().get_id() == var) {
				effects_by_variable[var].push_back(eff);
			}
		}
	}
	for (const State &state : deviation_states) {
		for (int var : unaffected_variables) {
			int state_value = state[var].get_value();
			if (abs_state.count(var) > 1) {		// if the variable can be split in the abstract state
                state_value_count[var][state_value]++; // increase count for deviation on state value for var
            }
			for (auto eff : effects_by_variable[var]) {
				bool value_in_target = target_abs_state.contains(var, state_value); // state value is in target
				bool eff_in_target = target_abs_state.contains(var, eff.get_fact().get_value()); // eff in target
				if (!value_in_target || !eff_in_target) {
					// state value OR effect value is not in the target abstract state -> DEVIATION on this effect
					// FIX: force condition (if eff in target) to be met or prevent it (if state val in target)
                    for (auto condition : eff.get_conditions()) {
                        const FactPair &cond_pair = condition.get_pair();
                        int state_cond_value = state[cond_pair.var].get_value(); // state value on cond variable
                        if (eff_in_target) { // if the effect is in the target, it has to be triggered in abs
                            if (cond_pair.value != state_cond_value // if the condition is not met in the state
                                && abs_state.contains(cond_pair.var, cond_pair.value)) { // but contained in abs
                                // we want to force the variable to have the condition value instead of the state value
                                cond_value_count[cond_pair.var][state_cond_value]++; // increase count for dev on cond
                                cond_value_wanted[cond_pair.var][state_cond_value][cond_pair.value] = true;
                            }
                        } else if (cond_pair.value == state_cond_value // if the condition is met in the state
                                   && abs_state.count(cond_pair.var) > 1) { // and the condition var can be split
                            // the wanted values are all values except the current state value (prevent the condition)
                            cond_value_count[cond_pair.var][state_cond_value]++;
                            for (int val = 0; val < domain_sizes[cond_pair.var]; ++val) {
                                if (val != state_cond_value && abs_state.contains(cond_pair.var, val)) {
                                    cond_value_wanted[cond_pair.var][state_cond_value][val] = true;
                                }
                            }
                        }
                    }
				}
			}
		}
    }
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

static void get_deviation_splits(const AbstractState &abs_state,
						const AbstractState &target_abs_state,
						const vector<int> &domain_sizes,
						const vector<vector<int>> &state_value_count,
						const vector<vector<int>> &cond_value_count,
						const vector<vector<vector<bool>>> &cond_value_wanted,
						vector<vector<Split>> &splits) {
    /*
      For each fact in the concrete state that is not contained in the
      target abstract state, loop over all values in the domain of the
      corresponding variable. The values that are in both the current and
      the target abstract state are the "wanted" ones, i.e., the ones that
      we want to split off. This test can be specialized for applicability and
      deviation flaws. Here, we consider deviation flaws.

      Let the desired abstract transition be (a, o, t) and the deviation be
      (a, o, b). We distinguish three cases for each variable v:

      pre(o)[v] defined: no split possible since o is applicable in s.
      pre(o)[v] undefined, uncond eff(o)[v] defined: no split possible since regression adds whole domain.
      pre(o)[v] and eff(o)[v] undefined: if s[v] \notin t[v], wanted = intersect(a[v], b[v]).
            -> all v where state_value_count[v][s[v]] > 0 and s[v] \notin t[v]
      pre(o)[v] undefined, cond eff(o)[v] defined: if s[v] \notin t[v], cond eff(o)[v] \in t[v],
                                    for cond[w]: if cond[w] \not in s[w]: wanted = intersect(a[w], {cond[w]})
      pre(o)[v] undefined, cond eff(o)[v] defined: if cond eff(o)[v] \notin t[v],
                                    for cond[w]: if cond[w] \in s[w]: wanted = intersect(a[w], domain[w]\{s[w]})
            -> all v where cond_value_count[v][s[v]] > 0
                    wanted = {c | c \in domain[v] and cond_value_wanted[v][s[v]][c] == true}
      state_value_count, cond_value_count, and cond_value_wanted are computed in update_value_counts().
    */
	int num_vars = domain_sizes.size();
	for (int var = 0; var < num_vars; ++var) {
		for (int val = 0; val < domain_sizes[var]; ++val) {
            if (state_value_count[var][val] > 0 && !target_abs_state.contains(var, val)) {
                // if the variable is not in the target abstract state, we can split it off
                vector<int> wanted;
                for (int value = 0; value < domain_sizes[var]; ++value) {
                    if (abs_state.contains(var, value) &&
                        target_abs_state.contains(var, value)) {
                        wanted.push_back(value);
                    }
                }
                if (!wanted.empty()) { // if there are wanted values, we can split off the variable
                    add_split(splits, Split(abs_state.get_id(), var, val, move(wanted), state_value_count[var][val]));
                }
            }
            if (cond_value_count[var][val] > 0) {
                vector<int> wanted;
                for (int c_val = 0; c_val < domain_sizes[var]; ++c_val) {
                    if (cond_value_wanted[var][val][c_val]) {
                        wanted.push_back(c_val);
                    }
                }
                if (!wanted.empty()) { // if there are wanted values, we can split off the variable
                    add_split(splits, Split(
                                  abs_state.get_id(), var, val,
                                  move(wanted), cond_value_count[var][val]));
                }
            }
        }
	}
}

// TODO: Add comment about split considering multiple transitions.
unique_ptr<Split> FlawSearch::create_split(
    const vector<StateID> &state_ids, int abstract_state_id) {
    compute_splits_timer.resume();
    const AbstractState &abstract_state = abstraction.get_state(abstract_state_id);

    if (log.is_at_least_debug()) {
        log << endl;
        log << "Create split for abstract state " << abstract_state_id << " and "
            << state_ids.size() << " concrete states." << endl;
    }

    vector<vector<Split>> splits(task_proxy.get_variables().size());
    for (auto &pair : get_f_optimal_transitions(abstract_state_id)) {
        // go through f-optimal outgoing transitions
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
                if (state_value != fact.value) { // Applicability flaw
                    applicable[i] = false;
                    ++state_value_count[state_value];
                }
            }
            for (int value = 0; value < domain_sizes[fact.var]; ++value) {
                if (state_value_count[value] > 0) {
                    assert(value != fact.value);
                    add_split(splits, Split(
                                  abstract_state_id, fact.var, value,
                                  {fact.value}, state_value_count[value]));
                }
            }
        }
        int num_vars = domain_sizes.size();
		vector<int> unaffected_variables = get_unaffected_variables(op, num_vars, abstract_state);

        phmap::flat_hash_map<int, vector<State>> dev_states_by_target;
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

                // At most one of the f-optimal targets can include the successor state.
                if (!target_hit && abstraction.get_state(target).includes(succ_state)) {
                    // No flaw
                    target_hit = true;
                } else {
                    // Deviation flaw
                    assert(target != get_abstract_state_id(succ_state));
                    dev_states_by_target[target].push_back(state);
                }
            }
        }
		// iterate over dev_states_by_target to find deviation splits
		vector<vector<int>> state_value_count; // counts how many concrete states have a certain value for a var
    	vector<vector<int>> cond_value_count; // counts how many times a cond effect is relevant for a value + var
    	vector<vector<vector<bool>>> cond_value_wanted; // wanted values for deviations caused by conditional effects
		for (auto &[target, dev_states] : dev_states_by_target) {
			// update state_value_count, cond_value_count, cond_value_wanted, effects_by_variable
			update_value_counts(abstract_state, dev_states, abstraction.get_state(target), op, unaffected_variables,
								state_value_count, cond_value_count, cond_value_wanted, domain_sizes);
			get_deviation_splits(
                abstract_state, abstraction.get_state(target),
                domain_sizes, state_value_count,
                cond_value_count, cond_value_wanted, splits);
        }
    }

    int num_splits = 0;
    for (auto &var_splits : splits) {
        num_splits += var_splits.size();
    }
	if (num_splits == 0) {
		return nullptr;
	}
    if (log.is_at_least_debug()) {
        log << "Unique splits: " << num_splits << endl;
    }
    compute_splits_timer.stop();

    if (num_splits == 0) {
        return nullptr;
    }
    pick_split_timer.resume();
    Split split = split_selector.pick_split(abstract_state, move(splits), rng);
    pick_split_timer.stop();
    return make_unique<Split>(move(split));
}

SearchStatus FlawSearch::search_for_flaws(const utils::CountdownTimer &cegar_timer) {
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
        // To remain complete, only take the expansions limit into account once at least one flaw has been found.
        if (current_num_expanded_states >= max_state_expansions && flawed_states.num_abstract_states() > 0) {
            log << "Expansion limit reached with flaws." << endl;
            search_status = FAILED;
            break;
        }
        search_status = step();
    }
    // Clear open list.
    stack<StateID>().swap(open_list);

    int current_num_expanded_states = num_overall_expanded_concrete_states -
        num_expansions_in_prev_searches;
    max_expanded_concrete_states = max(max_expanded_concrete_states,
                                       current_num_expanded_states);
    if (log.is_at_least_debug()) {
        log << "Flaw search expanded " << current_num_expanded_states
            << " states." << endl;
    }

    /* For MAX_H, we don't return SOLVED when hitting a goal state. So if MAX_H
       fails to find a single flaw, we adapt the search status here. */
    if (pick_flawed_abstract_state == PickFlawedAbstractState::MAX_H
        && search_status == FAILED && flawed_states.num_abstract_states() == 0) {
        search_status = SOLVED;
    }

    flaw_search_timer.stop();
    return search_status;
}

unique_ptr<Split> FlawSearch::get_single_split(const utils::CountdownTimer &cegar_timer) {
    auto search_status = search_for_flaws(cegar_timer);

    if (search_status == TIMEOUT)
        return nullptr;

    if (search_status == FAILED) {
        assert(!flawed_states.empty());

        FlawedState flawed_state = flawed_states.pop_random_flawed_state_and_clear(rng);
        StateID state_id = *rng.choose(flawed_state.concrete_states);

        if (log.is_at_least_debug()) {
            vector<OperatorID> trace;
            search_space->trace_path(state_registry->lookup_state(state_id), trace);
            vector<string> operator_names;
            operator_names.reserve(trace.size());
            for (OperatorID op_id : trace) {
                operator_names.push_back(task_proxy.get_operators()[op_id].get_name());
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
                log << "Ignore flawed state with increased f value: " << abs_id << endl;
            }
        }
    }
    // The f value increased for all states.
    return FlawedState::no_state;
}

unique_ptr<Split>
FlawSearch::get_min_h_batch_split(const utils::CountdownTimer &cegar_timer) {
    assert(pick_flawed_abstract_state == PickFlawedAbstractState::BATCH_MIN_H);
    if (last_refined_flawed_state != FlawedState::no_state) {
        // Recycle flaws of the last refined abstract state.
        Cost old_h = last_refined_flawed_state.h;
        for (const StateID &state_id : last_refined_flawed_state.concrete_states) {
            State state = state_registry->lookup_state(state_id);
            // We only add non-goal states to flawed_states.
            assert(!task_properties::is_goal_state(task_proxy, state));
            int abs_id = get_abstract_state_id(state);
            if (get_h_value(abs_id) == old_h) {
                add_flaw(abs_id, state);
            }
        }
    }
    FlawedState flawed_state = get_flawed_state_with_min_h();

    auto search_status = SearchStatus::FAILED;
    if (flawed_state == FlawedState::no_state) {
        search_status = search_for_flaws(cegar_timer);
        if (search_status == SearchStatus::FAILED) {
            flawed_state = get_flawed_state_with_min_h();
        }
    }

    if (search_status == TIMEOUT) {
        return nullptr;
	}

    if (search_status == FAILED) {
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

    assert(search_status == SOLVED);
    return nullptr;
}

FlawSearch::FlawSearch(
    const shared_ptr<AbstractTask> &task,
    const Abstraction &abstraction,
    const ShortestPaths &shortest_paths,
    utils::RandomNumberGenerator &rng,
    PickFlawedAbstractState pick_flawed_abstract_state,
    PickSplit pick_split,
    PickSplit tiebreak_split,
    int max_concrete_states_per_abstract_state,
    int max_state_expansions,
    const utils::LogProxy &log) :
    task_proxy(*task),
    domain_sizes(get_domain_sizes(task_proxy)),
    abstraction(abstraction),
    shortest_paths(shortest_paths),
    split_selector(task, pick_split, tiebreak_split, log.is_at_least_debug()),
    rng(rng),
    pick_flawed_abstract_state(pick_flawed_abstract_state),
    max_concrete_states_per_abstract_state(max_concrete_states_per_abstract_state),
    max_state_expansions(max_state_expansions),
    log(log),
    silent_log(utils::get_silent_log()),
    last_refined_flawed_state(FlawedState::no_state),
    best_flaw_h((pick_flawed_abstract_state == PickFlawedAbstractState::MAX_H) ? 0 : INF),
    num_searches(0),
    num_overall_expanded_concrete_states(0),
    max_expanded_concrete_states(0),
    flaw_search_timer(false),
    compute_splits_timer(false),
    pick_split_timer(false) {
}

unique_ptr<Split> FlawSearch::get_split(const utils::CountdownTimer &cegar_timer) {
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
            << static_cast<int>(pick_flawed_abstract_state)
            << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }

    if (split) {
        assert((pick_flawed_abstract_state != PickFlawedAbstractState::MAX_H
                && pick_flawed_abstract_state != PickFlawedAbstractState::MIN_H)
               || best_flaw_h == get_h_value(split->abstract_state_id));
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
        const AbstractState *next_abstract_state = &abstraction.get_state(step.target_id);
        if (task_properties::is_applicable(op, concrete_state)) {
            if (debug)
                log << "  Move to " << *next_abstract_state << " with "
                    << op.get_name() << endl;
            State next_concrete_state = state_registry->get_successor_state(concrete_state, op);
            if (!next_abstract_state->includes(next_concrete_state)) {
                if (debug)
                    log << "  Paths deviate." << endl;
                return create_split({concrete_state.get_id()}, abstract_state->get_id());
            }
            abstract_state = next_abstract_state;
            concrete_state = move(next_concrete_state);
        } else {
            if (debug)
                log << "  Operator not applicable: " << op.get_name() << endl;
            return create_split({concrete_state.get_id()}, abstract_state->get_id());
        }
    }
    assert(abstraction.get_goals().count(abstract_state->get_id()));
    if (task_properties::is_goal_state(task_proxy, concrete_state)) {
        // We found a concrete solution.
        return nullptr;
    } else {
        if (debug)
            log << "  Goal test failed." << endl;
        return create_split({concrete_state.get_id()}, abstract_state->get_id());
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
        log << "Average flaw search time: " << flaw_search_timer() / num_searches << endl;
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
