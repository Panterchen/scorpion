#include "transition_rewirer.h"

#include "abstract_state.h"
#include "transition.h"

#include "../task_proxy.h"

#include "../task_utils/task_properties.h"

#include <algorithm>
#include <map>
#include <unordered_map>

using namespace std;

namespace cartesian_abstractions {
static vector<vector<FactPair>> get_preconditions_by_operator(
    const OperatorsProxy &ops) {
    vector<vector<FactPair>> preconditions_by_operator;
    preconditions_by_operator.reserve(ops.size());
    for (OperatorProxy op : ops) {
        vector<FactPair> preconditions = task_properties::get_fact_pairs(op.get_preconditions());
        sort(preconditions.begin(), preconditions.end());
        preconditions_by_operator.push_back(move(preconditions));
    }
    return preconditions_by_operator;
}

static vector<FactPair> get_postconditions(
    const OperatorProxy &op) {
    // Use map to obtain sorted postconditions.
    map<int, int> var_to_post;
    for (FactProxy fact : op.get_preconditions()) {
        var_to_post[fact.get_variable().get_id()] = fact.get_value();
    }
    for (EffectProxy effect : op.get_effects()) {
		// only if effect has no conditions
		if (effect.get_conditions().empty()) {
        	FactPair fact = effect.get_fact().get_pair();
        	var_to_post[fact.var] = fact.value;
		}
    }
    vector<FactPair> postconditions;
    postconditions.reserve(var_to_post.size());
    for (const pair<const int, int> &fact : var_to_post) {
        postconditions.emplace_back(fact.first, fact.second);
    }
    return postconditions;
}

static vector<vector<FactPair>> get_postconditions_by_operator(
    const OperatorsProxy &ops) {
    vector<vector<FactPair>> postconditions_by_operator;
    postconditions_by_operator.reserve(ops.size());
    for (OperatorProxy op : ops) {
        postconditions_by_operator.push_back(get_postconditions(op));
    }
    return postconditions_by_operator;
}

static vector<FactPair> get_unconditional_effects(
    const OperatorProxy &op) {
    // Use map to obtain sorted unconditional effect facts.
    map<int, int> var_to_post;
    for (EffectProxy effect : op.get_effects()) {
        if (effect.get_conditions().empty()) {
            FactPair fact = effect.get_fact().get_pair();
            var_to_post[fact.var] = fact.value;
        }
    }
    vector<FactPair> unconditional_effects;
    unconditional_effects.reserve(var_to_post.size());
    for (const pair<const int, int> &fact : var_to_post) {
        unconditional_effects.emplace_back(fact.first, fact.second);
    }
    return unconditional_effects;
}

static vector<vector<FactPair>> get_unconditional_effects_by_operator(
    const OperatorsProxy &ops) {
    vector<vector<FactPair>> unconditional_effects_by_operator;
    unconditional_effects_by_operator.reserve(ops.size());
    for (OperatorProxy op : ops) {
        unconditional_effects_by_operator.push_back(get_unconditional_effects(op));
    }
    return unconditional_effects_by_operator;
}

static vector<pair<vector<FactPair>, FactPair>> get_conditional_effects(
	const OperatorProxy &op) {
	// for given operator, return a vector of EffectProxies that have conditions
	vector<pair<vector<FactPair>, FactPair>> conditional_effects;
	for (const EffectProxy &effect : op.get_effects()) {
        if (!effect.get_conditions().empty()) {
            // if effect has no conditions, it is an unconditional effect
			vector<FactPair> condition_facts = task_properties::get_fact_pairs(effect.get_conditions());
			map<int, int> var_to_cond;
			for (const FactPair &fact : condition_facts) {
				var_to_cond[fact.var] = fact.value;
			}
    		vector<FactPair> conditions_sorted;
    		conditions_sorted.reserve(var_to_cond.size());
    		for (const pair<const int, int> &fact : var_to_cond) {
        		conditions_sorted.emplace_back(fact.first, fact.second);
    		}
            conditional_effects.emplace_back(conditions_sorted, effect.get_fact().get_pair());
        }
    } // if operator has no conditional effects, return empty vector
	return conditional_effects;
}

static vector<vector<pair<vector<FactPair>, FactPair>>> get_conditional_effects_by_operator(
    const OperatorsProxy &ops) {
    vector<vector<pair<vector<FactPair>, FactPair>>> conditional_effects_by_operator;
    conditional_effects_by_operator.reserve(ops.size());
    for (OperatorProxy op : ops) {
        conditional_effects_by_operator.push_back(get_conditional_effects(op));
    }
    return conditional_effects_by_operator;
}

static int lookup_value(const vector<FactPair> &facts, int var) {
    assert(is_sorted(facts.begin(), facts.end()));
    for (const FactPair &fact : facts) {
        if (fact.var == var) {
            return fact.value;
        } else if (fact.var > var) {
            return UNDEFINED;
        }
    }
    return UNDEFINED;
}

static void remove_transitions_with_given_target(
    Transitions &transitions, int state_id) {
    auto new_end = remove_if(
        transitions.begin(), transitions.end(),
        [state_id](const Transition &t) {return t.target_id == state_id;});
    assert(new_end != transitions.end());
    transitions.erase(new_end, transitions.end());
}

static void add_transition(deque<Transitions> &incoming, deque<Transitions> &outgoing, int src, int op, int dest) {
    assert(src != dest);
    assert(find(outgoing[src].begin(), outgoing[src].end(), Transition(op, dest)) == outgoing[src].end());
    assert(find(incoming[dest].begin(), incoming[dest].end(), Transition(op, src)) == incoming[dest].end());
    outgoing[src].emplace_back(op, dest);
    incoming[dest].emplace_back(op, src);
}

static void add_loop(deque<Loops> &loops, int state_id, int op_id) {
    assert(utils::in_bounds(state_id, loops));
    loops[state_id].push_back(op_id);
}


TransitionRewirer::TransitionRewirer(const OperatorsProxy &ops)
    : preconditions_by_operator(get_preconditions_by_operator(ops)),
      postconditions_by_operator(get_postconditions_by_operator(ops)),
      unconditional_effects_by_operator(get_unconditional_effects_by_operator(ops)),
	  conditional_effects_by_operator(get_conditional_effects_by_operator(ops)) {
}

void TransitionRewirer::rewire_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id,
    const AbstractState &v1, const AbstractState &v2, int var) const {
    rewire_incoming_transitions(incoming, outgoing, states, v_id, v1, v2, var);
    rewire_outgoing_transitions(incoming, outgoing, states, v_id, v1, v2, var);
}

