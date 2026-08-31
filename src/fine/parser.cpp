#include "parser.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace fine::syntax {
namespace {

enum class TokenKind {
    eof,
    identifier,
    integer,
    enum_kw,
    let_kw,
    model_kw,
    proof_kw,
    synth_kw,
    ensures_kw,
    if_kw,
    else_kw,
    takes_kw,
    gives_kw,
    table_kw,
    true_kw,
    false_kw,
    left_brace,
    right_brace,
    left_paren,
    right_paren,
    colon,
    comma,
    semicolon,
    equal,
    equal_equal,
    greater_equal,
    less_equal,
    logical_and,
    logical_or,
    plus,
    minus,
};

struct Token {
    TokenKind kind;
    SourceSpan span;
    std::string_view text;
};

static SourceSpan joined(SourceSpan first, SourceSpan last) {
    return {first.begin, last.end};
}

static char const* spelling(TokenKind kind) {
    switch (kind) {
    case TokenKind::eof: return "end of file";
    case TokenKind::identifier: return "an identifier";
    case TokenKind::integer: return "an integer";
    case TokenKind::enum_kw: return "`enum`";
    case TokenKind::let_kw: return "`let`";
    case TokenKind::model_kw: return "`model`";
    case TokenKind::proof_kw: return "`proof`";
    case TokenKind::synth_kw: return "`synth`";
    case TokenKind::ensures_kw: return "`ensures`";
    case TokenKind::if_kw: return "`if`";
    case TokenKind::else_kw: return "`else`";
    case TokenKind::takes_kw: return "`takes`";
    case TokenKind::gives_kw: return "`gives`";
    case TokenKind::table_kw: return "`table`";
    case TokenKind::true_kw: return "`true`";
    case TokenKind::false_kw: return "`false`";
    case TokenKind::left_brace: return "`{`";
    case TokenKind::right_brace: return "`}`";
    case TokenKind::left_paren: return "`(`";
    case TokenKind::right_paren: return "`)`";
    case TokenKind::colon: return "`:`";
    case TokenKind::comma: return "`,`";
    case TokenKind::semicolon: return "`;`";
    case TokenKind::equal: return "`=`";
    case TokenKind::equal_equal: return "`==`";
    case TokenKind::greater_equal: return "`>=`";
    case TokenKind::less_equal: return "`<=`";
    case TokenKind::logical_and: return "`&&`";
    case TokenKind::logical_or: return "`||`";
    case TokenKind::plus: return "`+`";
    case TokenKind::minus: return "`-`";
    }
    return "a token";
}

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    Token next() {
        skip_space_and_comments();
        SourcePosition begin = position();
        if (at_end()) return {TokenKind::eof, {begin, begin}, {}};

        char c = peek();
        if (is_name_start(c)) {
            std::size_t start = offset_;
            advance();
            while (!at_end() && is_name_continue(peek())) advance();
            std::string_view text = source_.substr(start, offset_ - start);
            return {keyword(text), {begin, position()}, text};
        }

        if (c >= '0' && c <= '9') {
            std::size_t start = offset_;
            advance();
            while (!at_end() && peek() >= '0' && peek() <= '9') advance();
            return {TokenKind::integer, {begin, position()},
                    source_.substr(start, offset_ - start)};
        }

        if ((c == '=' && peek(1) == '=') ||
            (c == '>' && peek(1) == '=') ||
            (c == '<' && peek(1) == '=') ||
            (c == '&' && peek(1) == '&') ||
            (c == '|' && peek(1) == '|')) {
            advance();
            advance();
            TokenKind kind = c == '=' ? TokenKind::equal_equal
                             : c == '>' ? TokenKind::greater_equal
                             : c == '<' ? TokenKind::less_equal
                             : c == '&' ? TokenKind::logical_and
                                        : TokenKind::logical_or;
            return {kind, {begin, position()}, source_.substr(begin.offset, 2)};
        }

        advance();
        TokenKind kind;
        switch (c) {
        case '{': kind = TokenKind::left_brace; break;
        case '}': kind = TokenKind::right_brace; break;
        case '(': kind = TokenKind::left_paren; break;
        case ')': kind = TokenKind::right_paren; break;
        case ':': kind = TokenKind::colon; break;
        case ',': kind = TokenKind::comma; break;
        case ';': kind = TokenKind::semicolon; break;
        case '=': kind = TokenKind::equal; break;
        case '+': kind = TokenKind::plus; break;
        case '-': kind = TokenKind::minus; break;
        default:
            throw ParseError({begin, position()},
                             std::string("unexpected character `") + c + "`");
        }
        return {kind, {begin, position()}, source_.substr(begin.offset, 1)};
    }

private:
    std::string_view source_;
    std::size_t offset_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;

