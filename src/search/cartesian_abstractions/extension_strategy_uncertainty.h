#ifndef CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_UNCERTAINTY_H
#define CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_UNCERTAINTY_H

#include "extension_strategy.h"

#include <memory>

#include "types.h"

#include "../utils/collections.h"

#include <cassert>
#include <deque>
#include <vector>

struct FactPair;
class OperatorsProxy;
class TaskProxy;
class CartesianSet;


namespace cartesian_abstractions {
class ExtensionStrategyUncertainty : public ExtensionStrategy {
    const AxiomsProxy axioms;
    const VariablesProxy variables;
private:
    std::deque<std::pair<FactPair, bool>> setup_fact_queue(const CartesianSet &a);
    void enqueue(std::deque<std::pair<FactPair, bool>> &q, std::vector<bool> &seen_vars, FactPair fact, bool x);
public:
    ExtensionStrategyUncertainty(const AxiomsProxy &axioms, const VariablesProxy &variables);
    virtual ~ExtensionStrategyUncertainty() override = default;
    virtual CartesianSet get_extension(const CartesianSet &a) override;
    virtual int get_extension_value(const CartesianSet &a, int variable) override;
};
}

#endif
