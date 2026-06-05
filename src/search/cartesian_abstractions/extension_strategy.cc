#include "extension_strategy.h"

#include "../plugins/plugin.h"

#include <iostream>

using namespace std;

namespace cartesian_abstractions {

ExtensionStrategyInstance::ExtensionStrategyInstance() {
}

ExtensionStrategy::ExtensionStrategy(utils::Verbosity verbosity)
    : log(utils::get_log_for_verbosity(verbosity)) {
}

void ExtensionStrategy::dump_options() const {
    if (log.is_at_least_normal()) {
        log << "Extension strategy options:" << endl;
        log << "Type: " << name() << endl;
        dump_strategy_specific_options();
    }
}

void add_extension_strategy_options_to_feature(plugins::Feature &feature) {
    utils::add_log_options_to_feature(feature);
}

tuple<utils::Verbosity> get_extension_strategy_arguments_from_options(
    const plugins::Options &opts) {
    return utils::get_log_arguments_from_options(opts);
}

static class ExtensionStrategyCategoryPlugin
    : public plugins::TypedCategoryPlugin<ExtensionStrategy> {
public:
    ExtensionStrategyCategoryPlugin()
        : TypedCategoryPlugin("ExtensionStrategy") {
        document_synopsis(
        "This page describes the various extension strategies for Cartesian sets with derived variables supported "
        "by the planner.");
    }
} _category_plugin;

}
