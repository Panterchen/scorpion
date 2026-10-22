#include "transition_rewirer.h"

#include "abstract_state.h"
#include "transition.h"
#include "extension_strategy.h"
#include "utils.h"

#include "../task_utils/task_properties.h"

#include <algorithm>
#include <map>
#include <optional>

using namespace std;

namespace cartesian_abstractions {

static void remove_transitions_with_given_target(
    Transitions &transitions, int state_id) {
    erase_if(transitions, [state_id](const Transition &t) {
        return t.target_id == state_id;
    });
}

static void add_transition(
    deque<Transitions> &incoming, deque<Transitions> &outgoing, int src, int op,
    int dest) {
    assert(src != dest);
    assert(
        find(
            outgoing[src].begin(), outgoing[src].end(), Transition(op, dest)) ==
        outgoing[src].end());
    assert(
        find(
            incoming[dest].begin(), incoming[dest].end(),
            Transition(op, src)) == incoming[dest].end());
    outgoing[src].emplace_back(op, dest);
    incoming[dest].emplace_back(op, src);
}

static void add_loop(deque<Loops> &loops, int state_id, int op_id) {
    assert(utils::in_bounds(state_id, loops));
    loops[state_id].push_back(op_id);
}

TransitionRewirer::TransitionRewirer(const TaskProxy &task,
    const std::shared_ptr<ExtensionStrategy> &extension_strategy,
    const std::shared_ptr<const VariableDependencies> &variable_dependencies,
    bool verify_transitions_debug)
    : vars(task.get_variables()),
    extension_strategy_instance(extension_strategy->create(task)),
    preconditions_by_operator(compute_preconditions_by_operator(task.get_operators())),
    postconditions_by_operator(compute_postconditions_by_operator(task.get_operators())),
    variable_dependencies(variable_dependencies),
    verify_transitions_debug(verify_transitions_debug),
    task_has_axioms(task_properties::has_axioms(task)) {
}

void TransitionRewirer::rewire_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id, const AbstractState &v1,
    const AbstractState &v2, int var, const pair<bool, bool> &cons,
    utils::LogProxy &log) const {
    rewire_incoming_transitions(
        incoming, outgoing, states, v_id, v1, v2, var, cons, log);
    rewire_outgoing_transitions(
        incoming, outgoing, states, v_id, v1, v2, var, cons, log);
}

