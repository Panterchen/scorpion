#include "extension_strategy_naive.h"
#include "cartesian_set.h"

#include <cassert>

using namespace std;

namespace cartesian_abstractions {
RegressionStrategyNaive::RegressionStrategyNaive(
    const variablesProxy &variables, const OperatorsProxy &operators)
    : variables(variables), operators(operators) { 
}

//TODO: generally for any of these methods operators preconditions/effects are needed which should probably be precomputed or passed as arguments, see eg. flaw_search or transition_rewirer

CartesianSet RegressionStrategyNaive::get_regression(const CartesianSet &a, int operator_id){
    CartesianSet result = a;
    // TODO: this function might not be necessary for splits directly but could come in handy when using more sophisticated regression, i.e. regression+extension
    // as extension takes a full cartesian set
    return result; 
}


vector<int> RegressionStrategyNaive::get_regression_values(const CartesianSet &a, int variable, int operator_id) {
    vector<int> result;
    // TODO: this can either return the correct regression or can be used to deternmine "wanted" values (see flaw_search::get_deviation_splits)
    // it is probably not necessary to compute the whole regression, especially as splits can only be done in a limited amount of cases (see flaw_search::get_deviation_splits)
    return result; 
}


}
