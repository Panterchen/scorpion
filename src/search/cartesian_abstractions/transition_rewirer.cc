#include "transition_rewirer.h"

#include "abstract_state.h"
#include "transition.h"
#include "extension_strategy.h"
#include "regression_strategy.h"
#include "utils.h"

#include "../task_utils/task_properties.h"

#include <algorithm>
#include <map>

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
    //std::cout << "added transition from " << src << " to " << dest << " via " << op << std::endl;     // TODO : remove log later
}

static void add_loop(deque<Loops> &loops, int state_id, int op_id) {
    assert(utils::in_bounds(state_id, loops));
    loops[state_id].push_back(op_id);
    //std::cout << "added loop on " << state_id << " with op " << op_id << std::endl;     // TODO : remove log later
}

TransitionRewirer::TransitionRewirer(const TaskProxy &task,
    const std::shared_ptr<ExtensionStrategy> &extension_strategy,
    const std::shared_ptr<RegressionStrategy> &regression_strategy)
    : vars(task.get_variables()), extension_strategy_instance(extension_strategy->create(task)),
    regression_strategy_instance(regression_strategy->create(task, extension_strategy)),
    preconditions_by_operator(compute_preconditions_by_operator(task.get_operators())),
    postconditions_by_operator(compute_postconditions_by_operator(task.get_operators())){
}

void TransitionRewirer::rewire_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id, const AbstractState &v1,
    const AbstractState &v2, int var) const {
    // if task contains derived variables, check if both new states are still
    // consistent after extension and remove all the incoming / outgoing transitions
    // to them if not.
    // TODO : remove log later
    // std::cout << "Rewire Transitions for state " << v_id << " into states " << v1.get_id() << ", " << v2.get_id() << std::endl;
    rewire_incoming_transitions(incoming, outgoing, states, v_id, v1, v2,
        var);
    rewire_outgoing_transitions(incoming, outgoing, states, v_id, v1, v2,
        var);
}

