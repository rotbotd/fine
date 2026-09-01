#include "rainfall.h"

#include <iomanip>
#include <ostream>
#include <random>
#include <sstream>

namespace fine {
    namespace {

        std::string fresh_run() {
            std::random_device random;
            std::ostringstream result;
            result << "run:" << std::hex << std::setfill('0');
            for (unsigned i = 0; i < 4; ++i)
                result << std::setw(8) << random();
            return result.str();
        }

    }  // namespace

    RainfallRecorder::RainfallRecorder(z3::context &context, std::ostream &output, std::string run)
        : context_(context), output_(output), run_(run.empty() ? fresh_run() : std::move(run)) {}

    std::string RainfallRecorder::quote(std::string_view text) {
        std::ostringstream result;
        result << '"';
        for (unsigned char character : text) {
            switch (character) {
            case '"': result << "\\\""; break;
            case '\\': result << "\\\\"; break;
            case '\b': result << "\\b"; break;
            case '\f': result << "\\f"; break;
            case '\n': result << "\\n"; break;
            case '\r': result << "\\r"; break;
            case '\t': result << "\\t"; break;
            default:
                if (character < 0x20) {
                    result << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(character)
                           << std::dec << std::setfill(' ');
                }
                else {
                    result << static_cast<char>(character);
                }
            }
        }
        result << '"';
        return result.str();
    }

    std::string RainfallRecorder::string_array(std::vector<std::string> const &values) {
        std::ostringstream result;
        result << '[';
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i)
                result << ',';
            result << quote(values[i]);
        }
        result << ']';
        return result.str();
    }

    RainfallField RainfallRecorder::string_field(std::string name, std::string_view value) {
        return {std::move(name), quote(value)};
    }

    RainfallField RainfallRecorder::number_field(std::string name, std::size_t value) {
        return {std::move(name), std::to_string(value)};
    }

    RainfallField RainfallRecorder::boolean_field(std::string name, bool value) {
        return {std::move(name), value ? "true" : "false"};
    }

    RainfallField RainfallRecorder::raw_field(std::string name, std::string json) {
        return {std::move(name), std::move(json)};
    }

    void RainfallRecorder::record(std::string_view kind, std::string_view operation,
                                  std::vector<std::string> const &within, std::string_view producer,
                                  std::string_view coverage, std::vector<RainfallField> const &data) {
        std::size_t current = sequence_++;
        output_ << "{\"schema\":\"fine.rainfall.v2\",\"run\":" << quote(run_)
                << ",\"recorder\":\"recorder:0\",\"manager\":\"manager:0\""
                << ",\"event_id\":\"event:" << current << "\",\"sequence\":" << current << ",\"kind\":" << quote(kind)
                << ",\"operation\":" << quote(operation) << ",\"within\":" << string_array(within)
                << ",\"producer\":{\"component\":" << quote(producer) << ",\"coverage\":" << quote(coverage)
                << "},\"data\":{";
        for (std::size_t i = 0; i < data.size(); ++i) {
            if (i)
                output_ << ',';
            output_ << quote(data[i].name) << ':' << data[i].json;
        }
        output_ << "}}\n";
        output_.flush();
    }

    std::string RainfallRecorder::term(z3::expr const &expression, std::string_view representation) {
        for (std::size_t i = 0; i < terms_.size(); ++i) {
            if (Z3_is_eq_ast(context_, terms_[i], expression))
                return "term:" + std::to_string(i);
        }

        std::size_t handle = terms_.size();
        terms_.push_back(expression);  // strong ref held until recorder destruction
        std::string reference = "term:" + std::to_string(handle);
        std::ostringstream identity;
        identity << "{\"run\":" << quote(run_) << ",\"recorder\":\"recorder:0\",\"manager\":\"manager:0\""
                 << ",\"handle\":" << handle << ",\"ast_id_at_observation\":" << Z3_get_ast_id(context_, expression)
                 << '}';
        record("object", "term.declare", {}, "fine.ast-registry",
               "Fine-owned Z3 terms observed through the public API; no internal rewrite coverage",
               {string_field("id", reference), raw_field("identity", identity.str()),
                string_field("representation", representation), string_field("text", expression.to_string())});
        return reference;
    }

}  // namespace fine