void TransitionRewirer::rewire_incoming_uc(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, const AbstractState &v1, const AbstractState &v2,
	int var, int op_id, int u_id, int post) const {
    /* State v has been split into v1 and v2. Now for all transitions
       u->v we need to add transitions u->v1 or u->v2. */

    int v1_id = v1.get_id();
    int v2_id = v2.get_id();
    const AbstractState &u = *states[u_id];
	if (post == UNDEFINED) {
        // op has no precondition and no effect on var.
        bool u_and_v1_intersect = u.domain_subsets_intersect(v1, var);
        if (u_and_v1_intersect) {
            add_transition(incoming, outgoing, u_id, op_id, v1_id);
        }
        /* If u and v1 don't intersect, we must add the other transition
           and can avoid an intersection test. */
        if (!u_and_v1_intersect || u.domain_subsets_intersect(v2, var)) {
            add_transition(incoming, outgoing, u_id, op_id, v2_id);
        }
    } else if (v1.contains(var, post)) {
        // op can only end in v1.
        add_transition(incoming, outgoing, u_id, op_id, v1_id);
    } else {
        // op can only end in v2.
        assert(v2.contains(var, post));
        add_transition(incoming, outgoing, u_id, op_id, v2_id);
    }
}

void TransitionRewirer::rewire_incoming_c(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, const AbstractState &v1, const AbstractState &v2,
    int var, int op_id, int u_id) const {
	const AbstractState &u = *states[u_id];
    // only consider relevant effects that have an effect or a condition on var
    assert(has_conditional_effects(op_id));
    vector<pair<vector<FactPair>, FactPair>> relevant_effects = get_relevant_effects(op_id, var);
    int post = get_postcondition_value(op_id, var);
    // Track applicable effects for var
    unordered_set<int> applicable_v1; // values applicable in v1
    unordered_set<int> applicable_v2; // values applicable in v2
    // Add unconditional effect if it exists
    if (post != UNDEFINED) {
        applicable_v1.insert(post);
        applicable_v2.insert(post);
    }
    bool u_to_v1_blocked = false;
    bool u_to_v2_blocked = false;
    for (const auto &effect : relevant_effects) {
        const FactPair fact = effect.second;
        if (fact.var != var) {
            continue;
        }
        const vector<FactPair> &condition = effect.first;
        // Check if condition is applicable
        if (u.includes(condition)) {
            // Add to applicable set
            applicable_v1.insert(fact.value);
            applicable_v2.insert(fact.value);
            // Check if forced
            bool forced = true;
            for (const FactPair &cond_fact : condition) {
                if (u.count(cond_fact.var) > 1) {
                    forced = false;
                    break;
                }
            }
            // If forced but effect not in v1 or v2 state, block
            if (forced) {
                if (!v1.contains(var, fact.value)) {
                    u_to_v1_blocked = true;
                }
                if (!v2.contains(var, fact.value)) {
                    u_to_v2_blocked = true;
                }
            }
        }
    }
	// Update block based on applicable effects and intersection with u
    // Check intersection: dom(var, v) ∩ (applicable[var] ∪ dom(var, u)) ≠ ∅
    if (!u_to_v1_blocked) {
        bool has_intersection = false;
        for (int value : applicable_v1) {
            if (v1.contains(var, value)) {
                has_intersection = true;
                break;
            }
        }
        if (!has_intersection && !u.domain_subsets_intersect(v1, var)) {
            u_to_v1_blocked = true;
        }
    }
    if (!u_to_v2_blocked) {
        bool has_intersection = false;
        for (int value : applicable_v2) {
            if (v2.contains(var, value)) {
                has_intersection = true;
                break;
            }
        }
        if (!has_intersection && !u.domain_subsets_intersect(v2, var)) {
            u_to_v2_blocked = true;
        }
    }
    if (!u_to_v1_blocked) {
        add_transition(incoming, outgoing, u_id, op_id, v1.get_id());
    }
    if (!u_to_v2_blocked) {
        add_transition(incoming, outgoing, u_id, op_id, v2.get_id());
    }
}