void TransitionRewirer::rewire_incoming_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id, const AbstractState &v1,
    const AbstractState &v2, int var) const {
    /* State v has been split into v1 and v2. Now for all transitions
       u->v we need to add transitions u->v1, u->v2, or both.
       We check for conflicts on derived variables in all cases, since even
       for a split on a basic variable, a derived variable may depend on its
       value, and we may get different derived variable domains for v1 and v2
       after the split.
    */
    bool debug_output = false; // TODO: pass as parameter

    int v1_id = v1.get_id();
    int v2_id = v2.get_id();

    pair<bool, bool> cons = consistency_check(v1, v2);

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

        // cout << "Rewiring incoming transition for op " << op_id << " from state " << u_id << endl;

        const AbstractState &u = *states[u_id];

        // bool u_v1_valid = cons.first && explicit_transition_check(u, v1, op_id);
        // bool u_v2_valid = cons.second && explicit_transition_check(u, v2, op_id);
        bool added_u_v1 = false;
        bool added_u_v2 = false;

        int post = UNDEFINED;
        bool derived = vars[var].is_derived(); // check if var is derived
        bool derived_conflict_v1 = !cons.first || conflict_derived_domains(u.get_cartesian_set(), op_id, v1.get_cartesian_set());
        bool derived_conflict_v2 = !cons.second || conflict_derived_domains(u.get_cartesian_set(), op_id, v2.get_cartesian_set());

        if (derived) {
            // determine derived variable value
            CartesianSet u_cs = update_cartesian_set(u.get_cartesian_set(), op_id);
            post = extension_strategy_instance->get_extension_value(u_cs, var);
        } else{
            // determine basic variable post value
            post = get_postcondition_value(op_id, var);
        }

        if (post == UNDEFINED) {
            // op has no precondition and no effect on var.
            bool u_and_v1_intersect = derived || u.domain_subsets_intersect(v1, var);
            if (u_and_v1_intersect && !derived_conflict_v1) {
                add_transition(incoming, outgoing, u_id, op_id, v1_id);
                added_u_v1 = true;
            }
            /* If u and v1 don't intersect, we must add the other transition
            and can avoid an intersection test. */
            if (!derived_conflict_v2 && (derived || !u_and_v1_intersect || u.domain_subsets_intersect(v2, var))) {
                add_transition(incoming, outgoing, u_id, op_id, v2_id);
                added_u_v2 = true;
            }
        } else {
            if (derived) {
                if (!derived_conflict_v1) {
                    add_transition(incoming, outgoing, u_id, op_id, v1_id);
                    added_u_v1 = true;
                }
                if (!derived_conflict_v2) {
                    add_transition(incoming, outgoing, u_id, op_id, v2_id);
                    added_u_v2 = true;
                }
            } else {
                if (v1.contains(var, post)) {
                    // op can only end in v1.
                    if (!derived_conflict_v1) {
                        add_transition(incoming, outgoing, u_id,
                            op_id, v1_id);
                        added_u_v1 = true;
                    }
                } else {
                    // op can only end in v2.
                    assert(v2.contains(var, post));
                    if (!derived_conflict_v2) {
                        add_transition(incoming, outgoing, u_id,
                            op_id, v2_id);
                        added_u_v2 = true;
                    }
                }
            }
        }
        if (debug_output) {
            verify_rewiring_incoming(u, v1, v2, op_id, added_u_v1, added_u_v2,
                cons);
        }
        /* if (added_u_v2 && !u_v2_valid) {
            std::cout << "Transition u (" << u_id << ") - op (" << op_id << ") -> v2 ("<< v2_id <<") invalid but added." << std::endl;
        }
        if (!added_u_v2 && u_v2_valid) {
            std::cout << "Transition u (" << u_id << ") - op (" << op_id << ") -> v2 ("<< v2_id <<") valid but not added." << std::endl;
        }
        if (added_u_v1 && !u_v1_valid) {
            std::cout << "Transition u (" << u_id << ") - op (" << op_id << ") -> v1 ("<< v1_id <<") invalid but added." << std::endl;
        }
        if (!added_u_v1 && u_v1_valid) {
            std::cout << "Transition u (" << u_id << ") - op (" << op_id << ") -> v1 ("<< v1_id <<") valid but not added." << std::endl;
        } */
        // bool mistake_v1 = ((added_u_v1 && !u_v1_valid) || (!added_u_v1 && u_v1_valid) );
        // bool mistake_v2 = ((added_u_v2 && !u_v2_valid) || (!added_u_v2 && u_v2_valid));
        // if (mistake_v1 || mistake_v2) {
        //     // print information about transition where stuff goes wrong...
        //     std::cout << "Variable: " << var << ", derived ? " << derived << ", postcondition[var]: " << post << std::endl;
        //     std::cout << "u cartesian set " << u.get_cartesian_set() << std::endl;
        //     if (mistake_v1) {
        //         std::cout << "v1 cartesian set " << v1.get_cartesian_set() << std::endl;
        //         std::cout << "derived conflict? " << derived_conflict_v1 << std::endl;
        //     }
        //     if (mistake_v2) {
        //         std::cout << "v2 cartesian set " << v2.get_cartesian_set() << std::endl;
        //         std::cout << "derived conflict? " << derived_conflict_v2 << std::endl;
        //     }
        // }
    }
}