    bool at_end() const { return offset_ == source_.size(); }
    char peek(std::size_t ahead = 0) const {
        return offset_ + ahead < source_.size() ? source_[offset_ + ahead] : '\0';
    }
    SourcePosition position() const { return {offset_, line_, column_}; }

    void advance() {
        char c = source_[offset_++];
        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
    }

    void skip_space_and_comments() {
        for (;;) {
            while (!at_end() && (peek() == ' ' || peek() == '\t' ||
                                 peek() == '\r' || peek() == '\n'))
                advance();
            if (peek() != '/' || peek(1) != '/') return;
            while (!at_end() && peek() != '\n') advance();
        }
    }

    static bool is_name_start(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    static bool is_name_continue(char c) {
        return is_name_start(c) || (c >= '0' && c <= '9');
    }
    static TokenKind keyword(std::string_view text) {
        if (text == "enum") return TokenKind::enum_kw;
        if (text == "let") return TokenKind::let_kw;
        if (text == "model") return TokenKind::model_kw;
        if (text == "proof") return TokenKind::proof_kw;
        if (text == "synth") return TokenKind::synth_kw;
        if (text == "ensures") return TokenKind::ensures_kw;
        if (text == "if") return TokenKind::if_kw;
        if (text == "else") return TokenKind::else_kw;
        if (text == "takes") return TokenKind::takes_kw;
        if (text == "gives") return TokenKind::gives_kw;
        if (text == "table") return TokenKind::table_kw;
        if (text == "true") return TokenKind::true_kw;
        if (text == "false") return TokenKind::false_kw;
        return TokenKind::identifier;
    }
};

class Parser {
public:
    explicit Parser(std::string_view source) : lexer_(source) { advance(); }

    Document document() {
        SourcePosition begin = current_.span.begin;
        Document result;
        while (current_.kind != TokenKind::eof) {
            switch (current_.kind) {
            case TokenKind::enum_kw: result.declarations.emplace_back(enum_decl()); break;
            case TokenKind::let_kw: result.declarations.emplace_back(let_decl()); break;
            case TokenKind::model_kw: result.declarations.emplace_back(model_decl()); break;
            case TokenKind::proof_kw: result.declarations.emplace_back(proof_decl()); break;
            case TokenKind::synth_kw: result.declarations.emplace_back(synth_decl()); break;
            default:
                fail("expected a declaration beginning with `enum`, `let`, `model`, `proof`, or `synth`");
            }
        }
        result.span = {begin, current_.span.end};
        return result;
    }

    Expr expression_document() {
        Expr result = expr();
        take(TokenKind::eof, "after the expression");
        return result;
    }

private:
    Lexer lexer_;
    Token current_{};

    void advance() { current_ = lexer_.next(); }
    [[noreturn]] void fail(std::string message) const {
        if (current_.kind == TokenKind::eof) message += "; found end of file";
        else message += "; found `" + std::string(current_.text) + "`";
        throw ParseError(current_.span, std::move(message));
    }
    Token take(TokenKind kind, std::string_view context = {}) {
        if (current_.kind != kind) {
            std::string message = "expected ";
            message += spelling(kind);
            if (!context.empty()) message += " " + std::string(context);
            fail(std::move(message));
        }
        Token token = current_;
        advance();
        return token;
    }
    bool accept(TokenKind kind) {
        if (current_.kind != kind) return false;
        advance();
        return true;
    }

