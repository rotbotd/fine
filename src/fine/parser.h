#pragma once

#include <cstddef>
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

    SourceSpan span() const noexcept { return span_; }
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
    enum class Kind { name, boolean, tuple };

    Kind kind = Kind::name;
    SourceSpan span;
    std::string name;
    bool boolean_value = false;
    std::vector<Expr> elements;
};

struct EnumDecl {
    SourceSpan span;
    std::string name;
    std::vector<std::string> cases;
    std::vector<SourceSpan> case_spans;
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

using Declaration = std::variant<EnumDecl, LetDecl, ModelDecl, ProofDecl>;

struct Document {
    SourceSpan span;
    std::vector<Declaration> declarations;
};

// Throws ParseError on the first lexical or syntactic error.
Document parse(std::string_view source);

} // namespace fine::syntax
