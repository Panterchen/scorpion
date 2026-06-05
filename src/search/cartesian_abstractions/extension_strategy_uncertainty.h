#ifndef CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_UNCERTAINTY_H
#define CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_UNCERTAINTY_H

#include "extension_strategy.h"
#include <memory>

#include "types.h"
#include "../utils/collections.h"

#include <cassert>
#include <deque>
#include <unordered_set>
#include <vector>

struct FactPair;
class OperatorsProxy;
class TaskProxy;
class CartesianSet;


namespace cartesian_abstractions {

/*
 * Instance: propagates derived variable values according to the corresponding
 * axioms through the Cartesian set using uncertainty semantics.
 */

class ExtensionStrategyUncertaintyInstance : public ExtensionStrategyInstance {
    const AxiomsProxy axioms;
    const VariablesProxy variables;
private:
    std::deque<std::pair<FactPair, bool>> setup_fact_queue(const CartesianSet &a);
    void enqueue(std::deque<std::pair<FactPair, bool>> &q,
        std::vector<bool> &seen_vars, FactPair fact, bool x);
public:
    ExtensionStrategyUncertaintyInstance(const AxiomsProxy &axioms,
        const VariablesProxy &variables);
    virtual ~ExtensionStrategyUncertaintyInstance() override = default;
    virtual CartesianSet get_extension(const CartesianSet &a) override;
    virtual int get_extension_value(const CartesianSet &a, int variable) override;
};

/*
 * Generator: creates ExtensionStrategyUncertaintyInstance objects.
 * Plugin name: extend_uncertain
 */
class ExtensionStrategyUncertainty : public ExtensionStrategy {
protected:
    virtual std::string name() const override;
    virtual void dump_strategy_specific_options() const override;
public:
    explicit ExtensionStrategyUncertainty(utils::Verbosity verbosity);
    virtual ~ExtensionStrategyUncertainty() override = default;
    virtual std::unique_ptr<ExtensionStrategyInstance> create(
        const TaskProxy &task_proxy) const override;
};
}

#endif