    EnumDecl enum_decl() {
        Token first = take(TokenKind::enum_kw);
        Token name = take(TokenKind::identifier, "after `enum`");
        take(TokenKind::left_brace, "after the enum name");
        EnumDecl result;
        result.name = std::string(name.text);
        if (current_.kind == TokenKind::right_brace)
            fail("an enum needs at least one case");
        for (;;) {
            Token item = take(TokenKind::identifier, "as an enum case");
            result.cases.emplace_back(item.text);
            result.case_spans.push_back(item.span);
            if (!accept(TokenKind::comma)) break;
            if (current_.kind == TokenKind::right_brace) break;
        }
        Token close = take(TokenKind::right_brace, "to close the enum");
        result.span = joined(first.span, close.span);
        return result;
    }

    LetDecl let_decl() {
        Token first = take(TokenKind::let_kw);
        Token name = take(TokenKind::identifier, "after `let`");
        take(TokenKind::colon, "after the binding name");
        Type annotation = type();
        if (annotation.kind != Type::Kind::table)
            fail("a `let` in this source slice must have a `Table(key, value)` type");
        take(TokenKind::equal, "after the binding type");
        TableLiteral value = table_literal();
        Token close = take(TokenKind::semicolon, "after the table literal");
        return {joined(first.span, close.span), std::string(name.text),
                std::move(annotation), std::move(value)};
    }

    ModelDecl model_decl() {
        Token first = take(TokenKind::model_kw);
        Token name = take(TokenKind::identifier, "after `model`");
        take(TokenKind::colon, "after the model-hole name");
        Type annotation = type();
        if (annotation.kind != Type::Kind::table)
            fail("a `model` declaration must have an array-shaped `Table(key, value)` type");
        std::optional<TableLiteral> value;
        if (accept(TokenKind::equal)) value = table_literal();
        Token close = take(TokenKind::semicolon, "after the model declaration");
        return {joined(first.span, close.span), std::string(name.text),
                std::move(annotation), std::move(value)};
    }

    ProofDecl proof_decl() {
        Token first = take(TokenKind::proof_kw);
        Token name = take(TokenKind::identifier, "after `proof`");
        take(TokenKind::left_brace, "after the proof name");
        take(TokenKind::takes_kw, "as the first proof clause");
        std::vector<NamedArgument> takes = arguments();
        take(TokenKind::semicolon, "after `takes(...)`");
        take(TokenKind::gives_kw, "after the `takes` clause");
        take(TokenKind::left_paren, "after `gives`");
        Expr gives = expr();
        take(TokenKind::right_paren, "after the result expression");
        take(TokenKind::semicolon, "after `gives(...)`");
        Token close = take(TokenKind::right_brace, "to close the proof");
        return {joined(first.span, close.span), std::string(name.text),
                std::move(takes), std::move(gives)};
    }

    SynthDecl synth_decl() {
        Token first = take(TokenKind::synth_kw);
        Token name = take(TokenKind::identifier, "after `synth`");
        take(TokenKind::left_paren, "after the synthesized function name");
        std::vector<Parameter> parameters;
        while (current_.kind != TokenKind::right_paren) {
            Token parameter_name = take(TokenKind::identifier, "as a parameter name");
            take(TokenKind::colon, "after the parameter name");
            Type parameter_type = type();
            parameters.push_back({joined(parameter_name.span, parameter_type.span),
                                  std::string(parameter_name.text),
                                  std::move(parameter_type)});
            if (!accept(TokenKind::comma) && current_.kind != TokenKind::right_paren)
                fail("expected `,` or `)` after a parameter");
        }
        take(TokenKind::right_paren);
        take(TokenKind::colon, "before the synthesized result type");
        Type result_type = type();
        take(TokenKind::left_brace, "before the synthesis specification");
        take(TokenKind::ensures_kw, "as the synthesis specification");
        take(TokenKind::left_brace, "after `ensures`");
        std::vector<Expr> ensures;
        while (current_.kind != TokenKind::right_brace) {
            ensures.push_back(expr());
            take(TokenKind::semicolon, "after an ensured condition");
        }
        if (ensures.empty()) fail("a synthesis declaration needs at least one condition");
        take(TokenKind::right_brace, "to close `ensures`");
        Token close = take(TokenKind::right_brace, "to close the synthesis declaration");
        return {joined(first.span, close.span), std::string(name.text),
                std::move(parameters), std::move(result_type), std::move(ensures)};
    }

