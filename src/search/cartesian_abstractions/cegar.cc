#include "cegar.h"

#include "abstract_state.h"
#include "abstraction.h"
#include "shortest_paths.h"
#include "transition_system.h"
#include "utils.h"
#include "extension_strategy.h"
#include "regression_strategy.h"

#include "../task_utils/task_properties.h"
#include "../tasks/domain_abstracted_task.h"
#include "../utils/logging.h"
#include "../utils/memory.h"

#include <cassert>
#include <iostream>

using namespace std;

namespace cartesian_abstractions {
CEGAR::CEGAR(
    const shared_ptr<AbstractTask> &task, 
    const shared_ptr<ExtensionStrategy> &extension_strategy,
    const shared_ptr<RegressionStrategy> &regression_strategy,
    int max_states, int max_transitions,    
    double max_time, bool verify_transitions_debug,
    PickFlawedAbstractState pick_flawed_abstract_state,
    PickSplit pick_split, PickSplit tiebreak_split,
    int max_concrete_states_per_abstract_state, int max_state_expansions,
    TransitionRepresentation transition_representation,
    GoalProcessing goal_processing,
    utils::RandomNumberGenerator &rng, utils::LogProxy &log,
    DotGraphVerbosity dot_graph_verbosity)
    : task_proxy(*task),
      extension_strategy(extension_strategy),
      regression_strategy(regression_strategy),
      domain_sizes(get_domain_sizes(task_proxy)),
      max_states(max_states),
      max_stored_transitions(
          transition_representation == TransitionRepresentation::STORE
              ? max_transitions
              : INF),
      pick_flawed_abstract_state(pick_flawed_abstract_state),
      prune_non_induced_transitions(extension_strategy->supports_precise_checks()),
      goal_processing(goal_processing),
      variable_dependencies(make_shared<const VariableDependencies>(task_proxy)),
      transition_rewirer(
          make_shared<TransitionRewirer>(task_proxy, extension_strategy,
              variable_dependencies, verify_transitions_debug)),
      abstraction(make_unique<Abstraction>(
          task, transition_rewirer, transition_representation, log)),
      timer(max_time),
      log(log),
      dot_graph_verbosity(dot_graph_verbosity) {
    assert(max_states >= 1);
    if (task_properties::has_axioms(task_proxy) &&
        prune_non_induced_transitions &&
        (transition_representation != TransitionRepresentation::STORE ||
        pick_flawed_abstract_state ==
        PickFlawedAbstractState::FIRST_ON_SHORTEST_PATH)) {
        log << "Pruning non-induced transitions (hybrid extension) requires "
               "transition_representation=store and a pick_flawed_abstract_state "
               "other than first_on_shortest_path." << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
    int max_cached_spt_parents =
        (transition_representation == TransitionRepresentation::STORE)
            ? 0
            : max_transitions;
    shortest_paths = make_unique<ShortestPaths>(
        *transition_rewirer, task_properties::get_operator_costs(task_proxy),
        max_cached_spt_parents, timer, log);
    flaw_search = make_unique<FlawSearch>(
        task, *abstraction, *shortest_paths, rng, pick_flawed_abstract_state,
        pick_split, tiebreak_split, max_concrete_states_per_abstract_state,
        max_state_expansions, extension_strategy, regression_strategy,
        variable_dependencies, log);

    if (log.is_at_least_normal()) {
        log << "Start building abstraction." << endl;
        log << "Maximum number of states: " << max_states << endl;
        log << "Maximum number of stored transitions: " << max_transitions
            << endl;
        log << "Maximum time: " << timer.get_remaining_time() << endl;
    }

    refinement_loop();
    if (log.is_at_least_normal()) {
        log << "Done building abstraction." << endl;
        log << "Time for building abstraction: " << timer.get_elapsed_time()
            << endl;
        print_statistics();
    }
}

CEGAR::~CEGAR() {
}

unique_ptr<Abstraction> CEGAR::extract_abstraction() {
    assert(abstraction);
    return move(abstraction);
}

vector<int> CEGAR::get_goal_distances() const {
    assert(shortest_paths);
    return shortest_paths->get_goal_distances();
}

int CEGAR::prune_stalled_transitions() {
    int num_pruned = 0;
    for (const StalledTransition &t : flaw_search->get_stalled_transitions()) {
        const AbstractState &src = abstraction->get_state(t.source_id);
        const AbstractState &dest = abstraction->get_state(t.target_id);
        if (transition_rewirer->is_non_induced(src, t.op_id, dest) &&
            abstraction->remove_transition(t.source_id, t.op_id, t.target_id)) {
            ++num_pruned;
        }
    }
    return num_pruned;
}

void CEGAR::separate_facts_unreachable_before_goal() const {
    assert(abstraction->get_goals().size() == 1);
    assert(abstraction->get_num_states() == 1);
    assert(task_proxy.get_goals().size() == 1);
    FactProxy goal = task_proxy.get_goals()[0];
    utils::HashSet<FactProxy> reachable_facts =
        get_relaxed_possible_before(task_proxy, goal);
    for (VariableProxy var : task_proxy.get_variables()) {
        if (!may_keep_refining())
            break;
        int var_id = var.get_id();
        vector<int> unreachable_values;
        for (int value = 0; value < var.get_domain_size(); ++value) {
            FactProxy fact = var.get_fact(value);
            if (reachable_facts.count(fact) == 0)
                unreachable_values.push_back(value);
        }
        if (!unreachable_values.empty())
            abstraction->refine(
                abstraction->get_initial_state(), var_id, unreachable_values);
    }
    abstraction->mark_all_states_as_goals();
    /*
      Split off the goal fact from the initial state. Then the new initial
      state is the only non-goal state and no goal state will have to be split
      later.

      For all states s in which the landmark might have been achieved we need
      h(s)=0. If the limits don't allow splitting off all facts unreachable
      before the goal to achieve this, we instead preserve h(s)=0 for *all*
      states s and cannot split off the goal fact from the abstract initial
      state.
    */
    assert(abstraction->get_initial_state().includes(
        task_proxy.get_initial_state()));
    assert(reachable_facts.count(goal));
    if (may_keep_refining()) {
        abstraction->refine(
            abstraction->get_initial_state(), goal.get_variable().get_id(),
            {goal.get_value()});
    }
}

void CEGAR::preprocess_goals() const {
    switch (goal_processing) {
        case GoalProcessing::LAZY:
            break;
        case GoalProcessing::GREEDY_SINGLE:
            if (task_proxy.get_goals().size() == 1) {
                separate_facts_unreachable_before_goal();
            }
            break;
        case GoalProcessing::GREEDY:
        default:
            if (task_proxy.get_goals().size() == 1) {
                separate_facts_unreachable_before_goal();
            } else {
                // Iteratively split off the next goal fact from the current goal state.
                assert(abstraction->get_num_states() == 1);
                const AbstractState *current = &abstraction->get_initial_state();
                for (FactProxy goal : task_proxy.get_goals()) {
                    if (!may_keep_refining()) {
                        break;
                    }
                    FactPair fact = goal.get_pair();
                    auto pair = abstraction->refine(*current, fact.var, {fact.value});
                    dump_dot_graph();
                    current = &abstraction->get_state(pair.second);
                }
                assert(
                    !may_keep_refining() ||
                    !abstraction->get_goals().count(
                        abstraction->get_initial_state().get_id()));
                assert(abstraction->get_goals().size() == 1);
            }
    }
}

bool CEGAR::may_keep_refining() const {
    if (abstraction->get_num_states() >= max_states) {
        if (log.is_at_least_normal()) {
            log << "Reached maximum number of states." << endl;
        }
        return false;
    } else if (
        abstraction->get_num_stored_transitions() >= max_stored_transitions) {
        if (log.is_at_least_normal()) {
            log << "Reached maximum number of transitions." << endl;
        }
        return false;
    } else if (timer.is_expired()) {
        if (log.is_at_least_normal()) {
            log << "Reached time limit." << endl;
        }
        return false;
    } else if (!utils::extra_memory_padding_is_reserved()) {
        if (log.is_at_least_normal()) {
            log << "Reached memory limit." << endl;
        }
        return false;
    }
    return true;
}

void CEGAR::refinement_loop() {
    /*
      For landmark tasks we have to map all states in which the
      landmark might have been achieved to arbitrary abstract goal
      states.

      In any case, we separate all goal states from non-goal states
      to simplify the implementation. This way, we don't have to split
      goal states later.
    */
    if (log.is_at_least_debug()) { // print goal facts in debug mode
        log << "Goal facts:" << endl;
        for (FactProxy goal : task_proxy.get_goals()) {
            log << "  var" << goal.get_variable().get_id()
                << " (derived=" << goal.get_variable().is_derived()
                << ") = " << goal.get_value() << endl;
        }
    }
    utils::Timer process_goals_timer(false);
    process_goals_timer.resume();
    preprocess_goals();
    process_goals_timer.stop();

    // Initialize abstract goal distances and shortest path tree.
    if (log.is_at_least_debug()) {
        log << "Initialize abstract goal distances and shortest path tree."
            << endl;
    }
    shortest_paths->recompute(*abstraction, abstraction->get_goals());

    utils::Timer find_trace_timer(false);
    utils::Timer find_flaw_timer(false);
    utils::Timer refine_timer(false);
    utils::Timer update_goal_distances_timer(false);
    utils::Timer prune_timer(false);
    int num_pruned_transitions = 0;

    while (may_keep_refining()) {
        find_trace_timer.resume();
        unique_ptr<Solution> solution;
        solution = shortest_paths->extract_solution(
            abstraction->get_initial_state().get_id(),
            abstraction->get_goals());
        find_trace_timer.stop();

        if (solution) {
            int new_abstract_solution_cost =
                shortest_paths->get_32bit_goal_distance(
                    abstraction->get_initial_state().get_id());
            if (new_abstract_solution_cost > old_abstract_solution_cost) {
                old_abstract_solution_cost = new_abstract_solution_cost;
                if (log.is_at_least_verbose()) {
                    log << "Lower bound: " << old_abstract_solution_cost
                        << endl;
                }
            }
        } else {
            log << "Abstract task is unsolvable." << endl;
            break;
        }

        find_flaw_timer.resume();
        // split==nullptr iff we find a concrete solution or run out of time or
        // memory.
        unique_ptr<Split> split;
        if (pick_flawed_abstract_state ==
            PickFlawedAbstractState::FIRST_ON_SHORTEST_PATH) {
            split = flaw_search->get_split_legacy(*solution);
        } else {
            split = flaw_search->get_split(timer);
        }
        find_flaw_timer.stop();

        if (!utils::extra_memory_padding_is_reserved()) {
            log << "Reached memory limit in flaw search." << endl;
            break;
        }

        if (timer.is_expired()) {
            log << "Reached time limit in flaw search." << endl;
            break;
        }

        if (!split) {
            if (prune_non_induced_transitions &&
                flaw_search->get_last_stop_reason() ==
                StopReason::REFINEMENT_STALLED) {
                /*
                    Refinement stalled. Check whether the transitions causing
                    the deviation flaws are provably non-induced. If so,
                    remove them, recompute goal distances (the shortest path
                    tree may have used a removed transition), and search for
                    flaws again. Each round removes at least one transition,
                    so this should terminate. TODO: check this
                 */
                prune_timer.resume();
                int num_pruned = prune_stalled_transitions();
                prune_timer.stop();
                if (num_pruned > 0) {
                    num_pruned_transitions += num_pruned;
                    if (log.is_at_least_debug()) {
                        log << "Pruned " << num_pruned
                            << " non-induced transitions." << endl;
                    }
                    update_goal_distances_timer.resume();
                    shortest_paths->recompute(*abstraction, abstraction->get_goals());
                    update_goal_distances_timer.stop();
                    continue;
                }
            }
            switch (flaw_search->get_last_stop_reason()) {
            case StopReason::SOLVED:
                log << "Found concrete solution." << endl;
                if (log.is_at_least_debug()) {
                    for (OperatorProxy op : task_proxy.get_operators()) {
                        log << op.get_id() << "(pre : {";
                        for (FactProxy p : op.get_preconditions()) {
                            log << p.get_var_id() << "=" << p.get_value() << ", ";
                        }
                        log << "}, eff : {";
                        for (EffectProxy p : op.get_effects()) {
                            log << p.get_fact().get_var_id() << "=" << p.get_fact().get_value() << ", ";
                        }
                        log << "})" << endl;
                    }
                    for (OperatorProxy axiom : task_proxy.get_axioms()) {
                        log << "Axiom #" << axiom.get_id();
                        for (EffectProxy p : axiom.get_effects()) {
                            log << "body : {";
                            for (FactProxy ec : p.get_conditions()) {
                                log << ec.get_var_id() << "=" << ec.get_value() << ", ";
                            }
                            log << "}, head : {" << p.get_fact().get_var_id() << "=" << p.get_fact().get_value() << "}" << endl;
                        }
                        log << "}" << endl;
                    }
                    for (int abs_id = 0; abs_id < abstraction->get_num_states(); ++abs_id) {
                        log << "#" << abs_id << " : " << abstraction->get_state(abs_id).get_cartesian_set() << endl;
                        if (abstraction->get_initial_state().get_id() == abs_id) log << "initial state" << endl;
                        if (abstraction->get_goals().contains(abs_id)) log << "goal state" << endl;
                        log << "Outgoing : " << endl;
                        for (Transition t : abstraction->get_outgoing_transitions(abs_id)) {
                            log << abs_id << "-" << t.op_id << "->" << t.target_id << endl;
                        }
                        log << "Incoming : " << endl;
                        for (Transition t : abstraction->get_incoming_transitions(abs_id)) {
                            log << t.target_id << "-" << t.op_id << "->" << abs_id <<  endl;
                        }
                    }
                }
                break;
            case StopReason::TIMEOUT:
                log << "Reached time limit in flaw search." << endl;
                break;
            case StopReason::MEMORY_LIMIT:
                log << "Reached memory limit in flaw search." << endl;
                break;
            case StopReason::REFINEMENT_STALLED:
                log << "Refinement stalled: no further split found despite "
                       "existing flaws (NOT a verified solution)." << endl;
                // print out information about each state... ? for debugging...
                if (log.is_at_least_debug()) {
                    for (OperatorProxy op : task_proxy.get_operators()) {
                        log << op.get_id() << "(pre : {";
                        for (FactProxy p : op.get_preconditions()) {
                            log << p.get_var_id() << "=" << p.get_value() << ", ";
                        }
                        log << "}, eff : {";
                        for (EffectProxy p : op.get_effects()) {
                            log << p.get_fact().get_var_id() << "=" << p.get_fact().get_value() << ", ";
                        }
                        log << "})" << endl;
                    }
                    for (OperatorProxy axiom : task_proxy.get_axioms()) {
                        log << "Axiom #" << axiom.get_id();
                        for (EffectProxy p : axiom.get_effects()) {
                            log << "body : {";
                            for (FactProxy ec : p.get_conditions()) {
                                log << ec.get_var_id() << "=" << ec.get_value() << ", ";
                            }
                            log << "}, head : {" << p.get_fact().get_var_id() << "=" << p.get_fact().get_value() << "}";
                        }
                        log << endl;
                    }
                    for (int abs_id = 0; abs_id < abstraction->get_num_states(); ++abs_id) {
                        log << "#" << abs_id << " : " << abstraction->get_state(abs_id).get_cartesian_set() << endl;
                        if (abstraction->get_initial_state().get_id() == abs_id) log << "initial state" << endl;
                        if (abstraction->get_goals().contains(abs_id)) log << "goal state" << endl;
                        log << "Outgoing : " << endl;
                        for (Transition t : abstraction->get_outgoing_transitions(abs_id)) {
                            log << abs_id << "-" << t.op_id << "->" << t.target_id << endl;
                        }
                        log << "Incoming : " << endl;
                        for (Transition t : abstraction->get_incoming_transitions(abs_id)) {
                            log << t.target_id << "-" << t.op_id << "->" << abs_id <<  endl;
                        }
                    }
                }

                break;
            case StopReason::NONE:
                log << "Flaw search returned no split for an unknown reason." << endl;
                break;
            }
            break;
        }

        refine_timer.resume();
        int state_id = split->abstract_state_id;
        const AbstractState &abstract_state = abstraction->get_state(state_id);
        assert(!abstraction->get_goals().count(state_id));

        pair<int, int> new_state_ids =
            abstraction->refine(abstract_state, split->var_id, split->values);
        refine_timer.stop();

        dump_dot_graph();

        update_goal_distances_timer.resume();
        shortest_paths->update_incrementally(
            *abstraction, state_id, new_state_ids.first, new_state_ids.second,
            split->var_id);
        update_goal_distances_timer.stop();

        if (log.is_at_least_verbose() &&
            abstraction->get_num_states() % 1000 == 0) {
            log << abstraction->get_num_states() << "/" << max_states
                << " states, " << abstraction->get_num_stored_transitions()
                << "/" << max_stored_transitions << " transitions" << endl;
        }
    }
    if (log.is_at_least_normal()) {
        log << "Time for preprocessing goals: " << process_goals_timer << endl;
        log << "Time for finding abstract traces: " << find_trace_timer << endl;
        log << "Time for finding flaws and computing splits: "
            << find_flaw_timer << endl;
        log << "Time for splitting states: " << refine_timer << endl;
        log << "Time for updating goal distances: "
            << update_goal_distances_timer << endl;
        log << "Number of refinements: " << abstraction->get_num_states() - 1
            << endl;
        if (prune_non_induced_transitions) {
            log << "Time for pruning non-induced transitions: " << prune_timer
                << endl;
            log << "Number of pruned non-induced transitions: "
                << num_pruned_transitions << endl;
        }
    }
}

void CEGAR::dump_dot_graph() const {
    // Dump/write dot file for current abstraction.
    if (dot_graph_verbosity == DotGraphVerbosity::WRITE_TO_CONSOLE) {
        cout << create_dot_graph(task_proxy, *abstraction) << endl;
    } else if (dot_graph_verbosity == DotGraphVerbosity::WRITE_TO_FILE) {
        cout << "Writing dot graph to file 'graph" << abstraction->get_num_states()
            << ".dot'" << endl;
        write_to_file(
            "graph" + to_string(abstraction->get_num_states()) + ".dot",
            create_dot_graph(task_proxy, *abstraction));
    } else if (dot_graph_verbosity != DotGraphVerbosity::SILENT) {
        ABORT("Invalid dot graph verbosity");
    }
}

void CEGAR::print_statistics() const {
    abstraction->print_statistics();
    flaw_search->print_statistics();
    shortest_paths->print_statistics();
}
}
