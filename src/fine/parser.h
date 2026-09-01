#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fine::syntax {

    struct SourcePosition {
        std::size_t offset = 0;
        std::size_t line = 1;
        std::size_t column = 1;
    };

    struct SourceSpan {
        SourcePosition begin;
        SourcePosition end;
    };

    class ParseError : public std::runtime_error {
    public:
        ParseError(SourceSpan span, std::string message);

        SourceSpan span() const noexcept {
            return span_;
        }
        std::string format(std::string_view filename, std::string_view source) const;

    private:
        SourceSpan span_;
    };

    // These nodes retain only source syntax. Name and type resolution deliberately
    // happen later, against the declarations interned by a Z3 context.
    struct Type {
        enum class Kind { named, tuple, table };

        Kind kind = Kind::named;
        SourceSpan span;
        std::string name;
        std::vector<Type> arguments;
    };

    struct Expr {
        enum class Kind { name, boolean, integer, tuple, call, binary, conditional };
        enum class BinaryOp { equal, greater_equal, less_equal, logical_and, logical_or, add, subtract };

        Kind kind = Kind::name;
        SourceSpan span;
        std::string name;
        bool boolean_value = false;
        std::string integer_text;
        BinaryOp binary_op = BinaryOp::equal;
        std::vector<Expr> elements;
    };

    struct EnumField {
        SourceSpan span;
        std::string name;
        Type type;
    };

    struct EnumCase {
        SourceSpan span;
        std::string name;
        std::vector<EnumField> fields;
    };

    struct EnumDecl {
        SourceSpan span;
        std::string name;
        std::vector<EnumCase> cases;
    };

    struct TableEntry {
        SourceSpan span;
        Expr key;
        Expr value;
    };

    struct TableLiteral {
        SourceSpan span;
        Expr default_value;
        std::vector<TableEntry> entries;
    };

    struct LetDecl {
        SourceSpan span;
        std::string name;
        Type type;
        TableLiteral value;
    };

    struct ModelDecl {
        SourceSpan span;
        std::string name;
        Type type;
        // Empty means a model-shaped hole. A present table is a concrete model
        // witness, using the same syntax as an ordinary table binding.
        std::optional<TableLiteral> value;
    };

    struct NamedArgument {
        SourceSpan span;
        std::string name;
        Expr value;
    };

    struct ProofDecl {
        SourceSpan span;
        std::string name;
        std::vector<NamedArgument> takes;
        Expr gives;
    };

    struct Parameter {
        SourceSpan span;
        std::string name;
        Type type;
    };

    struct SynthDecl {
        SourceSpan span;
        std::string name;
        std::vector<Parameter> parameters;
        Type result_type;
        std::vector<Expr> ensures;
    };

    struct CheckDecl {
        SourceSpan span;
        std::string name;
        std::vector<Parameter> parameters;
        std::vector<Expr> assumes;
        std::vector<Expr> ensures;
    };

    struct CounterexampleEntry {
        SourceSpan span;
        std::string name;
        Type type;
        Expr value;
    };

    // This is a returned, parseable witness form, not an executable assertion.
    struct CounterexampleDecl {
        SourceSpan span;
        std::string name;
        std::vector<CounterexampleEntry> entries;
    };

    using Declaration = std::variant<EnumDecl, LetDecl, ModelDecl, ProofDecl, SynthDecl, CheckDecl, CounterexampleDecl>;

    struct Document {
        SourceSpan span;
        std::vector<Declaration> declarations;
    };

    // Throws ParseError on the first lexical or syntactic error.
    Document parse(std::string_view source);

    // Parse one ordinary Fine expression and require end of input. This is used by
    // witness round trips; it shares the declaration parser's lexer and precedence
    // implementation rather than introducing a printer-only syntax.
    Expr parse_expression(std::string_view source);

}  // namespace fine::syntax
