#ifndef CARTESIAN_ABSTRACTIONS_REGRESSION_STRATEGY_NAIVE_H
#define CARTESIAN_ABSTRACTIONS_REGRESSION_STRATEGY_NAIVE_H

#include "regression_strategy.h"

#include <memory>

#include "types.h"

#include "../utils/collections.h"

#include <cassert>
#include <deque>
#include <vector>

class CartesianSet;

namespace cartesian_abstractions {
class RegressionStrategyNaive : public RegressionStrategy {
public:
    RegressionStrategyNaive(const VariablesProxy &variables, const OperatorsProxy &operators);
    virtual ~RegressionStrategyNaive() override = default;
    virtual CartesianSet get_regression(const CartesianSet &a, int operator_id) override;
    virtual vector<int> get_regression_values(const CartesianSet &a, int variable, int operator_id) override;
};
}

#endif