void TransitionRewirer::rewire_incoming_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id, const AbstractState &v1,
    const AbstractState &v2, int var, const pair<bool, bool> &cons,
    utils::LogProxy &log) const {
    /* State v has been split into v1 and v2. Now for all transitions
       u->v we need to add transitions u->v1, u->v2, or both.
       We check for conflicts on derived variables in all cases, since even
       for a split on a basic variable, a derived variable may depend on its
       value, and we may get different derived variable domains for v1 and v2
       after the split.
    */

    if (log.is_at_least_debug()) {
        log << "--- Rewire Incoming Transitions ---" << endl;
    }

    int v1_id = v1.get_id();
    int v2_id = v2.get_id();

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

        const AbstractState &u = *states[u_id];

        bool added_u_v1 = false;
        bool added_u_v2 = false;

        int post = UNDEFINED;
        // flag if var is derived or if any derived variables depend on it
        bool derived = vars[var].is_derived() ;
        bool derived_dependent = !variable_dependencies->get_transitive_dependents(var).empty();

        // Lazy memoized version of checking for derived conflicts:
        optional<bool> conflict_v1_cache, conflict_v2_cache;
        auto derived_conflict_v1 = [&]() {
            if (!conflict_v1_cache) {
                conflict_v1_cache = !cons.first || conflict_derived_domains(
                u.get_cartesian_set(), op_id, v1.get_cartesian_set(), var);
            }
            return *conflict_v1_cache;
        };
        auto derived_conflict_v2 = [&]() {
            if (!conflict_v2_cache) {
                conflict_v2_cache = !cons.second || conflict_derived_domains(
                u.get_cartesian_set(), op_id, v2.get_cartesian_set(), var);
            }
            return *conflict_v2_cache;
        };

        if (log.is_at_least_debug()) {
            log << " from state " << u_id << ": " << u.get_cartesian_set()
            << " via operator " << op_id << " post=" << post
            << " derived=" << derived
            << " conflict_v1=" << derived_conflict_v1()
            << " conflict_v2=" << derived_conflict_v2() << endl;
        }

        if (!derived) {
            post = get_postcondition_value(op_id, var);
        }

        if (derived) {
            // For derived variables, post is irrelevant (whether or not it is
            // UNDEFINED): both successors are possible unless ruled out by a
            // derived conflict.
            if (!derived_conflict_v1()) {
                add_transition(incoming, outgoing, u_id, op_id, v1_id);
                added_u_v1 = true;
            }
            if (!derived_conflict_v2()) {
                add_transition(incoming, outgoing, u_id, op_id, v2_id);
                added_u_v2 = true;
            }
        } else if (derived_dependent) { // split happened on a basic variable with derived dependents
            if (post == UNDEFINED) {
                // op has no precondition and no effect on var.
                bool u_and_v1_intersect = u.domain_subsets_intersect(v1, var);
                if (u_and_v1_intersect && !derived_conflict_v1()) {
                    add_transition(incoming, outgoing, u_id, op_id, v1_id);
                    added_u_v1 = true;
                }
                /* If u and v1 don't intersect, we must add the other transition
                   and can avoid an intersection test. */
                bool u_and_v2_possible =
                    !u_and_v1_intersect || u.domain_subsets_intersect(v2, var);
                if (u_and_v2_possible && !derived_conflict_v2()) {
                    add_transition(incoming, outgoing, u_id, op_id, v2_id);
                    added_u_v2 = true;
                }
            } else if (v1.contains(var, post)) {
                // op can only end in v1.
                if (!derived_conflict_v1()) {
                    add_transition(incoming, outgoing, u_id, op_id, v1_id);
                    added_u_v1 = true;
                }
            } else {
                // op can only end in v2.
                assert(v2.contains(var, post));
                if (!derived_conflict_v2()) {
                    add_transition(incoming, outgoing, u_id, op_id, v2_id);
                    added_u_v2 = true;
                }
            }
        }  else { // split on basic variable with no derived dependencies
            if (post == UNDEFINED) {
                // op has no precondition and no effect on var.
                bool u_and_v1_intersect = u.domain_subsets_intersect(v1, var);
                if (u_and_v1_intersect) {
                    add_transition(incoming, outgoing, u_id, op_id, v1_id);
                    added_u_v1 = true;
                }
                if (!u_and_v1_intersect || u.domain_subsets_intersect(v2, var)) {
                    add_transition(incoming, outgoing, u_id, op_id, v2_id);
                    added_u_v2 = true;
                }
            } else if (v1.contains(var, post)) {
                add_transition(incoming, outgoing, u_id, op_id, v1_id);
                added_u_v1 = true;
            } else {
                assert(v2.contains(var, post));
                add_transition(incoming, outgoing, u_id, op_id, v2_id);
                added_u_v2 = true;
            }
        }
        if (verify_transitions_debug) {
            verify_rewiring_incoming(
                u, v1, v2, op_id, added_u_v1, added_u_v2, cons);
        }
        if (log.is_at_least_debug()) {
            if (added_u_v1) log << "Added u -> v1" << endl;
            if (added_u_v2) log << "Added u -> v2" << endl;
        }
    }
}

