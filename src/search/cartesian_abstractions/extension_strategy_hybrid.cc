#include "extension_strategy_hybrid.h"
#include "cartesian_set.h"
#include "../plugins/plugin.h"

using namespace std;

namespace cartesian_abstractions {

// ---- ExtensionStrategyHybridInstance ----

ExtensionStrategyHybridInstance::ExtensionStrategyHybridInstance(
    unique_ptr<ExtensionStrategyInstance> fast,
    unique_ptr<ExtensionStrategyInstance> precise)
    : fast(move(fast)), precise(move(precise)) {
}

CartesianSet ExtensionStrategyHybridInstance::get_extension(
    const CartesianSet &a) {
    return fast->get_extension(a);
}

int ExtensionStrategyHybridInstance::get_extension_value(
    const CartesianSet &a, int var) {
    return fast->get_extension_value(a, var);
}

CartesianSet ExtensionStrategyHybridInstance::get_precise_extension(
    const CartesianSet &a) {
    return precise->get_extension(a);
}

int ExtensionStrategyHybridInstance::get_precise_extension_value(
    const CartesianSet &a, int var) {
    return precise->get_extension_value(a, var);
}

// ---- ExtensionStrategyHybrid (generator) ----

ExtensionStrategyHybrid::ExtensionStrategyHybrid(
    const shared_ptr<ExtensionStrategy> &fast,
    const shared_ptr<ExtensionStrategy> &precise, utils::Verbosity verbosity)
    : ExtensionStrategy(verbosity), fast(fast), precise(precise) {
}

unique_ptr<ExtensionStrategyInstance> ExtensionStrategyHybrid::create(
    const TaskProxy &task_proxy) const {
    return make_unique<ExtensionStrategyHybridInstance>(
        fast->create(task_proxy), precise->create(task_proxy));
}

string ExtensionStrategyHybrid::name() const {
    return "hybrid";
}

void ExtensionStrategyHybrid::dump_strategy_specific_options() const {
}

class ExtensionStrategyHybridFeature
    : public plugins::TypedFeature<ExtensionStrategy, ExtensionStrategyHybrid> {
public:
    ExtensionStrategyHybridFeature() : TypedFeature("extend_hybrid") {
        document_title("Hybrid extension strategy");
        document_synopsis(
            "Uses the fast extension strategy for rewiring and regression and "
            "the precise extension strategy only to check the consistency of "
            "newly split abstract states and to prune non-induced transitions "
            "at which refinement stalls. Requires "
            "transition_representation=store.");

        add_option<shared_ptr<ExtensionStrategy>>(
            "fast", "extension strategy for all regular computations",
            "extend_naive()");
        add_option<shared_ptr<ExtensionStrategy>>(
            "precise",
            "extension strategy for consistency checks and pruning",
            "extend_uncertain()");
        add_extension_strategy_options_to_feature(*this);
    }

    virtual shared_ptr<ExtensionStrategyHybrid> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<ExtensionStrategyHybrid>(
            opts.get<shared_ptr<ExtensionStrategy>>("fast"),
            opts.get<shared_ptr<ExtensionStrategy>>("precise"),
            get_extension_strategy_arguments_from_options(opts));
    }
};

static plugins::FeaturePlugin<ExtensionStrategyHybridFeature> _plugin;

}
