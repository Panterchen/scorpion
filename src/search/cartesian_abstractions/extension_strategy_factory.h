#ifndef CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_FACTORY_H
#define CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_FACTORY_H

#include "../utils/logging.h"

#include <memory>
#include <string>

class TaskProxy;

namespace plugins {
class Options;
class Feature;
}

namespace cartesian_abstractions {
class ExtensionStrategy;

class ExtensionStrategyFactory {
protected:
    mutable utils::LogProxy log;

    virtual std::string name() const = 0;
    virtual void dump_strategy_specific_options() const = 0;
public:
    ExtensionStrategyFactory(utils::Verbosity verbosity);
    virtual ~ExtensionStrategyFactory() = default;
    void dump_options() const;
    virtual std::unique_ptr<ExtensionStrategy> compute_extension_strategy(
        const TaskProxy &task_proxy) const = 0;
};

extern void add_extension_strategy_options_to_feature(plugins::Feature &feature);
extern std::tuple<utils::Verbosity> get_extension_strategy_arguments_from_options(
    const plugins::Options &opts);
}

#endif
