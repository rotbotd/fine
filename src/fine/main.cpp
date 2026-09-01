#include "demo_source.h"
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

    int run_source(std::string_view filename, std::string_view source, bool rainfall = false,
                   RainRequest const *request = nullptr) {
        try {
            fine::syntax::Document document = fine::syntax::parse(source);
            if (!rainfall)
                return fine::execute(document, std::cout);
            fine::SourceSnapshot snapshot = fine::make_source_snapshot(
                filename, source, request ? request->revision : 0, request ? request->document_id : std::string{});
            std::ostringstream ordinary_output;
            return fine::execute(document, ordinary_output, &std::cout, &snapshot,
                                 request ? request->generation : std::string{});
        } catch (fine::syntax::ParseError const &error) {
            std::cerr << error.format(filename, source) << '\n';
            return EXIT_FAILURE;
        } catch (fine::SemanticError const &error) {
            std::cerr << error.format(filename, source) << '\n';
            return EXIT_FAILURE;
        }
    }

    int run_file(char const *path, bool rainfall = false, RainRequest const *request = nullptr) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            std::cerr << "fine: cannot open `" << path << "`\n";
            return EXIT_FAILURE;
        }
        std::ostringstream contents;
        contents << input.rdbuf();
        if (!input.good() && !input.eof()) {
            std::cerr << "fine: failed while reading `" << path << "`\n";
            return EXIT_FAILURE;
        }
        std::string source = contents.str();
        return run_source(path, source, rainfall, request);
    }

    bool parse_revision(std::string_view text, std::size_t &result) {
        auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    }

}  // namespace

int main(int argc, char **argv) try {
    if (argc == 2 && std::string_view(argv[1]) == "demo-bisim")
        return run_source("<demo-bisim>", fine::demo_source);
    if (argc == 3 && std::string_view(argv[1]) == "run")
        return run_file(argv[2]);
    if (argc == 3 && std::string_view(argv[1]) == "rain")
        return run_file(argv[2], true);
    if (argc == 9 && std::string_view(argv[1]) == "rain" && std::string_view(argv[2]) == "--document" &&
        std::string_view(argv[4]) == "--revision" && std::string_view(argv[6]) == "--generation") {
        RainRequest request{argv[3], 0, argv[7]};
        if (request.document_id.empty() || request.generation.empty() || !parse_revision(argv[5], request.revision)) {
            std::cerr << "fine: invalid rain snapshot request\n";
            return EXIT_FAILURE;
        }
        return run_file(argv[8], true, &request);
    }
    std::cerr << "usage: fine demo-bisim\n"
                 "       fine run <source.fine>\n"
                 "       fine rain <source.fine>\n"
                 "       fine rain --document <id> --revision <n> "
                 "--generation <id> <source.fine>\n";
    return EXIT_FAILURE;
} catch (z3::exception const &error) {
    std::cerr << "z3: " << error.msg() << '\n';
    return EXIT_FAILURE;
} catch (std::exception const &error) {
    std::cerr << "fine: " << error.what() << '\n';
    return EXIT_FAILURE;
}
