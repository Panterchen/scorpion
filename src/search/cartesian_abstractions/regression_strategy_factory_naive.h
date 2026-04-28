#ifndef CARTESIAN_ABSTRACTIONS_REGRESSION_STRATEGY_FACTORY_NAIVE_H
#define CARTESIAN_ABSTRACTIONS_REGRESSION_STRATEGY_FACTORY_NAIVE_H

#include "regression_strategy.h"

namespace cartesian_abstractions {
class AxiomProxy;

class RegressionStrategyFactoryNaive : public RegressionStrategyFactory {
protected:
    virtual std::string name() const override;
    virtual void dump_strategy_specific_options() const override;
public:
    RegressionStrategyFactoryNaive(
        utils::Verbosity verbosity);
    virtual std::unique_ptr<RegressionStrategy> compute_regression_strategy(
        const TaskProxy &task_proxy) const override;
};
}

#endif