void TransitionRewirer::rewire_outgoing_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id, const AbstractState &v1,
    const AbstractState &v2, int var, const pair<bool, bool> &cons,
    utils::LogProxy &log) const {
    /* State v has been split into v1 and v2. Now for all transitions
       v->w we need to add transitions v1->w, v2->w, or both. */

    if (log.is_at_least_debug()) {
        log << "--- Rewire Outgoing Transitions ---" << endl;
    }

    int v1_id = v1.get_id();
    int v2_id = v2.get_id();

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

        const AbstractState &w = *states[w_id];

        bool added_v1_w = false;
        bool added_v2_w = false;

        int pre = get_precondition_value(op_id, var);
        int post = get_postcondition_value(op_id, var);
        
        bool derived = vars[var].is_derived(); // check if var is derived
        bool derived_dependent = !variable_dependencies->get_transitive_dependents(var).empty();

        // Lazy memoized calculation of derived conflict on either v1, v2
        // if v1 or v2 have an inapplicability conflict with a derived precondition variable
        // if v1 or v2 have a conflict on the derived postconditions with w
        optional<bool> conflict_v1_cache, conflict_v2_cache;
        auto derived_conflict_v1 = [&] {
            if (!conflict_v1_cache) {
                conflict_v1_cache = !cons.first ||
                    precondition_derived_conflict(v1.get_cartesian_set(), op_id, var) ||
                        conflict_derived_domains(v1.get_cartesian_set(), op_id, w.get_cartesian_set(), var);
            }
            return *conflict_v1_cache;
        };
        auto derived_conflict_v2 = [&] {
            if (!conflict_v2_cache) {
                conflict_v2_cache = !cons.second ||
                    precondition_derived_conflict(v2.get_cartesian_set(), op_id, var) ||
                        conflict_derived_domains(v2.get_cartesian_set(), op_id, w.get_cartesian_set(), var);
            }
            return *conflict_v2_cache;
        };

        if (log.is_at_least_debug()) {
            log << " via operator " << op_id << " -> " << w_id
            << " : " << w.get_cartesian_set()
            << "pre=" << pre << " post=" << post
            << " derived=" << derived
            // << " conflict_v1 (pre)=" << pre_derived_conflict_v1
            // << " conflict_v2 (pre)=" << pre_derived_conflict_v2
            << " conflict_v1=" << derived_conflict_v1()
            << " conflict_v2" << derived_conflict_v2() << endl;
        }

        if (derived) { // variable is derived
            // post is irrelevant for derived variables, but pre still is.
            if (pre == UNDEFINED) {
                if (!derived_conflict_v1()) {
                    add_transition(incoming, outgoing, v1_id, op_id, w_id);
                    added_v1_w = true;
                }
                if (!derived_conflict_v2()) {
                    add_transition(incoming, outgoing, v2_id, op_id, w_id);
                    added_v2_w = true;
                }
            } else if (v1.contains(var, pre)) {
                // op can only start in v1.
                if (!derived_conflict_v1()) {
                    add_transition(incoming, outgoing, v1_id, op_id, w_id);
                    added_v1_w = true;
                }
            } else {
                // op can only start in v2.
                assert(v2.contains(var, pre));
                if (!derived_conflict_v2()) {
                    add_transition(incoming, outgoing, v2_id, op_id, w_id);
                    added_v2_w = true;
                }
            }
        } else if (derived_dependent) { // variable not derived, but part of some axiom bodies
            if (post == UNDEFINED) {
                assert(pre == UNDEFINED);
                // op has no precondition and no effect on var.
                bool v1_and_w_intersect = v1.domain_subsets_intersect(w, var);
                if (v1_and_w_intersect && !derived_conflict_v1()) {
                    add_transition(incoming, outgoing, v1_id, op_id, w_id);
                    added_v1_w = true;
                }
                /* If v1 and w don't intersect, we must add the other transition
                and can avoid an intersection test. */
                if ((!v1_and_w_intersect || v2.domain_subsets_intersect(w, var))
                    && !derived_conflict_v2()) {
                    add_transition(incoming, outgoing, v2_id, op_id, w_id);
                    added_v2_w = true;
                }
            } else if (pre == UNDEFINED) {
                // op has no precondition, but an effect on var.
                if (!derived_conflict_v1()){
                    add_transition(incoming, outgoing, v1_id, op_id, w_id);
                    added_v1_w = true;
                }
                if (!derived_conflict_v2()){
                    add_transition(incoming, outgoing, v2_id, op_id, w_id);
                    added_v2_w = true;
                }
            } else if (v1.contains(var, pre)) {
                // op can only start in v1.
                if (!derived_conflict_v1()){
                    add_transition(incoming, outgoing, v1_id, op_id, w_id);
                    added_v1_w = true;
                }
            } else {
                // op can only start in v2.
                assert(v2.contains(var, pre));
                if (!derived_conflict_v2()){
                    add_transition(incoming, outgoing, v2_id, op_id, w_id);
                    added_v2_w = true;
                }
            }
        } else { // basic case
            assert(!derived_conflict_v1() && !derived_conflict_v2());
            if (post == UNDEFINED) {
                assert(pre == UNDEFINED);
                // op has no precondition and no effect on var.
                bool v1_and_w_intersect = v1.domain_subsets_intersect(w, var);
                if (v1_and_w_intersect) {
                    add_transition(incoming, outgoing, v1_id, op_id, w_id);
                    added_v1_w = true;
                }
                /* If v1 and w don't intersect, we must add the other transition
                and can avoid an intersection test. */
                if (!v1_and_w_intersect || v2.domain_subsets_intersect(w, var)) {
                    add_transition(incoming, outgoing, v2_id, op_id, w_id);
                    added_v2_w = true;
                }
            } else if (pre == UNDEFINED) {
                // op has no precondition, but an effect on var.
                add_transition(incoming, outgoing, v1_id, op_id, w_id);
                added_v1_w = true;
                add_transition(incoming, outgoing, v2_id, op_id, w_id);
                added_v2_w = true;
            } else if (v1.contains(var, pre)) {
                // op can only start in v1.
                add_transition(incoming, outgoing, v1_id, op_id, w_id);
                added_v1_w = true;
            } else {
                // op can only start in v2.
                assert(v2.contains(var, pre));
                add_transition(incoming, outgoing, v2_id, op_id, w_id);
                added_v2_w = true;
            }
        }
        if (verify_transitions_debug) {
            verify_rewiring_outgoing(
                w, v1, v2, op_id, added_v1_w, added_v2_w, cons);
        }
        if (log.is_at_least_debug()) {
            if (added_v1_w) log << "Added v1 -> w" << endl;
            if (added_v2_w) log << "Added v2 -> w" << endl;
        }
    }
}

