#include "regression_strategy_factory.h"

#include "../plugins/plugin.h"

#include <iostream>

using namespace std;

namespace cartesian_abstractions {
RegressionStrategyFactory::RegressionStrategyFactory(utils::Verbosity verbosity)
    : log(utils::get_log_for_verbosity(verbosity)) {
}

void RegressionStrategyFactory::dump_options() const {
    if (log.is_at_least_normal()) {
        log << "Regression strategy options:" << endl;
        log << "Type: " << name() << endl;
        dump_strategy_specific_options();
    }
}

void add_regression_strategy_options_to_feature(plugins::Feature &feature) {
    utils::add_log_options_to_feature(feature);
}

tuple<utils::Verbosity> get_regression_strategy_arguments_from_options(
    const plugins::Options &opts) {
    return utils::get_log_arguments_from_options(opts);
}

static class RegressionStrategyFactoryCategoryPlugin
    : public plugins::TypedCategoryPlugin<RegressionStrategyFactory> {
public:
    RegressionStrategyFactoryCategoryPlugin()
        : TypedCategoryPlugin("RegressionStrategy") {
        document_synopsis(
            "This page describes the various regression strategies for Cartesian sets with derived variables supported "
            "by the planner.");
    }
} _category_plugin;
}
