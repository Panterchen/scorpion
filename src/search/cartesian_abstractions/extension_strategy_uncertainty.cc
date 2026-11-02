#include "extension_strategy_uncertainty.h"
#include "cartesian_set.h"
#include "../plugins/plugin.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <numeric>

using namespace std;

namespace cartesian_abstractions {

// ---- ExtensionStrategyUncertaintyInstance ----

ExtensionStrategyUncertaintyInstance::ExtensionStrategyUncertaintyInstance(
    const AxiomsProxy &axioms, const VariablesProxy &variables)
    : axioms(axioms), variables(variables), var_helper(variables) {
    int num_vars = variables.size();
    int num_axioms = axioms.size();
    int num_facts = var_helper.num_facts();

    axiom_head.assign(num_axioms, FactPair(-1, -1));
    body_size.assign(num_axioms, 0);
    supporting_base.assign(num_vars, 0);
    default_value.assign(num_vars, -1);
    for (int v : var_helper.derived_vars) {
        default_value[v] = variables[v].get_default_axiom_value();
    }

    vector<pair<int, int>> watches;   // (fact_id, axiom_id)
    for (OperatorProxy axiom : axioms) {
        int id = axiom.get_id();
        assert(id >= 0 && id < num_axioms);
        EffectProxy effect = axiom.get_effects()[0];
        axiom_head[id] = effect.get_fact().get_pair();
        supporting_base[axiom_head[id].var]++;

        vector<int> fact_ids;
        for (FactProxy cond : effect.get_conditions()) {
            FactPair p = cond.get_pair();
            fact_ids.push_back(var_helper.fact_offset[p.var] + p.value);
        }
        sort(fact_ids.begin(), fact_ids.end());
        fact_ids.erase(unique(fact_ids.begin(), fact_ids.end()), fact_ids.end());
        body_size[id] = fact_ids.size();
        for (int f : fact_ids) {
            watches.emplace_back(f, id);
        }
    }

    // sorted by (fact, axiom) -> CSR with ascending axiom ids per fact
    sort(watches.begin(), watches.end());
    watch_begin.assign(num_facts + 1, 0);
    for (const auto &w : watches) {
        watch_begin[w.first + 1]++;
    }
    partial_sum(watch_begin.begin(), watch_begin.end(), watch_begin.begin());
    watch_axioms.reserve(watches.size());
    for (const auto &w : watches) {
        watch_axioms.push_back(w.second);
    }

    body_epoch.assign(num_axioms, 0);
    unsat_epoch.assign(num_axioms, 0);
    body_cnt.assign(num_axioms, 0);
    supp_epoch.assign(num_vars, 0);
    seen_epoch.assign(num_vars, 0);
    supp_cnt.assign(num_vars, 0);

}

void ExtensionStrategyUncertaintyInstance::start_new_epoch() {
    if (++epoch == 0) {   // overflow: reset all stamps
        fill(body_epoch.begin(), body_epoch.end(), 0);
        fill(unsat_epoch.begin(), unsat_epoch.end(), 0);
        fill(supp_epoch.begin(), supp_epoch.end(), 0);
        fill(seen_epoch.begin(), seen_epoch.end(), 0);
        epoch = 1;
    }
}

// Number of body facts of the axiom that are not yet known to be true.
int &ExtensionStrategyUncertaintyInstance::body_left(int axiom_id) {
    if (body_epoch[axiom_id] != epoch) {
        body_epoch[axiom_id] = epoch;
        body_cnt[axiom_id] = body_size[axiom_id];
    }
    return body_cnt[axiom_id];
}

// Number of axioms that may still support the derived variable.
int &ExtensionStrategyUncertaintyInstance::supp_left(int var_id) {
    if (supp_epoch[var_id] != epoch) {
        supp_epoch[var_id] = epoch;
        supp_cnt[var_id] = supporting_base[var_id];
    }
    return supp_cnt[var_id];
}

CartesianSet ExtensionStrategyUncertaintyInstance::compute_extension(
    const CartesianSet &a) {
    CartesianSet result = a;
    start_new_epoch();

    // Initialize queue with facts known to be true/false
    deque<pair<FactPair, bool>> fact_queue = setup_fact_queue(a);

    while (!fact_queue.empty()) {
        FactPair fact = fact_queue.front().first;
        bool flag = fact_queue.front().second;
        fact_queue.pop_front();

        int fact_id = var_helper.fact_offset[fact.var] + fact.value;
        for (int i = watch_begin[fact_id]; i < watch_begin[fact_id + 1]; ++i) {
            int ax = watch_axioms[i];
            if (unsat_epoch[ax] == epoch) {
                continue;
            }
            int head_id = axiom_head[ax].var;
            if (flag) {
                if (--body_left(ax) != 0) {
                    continue;
                }
                // axiom fires: remove default value of the derived variable
                int default_val = default_value[head_id];
                result.remove(head_id, default_val);
                if (result.count(head_id) == 0) {
                    return result; // conflict, empty domain for derived variable
                }
                enqueue(fact_queue, FactPair(head_id, 1 - default_val), true);
            } else {
                // axiom can not fire anymore
                unsat_epoch[ax] = epoch;
                if (--supp_left(head_id) != 0) {
                    continue;
                }
                // no axiom can fire: remove the non-default value
                int default_val = default_value[head_id];
                result.remove(head_id, 1 - default_val);
                if (result.count(head_id) == 0) {
                    return result; // conflict, empty domain for derived variable
                }
                enqueue(fact_queue, FactPair(head_id, default_val), true);
            }
        }
    }
    return result;
}

int ExtensionStrategyUncertaintyInstance::compute_extension_value(
    const CartesianSet &a, int var) {
    start_new_epoch();

    // Initialize queue with facts known to be true/false in abstract state a
    deque<pair<FactPair, bool>> fact_queue = setup_fact_queue(a);

    while (!fact_queue.empty()) {
        FactPair fact = fact_queue.front().first;
        bool flag = fact_queue.front().second;
        fact_queue.pop_front();

        int fact_id = var_helper.fact_offset[fact.var] + fact.value;
        for (int i = watch_begin[fact_id]; i < watch_begin[fact_id + 1]; ++i) {
            int ax = watch_axioms[i];
            if (unsat_epoch[ax] == epoch) {
                continue;
            }
            const FactPair &head = axiom_head[ax];
            if (flag) {
                if (--body_left(ax) != 0) {
                    continue;
                }
                if (head.var == var) {
                    return head.value; // derived variable is set to axiom head
                }
                enqueue(fact_queue, head, true);
            } else {
                unsat_epoch[ax] = epoch;
                if (--supp_left(head.var) != 0) {
                    continue;
                }
                if (head.var == var) {
                    return default_value[var]; // derived variable is its default value
                }
                enqueue(fact_queue, head, false);
            }
        }
    }
    return UNDEFINED;
}

deque<pair<FactPair, bool>> ExtensionStrategyUncertaintyInstance::setup_fact_queue(
    const CartesianSet &a) {
    deque<pair<FactPair, bool>> fact_queue;
    // Add facts of basic variables that are definitely true/false in a.
    // Order (variables ascending, values ascending, emplace_front) is the
    // same as in the original implementation.
    for (int var_id : var_helper.basic_vars) {
        if (a.has_full_domain(var_id)) {
            continue;
        }
        int domain_size = var_helper.fact_offset[var_id + 1] - var_helper.fact_offset[var_id];
        int size = a.count(var_id);
        for (int val = 0; val < domain_size; ++val) {
            if (a.test(var_id, val)) {
                // if only one value in dom(var,a), the corresponding atom must be true
                if (size == 1) {
                    fact_queue.emplace_front(FactPair(var_id, val), true);
                }
                continue;
            }
            // all values for var not in the domain must be false
            fact_queue.emplace_front(FactPair(var_id, val), false);
        }
    }
    return fact_queue;
}

// Add item to queue if its variable has not been seen yet in this call
void ExtensionStrategyUncertaintyInstance::enqueue(
    deque<pair<FactPair, bool>> &q, FactPair fact, bool x) {
    if (seen_epoch[fact.var] == epoch) {
        return;
    }
    seen_epoch[fact.var] = epoch;
    q.emplace_back(fact, x);
    if (var_helper.is_derived[fact.var]) {
        // For derived variables, also emplace back the inverted fact
        q.emplace_back(FactPair(fact.var, 1 - fact.value), !x);
    }
}

CartesianSet ExtensionStrategyUncertaintyInstance::get_extension(
    const CartesianSet &a) {
    CartesianSet fast = compute_extension(a);
#ifdef VERIFY_EXTENSION
    CartesianSet slow = get_extension_old(a);
    if (!fast.is_superset_of(slow) || !slow.is_superset_of(fast)) {
        // Do not print fast/slow: operator<< asserts on empty domains.
        cerr << "extension mismatch for a = " << a << endl;
        abort();
    }
#endif
    return fast;
}

int ExtensionStrategyUncertaintyInstance::get_extension_value(
    const CartesianSet &a, int var) {
    int fast = compute_extension_value(a, var);
#ifdef VERIFY_EXTENSION
    int slow = get_extension_value_old(a, var);
    if (fast != slow) {
        cerr << "extension value mismatch: var=" << var << " fast=" << fast
             << " slow=" << slow << " a = " << a << endl;
        abort();
    }
#endif
    return fast;
}

// ---- Original implementation, only compiled for verification ----
#ifdef VERIFY_EXTENSION


CartesianSet ExtensionStrategyUncertaintyInstance::get_extension_old(
    const CartesianSet &a) {

    CartesianSet result = a;

    // Count number of unsatisfied body atoms for each axiom
    vector<int> unsat_body_atoms(axioms.size());
    // Count number of axioms possibly supporting each derived variable
    vector<int> supporting_axioms(variables.size());
    // Axioms that we know can not fire because they have at least one unsatisfied body atom
    std::unordered_set<int> unsat_axioms;

    for (OperatorProxy axiom : axioms) {
        unsat_body_atoms[axiom.get_id()] = axiom.get_effects()[0].get_conditions().size();
        supporting_axioms[axiom.get_effects()[0].get_fact().get_var_id()]++;
    }

    // Initialize queue with facts known to be true/false
    std::deque<std::pair<FactPair, bool>> fact_queue = setup_fact_queue(a);
    
    // variables already seen (added to queue)
    vector<bool> seen_vars(variables.size(), false);

    while (!fact_queue.empty()) {
        FactPair fact = fact_queue.front().first;
        bool flag = fact_queue.front().second;
        fact_queue.pop_front();
        if(flag){
            for (OperatorProxy r : axioms){
                if (unsat_axioms.contains(r.get_id())){
                    continue;
                }
                for (FactProxy f : r.get_effects()[0].get_conditions()){
                    if (f.get_pair() == fact){
                        unsat_body_atoms[r.get_id()]--;
                        if (unsat_body_atoms[r.get_id()] == 0) {
                            int head_id = r.get_effects()[0].get_fact().get_var_id();
                            int default_val = variables[head_id].get_default_axiom_value();
                            int derived_val = 1 - default_val;
                            result.remove(head_id, default_val); // remove default value (usually false) for derived variable as axiom fires (and makes it true usually)
                            if (result.count(head_id) == 0) {
                                // std::cout << "var " << head_id <<  "conflict!!" << a << "Unreachable: derived variable domain emptied" << std::endl;
                                return result; // conflict with extension, empty domain for derived variable
                            }
                            enqueue_old(fact_queue, seen_vars, FactPair(head_id, derived_val), true);
                        }
                        break; // assuming each fact only occurs once in an operator precondition
                    }
                }
            }
        } else {
            for (OperatorProxy r : axioms) {
                if (unsat_axioms.contains(r.get_id())) {
                    continue;
                }
                for (FactProxy f : r.get_effects()[0].get_conditions()) {
                    if (f.get_pair() == fact) {
                        int head_id = r.get_effects()[0].get_fact().get_var_id();
                        supporting_axioms[head_id]--;
                        unsat_axioms.insert(r.get_id());
                        if (supporting_axioms[head_id] == 0) {
                            int default_val = variables[head_id].get_default_axiom_value();
                            int derived_val = 1 - default_val;
                            result.remove(head_id, derived_val); // remove true value for derived variable as no axiom can fire
                            if (result.count(head_id) == 0) {
                                // std::cout << "var " << head_id <<  "conflict!!" << a << "Unreachable: derived variable domain emptied" << std::endl;
                                return result; // conflict with extension, empty domain for derived variable
                            }
                            enqueue_old(fact_queue, seen_vars, FactPair(head_id, default_val), true);
                        }
                        break; // assuming each fact only occurs once in an operator precondition
                    }
                }
            }
        }
    }
    return result; 
}


int ExtensionStrategyUncertaintyInstance::get_extension_value_old(
    const CartesianSet &a, int var) {
    // Count number of unsatisfied body atoms for each axiom
    vector<int> unsat_body_atoms(axioms.size());
    // Count number of axioms possibly supporting each derived variable
    vector<int> supporting_axioms(variables.size());
    // Axioms that we know can not fire because they have at least one unsatisfied body atom
    std::unordered_set<int> unsat_axioms;

    for (OperatorProxy axiom : axioms){
        unsat_body_atoms[axiom.get_id()] = axiom.get_effects()[0].get_conditions().size();
        supporting_axioms[axiom.get_effects()[0].get_fact().get_var_id()]++;
    }   

    // Initialize queue with facts known to be true/false in abstract state a
    std::deque<std::pair<FactPair, bool>> fact_queue = setup_fact_queue(a);
    // variables already seen (added to queue)
    vector<bool> seen_vars(variables.size(), false); 
   
    while (!fact_queue.empty()) {
        FactPair fact = fact_queue.front().first;
        bool flag = fact_queue.front().second;
        fact_queue.pop_front();
        if(flag){
            for (OperatorProxy r : axioms){
                if (unsat_axioms.contains(r.get_id())){
                    continue;
                }
                for (FactProxy f : r.get_effects()[0].get_conditions()){
                    if (f.get_pair() == fact){
                        unsat_body_atoms[r.get_id()]--;
                        if (unsat_body_atoms[r.get_id()] == 0) {
                            FactProxy head_atom = r.get_effects()[0].get_fact();
                            if (head_atom.get_var_id() == var) {
                                return head_atom.get_value(); // derived variable is set to axiom head (usually true)
                            } 
                            enqueue_old(fact_queue, seen_vars, head_atom.get_pair(), true);
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
                for (FactProxy f : r.get_effects()[0].get_conditions()){
                    if (f.get_pair() == fact){
                        FactProxy head_atom = r.get_effects()[0].get_fact();
                        int head_id = head_atom.get_var_id();
                        supporting_axioms[head_id]--;
                        unsat_axioms.insert(r.get_id());
                        if (supporting_axioms[head_id] == 0) {
                            if (head_id == var) {
                                return variables[var].get_default_axiom_value(); // derived variable is its default value (usually false)
                            } 
                            enqueue_old(fact_queue, seen_vars, head_atom.get_pair(), false);

                        }
                        break; // assuming each fact only occurs once in an operator precondition
                    }
                }
            }
        }
    }
    return UNDEFINED; 
}


std::deque<std::pair<FactPair, bool>> ExtensionStrategyUncertaintyInstance::setup_fact_queue_old(
    const CartesianSet &a) {
    std::deque<std::pair<FactPair, bool>> fact_queue;
    // Add fact pairs for basic variables definitiely true/false in a to the queue
    for (VariableProxy var_prox : variables) {
        // only iterate over basic variables, break as soon as the first derived variable is encountered
        // (assumes that the variables are ordered this way: first all basic, then all derived variables)
        // This assumption is false.
        if (var_prox.is_derived()){
            continue;
        }

        int var_id = var_prox.get_id();

        // abstract state a has reduced domain for var
        if (!a.has_full_domain(var_id)){
            int size = a.count(var_id);
            for(int val = 0; val < var_prox.get_domain_size(); val++){
                if (a.test(var_id, val)){
                    // if only one value in dom(var,a), the corresponding atom must be true
                    if (size == 1){
                        fact_queue.emplace_front(FactPair(var_id, val), true);
                    }
                    continue;
                }
                // all values for var not in the domain must be false
                fact_queue.emplace_front(FactPair(var_id, val), false);
            }
        }
    }
    return fact_queue;
}


// Add item to queue if not already seen
void ExtensionStrategyUncertaintyInstance::enqueue_old(
    std::deque<std::pair<FactPair, bool>> &q, std::vector<bool> &seen_vars,
    FactPair fact, bool x) {
    if (!seen_vars[fact.var]) {
        seen_vars[fact.var] = true;
        q.emplace_back(fact, x);
        if (variables[fact.var].is_derived()) {
            // For derived variables, also emplace back the inverted Fact
            q.emplace_back(FactPair(fact.var, 1-fact.value), !x);
        }
    }
}

#endif // VERIFY_EXTENSION

// ---- ExtensionStrategyUncertainty (generator) ----

ExtensionStrategyUncertainty::ExtensionStrategyUncertainty(utils::Verbosity verbosity)
    : ExtensionStrategy(verbosity) {
}

unique_ptr<ExtensionStrategyInstance> ExtensionStrategyUncertainty::create(
    const TaskProxy &task_proxy) const {
    return make_unique<ExtensionStrategyUncertaintyInstance>(
        task_proxy.get_axioms(), task_proxy.get_variables());
}

string ExtensionStrategyUncertainty::name() const {
    return "uncertainty";
}

void ExtensionStrategyUncertainty::dump_strategy_specific_options() const {
    /*if (log.is_at_least_normal()) {

    }*/
}

class ExtensionStrategyUncertaintyFeature
    : public plugins::TypedFeature<
          ExtensionStrategy, ExtensionStrategyUncertainty> {
public:
    ExtensionStrategyUncertaintyFeature()
        : TypedFeature("extend_uncertain") {
        document_title("Uncertainty extension strategy");

        document_synopsis(
            "An extension strategy that considers uncertainty semantics "
            "for Cartesian sets with derived variables.");

        add_extension_strategy_options_to_feature(*this);

        document_note("Note", "TODO"); //TODO
    }
    virtual shared_ptr<ExtensionStrategyUncertainty> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<
            ExtensionStrategyUncertainty>(
            get_extension_strategy_arguments_from_options(opts));
    }
};

static plugins::FeaturePlugin<ExtensionStrategyUncertaintyFeature> _plugin;

}
