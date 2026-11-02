#ifndef CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_UNCERTAINTY_H
#define CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_UNCERTAINTY_H

#include "extension_strategy.h"
#include "types.h"
#include "utils.h"

#include "../utils/collections.h"

#include <cassert>
#include <deque>
#include <memory>
#include <unordered_set>
#include <vector>

struct FactPair;
class OperatorsProxy;
class TaskProxy;
class CartesianSet;

// #define VERIFY_EXTENSION

namespace cartesian_abstractions {

/*
 * Instance: propagates derived variable values according to the corresponding
 * axioms through the Cartesian set using uncertainty semantics.
 */

class ExtensionStrategyUncertaintyInstance : public ExtensionStrategyInstance {
    const AxiomsProxy axioms;
    const VariablesProxy variables;
    const VariableHelper var_helper;

    std::vector<FactPair> axiom_head;      // per Axiom (Head-Fact)
    std::vector<int> body_size;            // per Axiom, no-duplicate fact-count
    std::vector<int> supporting_base;      // per var: count axiom with head-var
    std::vector<int> default_value;        // per var: only for derived, else -1
    // CSR-Watch-Lists: Axioms, with fact f in the body are in
    // watch_axioms[watch_begin[f] .. watch_begin[f+1]), asc with Axiom-ID
    std::vector<int> watch_begin;
    std::vector<int> watch_axioms;

    // --- Scratch-buffer with epochs (not reentrant) ---
    uint32_t epoch = 0;
    std::vector<uint32_t> body_epoch, unsat_epoch, supp_epoch, seen_epoch;
    std::vector<int> body_cnt, supp_cnt;

private:
    void start_new_epoch();
    int &body_left(int axiom_id);
    int &supp_left(int var_id);
    CartesianSet compute_extension(const CartesianSet &a);
    int compute_extension_value(const CartesianSet &a, int var);
    std::deque<std::pair<FactPair, bool>> setup_fact_queue(const CartesianSet &a);
    void enqueue(std::deque<std::pair<FactPair, bool>> &q, FactPair fact,
        bool x);

#ifdef VERIFY_EXTENSION
    // original implementation, kept only for verification
    CartesianSet get_extension_old(const CartesianSet &a);
    int get_extension_value_old(const CartesianSet &a, int var);
    std::deque<std::pair<FactPair, bool>> setup_fact_queue_old(const CartesianSet &a);
    void enqueue_old(std::deque<std::pair<FactPair, bool>> &q,
        std::vector<bool> &seen_vars, FactPair fact, bool x);
#endif


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
