#ifndef CARTESIAN_ABSTRACTIONS_REGRESSION_STRATEGY_H
#define CARTESIAN_ABSTRACTIONS_REGRESSION_STRATEGY_H

#include <utility>
#include "cartesian_set.h"

namespace cartesian_abstractions {

/*
  A regression strategy describes how values of derived variables are determined in cartesian sets.

  We distinguish X types of regression strategies: naive, uncertainty semantics

  Naive: derived variables only have a specific value if in preconsition, otherwise whole domain

  Uncertainty semantics: naive regression is extended using uncertinty semantics

*/

class RegressionStrategy {
public:
    explicit RegressionStrategy();
    virtual ~RegressionStrategy() = default;
    // the following two functions might not be usefull the way they are defined now
    virtual CartesianSet get_regression(const CartesianSet &a, int operator_id) = 0;
    virtual vector<int> get_regression_values(const CartesianSet &a, int variable, int operator_id) = 0;
};
}

#endif
