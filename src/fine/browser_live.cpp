#include "browser_live.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace fine {
    namespace {

        constexpr std::uint32_t empty_sequence = std::numeric_limits<std::uint32_t>::max();
        constexpr std::size_t slot_count = 8;
        constexpr std::size_t payload_capacity = 256 * 1024;

        struct alignas(4) BrowserLiveMailbox {
            std::uint32_t latest = empty_sequence;
            std::array<std::uint32_t, slot_count> sequences{};
            std::array<std::uint32_t, slot_count> lengths{};
            std::array<std::array<char, payload_capacity>, slot_count> payloads{};
        };

        BrowserLiveMailbox mailbox;

        void initialize_sequences() {
            for (std::uint32_t &sequence : mailbox.sequences)
                std::atomic_ref(sequence).store(empty_sequence, std::memory_order_relaxed);
        }

    }  // namespace

    void reset_browser_live_mailbox() {
        std::atomic_ref(mailbox.latest).store(empty_sequence, std::memory_order_release);
        initialize_sequences();
        for (std::uint32_t &length : mailbox.lengths)
            length = 0;
    }

    void publish_browser_live_payload(std::uint32_t sequence, std::string_view payload) {
#ifdef __EMSCRIPTEN__
        if (payload.size() > payload_capacity)
            throw std::runtime_error("live browser payload exceeds its fixed mailbox slot");
        std::size_t slot = sequence % slot_count;
        std::atomic_ref(mailbox.sequences[slot]).store(empty_sequence, std::memory_order_release);
        std::copy(payload.begin(), payload.end(), mailbox.payloads[slot].begin());
        mailbox.lengths[slot] = static_cast<std::uint32_t>(payload.size());
        std::atomic_ref(mailbox.sequences[slot]).store(sequence, std::memory_order_release);
        std::atomic_ref(mailbox.latest).store(sequence, std::memory_order_release);
#else
        (void)sequence;
        (void)payload;
#endif
    }

}  // namespace fine

#ifdef __EMSCRIPTEN__
extern "C" {

    EMSCRIPTEN_KEEPALIVE std::uintptr_t fine_live_mailbox_latest_address() {
        return reinterpret_cast<std::uintptr_t>(&fine::mailbox.latest);
    }

    EMSCRIPTEN_KEEPALIVE std::uintptr_t fine_live_mailbox_sequence_address(std::uint32_t slot) {
        return reinterpret_cast<std::uintptr_t>(&fine::mailbox.sequences.at(slot));
    }

    EMSCRIPTEN_KEEPALIVE std::uintptr_t fine_live_mailbox_length_address(std::uint32_t slot) {
        return reinterpret_cast<std::uintptr_t>(&fine::mailbox.lengths.at(slot));
    }

    EMSCRIPTEN_KEEPALIVE std::uintptr_t fine_live_mailbox_payload_address(std::uint32_t slot) {
        return reinterpret_cast<std::uintptr_t>(fine::mailbox.payloads.at(slot).data());
    }

    EMSCRIPTEN_KEEPALIVE std::uint32_t fine_live_mailbox_slot_count() {
        return static_cast<std::uint32_t>(fine::slot_count);
    }

    EMSCRIPTEN_KEEPALIVE std::uint32_t fine_live_mailbox_payload_capacity() {
        return static_cast<std::uint32_t>(fine::payload_capacity);
    }

    EMSCRIPTEN_KEEPALIVE void fine_live_mailbox_reset() {
        fine::reset_browser_live_mailbox();
    }

}
#endif
