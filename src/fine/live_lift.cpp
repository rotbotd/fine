#include "live_lift.h"
#include "rainfall_lift.h"

#include <stdexcept>
#include <utility>

namespace fine {

    struct LiveLiftPipeline::Snapshot {
        std::string run;
        std::uint64_t sequence;
        std::unique_ptr<z3::context> context;
        z3::expr term;

        Snapshot(std::string run, std::uint64_t sequence, std::unique_ptr<z3::context> context, Z3_ast term)
            : run(std::move(run)), sequence(sequence), context(std::move(context)), term(*this->context, term) {}
    };

    LiveLiftPipeline::LiveLiftPipeline(std::size_t capacity, Publish publish, Lift lift)
        : capacity_(capacity), publish_(std::move(publish)), lift_(std::move(lift)) {
        if (capacity_ == 0)
            throw std::invalid_argument("live lifting queue capacity must be positive");
        if (!publish_)
            throw std::invalid_argument("live lifting requires a publisher");
        if (!lift_) {
            lift_ = [](z3::context &context, z3::expr const &term) {
                return lift_rainfall_term(context, term, true).text;
            };
        }
        worker_ = std::thread([this] { worker_loop(); });
    }

    LiveLiftPipeline::~LiveLiftPipeline() {
        try {
            request_cancel();
            join();
        } catch (...) {
            // Destructors cannot report a worker failure. Explicit join() does.
        }
    }

    std::uint64_t LiveLiftPipeline::observe(std::string run, z3::context &source_context, z3::expr const &term) {
        std::uint64_t sequence;
        {
            std::lock_guard lock(mutex_);
            if (!accepting_)
                throw std::logic_error("cannot observe a term after live lifting stopped");
            sequence = next_sequence_++;
            ++stats_.observed;
            stats_.latest_observed = sequence;
        }

        // This is the only contact with the active manager. Translation happens
        // on its owner thread and produces a manager which the lifter owns alone.
        auto snapshot_context = std::make_unique<z3::context>();
        Z3_ast copied = Z3_translate(source_context, term, *snapshot_context);
        source_context.check_error();
        snapshot_context->check_error();
        if (!copied)
            throw std::runtime_error("failed to copy live lifting observation");
        auto snapshot = std::make_unique<Snapshot>(std::move(run), sequence, std::move(snapshot_context), copied);

        {
            std::lock_guard lock(mutex_);
            if (!accepting_) {
                ++stats_.dropped;
                return sequence;
            }
            if (queue_.size() == capacity_) {
                queue_.pop_front();
                ++stats_.dropped;
            }
            queue_.push_back(std::move(snapshot));
        }
        ready_.notify_one();
        return sequence;
    }

    void LiveLiftPipeline::close() {
        request_stop(false);
    }

    void LiveLiftPipeline::request_cancel() {
        request_stop(true);
    }

    void LiveLiftPipeline::request_stop(bool cancel) {
        {
            std::lock_guard lock(mutex_);
            if (joined_)
                return;
            accepting_ = false;
            stopping_ = true;
            if (cancel && queue_.size() > 1) {
                stats_.dropped += queue_.size() - 1;
                auto newest = std::move(queue_.back());
                queue_.clear();
                queue_.push_back(std::move(newest));
            }
        }
        ready_.notify_one();
    }

    LiveLiftStats LiveLiftPipeline::join() {
        close();
        if (worker_.joinable())
            worker_.join();
        std::exception_ptr failure;
        LiveLiftStats result;
        {
            std::lock_guard lock(mutex_);
            joined_ = true;
            failure = failure_;
            result = stats_;
        }
        if (failure)
            std::rethrow_exception(failure);
        return result;
    }

    void LiveLiftPipeline::worker_loop() {
        try {
            for (;;) {
                std::unique_ptr<Snapshot> snapshot;
                {
                    std::unique_lock lock(mutex_);
                    ready_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
                    if (queue_.empty()) {
                        if (stopping_)
                            return;
                        continue;
                    }
                    snapshot = std::move(queue_.front());
                    queue_.pop_front();
                }

                std::string text = lift_(*snapshot->context, snapshot->term);
                publish_({std::move(snapshot->run), snapshot->sequence, std::move(text)});
                {
                    std::lock_guard lock(mutex_);
                    ++stats_.published;
                    stats_.latest_published = snapshot->sequence;
                }
                // snapshot destruction releases the term before its context.
            }
        } catch (...) {
            std::lock_guard lock(mutex_);
            failure_ = std::current_exception();
            accepting_ = false;
            stopping_ = true;
            stats_.dropped += queue_.size();
            queue_.clear();
        }
    }

}  // namespace fine
