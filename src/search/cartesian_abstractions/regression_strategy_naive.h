#ifndef CARTESIAN_ABSTRACTIONS_REGRESSION_STRATEGY_NAIVE_H
#define CARTESIAN_ABSTRACTIONS_REGRESSION_STRATEGY_NAIVE_H

#include "regression_strategy.h"

#include <memory>

#include "types.h"

#include "../utils/collections.h"
#include "utils.h"
#include "cartesian_set.h"
#include "abstract_state.h"

#include <cassert>
#include <deque>
#include <vector>

struct FactPair;
class OperatorProxy;
class TaskProxy;
class CartesianSet;

namespace cartesian_abstractions {
class RegressionStrategyNaive : public RegressionStrategy {
    const VariablesProxy variables;
    std::vector<std::vector<FactPair>> preconditions_by_operator;
    std::vector<std::vector<FactPair>> postconditions_by_operator;

public:
    RegressionStrategyNaive(const VariablesProxy &variables, const OperatorsProxy &operators);
    virtual ~RegressionStrategyNaive() override = default;
    virtual CartesianSet get_regression(const CartesianSet &a, int operator_id) override;
    virtual std::vector<int> get_regression_values(const CartesianSet &a, int variable, int operator_id) override;
    virtual std::vector<int> get_wanted_values(const AbstractState &a, const AbstractState &t, int variable, int operator_id);
};
}

#endif
