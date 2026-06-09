#include "regression_strategy.h"
#include "regression_strategy_composed.h"
#include "cartesian_set.h"
#include "abstract_state.h"

#include "../plugins/plugin.h"

#include <cassert>

using namespace std;

namespace cartesian_abstractions {

// ----- RegressionStrategyComposedInstance -----

RegressionStrategyComposedInstance::RegressionStrategyComposedInstance(
    const VariablesProxy &variables,
    const OperatorsProxy &operators,
    unique_ptr<ExtensionStrategyInstance> extension_instance)
    : variables(variables),
      naive_instance(variables, operators),
      extension_instance(move(extension_instance)),
      cached_extended_regression(nullopt) {
}

void RegressionStrategyComposedInstance::prepare(
    const CartesianSet &t, int op_id) {
    /*
     * Computes the extended regression as extension(regr_naive(t, op_id)) and
     * caches the result. The cache is then used for all the other functions
     * in this class.
     * Called once per (t, op_id) pair in create_split before the per-variable
     * get_wanted_values() queries for that transition.
     */
    CartesianSet naive_regression = naive_instance.get_regression(t, op_id);
    // TODO: remove after debugging
    for (VariableProxy var: variables) {
        if (naive_regression.get_values(var.get_id()).empty()) {
            cout<<"WARNING: "<<var.get_id()<<"'s domain is empty after naive regression"<<endl;
        }
    }
    cached_extended_regression =
        extension_instance->get_extension(naive_regression);
    // TODO: remove after debugging
    CartesianSet extSet = cached_extended_regression.value();
    for (VariableProxy var: variables) {
        if (extSet.get_values(var.get_id()).empty()) {
            cout<<"WARNING: "<<var.get_id()<<"'s domain is empty after extended regression"<<endl;
        }
    }
}

CartesianSet RegressionStrategyComposedInstance::get_regression(
    const CartesianSet &a, int operator_id) {
    /*
     * returns the cached precomputed full regression as Cartesian set.
     * prepare() has to be called in advance to precompute the extended
     * regression via naive regression + extension strategy.
    */
    if (!cached_extended_regression.has_value()) {
        prepare(a, operator_id);
    }
    assert(cached_extended_regression.has_value());
    return cached_extended_regression.value();
}

vector<int> RegressionStrategyComposedInstance::get_regression_values(
    const CartesianSet &a, int variable, int operator_id) {
    /*
     * returns the cached precomputed regression values for variable.
     * prepare() has to be called in advance to precompute the extended
     * regression via naive regression + extension strategy.
    */
    if (!cached_extended_regression.has_value()) {
        prepare(a, operator_id);
    }
    assert(cached_extended_regression.has_value());
    return cached_extended_regression->get_values(variable);
}

vector<int> RegressionStrategyComposedInstance::get_wanted_values(
    const AbstractState &a, const AbstractState &t,
    int variable, int operator_id) {
    /*
     * Uses the cached extended regression (set by prepare()) to compute
     * wanted values for deviation split computation.
     *
     * wanted = intersect(a[v], extension(regr_naive(t, op_id))[v])
     *
     * For both basic and derived variables the extended regression already
     * determined the correct domains, so the intersection is handled uniformly.
     * prepare() must be called before this function for each (t, op_id) pair.
     */
    if (!cached_extended_regression.has_value()) {
        prepare(t.get_cartesian_set(), operator_id);
    }
    assert(cached_extended_regression.has_value());
    vector<int> wanted;
    const CartesianSet &ext_regr = cached_extended_regression.value();
    int domain_size = variables[variable].get_domain_size();
    for (int v = 0; v < domain_size; v++) {
        if (a.contains(variable, v) && ext_regr.test(variable, v)) {
            wanted.push_back(v);
        }
    }
    if (wanted.empty()) {
        std::cout << "No wanted values found for var " << variable << std::endl;
        std::cout << "var domain in a: " << a.get_cartesian_set().get_values(variable) << std::endl;
        std::cout << "var domain in ext_regr: " << ext_regr.get_values(variable) << std::endl;
    }
    return wanted;
}

// ----- RegressionStrategyComposed (generator) -----

RegressionStrategyComposed::RegressionStrategyComposed(
    utils::Verbosity verbosity)
    : RegressionStrategy(verbosity) {
}

unique_ptr<RegressionStrategyInstance> RegressionStrategyComposed::create(
    const TaskProxy &task_proxy,
    const shared_ptr<ExtensionStrategy> &extension_strategy) const {
    return make_unique<RegressionStrategyComposedInstance>(
        task_proxy.get_variables(),
        task_proxy.get_operators(),
        extension_strategy->create(task_proxy));
}

string RegressionStrategyComposed::name() const {
    return "composed";
}

void RegressionStrategyComposed::dump_strategy_specific_options() const {
}

class RegressionStrategyComposedFeature
    : public plugins::TypedFeature
          <RegressionStrategy, RegressionStrategyComposed> {
public:
    RegressionStrategyComposedFeature()
        : TypedFeature("regress_composed") {
        document_title("Composed regression strategy");
        document_synopsis(
            "A regression strategy that applies the cegar()-level extension "
            "strategy on top of naive regression to obtain tighter derived "
            "variable domains in deviation split computation.");
        add_regression_strategy_options_to_feature(*this);
    }
    virtual shared_ptr<RegressionStrategyComposed> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<RegressionStrategyComposed>(
            get_regression_strategy_arguments_from_options(opts));
    }
};

static plugins::FeaturePlugin<RegressionStrategyComposedFeature> _plugin;

}