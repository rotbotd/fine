#include "parser.h"
#ifdef FINE_HAS_LIVE_LIFT
#include "browser_live.h"
#include "live_lift.h"
#include "live_lift_probe.h"
#endif
#include "runtime.h"
#include "source.h"

#include "c++/z3++.h"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace {

    struct RainRequest {
        std::string document_id;
        std::size_t revision = 0;
        std::string generation;
    };

    std::string read_file(char const *path) {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("cannot open `" + std::string(path) + "`");
        std::ostringstream contents;
        contents << input.rdbuf();
        if (!input.good() && !input.eof())
            throw std::runtime_error("failed while reading `" + std::string(path) + "`");
        return contents.str();
    }

    void write_file(char const *path, std::string const &contents) {
        std::ofstream output(path, std::ios::binary);
        if (!output)
            throw std::runtime_error("cannot open `" + std::string(path) + "` for writing");
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!output)
            throw std::runtime_error("failed while writing `" + std::string(path) + "`");
    }

    bool parse_revision(std::string_view text, std::size_t &result) {
        auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    }

    std::string json_quote(std::string_view text) {
        std::ostringstream output;
        output << '"';
        for (unsigned char character : text) {
            switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20) {
                    constexpr char digits[] = "0123456789abcdef";
                    output << "\\u00" << digits[character >> 4] << digits[character & 15];
                }
                else {
                    output << static_cast<char>(character);
                }
            }
        }
        output << '"';
        return output.str();
    }

    int run_file(char const *path, bool rainfall = false, RainRequest const *request = nullptr,
                 fine::ExecutionOptions options = {}) {
        std::string source = read_file(path);
        try {
            fine::syntax::ConcreteSyntaxTree tree = fine::syntax::parse_tree(source);
            if (!rainfall) {
                fine::execute(tree.ast, std::cout, nullptr, nullptr, {}, options);
                return EXIT_SUCCESS;
            }
            fine::SourceSnapshot snapshot = fine::make_source_snapshot(path, source, request ? request->revision : 0,
                                                                       request ? request->document_id : std::string{});
            std::ostringstream ordinary_output;
            fine::execute(tree.ast, ordinary_output, &std::cout, &snapshot,
                          request ? request->generation : std::string{}, options);
            return EXIT_SUCCESS;
        } catch (fine::syntax::ParseError const &error) {
            std::cerr << error.format(path, source) << '\n';
            return EXIT_FAILURE;
        } catch (fine::SemanticError const &error) {
            std::cerr << error.format(path, source) << '\n';
            return EXIT_FAILURE;
        }
    }

    int materialize_file(char const *path, fine::ExecutionOptions first_options = {},
                         char const *output_path = nullptr) {
        std::string source = read_file(path);
        try {
            fine::syntax::ConcreteSyntaxTree tree = fine::syntax::parse_tree(source);
            std::ostringstream first_output;
            fine::ExecutionResult result = fine::execute(tree.ast, first_output, nullptr, nullptr, {}, first_options);
            std::string materialized = fine::apply_materializations(tree, result.materializations);
            fine::syntax::ConcreteSyntaxTree reparsed = fine::syntax::parse_tree(materialized);
            std::ostringstream check_output;
            fine::ExecutionOptions options;
            options.require_explicit_coeffects = true;
            options.require_materialized_proofs = true;
            fine::execute(reparsed.ast, check_output, nullptr, nullptr, {}, options);
            if (output_path)
                write_file(output_path, materialized);
            else
                std::cout << materialized;
            return EXIT_SUCCESS;
        } catch (fine::syntax::ParseError const &error) {
            std::cerr << error.format(path, source) << '\n';
            return EXIT_FAILURE;
        } catch (fine::SemanticError const &error) {
            std::cerr << error.format(path, source) << '\n';
            return EXIT_FAILURE;
        }
    }

    int checkpoint_file(char const *path, std::size_t budget, char const *output_path = nullptr,
                        char const *rainfall_output_path = nullptr, bool regular_search = false,
                        std::size_t regular_search_limit = 0, std::size_t regular_search_start = 1) {
        std::string source = read_file(path);
        try {
            fine::syntax::ConcreteSyntaxTree tree = fine::syntax::parse_tree(source);
            fine::ExecutionOptions first_options;
            first_options.proof_selector = fine::ProofSelector::z3_model;
            first_options.synthesize_partial_proofs = true;
            first_options.proof_search_cost = budget;
#ifdef FINE_HAS_LIVE_LIFT
            std::unique_ptr<fine::LiveLiftPipeline> live_lift;
            if (regular_search) {
                fine::reset_browser_live_mailbox();
                live_lift = std::make_unique<fine::LiveLiftPipeline>(
                    8, [&](fine::LiveLiftView view) {
                        std::istringstream metadata(view.run);
                        std::size_t begin = 0, end = 0, search_budget = 0, complete = 0;
                        std::size_t closed_frontier = 0, open_leaves = 0, cost = 0;
                        if (!(metadata >> begin >> end >> search_budget >> complete >> closed_frontier >> open_leaves >> cost))
                            throw std::runtime_error("invalid live proof metadata");
                        std::string candidate = fine::apply_materializations(
                            tree, {{fine::syntax::ConcreteRange{begin, end}, view.text}});
                        std::ostringstream payload;
                        payload << "{\"sequence\":" << view.sequence << ",\"budget\":" << search_budget
                                << ",\"cost\":" << cost << ",\"complete\":"
                                << (complete ? "true" : "false") << ",\"closed_frontier\":"
                                << closed_frontier << ",\"open_leaves\":" << open_leaves
                                << ",\"body\":" << json_quote(view.text)
                                << ",\"source\":" << json_quote(candidate) << '}';
                        fine::publish_browser_live_payload(static_cast<std::uint32_t>(view.sequence), payload.str());
                    });
                first_options.live_iterative_proof_search = true;
                first_options.live_proof_search_start = regular_search_start;
                first_options.live_proof_search_limit = regular_search_limit;
                first_options.live_lift = live_lift.get();
            }
#else
            (void)regular_search;
            (void)regular_search_limit;
            (void)regular_search_start;
#endif
            std::ostringstream first_output;
            std::ostringstream rainfall_output;
            fine::SourceSnapshot snapshot = fine::make_source_snapshot(path, source);
            fine::ExecutionResult result = fine::execute(
                tree.ast, first_output, rainfall_output_path ? &rainfall_output : nullptr,
                rainfall_output_path ? &snapshot : nullptr, {}, first_options);
#ifdef FINE_HAS_LIVE_LIFT
            if (live_lift) {
                live_lift->close();
                live_lift->join();
            }
#endif
            std::string checkpoint = fine::apply_materializations(tree, result.materializations);
            fine::syntax::ConcreteSyntaxTree reparsed = fine::syntax::parse_tree(checkpoint);
            fine::ExecutionOptions validation;
            validation.require_explicit_coeffects = true;
            validation.require_materialized_proofs = !result.checkpoint_open;
            validation.validate_partial_proofs = result.checkpoint_open;
            std::ostringstream validation_output;
            fine::ExecutionResult validated =
                fine::execute(reparsed.ast, validation_output, nullptr, nullptr, {}, validation);
            if (validated.checkpoint_open != result.checkpoint_open)
                throw std::runtime_error("checkpoint reparse changed whether the proof is complete");
            if (output_path)
                write_file(output_path, checkpoint);
            else
                std::cout << checkpoint;
            if (rainfall_output_path)
                write_file(rainfall_output_path, rainfall_output.str());
            return EXIT_SUCCESS;
        } catch (fine::syntax::ParseError const &error) {
            std::cerr << error.format(path, source) << '\n';
            return EXIT_FAILURE;
        } catch (fine::SemanticError const &error) {
            std::cerr << error.format(path, source) << '\n';
            return EXIT_FAILURE;
        }
    }

}  // namespace

