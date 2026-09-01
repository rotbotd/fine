#pragma once

#include "c++/z3++.h"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fine {

    // One JSON field whose value has already been encoded. The recorder owns the
    // envelope and the term registry; producers only supply operation-specific
    // data. This deliberately keeps chronology separate from evidence references.
    struct RainfallField {
        std::string name;
        std::string json;
    };

    class RainfallRecorder {
    public:
        RainfallRecorder(z3::context &context, std::ostream &output, std::string run = {});

        // Registers a strong z3::expr reference and returns its recorder-scoped
        // reference. Handles are never reused. Z3 AST IDs are emitted only as
        // diagnostics and are not used to establish identity.
        std::string term(z3::expr const &expression, std::string_view representation = "semantic-z3");

        void record(std::string_view kind, std::string_view operation, std::vector<std::string> const &within,
                    std::string_view producer, std::string_view coverage, std::vector<RainfallField> const &data = {});

        static RainfallField string_field(std::string name, std::string_view value);
        static RainfallField number_field(std::string name, std::size_t value);
        static RainfallField boolean_field(std::string name, bool value);
        static RainfallField raw_field(std::string name, std::string json);
        static std::string quote(std::string_view text);
        static std::string string_array(std::vector<std::string> const &values);

    private:
        z3::context &context_;
        std::ostream &output_;
        std::string run_;
        std::vector<z3::expr> terms_;
        std::size_t sequence_ = 0;
    };

}  // namespace fine
