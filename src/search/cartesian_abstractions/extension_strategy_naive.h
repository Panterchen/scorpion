#ifndef CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_NAIVE_H
#define CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_NAIVE_H

#include "extension_strategy.h"

#include <memory>

#include "types.h"

#include "../utils/collections.h"

#include <cassert>
#include <deque>
#include <vector>

class CartesianSet;


namespace cartesian_abstractions {

/*
 * Instance: does nothing - returns the Cartesian set as-is.
 */



class ExtensionStrategyNaiveInstance : public ExtensionStrategyInstance {
public:
    explicit ExtensionStrategyNaiveInstance();
    virtual ~ExtensionStrategyNaiveInstance() override = default;
    virtual CartesianSet get_extension(const CartesianSet &a) override;
    virtual int get_extension_value(const CartesianSet &a, int variable) override;
};

/*
 * Generator: creates ExtensionStrategyNaiveInstance objects.
 * Plugin name : extend_naive
 */

class ExtensionStrategyNaive : public ExtensionStrategy {
protected:
    virtual std::string name() const override;
    virtual void dump_strategy_specific_options() const override;
public:
    explicit ExtensionStrategyNaive(utils::Verbosity verbosity);
    virtual ~ExtensionStrategyNaive() override = default;
    virtual std::unique_ptr<ExtensionStrategyInstance> create(
        const TaskProxy &task_proxy) const override;
};

}

#endif
