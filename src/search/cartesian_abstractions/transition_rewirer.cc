#include "transition_rewirer.h"

#include "abstract_state.h"
#include "transition.h"

#include "../task_proxy.h"

#include "../task_utils/task_properties.h"

#include <algorithm>
#include <map>

using namespace std;

namespace cartesian_abstractions {
static vector<vector<FactPair>> get_preconditions_by_operator(
    const OperatorsProxy &ops) {
    vector<vector<FactPair>> preconditions_by_operator;
    preconditions_by_operator.reserve(ops.size());
    for (OperatorProxy op : ops) {
        vector<FactPair> preconditions =
            task_properties::get_fact_pairs(op.get_preconditions());
        sort(preconditions.begin(), preconditions.end());
        preconditions_by_operator.push_back(move(preconditions));
    }
    return preconditions_by_operator;
}

static vector<FactPair> get_postconditions(const OperatorProxy &op) {
    // Use map to obtain sorted postconditions.
    map<int, int> var_to_post;
    for (FactProxy fact : op.get_preconditions()) {
        if (!fact.get_variable().is_derived()) {
            var_to_post[fact.get_variable().get_id()] = fact.get_value();
        }   
    }
    for (EffectProxy effect : op.get_effects()) {
        FactPair fact = effect.get_fact().get_pair();
        var_to_post[fact.var] = fact.value;
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

TransitionRewirer::TransitionRewirer(const OperatorsProxy &ops, const TaskProxy &task)
    : task(task), preconditions_by_operator(get_preconditions_by_operator(ops)),
      postconditions_by_operator(get_postconditions_by_operator(ops)) {
}

void TransitionRewirer::rewire_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id, const AbstractState &v1,
    const AbstractState &v2, int var) const {
    rewire_incoming_transitions(incoming, outgoing, states, v_id, v1, v2, var);
    rewire_outgoing_transitions(incoming, outgoing, states, v_id, v1, v2, var);
}

void TransitionRewirer::rewire_incoming_transitions(
    deque<Transitions> &incoming, deque<Transitions> &outgoing,
    const AbstractStates &states, int v_id, const AbstractState &v1,
    const AbstractState &v2, int var) const {
    /* State v has been split into v1 and v2. Now for all transitions
       u->v we need to add transitions u->v1, u->v2, or both. */
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
        bool derived = task.get_variables()[var].is_derived(); // check if var is derived
        
        if (derived) {
            // determine derived variable value 
            post = get_derived_value(u, op_id, var);
        } else{
            // determine basic variable post value
            post = get_postcondition_value(op_id, var);
        }

        if (!derived && post == UNDEFINED) {
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
        } else if (derived && post == UNDEFINED) {
            // op can end in both v1 and v2 as derived variable value is not known
            add_transition(incoming, outgoing, u_id, op_id, v1_id);
            add_transition(incoming, outgoing, u_id, op_id, v2_id);
        } else if (v1.contains(var, post)) {
            // op can only end in v1.
            add_transition(incoming, outgoing, u_id, op_id, v1_id);
        } else {
            // op can only end in v2.
            assert(v2.contains(var, post));
            add_transition(incoming, outgoing, u_id, op_id, v2_id);
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
        bool derived = task.get_variables()[var].is_derived(); // check if var is derived
        bool derived_conflict_v1 = false;
        bool derived_conflict_v2 = false;

        if (!derived){
            derived_conflict_v1 = check_derived_conflict(v1, op_id, w);
            derived_conflict_v2 = check_derived_conflict(v2, op_id, w);
        }

        if (!derived && post == UNDEFINED) {
            assert(pre == UNDEFINED);
            // op has no precondition and no effect on var.
            bool v1_and_w_intersect = v1.domain_subsets_intersect(w, var);
            if (!derived_conflict_v1 && v1_and_w_intersect) {
                add_transition(incoming, outgoing, v1_id, op_id, w_id);
            }
            /* If v1 and w don't intersect, we must add the other transition
            and can avoid an intersection test. */
            if (!derived_conflict_v2 && (!v1_and_w_intersect || v2.domain_subsets_intersect(w, var))) {
                add_transition(incoming, outgoing, v2_id, op_id, w_id);
            }
        } else if (pre == UNDEFINED) {
            // op has no precondition, but an effect on var.
            if (!derived_conflict_v1){
                add_transition(incoming, outgoing, v1_id, op_id, w_id);
            }
            if (!derived_conflict_v2){
                add_transition(incoming, outgoing, v2_id, op_id, w_id);
            }
        } else if (v1.contains(var, pre)) {
            // op can only start in v1.
            if (!derived_conflict_v1){
                add_transition(incoming, outgoing, v1_id, op_id, w_id);
            }
        } else{
            // op can only start in v2.
            if (!derived_conflict_v2){
                cout << "Adding transition from " << v2_id << " to " << w_id << " via op " << task.get_operators()[op_id].get_name() << endl;
                add_transition(incoming, outgoing, v2_id, op_id, w_id);
            }
        }
    
    }
}

int TransitionRewirer::get_derived_value(const AbstractState &v, int op_id, int var) const {
    AxiomsProxy axioms = task.get_axioms();

    // Count number of unsatisfied body atoms for each axiom
    vector<int> unsat_body_atoms(axioms.size());
    // Count number of axioms possibly supporting each derived variable
    vector<int> supporting_axioms(task.get_variables().size()); // TODO: num derived variables
    // dont consider axioms again that we know can not fire
    std::unordered_set<int> unsat_axioms;

    for (OperatorProxy axiom : axioms){
        unsat_body_atoms[axiom.get_id()] = axiom.get_preconditions().size();
        supporting_axioms[axiom.get_effects()[0].get_fact().get_var_id()]++;
    }

    // Initialize queue with facts known to be true/false
    std::deque<std::pair<FactPair, bool>> fact_queue;

    // Add fact pairs for basic variables definitiely true/false in v oplus o to the queue
    for (VariableProxy var_prox : task.get_variables()){

        // only iterate over basic variables
        if (var_prox.is_derived()){
            break;
        }
        int i = var_prox.get_id();
        int post_val = get_postcondition_value(op_id, i);
        // value of i is determined by o
        if (post_val != UNDEFINED){
            // post condition fact must be true
            fact_queue.emplace_front(FactPair(i, post_val), true);
            // all other values for i must be false
            for(int val = 0; val < var_prox.get_domain_size(); val++){
                if (val == post_val){
                    continue;
                }
                fact_queue.emplace_back(FactPair(i, val), false);
            }

        // abstract state has single value for i which is not modified by o
        } else if (v.count(i) == 1){
            // single value fact must be true
            int single_val = v.get_cartesian_set().get_values(i)[0];
            fact_queue.emplace_front(FactPair(i, single_val), true);
            // all other values for i must be false
            for(int val = 0; val < var_prox.get_domain_size(); val++){
                if (val == single_val){
                    continue;
                }
                fact_queue.emplace_front(FactPair(i, val), false);
            }
        // abstract state v has non-full domain for i
        } else if (v.count(i) != var_prox.get_domain_size()){
            // all values not in v must be false
            for(int val = 0; val < var_prox.get_domain_size(); val++){
                if (v.contains(i, val)){
                    continue;
                }
                fact_queue.emplace_front(FactPair(i, val), false);
            }
        }
    }
    
    // variables already seen (added to queue)
    vector<bool> seen_vars(task.get_variables().size(), false); // TODO: num derived variables
   
    while (!fact_queue.empty()) {
        FactPair fact = fact_queue.front().first;
        bool flag = fact_queue.front().second;
        fact_queue.pop_front();
        if(flag){
            for (OperatorProxy r : axioms){
                if (unsat_axioms.contains(r.get_id())){
                    continue;
                }
                for (FactProxy f : r.get_preconditions()){
                    if (f.get_pair() == fact){
                        unsat_body_atoms[r.get_id()]--;
                        if (unsat_body_atoms[r.get_id()] == 0) {
                            FactProxy head_atom = r.get_effects()[0].get_fact();
                            if (head_atom.get_var_id() == var) {
                                return head_atom.get_value(); // derived variable is true
                            } 
                            enqueue(fact_queue, seen_vars, head_atom.get_pair(), true);
                        }
                        break; // assuming each fact only occurs once in an operator precondition
                    }
                }
            }
        } else {
            for (OperatorProxy r : axioms){
                if (unsat_axioms.contains(r.get_id())){
                    continue;
                }
                for (FactProxy f : r.get_preconditions()){
                    if (f.get_pair() == fact){
                        FactProxy head_atom = r.get_effects()[0].get_fact();
                        int head_id = head_atom.get_var_id();
                        supporting_axioms[head_id]--;
                        unsat_axioms.insert(r.get_id());
                        if (supporting_axioms[head_id] == 0) {
                            if (head_id == var) {
                                return 0; // derived variable is false
                            } 
                            enqueue(fact_queue, seen_vars, head_atom.get_pair(), false);
                        }
                        break; // assuming each fact only occurs once in an operator precondition
                    }
                }
            }
        }
    }
    return UNDEFINED; 
}

// Check if there is a conflict for derived variable values
// between state v updated with o compared to target state w.
bool TransitionRewirer::check_derived_conflict(const AbstractState &v, int op_id, const AbstractState &w) const {
    AxiomsProxy axioms = task.get_axioms();

    // Count number of unsatisfied body atoms for each axiom
    vector<int> unsat_body_atoms(axioms.size());
    // Count number of axioms possibly supporting each derived variable
    vector<int> supporting_axioms(task.get_variables().size()); // TODO: num derived variables
    // dont consider axioms again that we know can not fire
    std::unordered_set<int> unsat_axioms;

    for (OperatorProxy axiom : axioms){
        unsat_body_atoms[axiom.get_id()] = axiom.get_preconditions().size();
        supporting_axioms[axiom.get_effects()[0].get_fact().get_var_id()]++;
    }

    // Initialize queue with facts known to be true/false
    std::deque<std::pair<FactPair, bool>> fact_queue;

    // Add fact pairs for basic variables definitiely true/false in v oplus o to the queue
    for (VariableProxy var_prox : task.get_variables()){

        // only iterate over basic variables
        if (var_prox.is_derived()){
            break;
        }
        int i = var_prox.get_id();
        int post_val = get_postcondition_value(op_id, i);
        // value of i is determined by o
        if (post_val != UNDEFINED){
            // post condition fact must be true
            fact_queue.emplace_front(FactPair(i, post_val), true);
            // all other values for i must be false
            for(int val = 0; val < var_prox.get_domain_size(); val++){
                if (val == post_val){
                    continue;
                }
                fact_queue.emplace_back(FactPair(i, val), false);
            }

        // abstract state has single value for i which is not modified by o
        } else if (v.count(i) == 1){
            // single value fact must be true
            int single_val = v.get_cartesian_set().get_values(i)[0];
            fact_queue.emplace_front(FactPair(i, single_val), true);
            // all other values for i must be false
            for(int val = 0; val < var_prox.get_domain_size(); val++){
                if (val == single_val){
                    continue;
                }
                fact_queue.emplace_front(FactPair(i, val), false);
            }
        // abstract state v has non-full domain for i
        } else if (v.count(i) != var_prox.get_domain_size()){
            // all values not in v must be false
            for(int val = 0; val < var_prox.get_domain_size(); val++){
                if (v.contains(i, val)){
                    continue;
                }
                fact_queue.emplace_front(FactPair(i, val), false);
            }
        }
    }
    
    // variables already seen (added to queue)
    vector<bool> seen_vars(task.get_variables().size(), false); // TODO: num derived variables
   

    while (!fact_queue.empty()) {
        FactPair fact = fact_queue.front().first;
        bool flag = fact_queue.front().second;
        fact_queue.pop_front();
        if(flag){
            for (OperatorProxy r : axioms){
                if (unsat_axioms.contains(r.get_id())){
                    continue;
                }
                for (FactProxy f : r.get_preconditions()){
                    if (f.get_pair() == fact){
                        unsat_body_atoms[r.get_id()]--;
                        if (unsat_body_atoms[r.get_id()] == 0) {
                            FactProxy head_atom = r.get_effects()[0].get_fact();
                            if (!w.contains(head_atom.get_var_id(), true)){
                                cout << "Conflict found for derived variable " << head_atom.get_var_id() << " expected true in target state" << endl;
                                return true; // conflict found
                            } 
                            enqueue(fact_queue, seen_vars, head_atom.get_pair(), true);
                        }
                        break; // assuming each fact only occurs once in an operator precondition
                    }
                }
            }
        } else {
            for (OperatorProxy r : axioms){
                if (unsat_axioms.contains(r.get_id())){
                    continue;
                }
                for (FactProxy f : r.get_preconditions()){
                    if (f.get_pair() == fact){
                        FactProxy head_atom = r.get_effects()[0].get_fact();
                        int head_id = head_atom.get_var_id();
                        supporting_axioms[head_id]--;
                        unsat_axioms.insert(r.get_id());
                        if (supporting_axioms[head_id] == 0) {
                            if (!w.contains(head_id, false)) {
                                cout << "Conflict found for derived variable " << head_id << " expected false in target state" << endl;
                                return true; // conflict found
                            } 
                            enqueue(fact_queue, seen_vars, head_atom.get_pair(), false);
                        }
                        break; // assuming each fact only occurs once in an operator precondition
                    }
                }
            }
        }
    }
    return false; 
}

// Add item to queue if not already seen
void TransitionRewirer::enqueue(std::deque<std::pair<FactPair, bool>> &q, std::vector<bool> &seen_vars, FactPair fact, bool x) const {
    if (!seen_vars[fact.var]){
        seen_vars[fact.var] = true;
        q.emplace_back(fact, x);
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
        bool derived = task.get_variables()[var].is_derived(); // check if var is derived
        bool derived_conflict_v1 = false;
        bool derived_conflict_v2 = false;

        if (derived) {
            // derived value is the same for both v1 and v2 
            // computation of derived value only relies on basic variables, however v1 and v2 only differ in var which is derived
            post = get_derived_value(v1, op_id, var);
        } else {
            // determine basic variable post value
            post = get_postcondition_value(op_id, var);
            
            // conflicts, derived variable value is the same for v1 and v2, only var which is basic differs
            derived_conflict_v1 = check_derived_conflict(v1, op_id, v1);
            derived_conflict_v2 = check_derived_conflict(v2, op_id, v1);

        }

        cout << "Rewiring loop for op " << task.get_operators()[op_id].get_name() << endl;
        cout << "Precondition value: " << pre << ", Postcondition value: " << post << endl;
        cout << "var " << var << " is " << (derived ? "derived" : "basic") << endl;
        cout << "abstract state v1: " << v1.get_cartesian_set() << ", abstract state v2: " << v2.get_cartesian_set() << endl;

        if (pre == UNDEFINED) {
            // op has no precondition on var --> it must start in v1 and v2.
            // or var is derived
            if (post == UNDEFINED) {
                // op has no effect on var --> it must end in v1 and v2.

                if (derived || !derived_conflict_v1){
                    add_loop(loops, v1_id, op_id);
                }

                if (derived || !derived_conflict_v2){
                    add_loop(loops, v2_id, op_id);
                }

                // if var is derived we can possibly get all 4 combinations as derived values can change even without effect on var
                if (derived) {
                    add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                    add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                }
            } else if (v2.contains(var, post)) {
                // op must end in v2.

                if (derived || !derived_conflict_v1){
                    add_transition(incoming, outgoing, v1_id, op_id, v2_id);
                }
                if (derived || !derived_conflict_v2){
                    add_loop(loops, v2_id, op_id);
                }
            } else {
                // op must end in v1.
                assert(v1.contains(var, post));

                if (derived || !derived_conflict_v1){
                    add_loop(loops, v1_id, op_id);
                }
                if (derived || !derived_conflict_v2){
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
                add_loop(loops, v1_id, op_id);
                add_transition(incoming, outgoing, v1_id, op_id, v2_id);
            } else if (v1.contains(var, post)) {
                // op must end in v1.
                if (derived || !derived_conflict_v1){
                    add_loop(loops, v1_id, op_id);
                }
            } else {
                // op must end in v2.
                assert(v2.contains(var, post));
                if (derived || !derived_conflict_v1){
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
                add_loop(loops, v2_id, op_id);
                add_transition(incoming, outgoing, v2_id, op_id, v1_id);
            } else if (v1.contains(var, post)) {
                // op must end in v1.
                if (derived || !derived_conflict_v2){
                    add_transition(incoming, outgoing, v2_id, op_id, v1_id);
                }
            } else {
                // op must end in v2.
                assert(v2.contains(var, post));
                if (derived || !derived_conflict_v2){
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
}
