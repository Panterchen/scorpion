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
}

static void add_loop(deque<Loops> &loops, int state_id, int op_id) {
    assert(utils::in_bounds(state_id, loops));
    loops[state_id].push_back(op_id);
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
    std::cout << "Rewire Transitions for state " << v_id << " into states " << v1.get_id() << ", " << v2.get_id() << std::endl;
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
        int post = UNDEFINED;
        bool derived = vars[var].is_derived(); // check if var is derived
        bool derived_conflict_v1 = conflict_derived_domains(u.get_cartesian_set(), op_id, v1.get_cartesian_set());
        bool derived_conflict_v2 = conflict_derived_domains(u.get_cartesian_set(), op_id, v2.get_cartesian_set());

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
                log_new_transition(u, v1, op_id);
                add_transition(incoming, outgoing, u_id, op_id, v1_id);
            }
            /* If u and v1 don't intersect, we must add the other transition
            and can avoid an intersection test. */
            if (!derived_conflict_v2 && (derived || !u_and_v1_intersect || u.domain_subsets_intersect(v2, var))) {
                log_new_transition(u, v2, op_id);
                add_transition(incoming, outgoing, u_id, op_id, v2_id);
            }
        } else if (v1.contains(var, post)) {
            // op can only end in v1.
            if (!derived_conflict_v1) {
                log_new_transition(u, v1, op_id);
                add_transition(incoming, outgoing, u_id, op_id, v1_id);
            }
        } else {
            // op can only end in v2.
            assert(v2.contains(var, post));
            if (!derived_conflict_v2) {
                log_new_transition(u, v2, op_id);
                add_transition(incoming, outgoing, u_id, op_id, v2_id);
            }
        }

    }
}

void TransitionRewirer::rewire_outgoing_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id, const AbstractState &v1,
    const AbstractState &v2, int var) const {

    /* State v has been split into v1 and v2. Now for all transitions
       v->w we need to add transitions v1->w, v2->w, or both. */
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
        int pre = get_precondition_value(op_id, var);
        int post = get_postcondition_value(op_id, var);
        
        bool derived = vars[var].is_derived(); // check if var is derived
        // check if v1 or v2 have an inapplicability conflict with a derived precondition variable
        bool pre_derived_conflict_v1 = precondition_derived_conflict(v1.get_cartesian_set(), op_id);
        bool pre_derived_conflict_v2 = (derived ? pre_derived_conflict_v1 :
            precondition_derived_conflict(v2.get_cartesian_set(), op_id));
        // check if v1 or v2 have a conflict on the derived postconditions with w
        bool derived_conflict_v1 = pre_derived_conflict_v1 ||
            conflict_derived_domains(v1.get_cartesian_set(), op_id, w.get_cartesian_set());
        bool derived_conflict_v2 = pre_derived_conflict_v2 ||
            conflict_derived_domains(v2.get_cartesian_set(), op_id, w.get_cartesian_set());

        // if (!derived) {
        //     derived_conflict_v1 = conflict_derived_domains(v1.get_cartesian_set(), op_id, w.get_cartesian_set());
        //     derived_conflict_v2 = conflict_derived_domains(v2.get_cartesian_set(), op_id, w.get_cartesian_set());
        // }

        if (!derived && post == UNDEFINED) {
            assert(pre == UNDEFINED);
            // op has no precondition and no effect on var.
            bool v1_and_w_intersect = v1.domain_subsets_intersect(w, var);
            if (!derived_conflict_v1 && v1_and_w_intersect) {
                log_new_transition(v1, w, op_id);
                add_transition(incoming, outgoing, v1_id, op_id, w_id);
            }
            /* If v1 and w don't intersect, we must add the other transition
            and can avoid an intersection test. */
            if (!derived_conflict_v2 && (!v1_and_w_intersect || v2.domain_subsets_intersect(w, var))) {
                log_new_transition(v2, w, op_id);
                add_transition(incoming, outgoing, v2_id, op_id, w_id);
            }
        } else if (pre == UNDEFINED) {
            // op has no precondition, but an effect on var.
            if (!derived_conflict_v1){
                log_new_transition(v1, w, op_id);
                add_transition(incoming, outgoing, v1_id, op_id, w_id);
            }
            if (!derived_conflict_v2){
                log_new_transition(v2, w, op_id);
                add_transition(incoming, outgoing, v2_id, op_id, w_id);
            }
        } else if (v1.contains(var, pre)) {
            // op can only start in v1.
            if (!derived_conflict_v1){
                log_new_transition(v1, w, op_id);
                add_transition(incoming, outgoing, v1_id, op_id, w_id);
            }
        } else{
            // op can only start in v2.
            if (!derived_conflict_v2){
                //cout << "Adding transition from " << v2_id << " to " << w_id << " via op " << task.get_operators()[op_id].get_name() << endl;
                log_new_transition(v2, w, op_id);
                add_transition(incoming, outgoing, v2_id, op_id, w_id);
            }
        }
    
    }
}

