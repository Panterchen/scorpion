#ifndef CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_HYBRID_H
#define CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_HYBRID_H

#include "extension_strategy.h"

#include <memory>

#include "types.h"

#include "../utils/collections.h"

#include <cassert>
#include <deque>
#include <vector>

namespace cartesian_abstractions {

/*
 * Instance: delegates the regular extension (used for rewiring, regression
 * approximation, ...) to the "fast" instance and the precise extension (used
 * for consistency checks of new abstract states and for refuting stalled
 * transitions) to the "precise" instance.
 */
class ExtensionStrategyHybridInstance : public ExtensionStrategyInstance {
    std::unique_ptr<ExtensionStrategyInstance> fast;
    std::unique_ptr<ExtensionStrategyInstance> precise;
public:
    ExtensionStrategyHybridInstance(
        std::unique_ptr<ExtensionStrategyInstance> fast,
        std::unique_ptr<ExtensionStrategyInstance> precise);
    virtual ~ExtensionStrategyHybridInstance() override = default;
    virtual CartesianSet get_extension(const CartesianSet &a) override;
    virtual int get_extension_value(const CartesianSet &a, int var) override;
    virtual CartesianSet get_precise_extension(const CartesianSet &a) override;
    virtual int get_precise_extension_value(
        const CartesianSet &a, int var) override;
};

/*
 * Generator: combines two extension strategies.
 * Plugin name: extend_hybrid
 */
class ExtensionStrategyHybrid : public ExtensionStrategy {
    const std::shared_ptr<ExtensionStrategy> fast;
    const std::shared_ptr<ExtensionStrategy> precise;
protected:
    virtual std::string name() const override;
    virtual void dump_strategy_specific_options() const override;
public:
    ExtensionStrategyHybrid(
        const std::shared_ptr<ExtensionStrategy> &fast,
        const std::shared_ptr<ExtensionStrategy> &precise,
        utils::Verbosity verbosity);
    virtual ~ExtensionStrategyHybrid() override = default;
    virtual std::unique_ptr<ExtensionStrategyInstance> create(
        const TaskProxy &task_proxy) const override;
    virtual bool supports_precise_checks() const override {
        return true;
    }
};
}

#endif
