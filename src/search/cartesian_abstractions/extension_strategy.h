#ifndef CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_H
#define CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_H

#include <utility>
#include "cartesian_set.h"
#include "../utils/logging.h"

#include <memory>
#include <string>

class TaskProxy;

namespace plugins {
class Options;
class Feature;
}


namespace cartesian_abstractions {

/*
 * An ExtensionStrategyInstance is a per-task worker object that determines
 * values of derived variables in Cartesian sets. It is created once per CEGAR
 * run by its corresponding ExtensionStrategy generator object.
 *
 * Functionally an extension strategy describes how values of derived variables
 * are determined in cartesian sets depending on the chosen type.
 *
 * We distinguish X types of extension strategies:
 * - naive,
 * - uncertainty semantics
 *
 * Naive: no extension, cartesian set is returned as is
 *
 * Uncertainty semantics: A derived variable is true if there is a supporting
 * axiom whose body only contains atoms that are true in every concrete state
 * in the abstract state.
 * A derived variable is false if for all supporting axiom the body contains
 * at least one atom that is false in every concrete state in the abstract state.
 * Otherwise the value of the derived variable is unknown, i.e. the cartesian set
 * contains both domain values for the variable.
 */

class ExtensionStrategyInstance {
public:
    explicit ExtensionStrategyInstance();
    virtual ~ExtensionStrategyInstance() = default;
    virtual CartesianSet get_extension(const CartesianSet &a) = 0;
    virtual int get_extension_value(const CartesianSet &a, int var) = 0;
    /*
        Precise variants are only used for few checks where precision is worth
        the computational costs (consistency of new states, checking for non-
        induced transitions when flaw search stalls).
        By default the precise extensions default to the regular variants,
        hybrid extension strategy overrides them.
     */
    virtual CartesianSet get_precise_extension(const CartesianSet &a) {
        return get_extension(a);
    }
    virtual int get_precise_extension_value(const CartesianSet &a, int var) {
        return get_extension_value(a, var);
    }
};

/*
 * An ExtensionStrategy is a generator / plugin object created once at parse
 * time. It is shared across all subtasks and creates a fresh
 * ExtensionStrategyInstance per CEGAR run via create().
 */

class ExtensionStrategy {
protected:
    mutable utils::LogProxy log;
    virtual std::string name() const = 0;
    virtual void dump_strategy_specific_options() const = 0;
public:
    explicit ExtensionStrategy(utils::Verbosity verbosity);
    virtual ~ExtensionStrategy() = default;
    void dump_options() const;
    virtual std::unique_ptr<ExtensionStrategyInstance> create(
        const TaskProxy &task_proxy) const = 0;
    /*
        Quick check that returns true iff the precise extension is more precise
        than the regular one. CEGAR then tries to prune non-induced transitions
        at which refinement stalls.
     */
    virtual bool supports_precise_checks() const {
        return false;
    }
};

extern void add_extension_strategy_options_to_feature(plugins::Feature &feature);
extern std::tuple<utils::Verbosity> get_extension_strategy_arguments_from_options(
    const plugins::Options &opts);

}

#endif
