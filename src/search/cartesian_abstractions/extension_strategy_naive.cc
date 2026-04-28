#include "extension_strategy_naive.h"
#include "cartesian_set.h"

#include <cassert>

using namespace std;

namespace cartesian_abstractions {
ExtensionStrategyNaive::ExtensionStrategyNaive(){ 
}

CartesianSet ExtensionStrategyNaive::get_extension(const CartesianSet &a){

    CartesianSet result = a;
    return result; 
}


int ExtensionStrategyNaive::get_extension_value(const CartesianSet &a, int var) {
    if (a.count(var) == 1) {
        return a.get_values(var)[0];
    }
    return UNDEFINED; 
}


}