void TransitionRewirer::rewire_loops(
    deque<Loops> &loops, deque<Transitions> &incoming,
    deque<Transitions> &outgoing, int v_id, const AbstractState &v1,
    const AbstractState &v2, int var, const pair<bool, bool> &cons,
    utils::LogProxy &log) const {

    Loops old_loops = move(loops[v_id]);
    assert(loops[v_id].empty());

    /* State v has been split into v1 and v2. Now for all self-loops
       v->v we need to add one or two of the transitions v1->v1, v1->v2,
       v2->v1 and v2->v2. */

    if (log.is_at_least_debug()) {
        log << "--- Rewire Loops ---" << endl;
    }

    int v1_id = v1.get_id();
    int v2_id = v2.get_id();
    for (int op_id : old_loops) {
        bool added_loop_v1 = false;
        bool added_loop_v2 = false;
        bool added_v1_v2 = false;
        bool added_v2_v1 = false;

        int pre = get_precondition_value(op_id, var);

        int post = UNDEFINED;
        bool derived = vars[var].is_derived(); // check if var is derived
        bool derived_dependent = !variable_dependencies->get_transitive_dependents(var).empty();

        if (!derived) {
            post = get_postcondition_value(op_id, var);
        }
        // conflicts, derived variable value is the same for v1 and v2, only var which is basic differs
        // consider conflicts on preconditions (if there are preconditions on derived variables that could
        // depend on the changed basic variable.
        // Lazy, memoized: each of the four conflict_* lambdas below pulls
        // from pre_conflict_v1()/pre_conflict_v2() only if it needs them,
        // and conflict_derived_domains() only runs once per lambda, only
        // for the lambdas a given branch actually calls -- most branches
        // below only ever touch 1 or 2 of the 4.
        optional<bool> pre_conflict_v1_cache, pre_conflict_v2_cache;
        auto pre_conflict_v1 = [&]() {
            if (!pre_conflict_v1_cache) {
                pre_conflict_v1_cache =
                    precondition_derived_conflict(v1.get_cartesian_set(), op_id, var);
            }
            return *pre_conflict_v1_cache;
        };
        auto pre_conflict_v2 = [&]() {
            if (!pre_conflict_v2_cache) {
                pre_conflict_v2_cache =
                    precondition_derived_conflict(v2.get_cartesian_set(), op_id, var);
            }
            return *pre_conflict_v2_cache;
        };
        // v1 and v2 are the same except for domain of basic variable.
        // for conflict(a,o,b) we consider the basic variable domains of a and
        // derived variable values of b
        optional<bool> conflict_v1_cache, conflict_v2_cache;
        optional<bool> conflict_v1_to_v2_cache, conflict_v2_to_v1_cache;
        auto derived_conflict_v1 = [&]() {
            if (!conflict_v1_cache) {
                conflict_v1_cache = !cons.first || pre_conflict_v1() ||
                    conflict_derived_domains(v1.get_cartesian_set(), op_id, v1.get_cartesian_set(), var);
            }
            return *conflict_v1_cache;
        };
        auto derived_conflict_v2 = [&]() {
            if (!conflict_v2_cache) {
                conflict_v2_cache = !cons.second || pre_conflict_v2() ||
                    conflict_derived_domains(v2.get_cartesian_set(), op_id, v2.get_cartesian_set(), var);
            }
            return *conflict_v2_cache;
        };
        auto derived_conflict_v1_to_v2 = [&]() {
            if (!conflict_v1_to_v2_cache) {
                conflict_v1_to_v2_cache = !cons.first || !cons.second || pre_conflict_v1() ||
                    conflict_derived_domains(v1.get_cartesian_set(), op_id, v2.get_cartesian_set(), var);
            }
            return *conflict_v1_to_v2_cache;
        };
        auto derived_conflict_v2_to_v1 = [&]() {
            if (!conflict_v2_to_v1_cache) {
                conflict_v2_to_v1_cache = !cons.first || !cons.second || pre_conflict_v2() ||
                    conflict_derived_domains(v2.get_cartesian_set(), op_id, v1.get_cartesian_set(), var);
            }
            return *conflict_v2_to_v1_cache;
        };

        if (log.is_at_least_debug()) {
            log << "  loop op=" << op_id
                << " pre=" << pre << " post=" << post
                << " derived=" << derived
                << " conflict_v1=" << derived_conflict_v1()
                << " conflict_v2=" << derived_conflict_v2()
                << " conflict_v1->v2=" << derived_conflict_v1_to_v2()
                << " conflict_v2->v1=" << derived_conflict_v2_to_v1() << endl;
        }

        if (derived) {
            if (pre == UNDEFINED) {
                // post is irrelevant: derived values can change even without an effect
                // on var, so all four combinations are possible, subject to conflicts.
                if (!derived_conflict_v1()) {
                    add_loop(loops, v1_id, op_id);
                    added_loop_v1 = true;
                }
                if (!derived_conflict_v2()) {
                    add_loop(loops, v2_id, op_id);
                    added_loop_v2 = true;
                }
                if (!derived_conflict_v1_to_v2()) {
                    add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                    added_v1_v2 = true;
                }
                if (!derived_conflict_v2_to_v1()) {
                    add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                    added_v2_v1 = true;
                }
            } else if (v1.contains(var, pre)) {
                // op must start in v1.
                if (!derived_conflict_v1()) {
                    add_loop(loops, v1_id, op_id);
                    added_loop_v1 = true;
                }
                if (!derived_conflict_v1_to_v2()) {
                    add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                    added_v1_v2 = true;
                }
            } else {
                // op must start in v2.
                assert(v2.contains(var, pre));
                if (!derived_conflict_v2()) {
                    add_loop(loops, v2_id, op_id);
                    added_loop_v2 = true;
                }
                if (!derived_conflict_v2_to_v1()) {
                    add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                    added_v2_v1 = true;
                }
            }
        } else if (derived_dependent) {
            // var is not derived, but conflicts are still possible.
            if (pre == UNDEFINED) {
                if (post == UNDEFINED) {
                    // loops possible, not transitions if no derived conflict
                    if (!derived_conflict_v1()) {
                        add_loop(loops, v1_id, op_id);
                        added_loop_v1 = true;
                    }
                    if (!derived_conflict_v2()) {
                        add_loop(loops, v2_id, op_id);
                        added_loop_v2 = true;
                    }
                } else {
                    if (v1.contains(var, post)) { // op must end in v1.
                        if (!derived_conflict_v1()) {
                            add_loop(loops, v1_id, op_id);
                            added_loop_v1 = true;
                        }
                        if (!derived_conflict_v2_to_v1()) {
                            add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                            added_v2_v1 = true;
                        }
                    } else { // op must end in v2.
                        assert(v2.contains(var, post));
                        if (!derived_conflict_v1_to_v2()) {
                            add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                            added_v1_v2 = true;
                        }
                        if (!derived_conflict_v2()) {
                            add_loop(loops, v2_id, op_id);
                            added_loop_v2 = true;
                        }
                    }
                }
            } else if (post == UNDEFINED) { // precondition but no effect on var
                if (v1.contains(var, pre)) { // op must start in v1
                    if (!derived_conflict_v1()) {
                        add_loop(loops, v1_id, op_id);
                        added_loop_v1 = true;
                    }
                } else { // op must start in v2
                    assert(v2.contains(var, pre));
                    if (!derived_conflict_v2()) {
                        add_loop(loops, v2_id, op_id);
                        added_loop_v2 = true;
                    }
                }
            } else { // precondition and effect on var
                if (v1.contains(var, pre)) { // op must start in v1
                    if (v1.contains(var, post)) {
                        if (!derived_conflict_v1()) {
                            add_loop(loops, v1_id, op_id);
                            added_loop_v1 = true;
                        }
                    } else {
                        assert(v2.contains(var, post));
                        if (!derived_conflict_v1_to_v2()) {
                            add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                            added_v1_v2 = true;
                        }
                    }
                } else { // op must start in v2
                    assert(v2.contains(var, pre));
                    if (v1.contains(var, post)) {
                        if (!derived_conflict_v2_to_v1()) {
                            add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                            added_v2_v1 = true;
                        }
                    } else {
                        assert(v2.contains(var, post));
                        if (!derived_conflict_v2()) {
                            add_loop(loops, v2_id, op_id);
                            added_loop_v2 = true;
                        }
                    }
                }
            }
        } else { // base case, no derived variables affected
            assert(!derived_conflict_v1() && !derived_conflict_v2() &&
                !derived_conflict_v1_to_v2() && !derived_conflict_v2_to_v1());
            if (pre == UNDEFINED) {
                // op has no precondition on var -> must start in v1 and v2
                if (post == UNDEFINED) {
                    // op has no effect on var -> only loops possible
                    add_loop(loops, v1_id, op_id);
                    added_loop_v1 = true;
                    add_loop(loops, v2_id, op_id);
                    added_loop_v2 = true;
                } else {
                    // op has postcondition on var
                    if (v2.contains(var, post)) {
                        // op ends in v2
                        add_loop(loops, v2_id, op_id);
                        added_loop_v2 = true;
                        add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                        added_v1_v2 = true;
                    } else {
                        // op ends in v1
                        assert(v1.contains(var, post));
                        add_loop(loops, v1_id, op_id);
                        added_loop_v1 = true;
                        add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                        added_v2_v1 = true;
                    }
                }
            } else if (v1.contains(var, pre)) { // op must start in v1
                if (v1.contains(var, post)) {
                    add_loop(loops, v1_id, op_id);
                    added_loop_v1 = true;
                } else {
                    assert(v2.contains(var, post));
                    add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                    added_v1_v2 = true;
                }
            } else { // op must start in v2
                assert(v2.contains(var, pre));
                if (v1.contains(var, post)) {
                    add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                    added_v2_v1 = true;
                } else {
                    assert(v2.contains(var, post));
                    add_loop(loops, v2_id, op_id);
                    added_loop_v2 = true;
                }
            }
        }

        if (verify_transitions_debug) {
            verify_rewiring_loops(
                v1, v2, op_id, added_loop_v1, added_loop_v2, added_v1_v2,
                added_v2_v1, cons);
        }
        if (log.is_at_least_debug()) {
            if (added_loop_v1) log << "Added loop v1" << endl;
            if (added_loop_v2) log << "Added loop v2" << endl;
            if (added_v1_v2) log << "Added v1 -> v2" << endl;
            if (added_v2_v1) log << "Added v2 -> v1" << endl;
        }
    }
}

