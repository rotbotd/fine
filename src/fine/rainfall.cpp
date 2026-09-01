#include "rainfall.h"

#include <iomanip>
#include <ostream>
#include <random>
#include <sstream>
#include <stdexcept>

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

    RainfallRecorder::RainfallRecorder(z3::context &context, std::ostream &output, std::string run,
                                       SourceSnapshot const *snapshot)
        : context_(context), output_(output), run_(run.empty() ? fresh_run() : std::move(run)), snapshot_(snapshot) {
        if (!snapshot_)
            return;
        record("object", "source.document.declare", {}, "fine.source-snapshot",
               "Opaque document identity for this CLI compilation",
               {string_field("id", snapshot_->document_id), string_field("display_name", snapshot_->display_name)});
        std::ostringstream identity;
        identity << "{\"document\":" << quote(snapshot_->document_id) << ",\"revision\":" << snapshot_->revision
                 << ",\"exact_source_hash\":" << quote(snapshot_->exact_source_hash)
                 << ",\"byte_length\":" << snapshot_->byte_length << '}';
        record("object", "source.snapshot.declare", {}, "fine.source-snapshot",
               "Exact immutable source input for all source evidence in this run",
               {string_field("id", "snapshot:0"), raw_field("identity", identity.str())});
    }

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

    std::string RainfallRecorder::record(std::string_view kind, std::string_view operation,
                                         std::vector<std::string> const &within,
                                         std::string_view producer,
                                         std::string_view coverage,
                                         std::vector<RainfallField> const &data) {
        std::size_t current = sequence_++;
        std::string event = "event:" + std::to_string(current);
        output_ << "{\"schema\":\"fine.rainfall.v2\",\"run\":" << quote(run_)
                << ",\"recorder\":\"recorder:0\",\"manager\":\"manager:0\""
                << ",\"event_id\":" << quote(event) << ",\"sequence\":" << current << ",\"kind\":" << quote(kind)
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
        return event;
    }

    void RainfallRecorder::remember_quantifier_instance(
        std::string quantifier, std::string instance, std::string event) {
        auto key = std::make_pair(std::move(quantifier), std::move(instance));
        if (!pending_quantifier_instances_.emplace(std::move(key), std::move(event)).second)
            throw std::runtime_error("duplicate pending Rainfall quantifier instance");
    }

    std::optional<std::string> RainfallRecorder::take_quantifier_instance(
        std::string const &quantifier, std::string const &instance) {
        auto found = pending_quantifier_instances_.find({quantifier, instance});
        if (found == pending_quantifier_instances_.end())
            return std::nullopt;
        std::string event = std::move(found->second);
        pending_quantifier_instances_.erase(found);
        return event;
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

    std::string RainfallRecorder::source_node(std::size_t parse_local_node_id, syntax::SourceSpan span,
                                              std::string_view syntax_kind) {
        if (!snapshot_)
            throw std::runtime_error("Rainfall source node requires a source snapshot");
        auto found = source_nodes_.find(parse_local_node_id);
        if (found != source_nodes_.end())
            return found->second;
        std::string reference = "source:" + std::to_string(parse_local_node_id);
        std::ostringstream encoded_span;
        auto position = [&](syntax::SourcePosition value) {
            std::ostringstream result;
            result << "{\"offset\":" << value.offset << ",\"line\":" << value.line << ",\"column\":" << value.column
                   << '}';
            return result.str();
        };
        encoded_span << "{\"begin\":" << position(span.begin) << ",\"end\":" << position(span.end) << '}';
        source_nodes_.emplace(parse_local_node_id, reference);
        record("object", "source.node.declare", {}, "fine.parser", "Parse-local syntax identity bound to snapshot:0",
               {string_field("id", reference), string_field("snapshot", "snapshot:0"),
                number_field("parse_local_node_id", parse_local_node_id), raw_field("span", encoded_span.str()),
                string_field("syntax_kind", syntax_kind)});
        return reference;
    }

    void RainfallRecorder::source_term(std::size_t parse_local_node_id, syntax::SourceSpan span,
                                       std::string_view syntax_kind, z3::expr const &expression,
                                       std::string_view correspondence, std::vector<std::string> const &within) {
        std::string source = source_node(parse_local_node_id, span, syntax_kind);
        std::string term_reference = term(expression);
        record("derive", "source.term.evidence", within, "fine.elaborator",
               "Compiler-owned source-to-term correspondence for this exact snapshot and manager",
               {string_field("snapshot", "snapshot:0"), string_field("source", source),
                string_field("term", term_reference), string_field("correspondence", correspondence)});
    }

}  // namespace fine
