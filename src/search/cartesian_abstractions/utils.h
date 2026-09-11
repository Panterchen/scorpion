#ifndef CARTESIAN_ABSTRACTIONS_UTILS_H
#define CARTESIAN_ABSTRACTIONS_UTILS_H

#include "types.h"

#include "../task_proxy.h"

#include "../utils/hash.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

class AbstractTask;

namespace additive_heuristic {
class AdditiveHeuristic;
}

namespace plugins {
class Feature;
}

namespace cartesian_abstractions {

struct AxiomRule {
    FactPair head;                    // (var_id, val) Axiom head
    std::vector<FactPair> body;       // Axiom Body
};

struct AxiomSplitTarget {
    FactPair fact;
    bool force;  // if true: fact should be forced in the split, else prevented

    auto operator<=>(const AxiomSplitTarget &) const = default;
};

class Abstraction;

class VariableDependencies {
    // deps[var_id] =   (Variables affecting var_id (only non-empty for derived
    //                      var_id);
    //                   Variables, directly affected by var_id)
    std::vector<std::pair<std::vector<int>, std::vector<int>>> deps;

    // rules[var_id][value] = all AxiomRules with head (var_id, value)
    std::vector<std::vector<std::vector<AxiomRule>>> rules;

    // lazy cache for transitive closure of basic variable dependencies per var
    mutable std::vector<std::optional<std::vector<int>>> basic_deps_cache;
    mutable std::vector<std::vector<std::vector<std::optional<std::vector<AxiomSplitTarget>>>>> basic_targets_cache;
    mutable std::vector<std::optional<std::vector<int>>> transitive_dependents_cache;

    VariablesProxy vars;

    void resolve_basic_dependencies(
        int var_id, std::vector<int> &result, std::vector<bool> &visited) const;
    void resolve_basic_targets(
        int var_id, int value, bool force,
        std::vector<AxiomSplitTarget> &result,
        std::vector<std::vector<std::vector<bool>>> &visited) const;
    void resolve_transitive_dependents(
        int var_id, std::vector<int> &result, std::vector<bool> &visited) const;

    public:
        explicit VariableDependencies(const TaskProxy &task);

        const std::pair<std::vector<int>, std::vector<int>> &get(int var_id) const;

        const std::vector<AxiomRule> &get_rules(int var_id, int value) const;

        const std::vector<int> &get_basic_dependencies(int var_id) const;

        const std::vector<AxiomSplitTarget> &get_basic_targets(int var_id, int value, bool force) const;

        const std::vector<int> &get_transitive_dependents(int var_id) const;
};

extern bool g_hacked_sort_transitions;

extern std::unique_ptr<additive_heuristic::AdditiveHeuristic>
create_additive_heuristic(const std::shared_ptr<AbstractTask> &task);

/*
  The set of relaxed-reachable facts is the possibly-before set of facts that
  can be reached in the delete-relaxation before 'fact' is reached the first
  time, plus 'fact' itself.
*/
extern utils::HashSet<FactProxy> get_relaxed_possible_before(
    const TaskProxy &task, const FactProxy &fact);

extern std::vector<int> get_domain_sizes(const TaskProxy &task);

// Functions to precompute and lookup preconditions, postconditions
extern std::vector<std::vector<FactPair>> compute_preconditions_by_operator(
    const OperatorsProxy &ops);
extern std::vector<FactPair> compute_postconditions(const OperatorProxy &op);
extern std::vector<std::vector<FactPair>> compute_postconditions_by_operator(
    const OperatorsProxy &ops);
extern int lookup_value(const std::vector<FactPair> &facts, int var);

extern void add_common_cegar_options(plugins::Feature &feature);

extern std::string create_dot_graph(
    const TaskProxy &task_proxy, const Abstraction &abstraction);
extern void write_to_file(
    const std::string &file_name, const std::string &content);
}

/*
  TODO: Our proxy classes are meant to be temporary objects and as such
  shouldn't be stored in containers. Once we find a way to avoid
  storing them in containers, we should remove this hashing function.
*/
namespace utils {
inline void feed(HashState &hash_state, const FactProxy &fact) {
    feed(hash_state, fact.get_pair());
}
}

#endif