int TransitionRewirer::get_precondition_value(int op_id, int var) const {
    return lookup_value(preconditions_by_operator[op_id], var);
}

int TransitionRewirer::get_postcondition_value(int op_id, int var) const {
    return lookup_value(postconditions_by_operator[op_id], var);
}

int TransitionRewirer::get_num_operators() const {
    return preconditions_by_operator.size();
}

CartesianSet TransitionRewirer::update_cartesian_set(const CartesianSet &a, int op_id) const {
    CartesianSet result = a;   
    // update cartesian set with operator postconditions
    for (VariableProxy var : vars){
        if (var.is_derived()){
            result.add_all(var.get_id());
        } else if (get_postcondition_value(op_id, var.get_id()) != UNDEFINED){
            // post condition is the only value for var that can be true after applying operator
            result.set_single_value(var.get_id(), get_postcondition_value(op_id, var.get_id()));
        }
    }
    return result;
}

CartesianSet TransitionRewirer::intersect_cartesian_sets_on_basics(const CartesianSet &a,
    const CartesianSet &b) const {
    CartesianSet result = a;
    for (VariableProxy var : vars) {
        if (!var.is_derived()) {
            vector<int> values = a.get_intersection_values(b, var.get_id());
            result.remove_all(var.get_id());
            for (int val : values) {
                result.add(var.get_id(), val);
            }
        }
    }
    return result;
}

