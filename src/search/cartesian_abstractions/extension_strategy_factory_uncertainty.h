#ifndef CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_FACTORY_UNCERTAINTY_H
#define CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_FACTORY_UNCERTAINTY_H

#include "extension_strategy_factory.h"

namespace cartesian_abstractions {
class AxiomProxy;

class ExtensionStrategyFactoryUncertainty : public ExtensionStrategyFactory {
protected:
    virtual std::string name() const override;
    virtual void dump_strategy_specific_options() const override;
public:
    ExtensionStrategyFactoryUncertainty(
        utils::Verbosity verbosity);
    virtual std::unique_ptr<ExtensionStrategy> compute_extension_strategy(
        const TaskProxy &task_proxy) const override;
};
}

#endif
