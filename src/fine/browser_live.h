#pragma once

#include <cstdint>
#include <string_view>

namespace fine {

    void reset_browser_live_mailbox();
    void publish_browser_live_payload(std::uint32_t sequence, std::string_view payload);

}  // namespace fine