void TransitionRewirer::intersect_on_basics_inplace(
    CartesianSet &a, const CartesianSet &b) const {
    for (VariableProxy v : vars) {
        if (!v.is_derived()) {
            a.intersect_var(b, v.get_id()); // TODO
        }
    }
}

bool TransitionRewirer::conflict_derived_domains(
    const CartesianSet &a, int op_id, const CartesianSet &b, int var_id) const {
    // TODO: can this be made more efficient, if we only consider variables that are affected by the split?
    // instead of calculating the full extension, just calculate the conflicts for the variables that are actually affected
    // (i.e. the derived variables depending on the split variable if its basic, of the split variable itself if its derived)
    if (!task_has_axioms) {
        return false;
    }
    if (!vars[var_id].is_derived() && get_var_dependencies(var_id).second.empty()) {
         return false;
    }

    CartesianSet a_updated = update_cartesian_set(a, op_id);

    intersect_on_basics_inplace(a_updated, b);

    CartesianSet extended_a_o = extension_strategy_instance->get_extension(a_updated);
    if (vars[var_id].is_derived()) {  // split variable is derived, check conflict for the variable
        if (!extended_a_o.intersects(b, var_id)) {
            return true;
        }
    }
    for (int var : variable_dependencies->get_transitive_dependents(var_id)) {
        // iterate over all variables appearing in the head of an axiom with var_id in the body
        // check conflict for all these variables affected by the split
        if (vars[var].is_derived() && !extended_a_o.intersects(b, var)) {
            return true;
        }
    }
    return false;   
}

bool TransitionRewirer::precondition_derived_conflict(
    const CartesianSet &a, int op_id, int var_id) const {
    /*
     * Returns true if any derived variable precondition of op is not satisfiable
     * in the extension of a, i.e. if the basic variable domains of a don't
     * support the precondition value required for a derived variable and op
     * is inapplicable in a.
     */
    if (!task_has_axioms) {
        return false;
    }
    // check if operator has derived preconditions and if the derived variables
    // in these preconditions depend on the split variable
    bool has_derived_pre = false;
    vector<int> aff_vars = variable_dependencies->get_transitive_dependents(var_id);
    vector<FactPair> der_pre;
    for (const FactPair &pre : preconditions_by_operator[op_id]) {
        if (vars[pre.var].is_derived() && find(aff_vars.begin(), aff_vars.end(), pre.var) != aff_vars.end()) {
            der_pre.push_back(pre);
            has_derived_pre = true;
        }
    }
    // Fast Path for op without preconditions on derived variables
    if (!has_derived_pre) {
        return false;
    }

    // Technically we need to test against the extension to see if the precondition
    // value is allowed inside extension, but since we already use consistency
    // check as pre-filter before every reaching this point, it is sufficient to
    // check against the original set. This will possibly introduce some
    // applicability flaws, but they can be filtered out easily.
    for (const FactPair &pre : der_pre) {
        if (!a.test(pre.var, pre.value)) {
            return true;
        }
    }
    return false;
}

