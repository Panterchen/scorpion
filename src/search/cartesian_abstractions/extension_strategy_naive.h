#ifndef CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_UNCERTAINTY_H
#define CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_UNCERTAINTY_H

#include "extension_strategy.h"

#include <memory>

#include "types.h"

#include "../utils/collections.h"

#include <cassert>
#include <deque>
#include <vector>

class CartesianSet;


namespace cartesian_abstractions {
class ExtensionStrategyNaive : public ExtensionStrategy {
public:
    ExtensionStrategyNaive();
    virtual ~ExtensionStrategyNaive() override = default;
    virtual CartesianSet get_extension(const CartesianSet &a) override;
    virtual int get_extension_value(const CartesianSet &a, int variable) override;
};
}

#endif
