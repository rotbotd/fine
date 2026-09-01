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
        enum class Kind { name, boolean, integer, tuple, call, binary, conditional, hole };
        enum class BinaryOp { equal, greater_equal, less_equal, logical_and, logical_or, add, subtract };

        Kind kind = Kind::name;
        SourceSpan span;
        std::string name;
        bool boolean_value = false;
        std::string integer_text;
        BinaryOp binary_op = BinaryOp::equal;
        std::vector<Expr> elements;
        // Unique only within one parse. A source identity also includes the exact
        // snapshot that produced this node.
        std::size_t node_id = 0;
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
        std::size_t node_id = 0;
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
        std::size_t node_id = 0;
    };

    struct ModelDecl {
        SourceSpan span;
        std::string name;
        Type type;
        // Empty means a model-shaped hole. A present table is a concrete model
        // witness, using the same syntax as an ordinary table binding.
        std::optional<TableLiteral> value;
        std::size_t node_id = 0;
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
        std::size_t node_id = 0;
    };

    struct Parameter {
        SourceSpan span;
        std::string name;
        Type type;
    };

    struct ProofConstructor {
        SourceSpan span;
        std::string name;
        std::vector<Parameter> parameters;
        std::vector<Expr> premises;
        Expr result;
    };

    // A proof family is an indexed proposition over ordinary Fine values. Its
    // constructors become rules of a least Z3 relation; there is deliberately
    // no runtime datatype of proof witnesses in this first erased slice.
    struct ProofFamilyDecl {
        SourceSpan span;
        std::string name;
        std::vector<Parameter> indices;
        std::vector<ProofConstructor> constructors;
        std::size_t node_id = 0;
    };

    struct MatchBinding {
        SourceSpan span;
        std::string name;
    };

    struct MatchArm {
        SourceSpan span;
        std::string constructor;
        std::vector<MatchBinding> bindings;
        Expr value;
    };

    // Fine functions retain a deliberately visible recursive-datatype match.
    // The first executable slice does not hide recursion behind a printer-only
    // eliminator: elaboration registers this definition with the same Z3
    // manager later used by checks and Rainfall.
    struct FunctionDecl {
        SourceSpan span;
        std::string name;
        std::vector<Parameter> parameters;
        Type result_type;
        Expr scrutinee;
        std::vector<MatchArm> arms;
        std::size_t node_id = 0;
    };

    struct SynthDecl {
        SourceSpan span;
        std::string name;
        std::vector<Parameter> parameters;
        Type result_type;
        std::optional<Expr> scrutinee;
        std::vector<MatchArm> arms;
        std::vector<Expr> ensures;
        std::size_t node_id = 0;
    };

    struct CheckDecl {
        SourceSpan span;
        std::string name;
        std::vector<Parameter> parameters;
        std::optional<std::string> induction_parameter;
        std::optional<SourceSpan> induction_span;
        std::vector<Expr> assumes;
        std::vector<Expr> ensures;
        std::size_t node_id = 0;
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
        std::size_t node_id = 0;
    };

    using Declaration = std::variant<EnumDecl, LetDecl, ModelDecl, ProofDecl, ProofFamilyDecl, FunctionDecl,
                                     SynthDecl, CheckDecl, CounterexampleDecl>;

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