std::pair<bool, bool> TransitionRewirer::consistency_check(
    const AbstractState &v1, const AbstractState &v2, const int var) const {
    /*  checks two abstract states v1 and v2 for consistency
     *  w.r.t. the set of axioms in the given task. If the extension of the
     *  respective Cartesian set contains an empty variable domain for one of
     *  the derived variables, then the abstract state is inconsistent.
     *  The function returns a pair of bools <consistency_v1, consistency_v2>
     *  where consistency_vx is true if the state vx is consistent, and false
     *  else.
     */
    // we only need to check if the new states are still axiom consistent if
    // the task contains axioms and the split variable is a) derived or
    // b) has derived variables depending on it.
    // We assume that previously discovered inconsistent states get disconnected
    // from the transition systems and are never split themselves.
    bool derived_dep = vars[var].is_derived() || !variable_dependencies->get_transitive_dependents(var).empty();
    if (!task_has_axioms || !derived_dep) {
        return make_pair(true, true);
    }

    bool v1_consistent = true;
    bool v2_consistent = true;
    std::vector< int > derived_vars;
    for (VariableProxy var : vars) {
        if (var.is_derived()) {
            derived_vars.push_back(var.get_id());
        }
    }
    if (!derived_vars.empty()) {
        // validate the consistency of v1 and v2 w.r.t. to derived variables
        CartesianSet ext_v1 = extension_strategy_instance->get_precise_extension(v1.get_cartesian_set());
        CartesianSet ext_v2 = extension_strategy_instance->get_precise_extension(v2.get_cartesian_set());
        std::vector<int> v1_empty_dom;
        std::vector<int> v2_empty_dom;
        for (int var : derived_vars) {
            if (ext_v1.get_values(var).empty()) {
                v1_consistent = false;
                v1_empty_dom.push_back(var);
            }
            if (ext_v2.get_values(var).empty()) {
                v2_consistent = false;
                v2_empty_dom.push_back(var);
            }
        }
    }

    return std::pair<bool, bool>(v1_consistent, v2_consistent);
}

bool TransitionRewirer::is_non_induced(
    const AbstractState &source, int op_id, const AbstractState &target) const {
    /*
        For tasks with axioms, there can be non-induced transitions resulting
        from an imprecise overapproximation on the derived variables.
        We use precise extension to check:
        1) every derived precondition of op is possible in ext(source)
        2) derived variable after applying op must be compatible with the
            derived domains of target.
        Both tests are overapproximations, failing one proves that no state
        induces the transition
     */
    if (!task_has_axioms) {
        return false;
    }

    CartesianSet ext_src = extension_strategy_instance->get_precise_extension(
        source.get_cartesian_set());
    for (const FactPair &pre : preconditions_by_operator[op_id]) {
        if (vars[pre.var].is_derived() && !ext_src.test(pre.var, pre.value)) {
            return true;
        }
    }

    CartesianSet updated = update_cartesian_set(source.get_cartesian_set(), op_id);
    // updated = intersect_cartesian_sets_on_basics(updated, target.get_cartesian_set());
    intersect_on_basics_inplace(updated, target.get_cartesian_set());
    CartesianSet ext_post = extension_strategy_instance->get_precise_extension(updated);
    for (VariableProxy var : vars) {
        if (var.is_derived() &&
            !ext_post.intersects(target.get_cartesian_set(), var.get_id())) {
            return true;
        }
    }
    return false;
}

bool TransitionRewirer::explicit_transition_check(
    const AbstractState &source, const AbstractState &target,
    const int op) const {
        /*
     *  Literal implementation of the CheckTransition(a, o, b) algorithm.
     *  For every basic variable: precondition / postcondition / no-effect
     *  domain-intersection checks. For EVERY derived variable -- not just
     *  ones reachable from var_id -- a precondition check against a, and a
     *  re-derived domain check against b via the extension of
     *  (a|_basic updated by post(o)) intersected with b|_basic.
     *
     *  Unlike the previous version of this function (which delegated to
     *  precondition_derived_conflict/conflict_derived_domains scoped to
     *  var_id), this does NOT assume the transition was already valid
     *  pre-split for derived variables unrelated to the split variable --
     *  it re-checks all of them from scratch, matching the pseudocode's
     *  unconditional "for each v in derivedvars" loop. This makes it a
     *  genuine ground-truth reference for verify_transitions_debug,
     *  independent of the var_id-scoped optimization used elsewhere.
     */

    for (const VariableProxy &var : vars) {
        if (var.is_derived()) {
            continue;
        }
        int v = var.get_id();
        int pre_val = get_precondition_value(op, v);
        int post_val = get_postcondition_value(op, v);
        if (pre_val != UNDEFINED && !source.contains(v, pre_val)) {
            return false;
        }
        if (post_val != UNDEFINED && !target.contains(v, post_val)) {
            return false;
        }
        if (post_val == UNDEFINED &&
            !source.get_cartesian_set().intersects(target.get_cartesian_set(), v)) {
            return false;
        }
    }

    // (a|_basic updated by post(o)) intersected with b|_basic, then
    // extended -- this is the same core computation conflict_derived_domains
    // does, just performed once here for every derived variable instead of
    // being scoped to var_id's dependents.
    CartesianSet updated = update_cartesian_set(source.get_cartesian_set(), op);
    intersect_on_basics_inplace(updated, target.get_cartesian_set());
    // updated = intersect_cartesian_sets_on_basics(updated, target.get_cartesian_set());
    CartesianSet extended = extension_strategy_instance->get_extension(updated);

    for (const VariableProxy &var : vars) {
        if (!var.is_derived()) {
            continue;
        }
        int v = var.get_id();
        int pre_val = get_precondition_value(op, v);
        if (pre_val != UNDEFINED && !source.contains(v, pre_val)) {
            return false;
        }
        if (!extended.intersects(target.get_cartesian_set(), v)) {
            return false;
        }
    }

    return true;

}

