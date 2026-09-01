#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace fine {

    // A solver claim is bound to this exact immutable input. The document ID is
    // opaque and survives revisions in a future live host; a CLI invocation creates
    // a fresh document at revision zero.
    struct SourceSnapshot {
        std::string document_id;
        std::size_t revision = 0;
        std::string exact_source_hash;
        std::size_t byte_length = 0;
        std::string display_name;
    };

    SourceSnapshot make_source_snapshot(std::string_view display_name, std::string_view source,
                                        std::size_t revision = 0, std::string document_id = {});

}  // namespace fine