    Type type() {
        if (current_.kind == TokenKind::left_paren) {
            Token first = take(TokenKind::left_paren);
            Type result;
            result.kind = Type::Kind::tuple;
            result.arguments.push_back(type());
            take(TokenKind::comma, "between tuple type elements");
            result.arguments.push_back(type());
            while (accept(TokenKind::comma)) {
                if (current_.kind == TokenKind::right_paren) break;
                result.arguments.push_back(type());
            }
            Token close = take(TokenKind::right_paren, "to close the tuple type");
            result.span = joined(first.span, close.span);
            return result;
        }

        Token name = take(TokenKind::identifier, "as a type");
        Type result;
        result.kind = Type::Kind::named;
        result.span = name.span;
        result.name = std::string(name.text);
        if (name.text != "Table" || current_.kind != TokenKind::left_paren) return result;

        result.kind = Type::Kind::table;
        result.name.clear();
        take(TokenKind::left_paren);
        result.arguments.push_back(type());
        take(TokenKind::comma, "between the table key and value types");
        result.arguments.push_back(type());
        Token close = take(TokenKind::right_paren, "to close the table type");
        result.span = joined(name.span, close.span);
        return result;
    }

    static Expr binary(Expr::BinaryOp op, Expr left, Expr right) {
        Expr result;
        result.kind = Expr::Kind::binary;
        result.span = joined(left.span, right.span);
        result.binary_op = op;
        result.elements.push_back(std::move(left));
        result.elements.push_back(std::move(right));
        return result;
    }

    Expr expr() { return logical_or(); }

    Expr logical_or() {
        Expr result = logical_and();
        while (accept(TokenKind::logical_or))
            result = binary(Expr::BinaryOp::logical_or, std::move(result), logical_and());
        return result;
    }

    Expr logical_and() {
        Expr result = equality();
        while (accept(TokenKind::logical_and))
            result = binary(Expr::BinaryOp::logical_and, std::move(result), equality());
        return result;
    }

    Expr equality() {
        Expr result = relational();
        while (accept(TokenKind::equal_equal))
            result = binary(Expr::BinaryOp::equal, std::move(result), relational());
        return result;
    }

    Expr relational() {
        Expr result = additive();
        for (;;) {
            if (accept(TokenKind::greater_equal))
                result = binary(Expr::BinaryOp::greater_equal, std::move(result), additive());
            else if (accept(TokenKind::less_equal))
                result = binary(Expr::BinaryOp::less_equal, std::move(result), additive());
            else return result;
        }
    }

    Expr additive() {
        Expr result = primary();
        for (;;) {
            if (accept(TokenKind::plus))
                result = binary(Expr::BinaryOp::add, std::move(result), primary());
            else if (accept(TokenKind::minus))
                result = binary(Expr::BinaryOp::subtract, std::move(result), primary());
            else return result;
        }
    }

    Expr primary() {
        if (current_.kind == TokenKind::identifier) {
            Token name = take(TokenKind::identifier);
            Expr result;
            result.kind = Expr::Kind::name;
            result.span = name.span;
            result.name = std::string(name.text);
            return result;
        }
        if (current_.kind == TokenKind::true_kw || current_.kind == TokenKind::false_kw) {
            Token value = current_;
            advance();
            Expr result;
            result.kind = Expr::Kind::boolean;
            result.span = value.span;
            result.boolean_value = value.kind == TokenKind::true_kw;
            return result;
        }
        if (current_.kind == TokenKind::integer) {
            Token value = take(TokenKind::integer);
            Expr result;
            result.kind = Expr::Kind::integer;
            result.span = value.span;
            result.integer_text = std::string(value.text);
            return result;
        }
        if (current_.kind == TokenKind::if_kw) {
            Token first = take(TokenKind::if_kw);
            Expr condition = expr();
            take(TokenKind::left_brace, "before the true branch");
            Expr yes = expr();
            take(TokenKind::right_brace, "after the true branch");
            take(TokenKind::else_kw, "after the true branch");
            take(TokenKind::left_brace, "before the false branch");
            Expr no = expr();
            Token close = take(TokenKind::right_brace, "after the false branch");
            Expr result;
            result.kind = Expr::Kind::conditional;
            result.span = joined(first.span, close.span);
            result.elements.push_back(std::move(condition));
            result.elements.push_back(std::move(yes));
            result.elements.push_back(std::move(no));
            return result;
        }
        if (current_.kind == TokenKind::left_paren) {
            Token first = take(TokenKind::left_paren);
            Expr first_element = expr();
            if (!accept(TokenKind::comma)) {
                take(TokenKind::right_paren, "to close the grouped expression");
                return first_element;
            }
            Expr result;
            result.kind = Expr::Kind::tuple;
            result.elements.push_back(std::move(first_element));
            result.elements.push_back(expr());
            while (accept(TokenKind::comma)) {
                if (current_.kind == TokenKind::right_paren) break;
                result.elements.push_back(expr());
            }
            Token close = take(TokenKind::right_paren, "to close the tuple");
            result.span = joined(first.span, close.span);
            return result;
        }
        fail("expected a name, integer, Boolean, tuple, or conditional expression");
    }

