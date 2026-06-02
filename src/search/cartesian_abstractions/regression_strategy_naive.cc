#include "regression_strategy_naive.h"
#include "cartesian_set.h"
#include "abstract_state.h"
#include "utils.h"
#include "types.h"
#include <cassert>

using namespace std;

namespace cartesian_abstractions {
RegressionStrategyNaive::RegressionStrategyNaive(
    const VariablesProxy &variables, const OperatorsProxy &operators)
    : variables(variables),
        preconditions_by_operator(compute_preconditions_by_operator(operators)),
        postconditions_by_operator(compute_postconditions_by_operator(operators))
{
}

CartesianSet RegressionStrategyNaive::get_regression(const CartesianSet &a, int operator_id){
    CartesianSet result = a;
    /* This function calculates the naive regression overapproximation for a
     * Cartesian set 'a' and an operator 'o' as a full Cartesian set.
     * Used when the full regressed Cartesian set is needed, e.g. as input
     * to get_extension() in the uncertainty strategy. For deviation splits
     * in flaw search, get_wanted_values() is more efficient since it only
     * needs per-variable results and can avoid building the full set.
     */
    for (VariableProxy var_prox : variables) {
	// iterate over all variables and determine regression approximation
	int var_id = var_prox.get_id();
	vector<int> reg_values = get_regression_values(a, var_id, operator_id);
	// replace result values for var with reg_values
	result.remove_all(var_id);
	for (int val : reg_values) {
	    result.add(var_id, val);
	}
    }
    return result; 
}


vector<int> RegressionStrategyNaive::get_regression_values(const CartesianSet &a, int variable, int operator_id) {
    vector<int> result;
    /* This function calculates the naive regression overapproximation for a
     * variable, based on a Cartesian set 'a' and an operator 'o' as a vector
     * of regression values for the given variable.
    */
    VariableProxy var = variables[variable];
    int domain_size = var.get_domain_size();

    // lookup precondition and postcondition values for variable var in operator o.
    // Note: postconditions_by_operator encodes the postcondition as follows:
    // - if o has an effect on var: post_val = effect value
    // - if o has a precondition but no effect on var: post_val = precondition value
    // - if o has neither: post_val = UNDEFINED
    // This mirrors the semantics in compute_postconditions() in utils.cc.
    int pre_val = lookup_value(preconditions_by_operator[operator_id], variable);
    int post_val = lookup_value(postconditions_by_operator[operator_id], variable);

    if (var.is_derived()) {
        // Derived variables: distinguish two cases
	if (pre_val != UNDEFINED) {
	    // if a precondition is defined on var, the regr is the precondition
	    result.push_back(pre_val);
	} else {    // the regression is approximated as the full domain of var
	    for (int v = 0; v < domain_size; v++) {
	        result.push_back(v);
	    }
	}
    } else {  // basic variables: 4 possible cases
	vector<int> dom_a_var = a.get_values(variable);
	bool post_val_in_a = find(dom_a_var.begin(), dom_a_var.end(), post_val) != dom_a_var.end();
	if (post_val == UNDEFINED) {  // no effect, no precondition
	    result = dom_a_var; // regression is the current domain of var in a
	} else if (post_val != UNDEFINED && !post_val_in_a) { // effect not contained in a
	    // regression is empty, a is unreachable via o
	} else if (pre_val != UNDEFINED && post_val_in_a) { // precondition, postcondition in a
	    result.push_back(pre_val); // regression is the precondition value
	} else if (pre_val != post_val && post_val_in_a) {
	    // Effect only (no precondition, post_val != pre_val means pre_val==UNDEFINED
	    // since post_val was initialized from pre_val and overwritten by the effect).
	    // Naive regression opens var to its full domain.
	    for (int v = 0; v < domain_size; v++) {
	        result.push_back(v);
	    }
	}
    }
    return result; 
}

vector<int> RegressionStrategyNaive::get_wanted_values(
    const AbstractState &a, const AbstractState &t, int variable, int operator_id) {
    /* This function calculates the wanted vector used to determine a deviation
     * split in flaw_search->get_deviation_splits.
     * The logic for the wanted vector with naive regression is as follows:
     * Let the desired abstract transition be (a, o, t) and the deviation be
       (a, o, b). We distinguish three cases for each basic variable v:

      pre(o)[v] defined: no split possible since o is applicable in s.
      pre(o)[v] undefined, eff(o)[v] defined: no split possible since regression
      adds whole domain.
      pre(o)[v] and eff(o)[v] undefined: if s[v] \notin t[v],
      wanted = intersect(a[v], b[v]).

      For derived variables v we distinguish two cases:
      pre(o)[v] defined: no split possible since o is applicable in s.
      else: regr(t, o)[v] is approximated as the entire domain of v, so
      wanted = intersect(a[v], domain(v)) = a[v]
      -> this will never produce a useful split, handled in get_deviation_splits
     */
    if (variables[variable].is_derived()) { // wanted = a[v]
        return a.get_cartesian_set().get_values(variable);
    } else {
        vector<int> wanted;
        int domain_size = variables[variable].get_domain_size();
        for (int v = 0; v < domain_size; v++) {
            if (a.contains(variable, v) && t.contains(variable, v)) {
                wanted.push_back(v);
            }
        } // wanted = intersect(a[v], t[v])
        return wanted;
    }
}


}
