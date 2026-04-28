#ifndef CARTESIAN_ABSTRACTIONS_REGRESSION_STRATEGY_FACTORY_H
#define CARTESIAN_ABSTRACTIONS_REGRESSION_STRATEGY_FACTORY_H

#include "../utils/logging.h"

#include <memory>
#include <string>

class TaskProxy;

namespace plugins {
class Options;
class Feature;
}

namespace cartesian_abstractions {
class RegressionStrategy;

class RegressionStrategyFactory {
protected:
    mutable utils::LogProxy log;

    virtual std::string name() const = 0;
    virtual void dump_strategy_specific_options() const = 0;
public:
    RegressionStrategyFactory(utils::Verbosity verbosity);
    virtual ~RegressionStrategyFactory() = default;
    void dump_options() const;
    virtual std::unique_ptr<RegressionStrategy> compute_regression_strategy(
        const TaskProxy &task_proxy) const = 0;
};

extern void add_regression_strategy_options_to_feature(plugins::Feature &feature);
extern std::tuple<utils::Verbosity> get_regression_strategy_arguments_from_options(
    const plugins::Options &opts);
}

#endif