void TransitionRewirer::rewire_incoming_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id,
    const AbstractState &v1, const AbstractState &v2, int var) const {
    /* State v has been split into v1 and v2. Now for all transitions
       u->v we need to add transitions u->v1, u->v2, or both. */
    Transitions old_incoming = move(incoming[v_id]);

    unordered_set<int> updated_states;
    for (const Transition &transition : old_incoming) {
        int u_id = transition.target_id;
        bool is_new_state = updated_states.insert(u_id).second;
        if (is_new_state) {
            remove_transitions_with_given_target(outgoing[u_id], v_id);
        }
    }

    for (const Transition &transition : old_incoming) {
        int op_id = transition.op_id;
        int u_id = transition.target_id;
		if (!has_conditional_effects(op_id)) {
			int post = get_postcondition_value(op_id, var);
			rewire_incoming_uc(
                incoming, outgoing, states, v1, v2, var, op_id, u_id, post);
		} else {
			rewire_incoming_c(
                incoming, outgoing, states, v1, v2, var, op_id, u_id);
		}
    }
}

void TransitionRewirer::rewire_outgoing_uc(
	deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, const AbstractState &v1, const AbstractState &v2,
	int var, int op_id, int w_id, int pre, int post) const {
	int v1_id = v1.get_id();
    int v2_id = v2.get_id();
	const AbstractState &w = *states[w_id];
	if (post == UNDEFINED) {
        assert(pre == UNDEFINED);
        // op has no precondition and no effect on var.
        bool v1_and_w_intersect = v1.domain_subsets_intersect(w, var);
        if (v1_and_w_intersect) {
            add_transition(incoming, outgoing, v1_id, op_id, w_id);
        }
        /* If v1 and w don't intersect, we must add the other transition
           and can avoid an intersection test. */
        if (!v1_and_w_intersect || v2.domain_subsets_intersect(w, var)) {
            add_transition(incoming, outgoing, v2_id, op_id, w_id);
        }
    } else if (pre == UNDEFINED) {
        // op has no precondition, but an effect on var.
        add_transition(incoming, outgoing, v1_id, op_id, w_id);
        add_transition(incoming, outgoing, v2_id, op_id, w_id);
    } else if (v1.contains(var, pre)) {
        // op can only start in v1.
        add_transition(incoming, outgoing, v1_id, op_id, w_id);
    } else {
        // op can only start in v2.
        assert(v2.contains(var, pre));
        add_transition(incoming, outgoing, v2_id, op_id, w_id);
    }
}