void TransitionRewirer::verify_rewiring_incoming(
    const AbstractState &u, const AbstractState &v1, const AbstractState &v2,
    int op_id, bool added_u_v1, bool added_u_v2, pair<bool, bool> cons) const {
    bool valid_u_v1 = explicit_transition_check(u, v1, op_id);
    bool valid_u_v2 = explicit_transition_check(u, v2, op_id);
    if (valid_u_v1 != added_u_v1) {
        std::cout << "u (" << u.get_id() << ") -- " << op_id << "--> v1 ("
        << v1.get_id() << ", consistent: " << cons.first << "), added : "
        << added_u_v1 << "; valid : " << valid_u_v1 << std::endl;
    }
    if (valid_u_v2 != added_u_v2) {
        std::cout << "u (" << u.get_id() << ") -- " << op_id << "--> v2 ("
        << v2.get_id() << ", consistent: " << cons.second << "), added : "
        << added_u_v2 << "; valid : " << valid_u_v2 << std::endl;
    }
}
void TransitionRewirer::verify_rewiring_outgoing(
    const AbstractState &w, const AbstractState &v1, const AbstractState &v2,
    int op_id, bool added_v1_w, bool added_v2_w, pair<bool, bool> cons) const {
    bool valid_v1_w = explicit_transition_check(v1, w, op_id);
    bool valid_v2_w = explicit_transition_check(v2, w, op_id);
    if (valid_v1_w != added_v1_w) {
        std::cout << "v1 (" << v1.get_id() << ", consistent: " << cons.first
        << ") -- " << op_id << "-->  w ("  << w.get_id()  << "), added : "
        << added_v1_w << "; valid : " << valid_v1_w << std::endl;
    }
    if (valid_v2_w != added_v2_w) {
        std::cout << "v2 (" << v2.get_id() << ", consistent: " << cons.second
        << ") -- " << op_id << "-->  w ("  << w.get_id()  << "), added : "
        << added_v2_w << "; valid : " << valid_v2_w << std::endl;
    }
}
void TransitionRewirer::verify_rewiring_loops(
    const AbstractState &v1, const AbstractState &v2, int op_id,
    bool added_loop_v1, bool added_loop_v2, bool added_v1_v2, bool added_v2_v1,
    pair<bool, bool> cons) const {
    bool valid_v1 = explicit_transition_check(v1, v1, op_id);
    bool valid_v2 = explicit_transition_check(v2, v2, op_id);
    bool valid_v1_v2 = explicit_transition_check(v1, v2, op_id);
    bool valid_v2_v1 = explicit_transition_check(v2, v1, op_id);
    if (valid_v1 != added_loop_v1) {
        std::cout << "v1 (" << v1.get_id() << ", consistent: " << cons.first
        << ") loop " << op_id << ", added : "
        << added_loop_v1 << "; valid : " << valid_v1 << std::endl;
    }
    if (valid_v2 != added_loop_v2) {
        std::cout << "v2 (" << v2.get_id() << ", consistent: " << cons.second
        << ") loop " << op_id << ", added : "
        << added_loop_v2 << "; valid : " << valid_v2 << std::endl;
    }
    if (valid_v1_v2 != added_v1_v2) {
        std::cout << "v1 (" << v1.get_id() << ", consistent: " << cons.first
        << ") -- " << op_id << "-->  v2 ("  << v2.get_id()
        << ", consistent: " << cons.second << "), added : "
        << added_v1_v2 << "; valid : " << valid_v1_v2 << std::endl;
    }
    if (valid_v2_v1 != added_v2_v1) {
        std::cout << "v2 (" << v2.get_id() << ", consistent: " << cons.second
        << ") -- " << op_id << "-->  v1 ("  << v1.get_id()
        << ", consistent: " << cons.first << "), added : "
        << added_v2_v1 << "; valid : " << valid_v2_v1 << std::endl;
    }
}

}