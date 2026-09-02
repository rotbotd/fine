#include "parser.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace fine::syntax {
    namespace {

        struct Token {
            enum class Kind { identifier, integer, symbol, end };
            Kind kind = Kind::end;
            std::string text;
            SourceSpan span;
        };

        class Lexer {
        public:
            explicit Lexer(std::string_view source) : source_(source) {}

            std::vector<Token> lex() {
                std::vector<Token> result;
                while (true) {
                    skip_space_and_comments();
                    SourcePosition begin = position();
                    if (offset_ == source_.size()) {
                        result.push_back({Token::Kind::end, {}, {begin, begin}});
                        return result;
                    }
                    unsigned char c = static_cast<unsigned char>(source_[offset_]);
                    if (std::isalpha(c) || c == '_') {
                        std::size_t start = offset_;
                        advance();
                        while (offset_ < source_.size()) {
                            unsigned char next = static_cast<unsigned char>(source_[offset_]);
                            if (!std::isalnum(next) && next != '_')
                                break;
                            advance();
                        }
                        result.push_back({Token::Kind::identifier,
                                          std::string(source_.substr(start, offset_ - start)),
                                          {begin, position()}});
                        continue;
                    }
                    if (std::isdigit(c)) {
                        std::size_t start = offset_;
                        do {
                            advance();
                        } while (offset_ < source_.size() &&
                                 std::isdigit(static_cast<unsigned char>(source_[offset_])));
                        result.push_back({Token::Kind::integer,
                                          std::string(source_.substr(start, offset_ - start)),
                                          {begin, position()}});
                        continue;
                    }
                    auto two = offset_ + 1 < source_.size() ? source_.substr(offset_, 2) : std::string_view{};
                    if (two == "->" || two == "==") {
                        advance();
                        advance();
                        result.push_back({Token::Kind::symbol, std::string(two), {begin, position()}});
                        continue;
                    }
                    if (std::string_view("(){}[],:;=").find(static_cast<char>(c)) != std::string_view::npos) {
                        advance();
                        result.push_back({Token::Kind::symbol, std::string(1, static_cast<char>(c)),
                                          {begin, position()}});
                        continue;
                    }
                    throw ParseError({begin, after_one()},
                                     "unexpected character `" + std::string(1, static_cast<char>(c)) + "`");
                }
            }

        private:
            std::string_view source_;
            std::size_t offset_ = 0;
            std::size_t line_ = 1;
            std::size_t column_ = 1;

            SourcePosition position() const { return {offset_, line_, column_}; }

            SourcePosition after_one() const {
                SourcePosition result = position();
                if (offset_ < source_.size() && source_[offset_] == '\n') {
                    ++result.line;
                    result.column = 1;
                }
                else {
                    ++result.column;
                }
                ++result.offset;
                return result;
            }

            void advance() {
                if (source_[offset_++] == '\n') {
                    ++line_;
                    column_ = 1;
                }
                else {
                    ++column_;
                }
            }

            void skip_space_and_comments() {
                while (offset_ < source_.size()) {
                    unsigned char c = static_cast<unsigned char>(source_[offset_]);
                    if (std::isspace(c)) {
                        advance();
                        continue;
                    }
                    if (offset_ + 1 < source_.size() && source_[offset_] == '/' && source_[offset_ + 1] == '/') {
                        while (offset_ < source_.size() && source_[offset_] != '\n')
                            advance();
                        continue;
                    }
                    break;
                }
            }
        };

        class Parser {
        public:
            explicit Parser(std::string_view source) : tokens_(Lexer(source).lex()) {}

            Document document() {
                Document result;
                while (at("function"))
                    result.functions.push_back(function());
                if (!at("run"))
                    fail(peek(), "expected one `run` declaration after functions");
                result.run = run();
                if (peek().kind != Token::Kind::end)
                    fail(peek(), "unexpected declaration after `run`");
                return result;
            }

        private:
            std::vector<Token> tokens_;
            std::size_t cursor_ = 0;
            std::size_t next_node_id_ = 0;

            Token const &peek(std::size_t lookahead = 0) const {
                return tokens_[std::min(cursor_ + lookahead, tokens_.size() - 1)];
            }

            bool at(std::string_view text) const { return peek().text == text; }

            [[noreturn]] static void fail(Token const &token, std::string message) {
                throw ParseError(token.span, std::move(message));
            }

            Token take() { return tokens_[cursor_++]; }

            Token expect(std::string_view text) {
                if (!at(text))
                    fail(peek(), "expected `" + std::string(text) + "`");
                return take();
            }

            Token identifier(std::string_view role) {
                if (peek().kind != Token::Kind::identifier)
                    fail(peek(), "expected " + std::string(role));
                return take();
            }

            ValueType value_type() {
                Token token = take();
                if (token.text == "Int")
                    return {ValueType::Kind::integer, token.span};
                if (token.text == "Bool")
                    return {ValueType::Kind::boolean, token.span};
                fail(token, "expected value type `Int` or `Bool`");
            }

            ValueParameter value_parameter() {
                Token name = identifier("value parameter name");
                expect(":");
                ValueType type = value_type();
                return {{name.span.begin, type.span.end}, name.text, std::move(type)};
            }

            ProofType proof_type() {
                Token begin = expect("Id");
                expect("(");
                ValueType carrier = value_type();
                expect(",");
                ValueExpr left = value_expression();
                expect(",");
                ValueExpr right = value_expression();
                Token end = expect(")");
                return {ProofType::Kind::identity, {begin.span.begin, end.span.end}, next_node_id_++,
                        std::move(carrier), std::move(left), std::move(right)};
            }

            CoeffectParameter coeffect_parameter() {
                Token name = identifier("coeffect name");
                expect(":");
                ProofType type = proof_type();
                return {{name.span.begin, type.span.end}, name.text, std::move(type)};
            }

            std::vector<ValueParameter> value_parameters() {
                std::vector<ValueParameter> result;
                expect("(");
                if (!at(")")) {
                    while (true) {
                        result.push_back(value_parameter());
                        if (!at(","))
                            break;
                        take();
                    }
                }
                expect(")");
                return result;
            }

            std::vector<CoeffectParameter> coeffect_parameters() {
                std::vector<CoeffectParameter> result;
                expect("needs");
                expect("[");
                if (!at("]")) {
                    while (true) {
                        result.push_back(coeffect_parameter());
                        if (!at(","))
                            break;
                        take();
                    }
                }
                expect("]");
                return result;
            }

            ValueExpr value_expression() {
                ValueExpr left = primary_value();
                if (!at("=="))
                    return left;
                take();
                ValueExpr right = primary_value();
                SourceSpan span{left.span.begin, right.span.end};
                ValueExpr result;
                result.kind = ValueExpr::Kind::equal;
                result.span = span;
                result.node_id = next_node_id_++;
                result.elements.push_back(std::move(left));
                result.elements.push_back(std::move(right));
                return result;
            }

            ValueExpr primary_value() {
                if (at("(")) {
                    Token begin = take();
                    ValueExpr result = value_expression();
                    Token end = expect(")");
                    result.span = {begin.span.begin, end.span.end};
                    return result;
                }
                Token token = take();
                ValueExpr result;
                result.span = token.span;
                result.node_id = next_node_id_++;
                if (token.kind == Token::Kind::integer) {
                    result.kind = ValueExpr::Kind::integer;
                    result.integer_text = token.text;
                    return result;
                }
                if (token.text == "true" || token.text == "false") {
                    result.kind = ValueExpr::Kind::boolean;
                    result.boolean_value = token.text == "true";
                    return result;
                }
                if (token.kind != Token::Kind::identifier)
                    fail(token, "expected a value expression");
                result.name = token.text;
                if (!at("(")) {
                    result.kind = ValueExpr::Kind::name;
                    return result;
                }
                result.kind = ValueExpr::Kind::call;
                take();
                if (!at(")")) {
                    while (true) {
                        result.elements.push_back(value_expression());
                        if (!at(","))
                            break;
                        take();
                    }
                }
                Token close = expect(")");
                result.call_argument_end = close.span.end.offset;
                result.span.end = close.span.end;
                if (at("using")) {
                    take();
                    expect("[");
                    if (!at("]")) {
                        while (true) {
                            Token coeffect = identifier("coeffect name");
                            expect("=");
                            Token proof = identifier("proof name");
                            result.using_proofs.push_back(
                                {{coeffect.span.begin, proof.span.end}, coeffect.text, proof.text});
                            if (!at(","))
                                break;
                            take();
                        }
                    }
                    Token using_end = expect("]");
                    result.span.end = using_end.span.end;
                }
                return result;
            }

            ProofExpr proof_expression() {
                Token token = identifier("proof expression");
                ProofExpr result;
                result.span = token.span;
                result.node_id = next_node_id_++;
                if (token.text != "refl") {
                    result.kind = ProofExpr::Kind::name;
                    result.name = token.text;
                    return result;
                }
                result.kind = ProofExpr::Kind::reflexivity;
                expect("(");
                result.value = value_expression();
                Token end = expect(")");
                result.span.end = end.span.end;
                return result;
            }

            FunctionDecl function() {
                Token begin = expect("function");
                Token name = identifier("function name");
                FunctionDecl result;
                result.node_id = next_node_id_++;
                result.name = name.text;
                result.parameters = value_parameters();
                expect("->");
                result.result_type = value_type();
                if (at("needs"))
                    result.coeffects = coeffect_parameters();
                if (at("ensures")) {
                    take();
                    expect("{");
                    while (!at("}")) {
                        result.ensures.push_back(value_expression());
                        expect(";");
                    }
                    take();
                }
                expect("{");
                result.body = value_expression();
                Token end = expect("}");
                result.span = {begin.span.begin, end.span.end};
                return result;
            }

            RunDecl run() {
                Token begin = expect("run");
                Token name = identifier("run name");
                expect("{");
                RunDecl result;
                result.name = name.text;
                result.node_id = next_node_id_++;
                while (!at("}")) {
                    if (at("let")) {
                        Token statement_begin = take();
                        Token binding = identifier("value binding name");
                        expect(":");
                        ValueType type = value_type();
                        expect("=");
                        ValueExpr value = value_expression();
                        Token end = expect(";");
                        result.statements.emplace_back(LetDecl{{statement_begin.span.begin, end.span.end},
                                                              next_node_id_++, binding.text,
                                                              std::move(type), std::move(value)});
                    }
                    else if (at("proof")) {
                        Token statement_begin = take();
                        Token binding = identifier("proof binding name");
                        expect(":");
                        ProofType type = proof_type();
                        expect("=");
                        ProofExpr value = proof_expression();
                        Token end = expect(";");
                        result.statements.emplace_back(ProofDecl{{statement_begin.span.begin, end.span.end},
                                                                next_node_id_++, binding.text,
                                                                std::move(type), std::move(value)});
                    }
                    else if (at("assert")) {
                        Token statement_begin = take();
                        ValueExpr proposition = value_expression();
                        Token end = expect(";");
                        result.statements.emplace_back(AssertDecl{{statement_begin.span.begin, end.span.end},
                                                                 next_node_id_++, std::move(proposition)});
                    }
                    else {
                        fail(peek(), "expected `let`, `proof`, or `assert` in run body");
                    }
                }
                Token end = take();
                result.span = {begin.span.begin, end.span.end};
                return result;
            }
        };

        std::string source_line(std::string_view source, std::size_t line_number) {
            std::size_t current = 1;
            std::size_t begin = 0;
            while (current < line_number && begin < source.size()) {
                std::size_t next = source.find('\n', begin);
                if (next == std::string_view::npos)
                    return {};
                begin = next + 1;
                ++current;
            }
            std::size_t end = source.find('\n', begin);
            if (end == std::string_view::npos)
                end = source.size();
            return std::string(source.substr(begin, end - begin));
        }

    }  // namespace

    ParseError::ParseError(SourceSpan span, std::string message)
        : std::runtime_error(std::move(message)), span_(span) {}

    std::string ParseError::format(std::string_view filename, std::string_view source) const {
        std::ostringstream output;
        output << filename << ':' << span_.begin.line << ':' << span_.begin.column << ": " << what();
        std::string line = source_line(source, span_.begin.line);
        if (!line.empty()) {
            output << '\n' << line << '\n';
            std::size_t caret = span_.begin.column > 0 ? span_.begin.column - 1 : 0;
            output << std::string(caret, ' ') << '^';
        }
        return output.str();
    }

    Document parse(std::string_view source) { return Parser(source).document(); }

}  // namespace fine::syntax