void TransitionRewirer::rewire_loops(
    deque<Loops> &loops, deque<Transitions> &incoming,
    deque<Transitions> &outgoing, int v_id, const AbstractState &v1,
    const AbstractState &v2, int var) const {

    Loops old_loops = move(loops[v_id]);
    assert(loops[v_id].empty());

    /* State v has been split into v1 and v2. Now for all self-loops
       v->v we need to add one or two of the transitions v1->v1, v1->v2,
       v2->v1 and v2->v2. */
    int v1_id = v1.get_id();
    int v2_id = v2.get_id();
    for (int op_id : old_loops) {
        int pre = get_precondition_value(op_id, var);

        int post = UNDEFINED;
        bool derived = vars[var].is_derived(); // check if var is derived
        bool derived_conflict_v1 = false;
        bool derived_conflict_v2 = false;
        bool derived_conflict_v1_to_v2 = false;
        bool derived_conflict_v2_to_v1 = false;

        if (derived) {
            // derived value is the same for both v1 and v2 
            // computation of derived value only relies on basic variables,
            // however v1 and v2 only differ in var which is derived
            CartesianSet v1_cs = update_cartesian_set(v1.get_cartesian_set(), op_id);
            post = extension_strategy_instance->get_extension_value(v1_cs, var);
        } else {
            // determine basic variable post value
            post = get_postcondition_value(op_id, var);
            
            // conflicts, derived variable value is the same for v1 and v2, only var which is basic differs
            // consider conflicts on preconditions (if there are preconditions on derived variables that could
            // depend on the changed basic variable
            bool pre_conflict_v1 = precondition_derived_conflict(v1.get_cartesian_set(), op_id);
            bool pre_conflict_v2 = precondition_derived_conflict(v2.get_cartesian_set(), op_id);
            // v1 and v2 are the same except for domain of basic variable. for conflict(a,o,b) we consider the basic variable domains of a and derived variable values of b 
            derived_conflict_v1 = pre_conflict_v1 ||
                conflict_derived_domains(v1.get_cartesian_set(), op_id, v1.get_cartesian_set());
            derived_conflict_v2 = pre_conflict_v2 ||
                conflict_derived_domains(v2.get_cartesian_set(), op_id, v2.get_cartesian_set());
            derived_conflict_v1_to_v2 = pre_conflict_v1 ||
                conflict_derived_domains(v1.get_cartesian_set(), op_id, v2.get_cartesian_set());
            derived_conflict_v2_to_v1 = pre_conflict_v2 ||
                conflict_derived_domains(v2.get_cartesian_set(), op_id, v1.get_cartesian_set());
        }

        //cout << "Rewiring loop for op " << task.get_operators()[op_id].get_name() << endl;
        //cout << "Precondition value: " << pre << ", Postcondition value: " << post << endl;
        //cout << "var " << var << " is " << (derived ? "derived" : "basic") << endl;
        //cout << "abstract state v1: " << v1.get_cartesian_set() << ", abstract state v2: " << v2.get_cartesian_set() << endl;

        if (pre == UNDEFINED) {
            // op has no precondition on var --> it must start in v1 and v2.
            // or var is derived
            if (post == UNDEFINED) {
                // op has no effect on var --> it must end in v1 and v2.

                if (!derived_conflict_v1){
                    add_loop(loops, v1_id, op_id);
                }

                if (!derived_conflict_v2){
                    add_loop(loops, v2_id, op_id);
                }

                // if var is derived we can possibly get all 4 combinations as derived values can change even without effect on var
                if (derived) {
                    log_new_transition(v1, v2, op_id);
                    log_new_transition(v2, v1, op_id);
                    add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                    add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                }
            } else if (v2.contains(var, post)) {
                // op must end in v2.

                if (!derived_conflict_v1_to_v2) {  // derived_conflict_v1_to_v2 checked for conflict on v1-o->v2
                    log_new_transition(v1, v2, op_id);
                    add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                }
                if (!derived_conflict_v2) {
                    add_loop(loops, v2_id, op_id);
                }
            } else {
                // op must end in v1.
                assert(v1.contains(var, post));

                if (!derived_conflict_v1){
                    add_loop(loops, v1_id, op_id);
                }
                if (!derived_conflict_v2_to_v1) { // derived_conflict_v2_to_v1 checked for conflict v1-o->v2
                    log_new_transition(v2, v1, op_id);
                    add_transition(incoming, outgoing, v2_id, op_id, v1_id);
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
                }
                if (!derived_conflict_v1_to_v2) {
                    log_new_transition(v1, v2, op_id);
                    add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                }
            } else if (v1.contains(var, post)) {
                // op must end in v1.
                if (!derived_conflict_v1){
                    add_loop(loops, v1_id, op_id);
                }
            } else {
                // op must end in v2.
                assert(v2.contains(var, post));
                if (!derived_conflict_v1_to_v2){
                    log_new_transition(v1, v2, op_id);
                    add_transition(incoming, outgoing, v1_id, op_id, v2_id);
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
                }
                if (!derived_conflict_v2_to_v1) {
                    log_new_transition(v2, v1, op_id);
                    add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                }
            } else if (v1.contains(var, post)) {
                // op must end in v1.
                if (!derived_conflict_v2_to_v1) {
                    log_new_transition(v2, v1, op_id);
                    add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                }
            } else {
                // op must end in v2.
                assert(v2.contains(var, post));
                if (!derived_conflict_v2){
                    add_loop(loops, v2_id, op_id);
                }
            }
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

bool TransitionRewirer::conflict_derived_domains(const CartesianSet &a, int op_id, const CartesianSet &b) const {
    
    CartesianSet extended_a_o = extension_strategy_instance->get_extension(update_cartesian_set(a, op_id));
    CartesianSet extended_b = extension_strategy_instance->get_extension(b);

    for (VariableProxy var : vars){
        if (var.is_derived() && !extended_a_o.intersects(extended_b, var.get_id())) {
            // std::cout << "var " << var.get_id() <<  " conflict!! " << std::endl;
            // std::cout << "a : " << a << ", o : " << op_id << std::endl;
            // std::cout << "ext_a_o : " << extended_a_o << std::endl;
            // std::cout << "b : " << b << std::endl;
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
        if (vars[pre.var].is_derived() && !ext_a.test(pre.var, pre.value)) {
            std::cout << "precondition conflict on var " << pre.var << std::endl;
            return true;
        }
    }
    return false;
}

std::pair<bool, bool> TransitionRewirer::consistency_check(const AbstractState &v1, const AbstractState &v2) const {
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
        for (int var : derived_vars) {
            if (v1_consistent && ext_v1.get_values(var).empty()) {
                v1_consistent = false;
            }
            if (v2_consistent && ext_v2.get_values(var).empty()) {
                v2_consistent = false;
            }
            if (!v1_consistent && !v2_consistent) {
                break;
            }
        }
    }
    if (!v1_consistent || !v2_consistent) {
        std::cout << "Inconsistent abstract state found " << v1.get_id() << ": " << v1_consistent << ", " << v2.get_id() << ": " << v2_consistent << std::endl;
    }
    return std::pair<bool, bool>(v1_consistent, v2_consistent);
}

void TransitionRewirer::log_new_transition(const AbstractState &source, const AbstractState &target, int op) const {
    // for debugging (TODO remove later)
    regression_strategy_instance->prepare(target.get_cartesian_set(), op);
    CartesianSet regr_t_o = regression_strategy_instance->get_regression(target.get_cartesian_set(), op);
    CartesianSet a = source.get_cartesian_set();
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
            // std::cout << "  Empty intersection on var " << var_id << std::endl;
            // std::cout << "      src[var]" << a.get_values(var_id) << std::endl;
            // std::cout << "      regr(target, o)[var]" << regr_t_o.get_values(var_id) << std::endl;
        }
    }
}

}