void TransitionRewirer::rewire_outgoing_c(
	deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, const AbstractState &v1, const AbstractState &v2,
	int var, int op_id, int w_id) const {
	assert(has_conditional_effects(op_id));
    int pre = get_precondition_value(op_id, var);
    int post = get_postcondition_value(op_id, var);
    const AbstractState &w = *states[w_id];
    // Check preconditions
    bool pre_v1 = (pre == UNDEFINED || v1.contains(var, pre));
    bool pre_v2 = (pre == UNDEFINED || v2.contains(var, pre));
	assert(pre_v1 || pre_v2); // at least one state should be able to apply the operator
    if (!pre_v1 && !pre_v2) {
        return; // Neither state can apply the operator
    }
    // Get relevant conditional effects
    vector<pair<vector<FactPair>, FactPair>> relevant_effects = get_relevant_effects(op_id, var);
    // Collect all variables that appear in effects
    unordered_set<int> variables_set;
    variables_set.insert(var); // Always include var
    for (const auto &[condition, fact] : relevant_effects) {
        variables_set.insert(fact.var);
    }
    vector<int> variables(variables_set.begin(), variables_set.end());
    sort(variables.begin(), variables.end());
    bool blocked_from_v1 = !pre_v1; // Start blocked if precondition not satisfied
    bool blocked_from_v2 = !pre_v2;
    // Track applicable effects for each variable
    unordered_map<int, unordered_set<int>> applicable_from_v1;
    unordered_map<int, unordered_set<int>> applicable_from_v2;
    // Add unconditional effect if it exists
    if (post != UNDEFINED) {
        if (pre_v1) {
            applicable_from_v1[var].insert(post);
        }
        if (pre_v2) {
            applicable_from_v2[var].insert(post);
        }
    }
    // Process conditional effects
    for (const auto &[condition, fact] : relevant_effects) {
        bool cond_v1 = (pre_v1 && v1.includes(condition));
        bool cond_v2 = (pre_v2 && v2.includes(condition));
        // Check if forced (condition satisfied with all single values)
        bool forced_v1 = cond_v1;
        bool forced_v2 = cond_v2;
        for (const FactPair &cond_fact : condition) {
            if (forced_v1 && v1.count(cond_fact.var) > 1) {
                forced_v1 = false;
            }
            if (forced_v2 && v2.count(cond_fact.var) > 1) {
                forced_v2 = false;
            }
        }
        // If forced but effect not in w, block transition
        if (forced_v1 && !w.contains(fact.var, fact.value)) {
            blocked_from_v1 = true;
        }
        if (forced_v2 && !w.contains(fact.var, fact.value)) {
            blocked_from_v2 = true;
        }
        // Add to applicable sets (if condition is satisfied in v1 or v2)
        if (cond_v1) {
            applicable_from_v1[fact.var].insert(fact.value);
        }
        if (cond_v2) {
            applicable_from_v2[fact.var].insert(fact.value);
        }
    }
    // Check intersection condition for all variables for both transitions
    // dom(v, w) ∩ (applicable[v] ∪ dom(v, source)) ≠ ∅
    for (int v : variables) {
        if (!blocked_from_v1) {
            bool has_intersection = false;
            // Check if any applicable effect value is in w
            if (applicable_from_v1.count(v) > 0) {
                for (int value : applicable_from_v1[v]) {
                    if (w.contains(v, value)) {
                        has_intersection = true;
                        break;
                    }
                }
            }
            // Check if domains of v1 and w intersect on v
            if (!has_intersection && v1.domain_subsets_intersect(w, v)) {
                has_intersection = true;
            }
            if (!has_intersection) {
                blocked_from_v1 = true;
            }
        }
        if (!blocked_from_v2) {
            bool has_intersection = false;
            // Check if any applicable effect value is in w
            if (applicable_from_v2.count(v) > 0) {
                for (int value : applicable_from_v2[v]) {
                    if (w.contains(v, value)) {
                        has_intersection = true;
                        break;
                    }
                }
            }
            // Check if domains of v2 and w intersect on v
            if (!has_intersection && v2.domain_subsets_intersect(w, v)) {
                has_intersection = true;
            }
            if (!has_intersection) {
                blocked_from_v2 = true;
            }
        }
    }
    // Add transitions if not blocked
    if (!blocked_from_v1) {
        add_transition(incoming, outgoing, v1.get_id(), op_id, w_id);
    }
    if (!blocked_from_v2) {
        add_transition(incoming, outgoing, v2.get_id(), op_id, w_id);
    }
}