void TransitionRewirer::rewire_outgoing_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id, const AbstractState &v1,
    const AbstractState &v2, int var) const {
    bool debug_output = false; // TODO: pass in as parameter
    /* State v has been split into v1 and v2. Now for all transitions
       v->w we need to add transitions v1->w, v2->w, or both. */
    int v1_id = v1.get_id();
    int v2_id = v2.get_id();

    pair<bool, bool> cons = consistency_check(v1, v2);

    // cout << "V1 (" << v1_id << ") consistent: " << cons.first << endl;
    // cout << "V2 (" << v2_id << ") consistent: " << cons.second << endl;

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

        // cout << "Rewiring outgoing transition for op " << op_id << " to state " << w_id << endl;

        const AbstractState &w = *states[w_id];

        //bool v1_w_valid = explicit_transition_check(v1, w, op_id);
        //bool v2_w_valid = explicit_transition_check(v2, w, op_id);

        bool added_v1_w = false;
        bool added_v2_w = false;

        /*
        if (check_transition_validity(v1, w, op_id)) {
            std::cout << "v1 -> w valid, v1: " << v1_id  << ", op: " << op_id << ", w: " << w_id << std::endl;
        }
        if (check_transition_validity(v2, w, op_id)) {
            std::cout << "v2 -> w valid, v2: " << v2_id << ", op: " << op_id << ", w: " << w_id << std::endl;
        }*/

        int pre = get_precondition_value(op_id, var);
        int post = get_postcondition_value(op_id, var);
        
        bool derived = vars[var].is_derived(); // check if var is derived
        // check if v1 or v2 have an inapplicability conflict with a derived precondition variable
        bool pre_derived_conflict_v1 = precondition_derived_conflict(v1.get_cartesian_set(), op_id);
        bool pre_derived_conflict_v2 = precondition_derived_conflict(v2.get_cartesian_set(), op_id);
        // std::cout << "precondition conflict v1 " << pre_derived_conflict_v1 << std::endl;
        // std::cout << "precondition conflict v2 " << pre_derived_conflict_v2 << std::endl;

        // check if v1 or v2 have a conflict on the derived postconditions with w
        bool derived_conflict_v1 = pre_derived_conflict_v1 || !cons.first ||
            conflict_derived_domains(v1.get_cartesian_set(), op_id, w.get_cartesian_set());
        bool derived_conflict_v2 = pre_derived_conflict_v2 || !cons.second ||
            conflict_derived_domains(v2.get_cartesian_set(), op_id, w.get_cartesian_set());

        // if (!derived) {
        //     derived_conflict_v1 = conflict_derived_domains(v1.get_cartesian_set(), op_id, w.get_cartesian_set());
        //     derived_conflict_v2 = conflict_derived_domains(v2.get_cartesian_set(), op_id, w.get_cartesian_set());
        // }
        // std::cout << "derived_conflict_v1 " << derived_conflict_v1 << std::endl;
        // std::cout << "derived_conflict_v2 " << derived_conflict_v2 << std::endl;
        //
        // cout << "Precondition value: " << pre << ", Postcondition value: " << post << endl;
        // cout << "var " << var << " is " << (derived ? "derived" : "basic") << endl;
        // if (cons.first) {
        //     cout << "abstract state v1: " << v1.get_cartesian_set() << endl;
        // } else {
        //     cout << "abstract state v1: " << "inconsistent" << endl;
        // }
        // if (cons.second) {
        //     cout << "abstract state v2: " << v2.get_cartesian_set() << endl;
        // } else {
        //     cout << "abstract state v2: " << "inconsistent" << endl;
        // }
        // cout << "abstract state v1: " << v1.get_cartesian_set() << ", abstract state v2: " << v2.get_cartesian_set() << endl;

        if (!derived && post == UNDEFINED) {
            assert(pre == UNDEFINED);
            // op has no precondition and no effect on var.
            bool v1_and_w_intersect = v1.domain_subsets_intersect(w, var);
            if (!derived_conflict_v1 && v1_and_w_intersect) {
                // if (!v1_w_valid) {
                //     std::cout << "Transition v1 (" << v1_id << ") - op (" << op_id << ") -> w ("<< w_id <<") invalid but added." << std::endl;
                // }
                add_transition(incoming, outgoing, v1_id, op_id, w_id);
                added_v1_w = true;
            }
            /* If v1 and w don't intersect, we must add the other transition
            and can avoid an intersection test. */
            if (!derived_conflict_v2 && (!v1_and_w_intersect || v2.domain_subsets_intersect(w, var))) {
                // if (!v2_w_valid) {
                //     std::cout << "Transition v2 (" << v2_id << ") - op (" << op_id << ") -> w ("<< w_id <<") invalid but added." << std::endl;
                // }
                add_transition(incoming, outgoing, v2_id, op_id, w_id);
                added_v2_w = true;
            }
        } else if (pre == UNDEFINED) {
            // op has no precondition, but an effect on var.
            if (!derived_conflict_v1){
                // if (!v1_w_valid) {
                //     std::cout << "Transition v1 (" << v1_id << ") - op (" << op_id << ") -> w ("<< w_id <<") invalid but added." << std::endl;
                // }
                add_transition(incoming, outgoing, v1_id, op_id, w_id);
                added_v1_w = true;
            }
            if (!derived_conflict_v2){
                // if (!v2_w_valid) {
                //     std::cout << "Transition v2 (" << v2_id << ") - op (" << op_id << ") -> w ("<< w_id <<") invalid but added." << std::endl;
                // }
                add_transition(incoming, outgoing, v2_id, op_id, w_id);
                added_v2_w = true;
            }
        } else if (v1.contains(var, pre)) {
            // op can only start in v1.
            if (!derived_conflict_v1){
                // if (!v1_w_valid) {
                //     std::cout << "Transition v1 (" << v1_id << ") - op (" << op_id << ") -> w ("<< w_id <<") invalid but added." << std::endl;
                // }
                add_transition(incoming, outgoing, v1_id, op_id, w_id);
                added_v1_w = true;
            }
        } else {
            // op can only start in v2.
            if (!derived_conflict_v2){
                // if (!v2_w_valid) {
                //     std::cout << "Transition v2 (" << v2_id << ") - op (" << op_id << ") -> w ("<< w_id <<") invalid but added." << std::endl;
                // }
                add_transition(incoming, outgoing, v2_id, op_id, w_id);
                added_v2_w = true;
            }
        }
        if (debug_output) {
            verify_rewiring_outgoing(w, v1, v2, op_id, added_v1_w, added_v2_w,
                cons);
        }
      /*if (added_v1_w && !v1_w_valid) {
            std::cout << "Transition v1 (" << v1_id << ") - op (" << op_id << ") -> w ("<< w_id <<") invalid but added." << std::endl;
        }
        if (!added_v1_w && v1_w_valid) {
            std::cout << "Transition v1 (" << v1_id << ") - op (" << op_id << ") -> w ("<< w_id <<") valid but not added." << std::endl;
        }
        if (added_v2_w && !v2_w_valid) {
            std::cout << "Transition v2 (" << v2_id << ") - op (" << op_id << ") -> w ("<< w_id <<") invalid but added." << std::endl;
        }
        if (!added_v2_w && v2_w_valid) {
            std::cout << "Transition v2 (" << v2_id << ") - op (" << op_id << ") -> w ("<< w_id <<") valid but not added." << std::endl;
        } */

    }
}

