#include "extension_strategy_uncertainty.h"
#include "cartesian_set.h"

#include <cassert>

using namespace std;

namespace cartesian_abstractions {
ExtensionStrategyUncertainty::ExtensionStrategyUncertainty(
    const AxiomsProxy &axioms, const VariablesProxy &variables)
    : axioms(axioms), variables(variables) { 
}

CartesianSet ExtensionStrategyUncertainty::get_extension(const CartesianSet &a){

    CartesianSet result = a;

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
                            result.remove(head_id, 0); // remove false value for derived variable as axiom fires and makes it true
                            if (result.count(head_id) == 0) {
                                return result; // conflict with extension, empty domain for derived varibale TODO: return completely empty cartesian set?
                            }
                            enqueue(fact_queue, seen_vars, FactPair(head_id, 1), true);
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
                        int head_id = r.get_effects()[0].get_fact().get_var_id();
                        supporting_axioms[head_id]--;
                        unsat_axioms.insert(r.get_id());
                        if (supporting_axioms[head_id] == 0) {
                            result.remove(head_id, 1); // remove true value for derived variable as no axiom can fire
                            if (result.count(head_id) == 0) {
                                return result; // conflict with extension, empty domain for derived varibale TODO: return completely empty cartesian set?
                            }
                            enqueue(fact_queue, seen_vars, FactPair(head_id, 0), true);
                        }
                        break; // assuming each fact only occurs once in an operator precondition
                    }
                }
            }
        }
    }
    return result; 
}


int ExtensionStrategyUncertainty::get_extension_value(const CartesianSet &a, int var) {
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
                for (FactProxy f : r.get_effects()[0].get_conditions()){
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




std::deque<std::pair<FactPair, bool>> ExtensionStrategyUncertainty::setup_fact_queue(const CartesianSet &a){
    std::deque<std::pair<FactPair, bool>> fact_queue;

    // Add fact pairs for basic variables definitiely true/false in a to the queue
    for (VariableProxy var_prox : variables){

        // only iterate over basic variables
        if (var_prox.is_derived()){
            break;
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
void ExtensionStrategyUncertainty::enqueue(std::deque<std::pair<FactPair, bool>> &q, std::vector<bool> &seen_vars, FactPair fact, bool x) {
    if (!seen_vars[fact.var]){
        seen_vars[fact.var] = true;
        q.emplace_back(fact, x);
    }
}

}