void TransitionRewirer::rewire_outgoing_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id,
    const AbstractState &v1, const AbstractState &v2, int var) const {
    /* State v has been split into v1 and v2. Now for all transitions
       v->w we need to add transitions v1->w, v2->w, or both. */
    Transitions old_outgoing = move(outgoing[v_id]);

    unordered_set<int> updated_states;
    for (const Transition &transition : old_outgoing) {
        int w_id = transition.target_id;
        bool is_new_state = updated_states.insert(w_id).second;
        if (is_new_state) {
            remove_transitions_with_given_target(incoming[w_id], v_id);
        }
    }

    for (const Transition &transition : old_outgoing) {
        int op_id = transition.op_id;
        int w_id = transition.target_id;
		// check if op is conditional or unconditional
		if (!has_conditional_effects(op_id)) {
        	int pre = get_precondition_value(op_id, var);
        	int post = get_postcondition_value(op_id, var);
			rewire_outgoing_uc(
                incoming, outgoing, states, v1, v2, var, op_id, w_id, pre, post);
		} else {
			rewire_outgoing_c(
				incoming, outgoing, states, v1, v2, var, op_id, w_id);
		}
    }
}

void TransitionRewirer::rewire_loop_uc(
    deque<Loops> &loops, deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractState &v1, const AbstractState &v2, int var, int op_id, int pre, int post) const {
	int v1_id = v1.get_id();
    int v2_id = v2.get_id();
    if (pre == UNDEFINED) {
        // op has no precondition on var --> it must start in v1 and v2.
        if (post == UNDEFINED) {
            // op has no effect on var --> it must end in v1 and v2.
            add_loop(loops, v1_id, op_id);
            add_loop(loops, v2_id, op_id);
        } else if (v2.contains(var, post)) {
            // op must end in v2.
            add_transition(incoming, outgoing, v1_id, op_id, v2_id);
            add_loop(loops, v2_id, op_id);
        } else {
            // op must end in v1.
            assert(v1.contains(var, post));
            add_loop(loops, v1_id, op_id);
            add_transition(incoming, outgoing, v2_id, op_id, v1_id);
        }
    } else if (v1.contains(var, pre)) {
        // op must start in v1.
        assert(post != UNDEFINED);
        if (v1.contains(var, post)) {
            // op must end in v1.
            add_loop(loops, v1_id, op_id);
        } else {
            // op must end in v2.
            assert(v2.contains(var, post));
            add_transition(incoming, outgoing, v1_id, op_id, v2_id);
        }
    } else {
        // op must start in v2.
        assert(v2.contains(var, pre));
        assert(post != UNDEFINED);
        if (v1.contains(var, post)) {
            // op must end in v1.
            add_transition(incoming, outgoing, v2_id, op_id, v1_id);
        } else {
            // op must end in v2.
            assert(v2.contains(var, post));
            add_loop(loops, v2_id, op_id);
        }
    }
}