void TransitionRewirer::rewire_loops(
    deque<Loops> &loops, deque<Transitions> &incoming,
    deque<Transitions> &outgoing, int v_id, const AbstractState &v1,
    const AbstractState &v2, int var) const {

    bool debug_output = false; // TODO : eventually pass in as parameter
    pair<bool, bool> cons = consistency_check(v1, v2);

    // std::cout << "V1 " << v1.get_id() << " consistency: " << cons.first << std::endl;
    // std::cout << "V2 " << v2.get_id() << " consistency: " << cons.second << std::endl;

    Loops old_loops = move(loops[v_id]);
    assert(loops[v_id].empty());

    /* State v has been split into v1 and v2. Now for all self-loops
       v->v we need to add one or two of the transitions v1->v1, v1->v2,
       v2->v1 and v2->v2. */
    int v1_id = v1.get_id();
    int v2_id = v2.get_id();
    for (int op_id : old_loops) {
        //bool loop_v1_valid = explicit_transition_check(v1, v1, op_id);
        //bool loop_v2_valid = explicit_transition_check(v2, v2, op_id);
        //bool v1_v2_valid = explicit_transition_check(v1, v2, op_id);
        //bool v2_v1_valid = explicit_transition_check(v2, v1, op_id);

        bool added_loop_v1 = false;
        bool added_loop_v2 = false;
        bool added_v1_v2 = false;
        bool added_v2_v1 = false;

        int pre = get_precondition_value(op_id, var);

        int post = UNDEFINED;
        bool derived = vars[var].is_derived(); // check if var is derived
        bool derived_conflict_v1 = false || !cons.first;
        bool derived_conflict_v2 = false || !cons.second;
        bool derived_conflict_v1_to_v2 = false || !cons.first || !cons.second;
        bool derived_conflict_v2_to_v1 = false || !cons.second || !cons.first;

        if (derived) {
            // derived value is the same for both v1 and v2 
            // computation of derived value only relies on basic variables,
            // however v1 and v2 only differ in var which is derived
            CartesianSet v1_cs = update_cartesian_set(v1.get_cartesian_set(), op_id);
            post = extension_strategy_instance->get_extension_value(v1_cs, var);

        } else {
            // determine basic variable post value
            post = get_postcondition_value(op_id, var);
        }

        // conflicts, derived variable value is the same for v1 and v2, only var which is basic differs
        // consider conflicts on preconditions (if there are preconditions on derived variables that could
        // depend on the changed basic variable
        bool pre_conflict_v1 = precondition_derived_conflict(v1.get_cartesian_set(), op_id);
        bool pre_conflict_v2 = precondition_derived_conflict(v2.get_cartesian_set(), op_id);
        // std::cout << "precondition conflict v1" << pre_conflict_v1 << std::endl;
        // std::cout << "precondition conflict v2" << pre_conflict_v2 << std::endl;

        // v1 and v2 are the same except for domain of basic variable. for conflict(a,o,b) we consider the basic variable domains of a and derived variable values of b
        derived_conflict_v1 = pre_conflict_v1 || !cons.first ||
            conflict_derived_domains(v1.get_cartesian_set(), op_id, v1.get_cartesian_set());
        derived_conflict_v2 = pre_conflict_v2 || !cons.second ||
            conflict_derived_domains(v2.get_cartesian_set(), op_id, v2.get_cartesian_set());
        derived_conflict_v1_to_v2 = pre_conflict_v1 || !cons.first || !cons.second ||
            conflict_derived_domains(v1.get_cartesian_set(), op_id, v2.get_cartesian_set());
        derived_conflict_v2_to_v1 = pre_conflict_v2 || !cons.first || !cons.second ||
            conflict_derived_domains(v2.get_cartesian_set(), op_id, v1.get_cartesian_set());

        // std::cout << "derived_conflict_v1" << derived_conflict_v1 << std::endl;
        // std::cout << "derived_conflict_v2" << derived_conflict_v2 << std::endl;
        // std::cout << "derived_conflict_v1_to_v2" << derived_conflict_v1_to_v2 << std::endl;
        // std::cout << "derived_conflict_v2_to_v1" << derived_conflict_v2_to_v1 << std::endl;

        // cout << "Precondition value: " << pre << ", Postcondition value: " << post << endl;
        // cout << "var " << var << " is " << (derived ? "derived" : "basic") << endl;
        // if (cons.first) {
        //     cout << "abstract state v1: " << v1.get_cartesian_set() << endl;
        // } else {
        //     cout << "abstract state v1: " << "inconsistent" << endl;
        // }
        // if (cons.second) {
        //     cout << "abstract state v2: " << v2.get_cartesian_set() << endl;
        // } else {
        //     cout << "abstract state v2: " << "inconsistent" << endl;
        // }
        // cout << "abstract state v1: " << v1.get_cartesian_set() << ", abstract state v2: " << v2.get_cartesian_set() << endl;

        if (pre == UNDEFINED) {
            // op has no precondition on var --> it must start in v1 and v2.
            // or var is derived
            if (post == UNDEFINED) {
                // op has no effect on var --> it must end in v1 and v2.

                if (!derived_conflict_v1){
                    add_loop(loops, v1_id, op_id);
                    added_loop_v1 = true;
                }

                if (!derived_conflict_v2){
                    add_loop(loops, v2_id, op_id);
                    added_loop_v2 = true;
                }

                // if var is derived we can possibly get all 4 combinations as derived values can change even without effect on var
                if (derived) {
                    add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                    add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                    added_v1_v2 = true;
                    added_v2_v1 = true;
                }
            } else {
                if (derived) {
                    if (!derived_conflict_v1) {
                        add_loop(loops, v1_id, op_id);
                        added_loop_v1 = true;
                    }
                    if (!derived_conflict_v2) {
                        add_loop(loops, v2_id, op_id);
                        added_loop_v2 = true;
                    }
                    if (!derived_conflict_v1_to_v2) {
                        add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                        added_v1_v2 = true;
                    }
                    if (!derived_conflict_v2_to_v1) {
                        add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                        added_v2_v1 = true;
                    }
                } else {
                    if (v2.contains(var, post)) {
                        // op must end in v2.

                        if (!derived_conflict_v1_to_v2) {  // derived_conflict_v1_to_v2 checked for conflict on v1-o->v2
                            add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                            added_v1_v2 = true;
                        }
                        if (!derived_conflict_v2) {
                            add_loop(loops, v2_id, op_id);
                            added_loop_v2 = true;
                        }
                    } else {
                        // op must end in v1.
                        assert(v1.contains(var, post));

                        if (!derived_conflict_v1){
                            add_loop(loops, v1_id, op_id);
                            added_loop_v1 = true;
                        }
                        if (!derived_conflict_v2_to_v1) { // derived_conflict_v2_to_v1 checked for conflict v1-o->v2
                            add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                            added_v2_v1 = true;
                        }
                    }
                }
            }
        } else if (v1.contains(var, pre)) {
            // op must start in v1.
            if (!derived) {
                assert(post != UNDEFINED);
            }
            if (post == UNDEFINED) {
                // var is derived and can end in both v1 and v2
                if (!derived_conflict_v1) {
                    add_loop(loops, v1_id, op_id);
                    added_loop_v1 = true;
                }
                if (!derived_conflict_v1_to_v2) {
                    add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                    added_v1_v2 = true;
                }
            } else if (v1.contains(var, post)) {
                // op must end in v1.
                if (!derived_conflict_v1){
                    add_loop(loops, v1_id, op_id);
                    added_loop_v1 = true;
                }
            } else {
                // op must end in v2.
                assert(v2.contains(var, post));
                if (!derived_conflict_v1_to_v2){
                    add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                    added_v1_v2 = true;
                }
            }
        } else {
            // op must start in v2.
            assert(v2.contains(var, pre));

            if (!derived) {
                assert(post != UNDEFINED);
            }
            if (post == UNDEFINED) {
                // var is derived and can end in both v1 and v2
                if (!derived_conflict_v2) {
                    add_loop(loops, v2_id, op_id);
                    added_loop_v2 = true;
                }
                if (!derived_conflict_v2_to_v1) {
                    add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                    added_v2_v1 = true;
                }
            } else if (v1.contains(var, post)) {
                // op must end in v1.
                if (!derived_conflict_v2_to_v1) {
                    add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                    added_v2_v1 = true;
                }
            } else {
                // op must end in v2.
                assert(v2.contains(var, post));
                if (!derived_conflict_v2) {
                    add_loop(loops, v2_id, op_id);
                    added_loop_v2 = true;
                }
            }
        }
        if (debug_output) {
            verify_rewiring_loops(v1, v2, op_id, added_loop_v1, added_loop_v2,
                added_v1_v2, added_v2_v1, cons);
        }
      /*if (loop_v1_valid && !added_loop_v1) {
            std::cout << "Loop v1 (" << v1_id << ") valid but not added" << std::endl;
        }
        if (!loop_v1_valid && added_loop_v1) {
            std::cout << "Loop v1 (" << v1_id << ") invalid but added" << std::endl;
        }
        if (loop_v2_valid && !added_loop_v2) {
            std::cout << "Loop v2 (" << v2_id << ") valid but not added" << std::endl;
        }
        if (!loop_v2_valid && added_loop_v2) {
            std::cout << "Loop v2 (" << v2_id << ") invalid but added" << std::endl;
        }
        if (v1_v2_valid && !added_v1_v2) {
            std::cout << "Transition v1 (" << v1_id << ") - op (" << op_id << ") -> v2 ("<< v2_id <<") valid but not added." << std::endl;
        }
        if (!v1_v2_valid && added_v1_v2) {
            std::cout << "Transition v1 (" << v1_id << ") - op (" << op_id << ") -> v2 ("<< v2_id <<") invalid but added." << std::endl;
        }
        if (v2_v1_valid && !added_v2_v1) {
            std::cout << "Transition v2 (" << v2_id << ") - op (" << op_id << ") -> v1 ("<< v1_id <<") valid but not added." << std::endl;
        }
        if (!v2_v1_valid && added_v2_v1) {
            std::cout << "Transition v2 (" << v2_id << ") - op (" << op_id << ") -> v1 ("<< v1_id <<") invalid but added." << std::endl;
        } */
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

bool TransitionRewirer::conflict_derived_domains(const CartesianSet &a, int op_id, const CartesianSet &b) const {
    
    CartesianSet extended_a_o = extension_strategy_instance->get_extension(update_cartesian_set(a, op_id));
    CartesianSet extended_b = extension_strategy_instance->get_extension(b);

    for (VariableProxy var : vars){
        if (var.is_derived() && !extended_a_o.intersects(b, var.get_id())) {
            // std::cout << "var " << var.get_id() <<  " conflict!! " << std::endl;
            // std::cout << "a : " << a << ", o : " << op_id << std::endl;
            // std::cout << "ext_a_o : " << extended_a_o << std::endl;
            // std::cout << "b : " << b << std::endl;
            // std::cout << "ext_b : " << extended_b << std::endl;
            // std::cout << "naive_ext_b: " << naive_extended_b << std::endl;
            return true;
        }
    }
    return false;   
}

bool TransitionRewirer::precondition_derived_conflict(
    const CartesianSet &a, int op_id) const {
    /*
     * Returns true if any derived variable precondition of op is not satisfiable
     * in the extension of a, i.e. if the basic variable domains of a don't
     * support the precondition value required for a derived variable and op
     * is inapplicable in a.
     */
    // Fast Path for op without preconditions on derived variables
    bool has_derived_pre = false;
    for (const FactPair &pre : preconditions_by_operator[op_id]) {
        if (vars[pre.var].is_derived()) {
            has_derived_pre = true;
            break;
        }
    }
    if (!has_derived_pre) {
        return false;
    }

    CartesianSet ext_a = extension_strategy_instance->get_extension(a);

    for (const FactPair &pre : preconditions_by_operator[op_id]) {
        // if (vars[pre.var].is_derived() && !ext_a.test(pre.var, pre.value)) {
        if (vars[pre.var].is_derived() && !a.test(pre.var, pre.value)) {
            // std::cout << "precondition conflict on var " << pre.var << std::endl;
            return true;
        }
    }
    return false;
}

std::pair<bool, bool> TransitionRewirer::consistency_check(const AbstractState &v1, const AbstractState &v2) const {
    /*  This function checks two abstract states v1 and v2 for consistency
     *  w.r.t. the set of axioms in the given task. If the extension of the
     *  respective Cartesian set contains an empty variable domain for one of
     *  the derived variables, then the abstract state is inconsistent.
     *  The function returns a pair of bools <consistency_v1, consistency_v2>
     *  where consistency_vx is true if the state vx is consistent, and false
     *  else.
     */
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
        CartesianSet ext_v1 = extension_strategy_instance->get_extension(v1.get_cartesian_set());
        CartesianSet ext_v2 = extension_strategy_instance->get_extension(v2.get_cartesian_set());
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
        if (!v1_consistent) {
            CartesianSet base_v1 = v1.get_cartesian_set();
            for (int var : derived_vars) {
                base_v1.add_all(var);
            }
            std::cout << v1.get_id() << " is inconsistent, v1 :" << v1.get_cartesian_set() << "\n   v1_ext has empty domain in vars: " << v1_empty_dom <<
                "\n   v1_base_ext: " << extension_strategy_instance->get_extension(base_v1) << std::endl;
            // for (int var : v1_empty_dom) {
            //     std::cout << "Default Value for var " << var << ": " << vars[var].get_default_axiom_value() << std::endl;
            // }
        }
        if (!v2_consistent) {
            CartesianSet base_v2 = v2.get_cartesian_set();
            for (int var : derived_vars) {
                base_v2.add_all(var);
            }
            std::cout << v2.get_id() << " is inconsistent, v2 :" << v2.get_cartesian_set() << "\n   v2_ext has empty domain in vars: " << v2_empty_dom <<
                "\n   v2_base_ext: " << extension_strategy_instance->get_extension(base_v2) << std::endl;
            // for (int var : v2_empty_dom) {
            //     std::cout << "Default Value for var " << var << ": " << vars[var].get_default_axiom_value() << std::endl;
            // }
        }
    }

    return std::pair<bool, bool>(v1_consistent, v2_consistent);
}

bool TransitionRewirer::check_regression_intersection(const AbstractState &source, const AbstractState &target, int op) const {
    // TODO: can be removed?
    regression_strategy_instance->prepare(target.get_cartesian_set(), op);
    // CartesianSet ext_source = extension_strategy_instance->get_extension(source.get_cartesian_set());
    CartesianSet regr_t_o = regression_strategy_instance->get_regression(target.get_cartesian_set(), op);
    bool valid = true;
    for (const VariableProxy &var : vars) {
        int var_id = var.get_id();
        bool intersect_on_v = false;
        int domain_size = vars[var_id].get_domain_size();
        for (int v = 0; v < domain_size; v++) {
            if (source.contains(var_id, v) && regr_t_o.test(var_id, v)) {
                intersect_on_v = true;
                break;
            }
        }
        if (!intersect_on_v) {
            if (var.is_derived()) {
                std::cout << "  Added transition from " << source.get_id() << " to " << target.get_id() << " via op " << op << " on derived var " << var_id << " with empty intersection on regression" << std::endl;
            } else {
                std::cout << "  Added transition from " << source.get_id() << " to " << target.get_id() << " via op " << op << " on basic var " << var_id << " with empty intersection on regression" << std::endl;
            }
            valid = false;
            // std::cout << "  Empty intersection on var " << var_id << std::endl;
            // std::cout << "      src[var]" << a.get_values(var_id) << std::endl;
            // std::cout << "      regr(target, o)[var]" << regr_t_o.get_values(var_id) << std::endl;
        }
    }
    return valid;
}


bool TransitionRewirer::explicit_transition_check(const AbstractState &source, const AbstractState &target,
    const int op) const {
    /*
     *  implement the naive transition check that explicitly checks every var
     *  from the paper (inefficient but used to confirm accuracy for optimized
     *  version)
     */
    for (const VariableProxy &var : vars) {
        int var_id = var.get_id();
        int pre_val = get_precondition_value(op, var_id);
        int post_val = get_postcondition_value(op, var_id);
        if (!var.is_derived()) {
            if (pre_val != UNDEFINED && !source.contains(var_id, pre_val)) {
                return false;
            }
            if (post_val != UNDEFINED && !target.contains(var_id, post_val)) {
                return false;
            }
            if (post_val == UNDEFINED && !source.get_cartesian_set().intersects(target.get_cartesian_set(), var_id)) {
                return false;
            }
        }
    }
    if (precondition_derived_conflict(source.get_cartesian_set(), op)) {
        // std::cout << "Precondition conflict! for "<< source.get_id() << " to " << target.get_id() << std::endl;
        return false;
    }
    if (conflict_derived_domains(source.get_cartesian_set(), op, target.get_cartesian_set())) {
        // std::cout << "Conflict derived domains! for "<< source.get_id() << " to " << target.get_id() << std::endl;
        return false;
    }
    return true;
}

void TransitionRewirer::verify_rewiring_incoming(const AbstractState &u,
    const AbstractState &v1, const AbstractState &v2, int op_id,
    bool added_u_v1, bool added_u_v2, pair<bool, bool> cons) const {
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
void TransitionRewirer::verify_rewiring_outgoing(const AbstractState &w,
    const AbstractState &v1, const AbstractState &v2, int op_id,
    bool added_v1_w, bool added_v2_w, pair<bool, bool> cons) const {
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
void TransitionRewirer::verify_rewiring_loops(const AbstractState &v1, const AbstractState &v2,
    int op_id, bool added_loop_v1, bool added_loop_v2, bool added_v1_v2,
    bool added_v2_v1, pair<bool, bool> cons) const {
    bool valid_v1 = explicit_transition_check(v1, v1, op_id);
    bool valid_v2 = explicit_transition_check(v2, v2, op_id);
    bool valid_v1_v2 = explicit_transition_check(v1, v2, op_id);
    bool valid_v2_v1 = explicit_transition_check(v2, v2, op_id);
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