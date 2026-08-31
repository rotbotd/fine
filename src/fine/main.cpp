#include "demo_source.h"
#include "parser.h"
#include "runtime.h"

#include "c++/z3++.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

int run_source(std::string_view filename, std::string_view source) {
    try {
        fine::syntax::Document document = fine::syntax::parse(source);
        return fine::execute(document, std::cout);
    } catch (fine::syntax::ParseError const& error) {
        std::cerr << error.format(filename, source) << '\n';
        return EXIT_FAILURE;
    } catch (fine::SemanticError const& error) {
        std::cerr << error.format(filename, source) << '\n';
        return EXIT_FAILURE;
    }
}

int run_file(char const* path) {
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
    return run_source(path, source);
}

} // namespace

int main(int argc, char** argv) try {
    if (argc == 2 && std::string_view(argv[1]) == "demo-bisim")
        return run_source("<demo-bisim>", fine::demo_source);
    if (argc == 3 && std::string_view(argv[1]) == "run")
        return run_file(argv[2]);
    std::cerr << "usage: fine demo-bisim\n"
                 "       fine run <source.fine>\n";
    return EXIT_FAILURE;
} catch (z3::exception const& error) {
    std::cerr << "z3: " << error.msg() << '\n';
    return EXIT_FAILURE;
} catch (std::exception const& error) {
    std::cerr << "fine: " << error.what() << '\n';
    return EXIT_FAILURE;
}
