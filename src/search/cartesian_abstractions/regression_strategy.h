#ifndef CARTESIAN_ABSTRACTIONS_REGRESSION_STRATEGY_H
#define CARTESIAN_ABSTRACTIONS_REGRESSION_STRATEGY_H

#include <utility>
#include "cartesian_set.h"
#include "abstract_state.h"
#include "extension_strategy.h"

#include "../utils/logging.h"

#include <memory>
#include <string>
#include <optional>


class TaskProxy;

namespace plugins {
class Options;
class Feature;
}

namespace cartesian_abstractions {

/*
 * A RegressionStrategyInstance is a per-task worker object that determines the
 * regression for a Cartesian set given an operator. It is created once per
 * CEGAR run by its corresponding RegressionStrategy generator object.
 *
 * Functionally, a regression strategy describes how values of derived variables
 * are determined in cartesian sets in operator regression.
 *
 * We distinguish X types of regression strategies:
 * - naive,
 * - composed_via_ext
 *
 * Naive: derived variables only have a specific value if in precondition,
 * otherwise whole domain
 *
 * Composed via Extension: naive regression is extended using the chosen
 * extension strategy, i.e. uncertainty semantics.
 *
*/

class RegressionStrategyInstance {
public:
    explicit RegressionStrategyInstance();
    virtual ~RegressionStrategyInstance() = default;
    virtual void prepare(const CartesianSet &, int) {} // no-op function used for composed regression
    // the first two functions can be used to explicitly calculate the regression
    virtual CartesianSet get_regression(const CartesianSet &a, int operator_id) = 0;
    virtual std::vector<int> get_regression_values(const CartesianSet &a, int variable, int operator_id) = 0;
    // the third function implements usecase of the regression in the deviation split calculation of Flaw Search
    virtual std::vector<int> get_wanted_values(const AbstractState &a, const AbstractState &t, int variable, int operator_id) = 0;
};

/*
 * A RegressionStrategy is a generator / plugin object created once at parse
 * time. It is shared across all subtasks and creates a fresh
 * RegressionStrategyInstance per CEGAR run via create().
 */

class RegressionStrategy {
protected:
    mutable utils::LogProxy log;
    virtual std::string name() const = 0;
    virtual void dump_strategy_specific_options() const = 0;
public:
    explicit RegressionStrategy(utils::Verbosity verbosity);
    virtual ~RegressionStrategy() = default;
    void dump_options() const;
    virtual std::unique_ptr<RegressionStrategyInstance> create(
        const TaskProxy &task_proxy,
        const std::shared_ptr<ExtensionStrategy> &extension_strategy) const = 0;
};

extern void add_regression_strategy_options_to_feature(plugins::Feature &feature);
extern std::tuple<utils::Verbosity> get_regression_strategy_arguments_from_options(
    const plugins::Options &opts);

}

#endif