void TransitionRewirer::rewire_loop_c(
    deque<Loops> &loops, deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractState &v1, const AbstractState &v2, int var, int op_id) const {
	// check if there is a transition v1->v1, v1->v2, v2->v1 or v2->v2
    // get relevant effects for variable and operator
	assert(has_conditional_effects(op_id));
    int pre = get_precondition_value(op_id, var);
    int post = get_postcondition_value(op_id, var);
    // Check if operator can be applied in v1 and v2
    bool pre_v1 = (pre == UNDEFINED || v1.contains(var, pre));
    bool pre_v2 = (pre == UNDEFINED || v2.contains(var, pre));
    vector<pair<vector<FactPair>, FactPair>> relevant_effects = get_relevant_effects(op_id, var);
    // Collect all variables that appear in effects (for intersection check)
    unordered_set<int> other_variables_set;
    for (const auto &[condition, fact] : relevant_effects) {
        if (fact.var != var) {
            other_variables_set.insert(fact.var);
        }
    }
    // Track blocking for each transition
    bool blocked_v1_v1 = !pre_v1;
    bool blocked_v1_v2 = !pre_v1;
    bool blocked_v2_v1 = !pre_v2;
    bool blocked_v2_v2 = !pre_v2;
    // Track applicable effect values on var from each state
    unordered_set<int> applicable_var_from_v1;
    unordered_set<int> applicable_var_from_v2;
    // Track applicable effects on other variables (same from both states)
    unordered_map<int, unordered_set<int>> applicable_other_vars;
    // Add unconditional effect on var if it exists
    if (post != UNDEFINED) {
        if (pre_v1) {
            applicable_var_from_v1.insert(post);
        }
        if (pre_v2) {
            applicable_var_from_v2.insert(post);
        }
    }
    // Process conditional effects
    for (const auto &[condition, fact] : relevant_effects) {
        // Check if condition is satisfied in v1 and v2
        bool cond_v1 = (pre_v1 && v1.includes(condition));
        bool cond_v2 = (pre_v2 && v2.includes(condition));
        // Check if forced (must be applied)
        bool forced_v1 = cond_v1;
        bool forced_v2 = cond_v2;
        for (const FactPair &cond_fact : condition) {
            if (cond_fact.var == var) {
                // Condition on var might differ between v1 and v2
                if (forced_v1 && v1.count(var) > 1) {
                    forced_v1 = false;
                }
                if (forced_v2 && v2.count(var) > 1) {
                    forced_v2 = false;
                }
            } else {
                // Condition on other variables is the same for both states
                if ((forced_v1 || forced_v2) && v1.count(cond_fact.var) > 1) {
                    forced_v1 = false;
                    forced_v2 = false;
                }
            }
        }
        // Handle effects on var
        if (fact.var == var) {
            // Check blocking for fully applicable effects
            if (forced_v1) {
                if (!v1.contains(var, fact.value)) {
                    blocked_v1_v1 = true;
                }
                if (!v2.contains(var, fact.value)) {
                    blocked_v1_v2 = true;
                }
            }
            if (forced_v2) {
                if (!v2.contains(var, fact.value)) {
                    blocked_v2_v2 = true;
                }
                if (!v1.contains(var, fact.value)) {
                    blocked_v2_v1 = true;
                }
            }
            // Add to applicable sets
            if (cond_v1) {
                applicable_var_from_v1.insert(fact.value);
            }
            if (cond_v2) {
                applicable_var_from_v2.insert(fact.value);
            }
        } else {
            // Effect on other variable
            // Since v1 and v2 are the same on other variables, blocking is the same
            if (forced_v1 || forced_v2) {
                // Both states have the same value for this variable
                if (!v1.contains(fact.var, fact.value)) {
					assert(!v2.contains(fact.var, fact.value));
					if (forced_v1) { // block all transitions from v1
                        blocked_v1_v1 = true;
                        blocked_v1_v2 = true;
                    }
                    if (forced_v2) { // block all transitions from v2
                        blocked_v2_v1 = true;
                        blocked_v2_v2 = true;
                    }
                }
            }
            // Add to applicable set (same for both states)
            if (cond_v1 || cond_v2) {
                applicable_other_vars[fact.var].insert(fact.value);
            }
        }
    }
    // Check intersection for var
    // v1 → v1: check if dom(var, v1) ∩ (applicable_var_from_v1 ∪ dom(var, v1)) ≠ ∅
    if (!blocked_v1_v1) {
        bool has_intersection = false;
        // Check applicable effects
        for (int value : applicable_var_from_v1) {
            if (v1.contains(var, value)) {
                has_intersection = true;
                break;
            }
        }
        // Check domain intersection (should always be true)
        if (!has_intersection) {
            has_intersection = (v1.count(var) > 0); // should always be true
        }
        if (!has_intersection) {
            blocked_v1_v1 = true;
        }
    }
    // v1 → v2: check if dom(var, v2) ∩ applicable_var_from_v1 ≠ ∅
    if (!blocked_v1_v2) {
        bool has_intersection = false;
        // Check applicable effects
        for (int value : applicable_var_from_v1) {
            if (v2.contains(var, value)) {
                has_intersection = true;
                break;
            }
        }
		// v1 and v2 are disjoint on var, no intersection test needed
        if (!has_intersection) {
            blocked_v1_v2 = true;
        }
    }
    // v2 → v1: check if dom(var, v1) ∩ applicable_var_from_v2 ≠ ∅
    if (!blocked_v2_v1) {
        bool has_intersection = false;
        // Check applicable effects
        for (int value : applicable_var_from_v2) {
            if (v1.contains(var, value)) {
                has_intersection = true;
                break;
            }
        }
        // v1 and v2 are disjoint on var, no intersection test needed
        if (!has_intersection) {
            blocked_v2_v1 = true;
        }
    }
    // v2 → v2: check if dom(var, v2) ∩ (applicable_var_from_v2 ∪ dom(var, v2)) ≠ ∅
    if (!blocked_v2_v2) {
        bool has_intersection = false;
        // Check applicable effects
        for (int value : applicable_var_from_v2) {
            if (v2.contains(var, value)) {
                has_intersection = true;
                break;
            }
        }
        // Check domain intersection (should always be true)
        if (!has_intersection) {
            has_intersection = (v2.count(var) > 0); // should always be true
        }
        if (!has_intersection) {
            blocked_v2_v2 = true;
        }
    }
    // Check intersection for other variables (same for all transitions)
    for (int v : other_variables_set) {
        if (blocked_v1_v1 && blocked_v1_v2 && blocked_v2_v1 && blocked_v2_v2) {
            break; // All already blocked
        }
        bool has_intersection = false;
        // Check applicable effects
        if (applicable_other_vars.count(v) > 0) {
            for (int value : applicable_other_vars[v]) {
                if (v1.contains(v, value)) { // v1 and v2 have same domain on v
					assert(v2.contains(v, value));
                    has_intersection = true;
                    break;
                }
            }
        }
        // Check domain intersection (with itself, always true if non-empty)
        if (!has_intersection) {
            has_intersection = (v1.count(v) > 0); // this should always be true
        }
        if (!has_intersection) { // this should never happen
            // Block all transitions
            blocked_v1_v1 = true;
            blocked_v1_v2 = true;
            blocked_v2_v1 = true;
            blocked_v2_v2 = true;
        }
    }
    // Add transitions and loops that are not blocked
    if (!blocked_v1_v1) {
        add_loop(loops, v1.get_id(), op_id);
    }
    if (!blocked_v1_v2) {
        add_transition(incoming, outgoing, v1.get_id(), op_id, v2.get_id());
    }
    if (!blocked_v2_v1) {
        add_transition(incoming, outgoing, v2.get_id(), op_id, v1.get_id());
    }
    if (!blocked_v2_v2) {
        add_loop(loops, v2.get_id(), op_id);
    }
}

