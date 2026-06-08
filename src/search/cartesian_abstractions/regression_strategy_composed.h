#ifndef CARTESIAN_ABSTRACTIONS_REGRESSION_STRATEGY_COMPOSED_H
#define CARTESIAN_ABSTRACTIONS_REGRESSION_STRATEGY_COMPOSED_H

#include "regression_strategy.h"
#include "regression_strategy_naive.h"
#include "extension_strategy.h"

#include <memory>
#include <optional>
#include <vector>

class TaskProxy;

namespace cartesian_abstractions {

/*
 * Instance: composes naive regression with the extension strategy chosen at
 * the cegar() level. For each deviation split query, first computes the naive
 * regression of the target abstract state t through operator o, then applies
 * the extension strategy to determine derived variable values in the regressed
 * set. The extended result is cached per (t, op_id) call via prepare() and
 * reused across all per-variable get_wanted_values() queries for the same
 * transition.
 */
class RegressionStrategyComposedInstance : public RegressionStrategyInstance {
    const VariablesProxy variables;
    RegressionStrategyNaiveInstance naive_instance;
    std::unique_ptr<ExtensionStrategyInstance> extension_instance;
    std::optional<CartesianSet> cached_extended_regression;
public:
    RegressionStrategyComposedInstance(
        const VariablesProxy &variables,
        const OperatorsProxy &operators,
        std::unique_ptr<ExtensionStrategyInstance> extension_instance);
    virtual ~RegressionStrategyComposedInstance() override = default;
    virtual void prepare(const CartesianSet &t, int op_id) override;
    virtual CartesianSet get_regression(
        const CartesianSet &a, int operator_id) override;
    virtual std::vector<int> get_regression_values(
        const CartesianSet &a, int variable, int operator_id) override;
    virtual std::vector<int> get_wanted_values(
        const AbstractState &a, const AbstractState &t,
        int variable, int operator_id) override;
};

/*
 * Generator: creates RegressionStrategyComposedInstance objects.
 * Takes the cegar()-level ExtensionStrategy as a parameter to create() so
 * the same extension strategy used in TransitionRewirer is also used here.
 * No extension_strategy member needed — the generator is stateless beyond
 * verbosity.
 * Plugin name: regress_composed
 */
class RegressionStrategyComposed : public RegressionStrategy {
protected:
    virtual std::string name() const override;
    virtual void dump_strategy_specific_options() const override;
public:
    explicit RegressionStrategyComposed(utils::Verbosity verbosity);
    virtual ~RegressionStrategyComposed() override = default;
    virtual std::unique_ptr<RegressionStrategyInstance> create(
        const TaskProxy &task_proxy,
        const std::shared_ptr<ExtensionStrategy> &extension_strategy) const override;
};

}

#endif