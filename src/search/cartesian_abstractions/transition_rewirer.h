#ifndef CARTESIAN_ABSTRACTIONS_TRANSITION_REWIRER_H
#define CARTESIAN_ABSTRACTIONS_TRANSITION_REWIRER_H

#include "types.h"

#include "../utils/collections.h"

#include <cassert>
#include <deque>
#include <vector>
#include <utility>

struct FactPair;
class OperatorsProxy;

namespace cartesian_abstractions {
class TransitionRewirer {
    const std::vector<std::vector<FactPair>> preconditions_by_operator;
    const std::vector<std::vector<FactPair>> postconditions_by_operator;
    const std::vector<std::vector<FactPair>> unconditional_effects_by_operator;
	const std::vector<std::vector<std::pair<std::vector<FactPair>, FactPair>>> conditional_effects_by_operator;

    int get_precondition_value(int op_id, int var) const;
    int get_postcondition_value(int op_id, int var) const;
	std::vector<std::pair<std::vector<FactPair>, FactPair>> get_relevant_effects(
        int op_id, int var) const;
	std::vector<std::vector<FactPair>> get_conditions_for_fact(
		int op_id, const FactPair &fact) const;

	// Rewire transitions for unconditional effects.

	void rewire_incoming_uc(
        std::deque<Transitions> &incoming, std::deque<Transitions> &outgoing,
        const AbstractStates &states, const AbstractState &v1, const AbstractState &v2,
		int var, int op_id, int u_id, int post) const;
	void rewire_incoming_c(
        std::deque<Transitions> &incoming, std::deque<Transitions> &outgoing,
        const AbstractStates &states, const AbstractState &v1, const AbstractState &v2,
		int var, int op_id, int u_id) const;
    void rewire_incoming_transitions(
        std::deque<Transitions> &incoming, std::deque<Transitions> &outgoing,
        const AbstractStates &states, int v_id,
        const AbstractState &v1, const AbstractState &v2, int var) const;
	void rewire_outgoing_uc(
        std::deque<Transitions> &incoming, std::deque<Transitions> &outgoing,
        const AbstractStates &states, const AbstractState &v1, const AbstractState &v2,
		int var, int op_id, int w_id, int pre, int post) const;
	void rewire_outgoing_c(
        std::deque<Transitions> &incoming, std::deque<Transitions> &outgoing,
        const AbstractStates &states, const AbstractState &v1, const AbstractState &v2,
		int var, int op_id, int w_id) const;
    void rewire_outgoing_transitions(
        std::deque<Transitions> &incoming, std::deque<Transitions> &outgoing,
        const AbstractStates &states, int v_id,
        const AbstractState &v1, const AbstractState &v2, int var) const;

    // Rewire loops for unconditional effects.
	void rewire_loop_uc(
        std::deque<Loops> &loops,
        std::deque<Transitions> &incoming, std::deque<Transitions> &outgoing,
        const AbstractState &v1, const AbstractState &v2, int var, int op_id, int pre, int post) const;
	// Rewire loops for conditional effects.
	void rewire_loop_c(
        std::deque<Loops> &loops,
        std::deque<Transitions> &incoming, std::deque<Transitions> &outgoing,
        const AbstractState &v1, const AbstractState &v2, int var, int op_id) const;

public:
    explicit TransitionRewirer(const OperatorsProxy &ops);

    void rewire_transitions(
        std::deque<Transitions> &incoming, std::deque<Transitions> &outgoing,
        const AbstractStates &states, int v_id,
        const AbstractState &v1, const AbstractState &v2, int var) const;

    void rewire_loops(
        std::deque<Loops> &loops,
        std::deque<Transitions> &incoming, std::deque<Transitions> &outgoing,
        int v_id, const AbstractState &v1, const AbstractState &v2, int var) const;

    // todo: get effect conditions not just pre and post
    const std::vector<FactPair> &get_preconditions(int op_id) const {
        assert(utils::in_bounds(op_id, preconditions_by_operator));
        return preconditions_by_operator[op_id];
    }
    const std::vector<FactPair> &get_postconditions(int op_id) const {
        assert(utils::in_bounds(op_id, postconditions_by_operator));
        return postconditions_by_operator[op_id];
    }
    const std::vector<FactPair> &get_unconditional_effects(int op_id) const {
        assert(utils::in_bounds(op_id, unconditional_effects_by_operator));
        return unconditional_effects_by_operator[op_id];
    }
	const std::vector<std::pair<std::vector<FactPair>, FactPair>> &get_conditional_effects(int op_id) const {
		assert(utils::in_bounds(op_id, conditional_effects_by_operator));
		return conditional_effects_by_operator[op_id];
	}

    const std::vector<Facts> &get_preconditions() const {
        return preconditions_by_operator;
    }
    const std::vector<Facts> &get_postconditions() const {
        return postconditions_by_operator;
    }
    const std::vector<Facts> &get_unconditional_effects() const {
        return unconditional_effects_by_operator;
    }
	const std::vector<std::vector<std::pair<std::vector<FactPair>, FactPair>>> &get_conditional_effects() const {
		return conditional_effects_by_operator;
	}

    int get_num_operators() const;

    int get_num_conditional_effects(int op_id) const;

	bool has_conditional_effects(int op_id) const;

};
}

#endif