void TransitionRewirer::rewire_loops(
    deque<Loops> &loops, deque<Transitions> &incoming, deque<Transitions> &outgoing,
    int v_id, const AbstractState &v1, const AbstractState &v2, int var) const {
    for (int op_id : old_loops) {
		if (!has_conditional_effects(op_id)) {
			int pre = get_precondition_value(op_id, var);
    		int post = get_postcondition_value(op_id, var);
        	rewire_loop_uc(loops, incoming, outgoing, v1, v2, var, op_id, pre, post);
		} else {
			rewire_loop_c(loops, incoming, outgoing, v1, v2, var, op_id);
		}
    }
}

int TransitionRewirer::get_precondition_value(int op_id, int var) const {
    return lookup_value(preconditions_by_operator[op_id], var);
}

int TransitionRewirer::get_postcondition_value(int op_id, int var) const {
    return lookup_value(postconditions_by_operator[op_id], var);
}

vector<pair<vector<FactPair>, FactPair>> TransitionRewirer::get_relevant_effects(
    int op_id, int var) const {
    vector<pair<vector<FactPair>, FactPair>> relevant_effects;
    for (const auto &effect : conditional_effects_by_operator[op_id]) {
		const FactPair effect_fact = effect.second;
        if ((lookup_value(effect.first, var) != UNDEFINED) || (effect_fact.var == var)) {
            relevant_effects.emplace_back(effect.first, effect_fact);
        }
    }
    return relevant_effects;
}

vector<vector<FactPair>> TransitionRewirer::get_conditions_for_fact(
    int op_id, const FactPair &fact) const {
    vector<vector<FactPair>> conditions;
    for (const auto &effect : conditional_effects_by_operator[op_id]) {
        const FactPair effect_fact = effect.second;
        if (effect_fact == fact) {
            conditions.push_back(effect.first);
        }
    }
    return conditions;
}



int TransitionRewirer::get_num_operators() const {
    return preconditions_by_operator.size();
}

int TransitionRewirer::get_num_conditional_effects(int op_id) const {
    assert(utils::in_bounds(op_id, conditional_effects_by_operator));
    return conditional_effects_by_operator[op_id].size();
}

bool TransitionRewirer::has_conditional_effects(int op_id) const {
    assert(utils::in_bounds(op_id, conditional_effects_by_operator));
    return get_num_conditional_effects(op_id) > 0;
}

}// namespace cartesian_abstractions