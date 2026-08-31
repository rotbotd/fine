#include "parser.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace fine::syntax {
namespace {

enum class TokenKind {
    eof,
    identifier,
    enum_kw,
    let_kw,
    model_kw,
    proof_kw,
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
    case TokenKind::enum_kw: return "`enum`";
    case TokenKind::let_kw: return "`let`";
    case TokenKind::model_kw: return "`model`";
    case TokenKind::proof_kw: return "`proof`";
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
            case TokenKind::model_kw: {
                if (has_model_)
                    fail("this source slice permits one `model` hole; the first was at " +
                         std::to_string(model_span_.begin.line) + ":" +
                         std::to_string(model_span_.begin.column));
                ModelDecl decl = model_decl();
                has_model_ = true;
                model_span_ = decl.span;
                result.declarations.emplace_back(std::move(decl));
                break;
            }
            case TokenKind::proof_kw: result.declarations.emplace_back(proof_decl()); break;
            default:
                fail("expected a declaration beginning with `enum`, `let`, `model`, or `proof`");
            }
        }
        if (!has_model_)
            fail("expected exactly one array-shaped `model` hole in this source slice");
        result.span = {begin, current_.span.end};
        return result;
    }

private:
    Lexer lexer_;
    Token current_{};
    bool has_model_ = false;
    SourceSpan model_span_{};

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
            fail("a `model` hole must have an array-shaped `Table(key, value)` type");
        Token close = take(TokenKind::semicolon, "after the model-hole declaration");
        return {joined(first.span, close.span), std::string(name.text), std::move(annotation)};
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

    Expr expr() {
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
        if (current_.kind == TokenKind::left_paren) {
            Token first = take(TokenKind::left_paren);
            Expr result;
            result.kind = Expr::Kind::tuple;
            result.elements.push_back(expr());
            take(TokenKind::comma, "between tuple elements");
            result.elements.push_back(expr());
            while (accept(TokenKind::comma)) {
                if (current_.kind == TokenKind::right_paren) break;
                result.elements.push_back(expr());
            }
            Token close = take(TokenKind::right_paren, "to close the tuple");
            result.span = joined(first.span, close.span);
            return result;
        }
        fail("expected a name, Boolean, or tuple expression");
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

} // namespace fine::syntax