    TableLiteral table_literal() {
        Token first = take(TokenKind::table_kw, "as the value of a table binding");
        take(TokenKind::left_paren, "after `table`");
        Token default_name = take(TokenKind::identifier, "for the table option `default`");
        if (default_name.text != "default")
            throw ParseError(default_name.span, "expected the table option `default`; found `" +
                                                   std::string(default_name.text) + "`");
        take(TokenKind::colon, "after `default`");
        Expr default_value = expr();
        take(TokenKind::right_paren, "after the default value");
        take(TokenKind::left_brace, "to begin the table entries");

        std::vector<TableEntry> entries;
        while (current_.kind != TokenKind::right_brace) {
            if (current_.kind == TokenKind::eof)
                fail("expected `}` to close the table literal");
            Expr key = expr();
            take(TokenKind::colon, "between a table key and value");
            Expr value = expr();
            SourceSpan entry_span = joined(key.span, value.span);
            entries.push_back({entry_span, std::move(key), std::move(value)});
            if (!accept(TokenKind::comma) && current_.kind != TokenKind::right_brace)
                fail("expected `,` or `}` after a table entry");
        }
        Token close = take(TokenKind::right_brace);
        return {joined(first.span, close.span), std::move(default_value), std::move(entries)};
    }

    std::vector<NamedArgument> arguments() {
        take(TokenKind::left_paren);
        std::vector<NamedArgument> result;
        while (current_.kind != TokenKind::right_paren) {
            Token name = take(TokenKind::identifier, "as a named argument");
            take(TokenKind::colon, "after the argument name");
            Expr value = expr();
            result.push_back({joined(name.span, value.span), std::string(name.text),
                              std::move(value)});
            if (!accept(TokenKind::comma) && current_.kind != TokenKind::right_paren)
                fail("expected `,` or `)` after a named argument");
        }
        take(TokenKind::right_paren);
        return result;
    }
};

} // namespace

ParseError::ParseError(SourceSpan span, std::string message)
    : std::runtime_error(std::move(message)), span_(span) {}

std::string ParseError::format(std::string_view filename, std::string_view source) const {
    std::size_t line_begin = span_.begin.offset;
    while (line_begin > 0 && source[line_begin - 1] != '\n') --line_begin;
    std::size_t line_end = span_.begin.offset;
    while (line_end < source.size() && source[line_end] != '\n') ++line_end;
    std::string_view line = source.substr(line_begin, line_end - line_begin);
    std::size_t width = std::max<std::size_t>(1, span_.end.offset - span_.begin.offset);

    std::ostringstream out;
    out << filename << ':' << span_.begin.line << ':' << span_.begin.column
        << ": error: " << what() << '\n' << line << '\n'
        << std::string(span_.begin.column - 1, ' ') << '^';
    if (width > 1) out << std::string(width - 1, '~');
    return out.str();
}

Document parse(std::string_view source) {
    return Parser(source).document();
}

Expr parse_expression(std::string_view source) {
    return Parser(source).expression_document();
}

} // namespace fine::syntax
