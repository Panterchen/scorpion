#include "regression_strategy_factory_naive.h"

#include "regression_strategy_naive.h"

#include "../plugins/plugin.h"

using namespace std;

namespace cartesian_abstractions {
RegressionStrategyFactoryNaive::RegressionStrategyFactoryNaive(
    utils::Verbosity verbosity)
    : RegressionStrategyFactory(verbosity) {
}

unique_ptr<RegressionStrategy>
RegressionStrategyFactoryNaive::compute_regression_strategy(
    const TaskProxy &task_proxy) const {
    return make_unique<RegressionStrategyNaive>(task_proxy.get_variables(), task_proxy.get_operators());
}


string RegressionStrategyFactoryNaive::name() const {
    return "naive";
}

void RegressionStrategyFactoryNaive::dump_strategy_specific_options() const {
    /*if (log.is_at_least_normal()) {
        
    }*/
}

class RegressionStrategyFactoryNaiveFeature
    : public plugins::TypedFeature<
          RegressionStrategyFactory, RegressionStrategyFactoryNaive> {
public:
    RegressionStrategyFactoryNaiveFeature()
        : TypedFeature("regress_naive") {
        document_title("naive regression strategy");

        document_synopsis(
            "A regression strategy.");

        add_regression_strategy_options_to_feature(*this);

        document_note("Note", "TODO");
    }
    virtual shared_ptr<RegressionStrategyFactoryNaive> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<
            RegressionStrategyFactoryNaive>(
            get_regression_strategy_arguments_from_options(opts));
    }
};

static plugins::FeaturePlugin<RegressionStrategyFactoryNaiveFeature> _plugin;
}
