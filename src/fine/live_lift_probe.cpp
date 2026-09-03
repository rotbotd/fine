#include "live_lift_probe.h"

#include "live_lift.h"
#include "rainfall_lift.h"
#include "z3_fixedpoint.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fine {
    namespace {

        struct LiftGate {
            std::mutex mutex;
            std::condition_variable changed;
            bool entered = false;
            bool released = false;
        };

        struct SpacerProbeObserver {
            z3::context &context;
            LiveLiftPipeline &pipeline;
            std::size_t lemmas = 0;
            std::exception_ptr failure;

            static void on_lemma(void *raw, Z3_ast lemma, unsigned) noexcept {
                auto &observer = *static_cast<SpacerProbeObserver *>(raw);
                try {
                    observer.pipeline.observe("spacer-probe", observer.context, z3::expr(observer.context, lemma));
                    ++observer.lemmas;
                } catch (...) {
                    observer.failure = std::current_exception();
                }
            }

            static void on_predecessor(void *) noexcept {}
            static void on_unfold(void *) noexcept {}
        };

        void release(LiftGate &gate) {
            {
                std::lock_guard lock(gate.mutex);
                gate.released = true;
            }
            gate.changed.notify_all();
        }

        std::size_t run_spacer_probe(std::ostream &output) {
            using namespace std::chrono_literals;

            LiftGate gate;
            std::vector<LiveLiftView> views;
            LiveLiftPipeline pipeline(
                3, [&](LiveLiftView view) { views.push_back(std::move(view)); },
                [&](z3::context &context, z3::expr const &term) {
                    {
                        std::unique_lock lock(gate.mutex);
                        gate.entered = true;
                        gate.changed.notify_all();
                        gate.changed.wait(lock, [&] { return gate.released; });
                    }
                    return lift_rainfall_term(context, term, true).text;
                });

            std::mutex solver_mutex;
            std::condition_variable solver_changed;
            bool solver_done = false;
            std::size_t lemma_count = 0;
            std::exception_ptr solver_failure;
            std::thread solver([&] {
                try {
                    z3::context active_context;
                    z3::fixedpoint fixedpoint(active_context);
                    z3::params parameters(active_context);
                    parameters.set("engine", "spacer");
                    parameters.set("spacer.p3.share_lemmas", true);
                    parameters.set("spacer.p3.share_invariants", true);
                    fixedpoint.set(parameters);

                    z3::sort integer = active_context.int_sort();
                    z3::func_decl reachable =
                        active_context.function("LiveLiftReachable", integer, active_context.bool_sort());
                    fixedpoint.register_relation(reachable);
                    z3::expr n = active_context.int_const("n");
                    z3::expr base = reachable(active_context.int_val(0));
                    z3::expr step = z3::forall(n, z3::implies(reachable(n), reachable(n + 1)));
                    fixedpoint.add_rule(base, active_context.str_symbol("base"));
                    fixedpoint.add_rule(step, active_context.str_symbol("step"));

                    SpacerProbeObserver observer{active_context, pipeline};
                    Z3_fixedpoint_add_callback(active_context, fixedpoint, &observer, &SpacerProbeObserver::on_lemma,
                                               &SpacerProbeObserver::on_predecessor, &SpacerProbeObserver::on_unfold);
                    active_context.check_error();
                    z3::expr query = reachable(active_context.int_val(-1));
                    if (fixedpoint.query(query) != z3::unsat)
                        throw std::runtime_error("Spacer live lifting probe did not prove the query");
                    if (observer.failure)
                        std::rethrow_exception(observer.failure);
                    lemma_count = observer.lemmas;
                } catch (...) {
                    solver_failure = std::current_exception();
                }
                {
                    std::lock_guard lock(solver_mutex);
                    solver_done = true;
                }
                solver_changed.notify_one();
            });

            bool completed_while_lifter_blocked;
            {
                std::unique_lock lock(solver_mutex);
                completed_while_lifter_blocked = solver_changed.wait_for(lock, 5s, [&] { return solver_done; });
            }
            if (!completed_while_lifter_blocked) {
                release(gate);
                solver.join();
                throw std::runtime_error("Spacer query waited for a blocked Fine lifter");
            }
            solver.join();
            if (solver_failure) {
                release(gate);
                std::rethrow_exception(solver_failure);
            }
            {
                std::unique_lock lock(gate.mutex);
                if (!gate.changed.wait_for(lock, 5s, [&] { return gate.entered; })) {
                    gate.released = true;
                    lock.unlock();
                    gate.changed.notify_all();
                    throw std::runtime_error("Spacer exported no live lifting observation");
                }
            }

            pipeline.request_cancel();
            release(gate);
            LiveLiftStats stats = pipeline.join();
            if (lemma_count == 0 || stats.observed != lemma_count || stats.published == 0 || !stats.latest_observed ||
                !stats.latest_published || *stats.latest_published != *stats.latest_observed ||
                stats.published + stats.dropped != stats.observed || views.empty() ||
                views.back().sequence != *stats.latest_observed)
                throw std::runtime_error("Spacer live lifting ownership accounting failed");

            output << "spacer-completed-while-lifter-blocked: true\n"
                   << "spacer-lemma-observations: " << lemma_count << '\n'
                   << "spacer-latest-published: " << *stats.latest_published << '\n';
            return lemma_count;
        }

    }  // namespace

    int run_live_lift_probe(std::ostream &output) {
        using namespace std::chrono_literals;

        run_spacer_probe(output);

        LiftGate gate;
        std::vector<LiveLiftView> views;
        LiveLiftPipeline pipeline(
            3, [&](LiveLiftView view) { views.push_back(std::move(view)); },
            [&](z3::context &context, z3::expr const &term) {
                {
                    std::unique_lock lock(gate.mutex);
                    gate.entered = true;
                    gate.changed.notify_all();
                    gate.changed.wait(lock, [&] { return gate.released; });
                }
                return lift_rainfall_term(context, term, true).text;
            });

        std::mutex producer_mutex;
        std::condition_variable producer_changed;
        bool producer_done = false;
        std::exception_ptr producer_failure;
        constexpr std::size_t observation_count = 12;

        std::thread producer([&] {
            try {
                z3::context active_context;
                z3::expr x = active_context.int_const("x");
                for (std::size_t i = 0; i < observation_count; ++i) {
                    z3::expr value = active_context.int_val(static_cast<int>(i));
                    pipeline.observe("probe", active_context, x + value == value);
                }
            } catch (...) {
                producer_failure = std::current_exception();
            }
            {
                std::lock_guard lock(producer_mutex);
                producer_done = true;
            }
            producer_changed.notify_one();
        });

        bool completed_while_lifter_blocked;
        {
            std::unique_lock lock(producer_mutex);
            completed_while_lifter_blocked = producer_changed.wait_for(lock, 5s, [&] { return producer_done; });
        }
        if (!completed_while_lifter_blocked) {
            release(gate);
            producer.join();
            throw std::runtime_error("live lifting producer waited for a blocked lifter");
        }
        producer.join();
        if (producer_failure) {
            release(gate);
            std::rethrow_exception(producer_failure);
        }

        {
            std::unique_lock lock(gate.mutex);
            if (!gate.changed.wait_for(lock, 5s, [&] { return gate.entered; })) {
                gate.released = true;
                lock.unlock();
                gate.changed.notify_all();
                throw std::runtime_error("live lifting worker did not consume a snapshot");
            }
        }

        pipeline.request_cancel();
        release(gate);
        LiveLiftStats stats = pipeline.join();

        if (stats.observed != observation_count || stats.published == 0 || !stats.latest_observed ||
            !stats.latest_published || *stats.latest_observed != observation_count - 1 ||
            *stats.latest_published != *stats.latest_observed || stats.published + stats.dropped != stats.observed ||
            views.empty() || views.back().sequence != observation_count - 1)
            throw std::runtime_error("live lifting ownership accounting failed");

        output << "producer-completed-while-lifter-blocked: true\n"
               << "observed: " << stats.observed << '\n'
               << "published: " << stats.published << '\n'
               << "dropped-intermediate: " << stats.dropped << '\n'
               << "latest-observed: " << *stats.latest_observed << '\n'
               << "latest-published: " << *stats.latest_published << '\n'
               << "latest-exact-generated-term: " << views.back().text << '\n';
        return 0;
    }

}  // namespace fine
