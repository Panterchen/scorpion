#ifndef CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_H
#define CARTESIAN_ABSTRACTIONS_EXTENSION_STRATEGY_H

#include <utility>
#include "cartesian_set.h"

namespace cartesian_abstractions {

/*
  An extension strategy describes how values of derived variables are determined in cartesian sets.

  We distinguish X types of extension strategies: naive, uncertainty semantics

  Naive: no extension, cartesian set is treturned as is 

  Uncertainty semantics: A derived variable is true if there is a supporting axiom whose body only contains atoms that are true in every concrete state in the abstract state.
  A derived variable is false if for all supporting axiom the body contains at least one atom that is false in every concrete state in the abstract state. Otherwise the value of the
  derived variable is unknown, i.e. the cartesian set contains both domain values for the variable.

*/

class ExtensionStrategy {
public:
    explicit ExtensionStrategy();
    virtual ~ExtensionStrategy() = default;
    virtual CartesianSet get_extension(const CartesianSet &a) = 0;
    virtual int get_extension_value(const CartesianSet &a, int variable) = 0;
};
}

#endif
