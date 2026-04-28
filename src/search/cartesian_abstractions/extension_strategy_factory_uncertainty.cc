#include "extension_strategy_factory_uncertainty.h"

#include "extension_strategy_uncertainty.h"

#include "../plugins/plugin.h"

using namespace std;

namespace cartesian_abstractions {
ExtensionStrategyFactoryUncertainty::ExtensionStrategyFactoryUncertainty(
    utils::Verbosity verbosity)
    : ExtensionStrategyFactory(verbosity) {
}

unique_ptr<ExtensionStrategy>
ExtensionStrategyFactoryUncertainty::compute_extension_strategy(
    const TaskProxy &task_proxy) const {
    return make_unique<ExtensionStrategyUncertainty>(task_proxy.get_axioms(), task_proxy.get_variables()); // TODO: num_variables should be number of derived variables 
}


string ExtensionStrategyFactoryUncertainty::name() const {
    return "uncertainty";
}

void ExtensionStrategyFactoryUncertainty::dump_strategy_specific_options() const {
    /*if (log.is_at_least_normal()) {
        
    }*/
}

class ExtensionStrategyFactoryUncertaintyFeature
    : public plugins::TypedFeature<
          ExtensionStrategyFactory, ExtensionStrategyFactoryUncertainty> {
public:
    ExtensionStrategyFactoryUncertaintyFeature()
        : TypedFeature("extend_uncertain") {
        document_title("Uncertainty extension strategy");

        document_synopsis(
            "An extension strategy that considers uncertainty semantics for Cartesian sets.");

        add_extension_strategy_options_to_feature(*this);

        document_note("Note", "TODO");
    }
    virtual shared_ptr<ExtensionStrategyFactoryUncertainty> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<
            ExtensionStrategyFactoryUncertainty>(
            get_extension_strategy_arguments_from_options(opts));
    }
};

static plugins::FeaturePlugin<ExtensionStrategyFactoryUncertaintyFeature> _plugin;
}
