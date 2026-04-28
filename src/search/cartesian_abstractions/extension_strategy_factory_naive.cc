#include "extension_strategy_factory_naive.h"

#include "extension_strategy_naive.h"

#include "../plugins/plugin.h"

using namespace std;

namespace cartesian_abstractions {
ExtensionStrategyFactoryNaive::ExtensionStrategyFactoryNaive(
    utils::Verbosity verbosity)
    : ExtensionStrategyFactory(verbosity) {
}

unique_ptr<ExtensionStrategy>
ExtensionStrategyFactoryNaive::compute_extension_strategy(
    const TaskProxy &task_proxy) const {
    return make_unique<ExtensionStrategyNaive>();
}


string ExtensionStrategyFactoryNaive::name() const {
    return "naive";
}

void ExtensionStrategyFactoryNaive::dump_strategy_specific_options() const {
    /*if (log.is_at_least_normal()) {
        
    }*/
}

class ExtensionStrategyFactoryNaiveFeature
    : public plugins::TypedFeature<
          ExtensionStrategyFactory, ExtensionStrategyFactoryNaive> {
public:
    ExtensionStrategyFactoryNaiveFeature()
        : TypedFeature("extend_naive") {
        document_title("naive extension strategy, i.e. no extension");

        document_synopsis(
            "An extension strategy that does nothing.");

        add_extension_strategy_options_to_feature(*this);

        document_note("Note", "TODO");
    }
    virtual shared_ptr<ExtensionStrategyFactoryNaive> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<
            ExtensionStrategyFactoryNaive>(
            get_extension_strategy_arguments_from_options(opts));
    }
};

static plugins::FeaturePlugin<ExtensionStrategyFactoryNaiveFeature> _plugin;
}