int main(int argc, char **argv) try {
#ifdef FINE_HAS_LIVE_LIFT
    if (argc == 2 && std::string_view(argv[1]) == "live-lift-probe")
        return fine::run_live_lift_probe(std::cout);
    if (argc == 7 && std::string_view(argv[1]) == "live-checkpoint" &&
        std::string_view(argv[2]) == "--output" && std::string_view(argv[4]) == "--rain-output")
        return checkpoint_file(argv[6], 1, argv[3], argv[5], true);
    if (argc == 9 && std::string_view(argv[1]) == "live-checkpoint" &&
        std::string_view(argv[2]) == "--proof-start" && std::string_view(argv[4]) == "--output" &&
        std::string_view(argv[6]) == "--rain-output") {
        std::size_t start = 0;
        if (!parse_revision(argv[3], start) || start == 0) {
            std::cerr << "fine: proof start must be a positive integer\n";
            return EXIT_FAILURE;
        }
        return checkpoint_file(argv[8], 1, argv[5], argv[7], true, 0, start);
    }
    if (argc == 7 && std::string_view(argv[1]) == "live-checkpoint" &&
        std::string_view(argv[2]) == "--proof-limit" && std::string_view(argv[4]) == "--output") {
        std::size_t limit = 0;
        if (!parse_revision(argv[3], limit) || limit == 0) {
            std::cerr << "fine: proof limit must be a positive integer\n";
            return EXIT_FAILURE;
        }
        return checkpoint_file(argv[6], 1, argv[5], nullptr, true, limit);
    }
#endif
    if (argc == 3 && std::string_view(argv[1]) == "validate-checkpoint") {
        fine::ExecutionOptions options;
        options.validate_partial_proofs = true;
        return run_file(argv[2], false, nullptr, options);
    }
    if (argc == 3 && std::string_view(argv[1]) == "run")
        return run_file(argv[2]);
    if (argc == 3 && std::string_view(argv[1]) == "rain")
        return run_file(argv[2], true);
    if (argc == 3 && std::string_view(argv[1]) == "materialize")
        return materialize_file(argv[2]);
    if (argc == 5 && std::string_view(argv[1]) == "materialize" &&
        std::string_view(argv[2]) == "--output")
        return materialize_file(argv[4], {}, argv[3]);
    if (argc == 3 && std::string_view(argv[1]) == "roundtrip") {
        std::string source = read_file(argv[2]);
        try {
            std::cout << fine::syntax::parse_tree(source).render();
            return EXIT_SUCCESS;
        } catch (fine::syntax::ParseError const &error) {
            std::cerr << error.format(argv[2], source) << '\n';
            return EXIT_FAILURE;
        }
    }
    if (argc == 5 && std::string_view(argv[1]) == "checkpoint" &&
        std::string_view(argv[2]) == "--proof-budget") {
        std::size_t budget = 0;
        if (!parse_revision(argv[3], budget) || budget == 0) {
            std::cerr << "fine: proof budget must be a positive integer\n";
            return EXIT_FAILURE;
        }
        return checkpoint_file(argv[4], budget);
    }
    if (argc == 7 && std::string_view(argv[1]) == "checkpoint" &&
        std::string_view(argv[2]) == "--proof-budget" && std::string_view(argv[4]) == "--output") {
        std::size_t budget = 0;
        if (!parse_revision(argv[3], budget) || budget == 0) {
            std::cerr << "fine: proof budget must be a positive integer\n";
            return EXIT_FAILURE;
        }
        return checkpoint_file(argv[6], budget, argv[5]);
    }
    if (argc == 9 && std::string_view(argv[1]) == "checkpoint" &&
        std::string_view(argv[2]) == "--proof-budget" && std::string_view(argv[4]) == "--output" &&
        std::string_view(argv[6]) == "--rain-output") {
        std::size_t budget = 0;
        if (!parse_revision(argv[3], budget) || budget == 0) {
            std::cerr << "fine: proof budget must be a positive integer\n";
            return EXIT_FAILURE;
        }
        return checkpoint_file(argv[8], budget, argv[5], argv[7]);
    }
    if (argc == 6 && std::string_view(argv[1]) == "rain" && std::string_view(argv[2]) == "--checkpoint" &&
        std::string_view(argv[3]) == "--proof-budget") {
        std::size_t budget = 0;
        if (!parse_revision(argv[4], budget) || budget == 0) {
            std::cerr << "fine: proof budget must be a positive integer\n";
            return EXIT_FAILURE;
        }
        fine::ExecutionOptions options;
        options.proof_selector = fine::ProofSelector::z3_model;
        options.synthesize_partial_proofs = true;
        options.proof_search_cost = budget;
        return run_file(argv[5], true, nullptr, options);
    }
    if (argc == 5 && std::string_view(argv[2]) == "--proof-selector" && std::string_view(argv[3]) == "z3") {
        fine::ExecutionOptions options;
        options.proof_selector = fine::ProofSelector::z3_model;
        if (std::string_view(argv[1]) == "run")
            return run_file(argv[4], false, nullptr, options);
        if (std::string_view(argv[1]) == "rain")
            return run_file(argv[4], true, nullptr, options);
        if (std::string_view(argv[1]) == "materialize")
            return materialize_file(argv[4], options);
    }
    if (argc == 7 && std::string_view(argv[1]) == "materialize" &&
        std::string_view(argv[2]) == "--proof-selector" && std::string_view(argv[3]) == "z3" &&
        std::string_view(argv[4]) == "--output") {
        fine::ExecutionOptions options;
        options.proof_selector = fine::ProofSelector::z3_model;
        return materialize_file(argv[6], options, argv[5]);
    }
    if (argc == 9 && std::string_view(argv[1]) == "rain" && std::string_view(argv[2]) == "--document" &&
        std::string_view(argv[4]) == "--revision" && std::string_view(argv[6]) == "--generation") {
        RainRequest request{argv[3], 0, argv[7]};
        if (request.document_id.empty() || request.generation.empty() || !parse_revision(argv[5], request.revision)) {
            std::cerr << "fine: invalid rain snapshot request\n";
            return EXIT_FAILURE;
        }
        return run_file(argv[8], true, &request);
    }
    if (argc == 11 && std::string_view(argv[1]) == "rain" && std::string_view(argv[2]) == "--proof-selector" &&
        std::string_view(argv[3]) == "z3" && std::string_view(argv[4]) == "--document" &&
        std::string_view(argv[6]) == "--revision" && std::string_view(argv[8]) == "--generation") {
        RainRequest request{argv[5], 0, argv[9]};
        if (request.document_id.empty() || request.generation.empty() || !parse_revision(argv[7], request.revision)) {
            std::cerr << "fine: invalid rain snapshot request\n";
            return EXIT_FAILURE;
        }
        fine::ExecutionOptions options;
        options.proof_selector = fine::ProofSelector::z3_model;
        return run_file(argv[10], true, &request, options);
    }
    std::cerr << "usage: fine run <source.fine>\n"
                 "       fine rain <source.fine>\n"
                 "       fine materialize <source.fine>\n"
                 "       fine materialize [--proof-selector z3] --output <output.fine> <source.fine>\n"
                 "       fine roundtrip <source.fine>\n"
                 "       fine checkpoint --proof-budget <n> <source.fine>\n"
                 "       fine checkpoint --proof-budget <n> --output <output.fine> <source.fine>\n"
                 "       fine checkpoint --proof-budget <n> --output <output.fine> "
                 "--rain-output <trace.rain> <source.fine>\n"
                 "       fine validate-checkpoint <source.fine>\n"
                 "       fine rain --checkpoint --proof-budget <n> <source.fine>\n"
                 "       fine {run|rain|materialize} --proof-selector z3 <source.fine>\n"
                 "       fine rain --document <id> --revision <n> "
                 "--generation <id> <source.fine>\n"
                 "       fine rain --proof-selector z3 --document <id> --revision <n> "
                 "--generation <id> <source.fine>\n";
#ifdef FINE_HAS_LIVE_LIFT
    std::cerr << "       fine live-lift-probe\n"
                 "       fine live-checkpoint --output <output.fine> --rain-output <trace.rain> <source.fine>\n"
                 "       fine live-checkpoint --proof-start <n> --output <output.fine> "
                 "--rain-output <trace.rain> <source.fine>\n"
                 "       fine live-checkpoint --proof-limit <n> --output <output.fine> <source.fine>\n";
#endif
    return EXIT_FAILURE;
} catch (z3::exception const &error) {
    std::cerr << "z3: " << error.msg() << '\n';
    return EXIT_FAILURE;
} catch (std::exception const &error) {
    std::cerr << "fine: " << error.what() << '\n';
    return EXIT_FAILURE;
}
