#include "extension_strategy_naive.h"
#include "cartesian_set.h"

#include "../plugins/plugin.h"

#include <cassert>

using namespace std;

namespace cartesian_abstractions {

// ---- ExtensionStrategyNaiveInstance ----

ExtensionStrategyNaiveInstance::ExtensionStrategyNaiveInstance(){
}

CartesianSet ExtensionStrategyNaiveInstance::get_extension(const CartesianSet &a) {
    // CartesianSet result = a;
    return a;
}

int ExtensionStrategyNaiveInstance::get_extension_value(const CartesianSet &a, int var) {
    if (a.count(var) == 1) {
        return a.get_values(var)[0];
    }
    return UNDEFINED; 
}

// ---- ExtensionStrategyNaive (generator) ----

ExtensionStrategyNaive::ExtensionStrategyNaive(utils::Verbosity verbosity)
    : ExtensionStrategy(verbosity){
}

unique_ptr<ExtensionStrategyInstance> ExtensionStrategyNaive::create(
    const TaskProxy &) const {
    return make_unique<ExtensionStrategyNaiveInstance>();
}

string ExtensionStrategyNaive::name() const {
    return "naive";
}

void ExtensionStrategyNaive::dump_strategy_specific_options() const {
    /*if (log.is_at_least_normal()) {

    }*/
}

class ExtensionStrategyNaiveFeature
    : public plugins::TypedFeature<
          ExtensionStrategy, ExtensionStrategyNaive> {
public:
    ExtensionStrategyNaiveFeature()
        : TypedFeature("extend_naive") {
        document_title("Naive extension strategy (no extension)");

        document_synopsis(
            "An extension strategy that does nothing.");

        add_extension_strategy_options_to_feature(*this);

        document_note("Note", "TODO");
    }
    virtual shared_ptr<ExtensionStrategyNaive> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<
            ExtensionStrategyNaive>(
            get_extension_strategy_arguments_from_options(opts));
    }
};

static plugins::FeaturePlugin<ExtensionStrategyNaiveFeature> _plugin;

}
