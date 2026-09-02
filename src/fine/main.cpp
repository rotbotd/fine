#include "parser.h"
#include "runtime.h"
#include "source.h"

#include "c++/z3++.h"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>
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

    bool parse_revision(std::string_view text, std::size_t &result) {
        auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    }

    int run_file(char const *path, bool rainfall = false, RainRequest const *request = nullptr) {
        std::string source = read_file(path);
        try {
            fine::syntax::Document document = fine::syntax::parse(source);
            if (!rainfall) {
                fine::execute(document, std::cout);
                return EXIT_SUCCESS;
            }
            fine::SourceSnapshot snapshot = fine::make_source_snapshot(
                path, source, request ? request->revision : 0,
                request ? request->document_id : std::string{});
            std::ostringstream ordinary_output;
            fine::execute(document, ordinary_output, &std::cout, &snapshot,
                          request ? request->generation : std::string{});
            return EXIT_SUCCESS;
        }
        catch (fine::syntax::ParseError const &error) {
            std::cerr << error.format(path, source) << '\n';
            return EXIT_FAILURE;
        }
        catch (fine::SemanticError const &error) {
            std::cerr << error.format(path, source) << '\n';
            return EXIT_FAILURE;
        }
    }

    int materialize_file(char const *path) {
        std::string source = read_file(path);
        try {
            fine::syntax::Document document = fine::syntax::parse(source);
            std::ostringstream first_output;
            fine::ExecutionResult result = fine::execute(document, first_output);
            std::string materialized = fine::apply_materializations(source, result.materializations);
            fine::syntax::Document reparsed = fine::syntax::parse(materialized);
            std::ostringstream check_output;
            fine::ExecutionOptions options;
            options.require_explicit_coeffects = true;
            options.require_materialized_proofs = true;
            fine::execute(reparsed, check_output, nullptr, nullptr, {}, options);
            std::cout << materialized;
            return EXIT_SUCCESS;
        }
        catch (fine::syntax::ParseError const &error) {
            std::cerr << error.format(path, source) << '\n';
            return EXIT_FAILURE;
        }
        catch (fine::SemanticError const &error) {
            std::cerr << error.format(path, source) << '\n';
            return EXIT_FAILURE;
        }
    }

}  // namespace

int main(int argc, char **argv) try {
    if (argc == 3 && std::string_view(argv[1]) == "run")
        return run_file(argv[2]);
    if (argc == 3 && std::string_view(argv[1]) == "rain")
        return run_file(argv[2], true);
    if (argc == 3 && std::string_view(argv[1]) == "materialize")
        return materialize_file(argv[2]);
    if (argc == 9 && std::string_view(argv[1]) == "rain" &&
        std::string_view(argv[2]) == "--document" &&
        std::string_view(argv[4]) == "--revision" &&
        std::string_view(argv[6]) == "--generation") {
        RainRequest request{argv[3], 0, argv[7]};
        if (request.document_id.empty() || request.generation.empty() ||
            !parse_revision(argv[5], request.revision)) {
            std::cerr << "fine: invalid rain snapshot request\n";
            return EXIT_FAILURE;
        }
        return run_file(argv[8], true, &request);
    }
    std::cerr << "usage: fine run <source.fine>\n"
                 "       fine rain <source.fine>\n"
                 "       fine materialize <source.fine>\n"
                 "       fine rain --document <id> --revision <n> "
                 "--generation <id> <source.fine>\n";
    return EXIT_FAILURE;
}
catch (z3::exception const &error) {
    std::cerr << "z3: " << error.msg() << '\n';
    return EXIT_FAILURE;
}
catch (std::exception const &error) {
    std::cerr << "fine: " << error.what() << '\n';
    return EXIT_FAILURE;
}
