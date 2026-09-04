#pragma once

#include "c++/z3++.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace fine {

    struct LiveLiftEdit {
        std::size_t begin = 0;
        std::size_t end = 0;
        std::string text;
    };

    struct LiveLiftView {
        std::string run;
        std::uint64_t sequence = 0;
        std::string text;
        std::vector<LiveLiftEdit> prior_edits;
    };

    struct LiveLiftStats {
        std::size_t observed = 0;
        std::size_t published = 0;
        std::size_t dropped = 0;
        std::optional<std::uint64_t> latest_observed;
        std::optional<std::uint64_t> latest_published;
    };

    // A single producer owns the active solver context. observe() copies a term
    // into an independent context before returning; the worker never touches the
    // active context. A bounded queue keeps the newest observations and may drop
    // intermediate presentation frames. Cancellation keeps the newest queued
    // snapshot so the last source view can still be validated and published.
    // Each snapshot may also own completed concrete edits which the publisher
    // must compose with the newly lifted term.
    class LiveLiftPipeline {
    public:
        using Lift = std::function<std::string(z3::context &, z3::expr const &)>;
        using Publish = std::function<void(LiveLiftView)>;

        LiveLiftPipeline(std::size_t capacity, Publish publish, Lift lift = {});
        ~LiveLiftPipeline();

        LiveLiftPipeline(LiveLiftPipeline const &) = delete;
        LiveLiftPipeline &operator=(LiveLiftPipeline const &) = delete;

        std::uint64_t observe(std::string run, z3::context &source_context, z3::expr const &term);
        std::uint64_t observe(std::string run, z3::context &source_context, z3::expr const &term, Lift lift);
        std::uint64_t observe(std::string run, z3::context &source_context, z3::expr const &term,
                              std::vector<LiveLiftEdit> prior_edits, Lift lift);

        // close() drains every queued view. request_cancel() discards every
        // queued intermediate view except the newest one. join() waits for the
        // worker, reports ownership statistics, and rethrows worker failures.
        void close();
        void request_cancel();
        LiveLiftStats join();

    private:
        struct Snapshot;

        void request_stop(bool cancel);
        void worker_loop();

        std::size_t capacity_;
        Publish publish_;
        Lift lift_;
        std::mutex mutex_;
        std::condition_variable ready_;
        std::deque<std::unique_ptr<Snapshot>> queue_;
        std::thread worker_;
        bool accepting_ = true;
        bool stopping_ = false;
        bool joined_ = false;
        std::uint64_t next_sequence_ = 0;
        LiveLiftStats stats_;
        std::exception_ptr failure_;
    };

}  // namespace fine
