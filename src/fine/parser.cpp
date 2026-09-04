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

        struct LexedSource {
            std::vector<Token> semantic;
            std::vector<ConcreteToken> concrete;
            SourcePosition end;
        };

        class Lexer {
        public:
            explicit Lexer(std::string_view source) : source_(source) {}

            LexedSource lex() {
                LexedSource result;
                while (true) {
                    lex_trivia(result.concrete);
                    SourcePosition begin = position();
                    if (offset_ == source_.size()) {
                        result.semantic.push_back({Token::Kind::end, {}, {begin, begin}});
                        result.end = begin;
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
                        emit(result, Token::Kind::identifier, ConcreteTokenKind::identifier, start, begin);
                        continue;
                    }
                    if (std::isdigit(c)) {
                        std::size_t start = offset_;
                        do {
                            advance();
                        } while (offset_ < source_.size() &&
                                 std::isdigit(static_cast<unsigned char>(source_[offset_])));
                        emit(result, Token::Kind::integer, ConcreteTokenKind::integer, start, begin);
                        continue;
                    }
                    auto two = offset_ + 1 < source_.size() ? source_.substr(offset_, 2) : std::string_view{};
                    if (two == "->" || two == "==" || two == "=>") {
                        advance();
                        advance();
                        emit(result, Token::Kind::symbol, ConcreteTokenKind::symbol, begin.offset, begin);
                        continue;
                    }
                    if (std::string_view("(){}[],:;=?-").find(static_cast<char>(c)) != std::string_view::npos) {
                        advance();
                        emit(result, Token::Kind::symbol, ConcreteTokenKind::symbol, begin.offset, begin);
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

            SourcePosition position() const {
                return {offset_, line_, column_};
            }

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

            void emit(LexedSource &result, Token::Kind semantic_kind, ConcreteTokenKind concrete_kind,
                      std::size_t start, SourcePosition begin) {
                std::string text(source_.substr(start, offset_ - start));
                SourceSpan span{begin, position()};
                result.semantic.push_back({semantic_kind, text, span});
                result.concrete.push_back({concrete_kind, std::move(text), span});
            }

            void lex_trivia(std::vector<ConcreteToken> &result) {
                while (offset_ < source_.size()) {
                    unsigned char c = static_cast<unsigned char>(source_[offset_]);
                    if (std::isspace(c)) {
                        SourcePosition begin = position();
                        std::size_t start = offset_;
                        do {
                            advance();
                        } while (offset_ < source_.size() &&
                                 std::isspace(static_cast<unsigned char>(source_[offset_])));
                        result.push_back({ConcreteTokenKind::whitespace,
                                          std::string(source_.substr(start, offset_ - start)),
                                          {begin, position()}});
                        continue;
                    }
                    if (offset_ + 1 < source_.size() && source_[offset_] == '/' && source_[offset_ + 1] == '/') {
                        SourcePosition begin = position();
                        std::size_t start = offset_;
                        while (offset_ < source_.size() && source_[offset_] != '\n')
                            advance();
                        result.push_back({ConcreteTokenKind::line_comment,
                                          std::string(source_.substr(start, offset_ - start)),
                                          {begin, position()}});
                        continue;
                    }
                    break;
                }
            }
        };

        class Parser {
        public:
            explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

            Document document() {
                Document result;
                while (peek().kind != Token::Kind::end) {
                    if (at("enum")) {
                        result.enums.push_back(enum_declaration());
                        continue;
                    }
                    if (at("proof") && peek(1).text == "inductive") {
                        result.proof_inductives.push_back(proof_inductive());
                        continue;
                    }
                    if (at("function")) {
                        result.functions.push_back(function());
                        continue;
                    }
                    if (at("proof") && peek(1).text == "function") {
                        result.proof_functions.push_back(proof_function());
                        continue;
                    }
                    if (at("run")) {
                        if (result.run)
                            fail(peek(), "a document may contain at most one `run` declaration");
                        result.run = run();
                        continue;
                    }
                    fail(peek(), "expected `enum`, `proof inductive`, `function`, `proof function`, or `run`");
                }
                return result;
            }

            CounterexampleWitness counterexample_witness() {
                Token begin = expect("counterexample");
                Token function = identifier("counterexample function name");
                CounterexampleWitness result;
                result.function = function.text;
                if (at("takes")) {
                    take();
                    expect("[");
                    if (!at("]")) {
                        while (true) {
                            result.assumed_coeffects.push_back(identifier("assumed coeffect name").text);
                            if (!at(","))
                                break;
                            take();
                        }
                    }
                    expect("]");
                }
                expect("{");
                while (!at("}")) {
                    Token name = identifier("counterexample assignment name");
                    expect(":");
                    ValueType type = value_type();
                    expect("=");
                    ValueExpr value = value_expression();
                    Token end = expect(";");
                    result.entries.push_back(
                        {{name.span.begin, end.span.end}, name.text, std::move(type), std::move(value)});
                }
                Token end = expect("}");
                if (peek().kind != Token::Kind::end)
                    fail(peek(), "expected end of counterexample witness");
                result.span = {begin.span.begin, end.span.end};
                return result;
            }

        private:
            std::vector<Token> tokens_;
            std::size_t cursor_ = 0;
            std::size_t next_node_id_ = 0;

            Token const &peek(std::size_t lookahead = 0) const {
                return tokens_[std::min(cursor_ + lookahead, tokens_.size() - 1)];
            }

            bool at(std::string_view text) const {
                return peek().text == text;
            }

            [[noreturn]] static void fail(Token const &token, std::string message) {
                throw ParseError(token.span, std::move(message));
            }

            Token take() {
                return tokens_[cursor_++];
            }

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
                    return {ValueType::Kind::integer, "Int", token.span};
                if (token.text == "Bool")
                    return {ValueType::Kind::boolean, "Bool", token.span};
                if (token.kind == Token::Kind::identifier)
                    return {ValueType::Kind::enumeration, token.text, token.span};
                fail(token, "expected a value type");
            }

            EnumDecl enum_declaration() {
                Token begin = expect("enum");
                Token name = identifier("enum name");
                expect("{");
                EnumDecl result;
                result.node_id = next_node_id_++;
                result.name = name.text;
                while (!at("}")) {
                    Token constructor = identifier("enum constructor name");
                    EnumConstructorDecl item;
                    item.name = constructor.text;
                    SourcePosition end = constructor.span.end;
                    if (at("(")) {
                        take();
                        if (!at(")")) {
                            while (true) {
                                item.fields.push_back(value_type());
                                if (!at(","))
                                    break;
                                take();
                            }
                        }
                        Token close = expect(")");
                        end = close.span.end;
                    }
                    item.span = {constructor.span.begin, end};
                    result.constructors.push_back(std::move(item));
                    if (at(","))
                        take();
                    else if (!at("}"))
                        fail(peek(), "expected `,` or `}` after enum constructor");
                }
                Token close = take();
                result.span = {begin.span.begin, close.span.end};
                return result;
            }

            ValueParameter value_parameter() {
                Token name = identifier("value parameter name");
                expect(":");
                ValueType type = value_type();
                return {{name.span.begin, type.span.end}, name.text, std::move(type)};
            }

            ProofType proof_type() {
                Token begin = identifier("proof type name");
                expect("(");
                if (begin.text != "Id") {
                    ProofType result;
                    result.kind = ProofType::Kind::inductive;
                    result.name = begin.text;
                    result.node_id = next_node_id_++;
                    if (!at(")")) {
                        while (true) {
                            result.arguments.push_back(value_expression());
                            if (!at(","))
                                break;
                            take();
                        }
                    }
                    Token end = expect(")");
                    result.span = {begin.span.begin, end.span.end};
                    return result;
                }
                ValueType carrier = value_type();
                expect(",");
                ValueExpr left = value_expression();
                expect(",");
                ValueExpr right = value_expression();
                Token end = expect(")");
                ProofType result;
                result.kind = ProofType::Kind::identity;
                result.span = {begin.span.begin, end.span.end};
                result.node_id = next_node_id_++;
                result.name = "Id";
                result.carrier = std::move(carrier);
                result.left = std::move(left);
                result.right = std::move(right);
                return result;
            }

            ProofInductiveDecl proof_inductive() {
                Token begin = expect("proof");
                expect("inductive");
                Token name = identifier("proof inductive name");
                ProofInductiveDecl result;
                result.node_id = next_node_id_++;
                result.name = name.text;
                result.indices = value_parameters();
                expect("{");
                while (!at("}")) {
                    Token constructor = identifier("proof constructor name");
                    ProofConstructorDecl item;
                    item.node_id = next_node_id_++;
                    item.name = constructor.text;
                    auto [value_parameters, proof_parameters] = proof_constructor_parameters();
                    item.parameters = std::move(value_parameters);
                    item.explicit_proof_parameters = std::move(proof_parameters);
                    if (at("takes"))
                        item.proof_parameters = coeffect_parameters();
                    expect("->");
                    item.result_type = proof_type();
                    Token end = expect(";");
                    item.span = {constructor.span.begin, end.span.end};
                    result.constructors.push_back(std::move(item));
                }
                Token end = take();
                result.span = {begin.span.begin, end.span.end};
                return result;
            }

            std::pair<std::vector<ValueParameter>, std::vector<CoeffectParameter>> proof_constructor_parameters() {
                std::vector<ValueParameter> values;
                std::vector<CoeffectParameter> proofs;
                bool saw_proof = false;
                expect("(");
                if (!at(")")) {
                    while (true) {
                        Token name = identifier("proof constructor parameter name");
                        expect(":");
                        bool proof_typed = at("Id") || peek(1).text == "(";
                        if (proof_typed) {
                            ProofType type = proof_type();
                            proofs.push_back({{name.span.begin, type.span.end}, name.text, std::move(type)});
                            saw_proof = true;
                        }
                        else {
                            if (saw_proof)
                                fail(name, "value parameters must precede explicit proof parameters in a proof "
                                           "constructor");
                            ValueType type = value_type();
                            values.push_back({{name.span.begin, type.span.end}, name.text, std::move(type)});
                        }
                        if (!at(","))
                            break;
                        take();
                    }
                }
                expect(")");
                return {std::move(values), std::move(proofs)};
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
                expect("takes");
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
                if (at("match")) {
                    Token begin = take();
                    ValueExpr result;
                    result.kind = ValueExpr::Kind::match;
                    result.node_id = next_node_id_++;
                    result.elements.push_back(value_expression());
                    expect("{");
                    while (!at("}")) {
                        Token constructor = identifier("match constructor");
                        std::vector<std::string> binders;
                        SourcePosition arm_begin = constructor.span.begin;
                        if (at("(")) {
                            take();
                            if (!at(")")) {
                                while (true) {
                                    binders.push_back(identifier("pattern binder").text);
                                    if (!at(","))
                                        break;
                                    take();
                                }
                            }
                            expect(")");
                        }
                        expect("=>");
                        ValueExpr body = value_expression();
                        result.match_constructors.push_back(constructor.text);
                        result.match_binders.push_back(std::move(binders));
                        result.match_arm_spans.push_back({arm_begin, body.span.end});
                        result.elements.push_back(std::move(body));
                        if (at(","))
                            take();
                        else if (!at("}"))
                            fail(peek(), "expected `,` or `}` after match arm");
                    }
                    Token end = take();
                    result.span = {begin.span.begin, end.span.end};
                    return result;
                }
                if (at("(")) {
                    Token begin = take();
                    ValueExpr result = value_expression();
                    Token end = expect(")");
                    result.span = {begin.span.begin, end.span.end};
                    return result;
                }
                if (at("-")) {
                    Token begin = take();
                    if (peek().kind != Token::Kind::integer)
                        fail(peek(), "expected digits after `-`");
                    Token numeral = take();
                    ValueExpr result;
                    result.kind = ValueExpr::Kind::integer;
                    result.span = {begin.span.begin, numeral.span.end};
                    result.node_id = next_node_id_++;
                    result.integer_text = "-" + numeral.text;
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
                if (at("match")) {
                    Token begin = take();
                    ProofExpr result;
                    result.kind = ProofExpr::Kind::match;
                    result.node_id = next_node_id_++;
                    result.matched_proof = identifier("proof match scrutinee").text;
                    expect("{");
                    while (!at("}")) {
                        Token constructor = identifier("proof match constructor");
                        SourcePosition arm_begin = constructor.span.begin;
                        std::vector<std::string> binders;
                        expect("(");
                        if (!at(")")) {
                            while (true) {
                                binders.push_back(identifier("proof match binder").text);
                                if (!at(","))
                                    break;
                                take();
                            }
                        }
                        expect(")");
                        expect("=>");
                        ProofExpr body = proof_expression();
                        result.match_constructors.push_back(constructor.text);
                        result.match_binders.push_back(std::move(binders));
                        result.match_arm_spans.push_back({arm_begin, body.span.end});
                        result.match_bodies.push_back(std::move(body));
                        if (at(","))
                            take();
                        else if (!at("}"))
                            fail(peek(), "expected `,` or `}` after proof match arm");
                    }
                    Token end = take();
                    result.span = {begin.span.begin, end.span.end};
                    return result;
                }
                if (at("?")) {
                    Token token = take();
                    ProofExpr result;
                    result.kind = ProofExpr::Kind::hole;
                    result.span = token.span;
                    result.node_id = next_node_id_++;
                    return result;
                }
                Token token = identifier("proof expression");
                ProofExpr result;
                result.span = token.span;
                result.node_id = next_node_id_++;
                if (token.text != "refl") {
                    result.name = token.text;
                    if (!at("[") && !at("(")) {
                        result.kind = ProofExpr::Kind::name;
                        return result;
                    }
                    result.kind = ProofExpr::Kind::application;
                    expect("(");
                    if (!at(")")) {
                        while (true) {
                            std::size_t saved_cursor = cursor_;
                            std::size_t saved_node_id = next_node_id_;
                            bool accepted_proof = false;
                            try {
                                ProofExpr argument = proof_expression();
                                if (at(",") || at(")")) {
                                    result.argument_kinds.push_back(ProofExpr::ArgumentKind::proof);
                                    result.proof_arguments.push_back(std::move(argument));
                                    accepted_proof = true;
                                }
                            } catch (ParseError const &) {
                            }
                            if (!accepted_proof) {
                                cursor_ = saved_cursor;
                                next_node_id_ = saved_node_id;
                                result.argument_kinds.push_back(ProofExpr::ArgumentKind::value);
                            result.value_arguments.push_back(value_expression());
                            }
                            if (!at(","))
                                break;
                            take();
                        }
                    }
                    Token end = expect(")");
                    result.call_argument_end = end.span.end.offset;
                    result.span.end = end.span.end;
                    if (at("using")) {
                        take();
                        expect("[");
                        if (!at("]")) {
                            while (true) {
                                Token coeffect = identifier("coeffect name");
                                expect("=");
                                ProofExpr proof = proof_expression();
                                result.using_coeffects.push_back(coeffect.text);
                                result.using_spans.push_back({coeffect.span.begin, proof.span.end});
                                result.using_proofs.push_back(std::move(proof));
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
                result.kind = ProofExpr::Kind::reflexivity;
                expect("(");
                result.value = value_expression();
                Token end = expect(")");
                result.span.end = end.span.end;
                return result;
            }

            ProofFunctionDecl proof_function() {
                Token begin = expect("proof");
                expect("function");
                Token name = identifier("proof function name");
                ProofFunctionDecl result;
                result.node_id = next_node_id_++;
                result.name = name.text;
                result.parameters = value_parameters();
                if (at("takes"))
                    result.proof_parameters = coeffect_parameters();
                if (at("inducts")) {
                    take();
                    expect("(");
                    result.induction_parameter = identifier("induction parameter name").text;
                    expect(")");
                }
                expect("->");
                result.result_type = proof_type();
                Token end;
                if (at("{")) {
                    take();
                    result.has_body = true;
                    result.body = proof_expression();
                    end = expect("}");
                }
                else {
                    end = expect(";");
                }
                result.span = {begin.span.begin, end.span.end};
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
                if (at("takes"))
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
                                                               next_node_id_++,
                                                               binding.text,
                                                               std::move(type),
                                                               std::move(value)});
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
                                                                 next_node_id_++,
                                                                 binding.text,
                                                                 std::move(type),
                                                                 std::move(value)});
                    }
                    else if (at("assert")) {
                        Token statement_begin = take();
                        ValueExpr proposition = value_expression();
                        Token end = expect(";");
                        result.statements.emplace_back(AssertDecl{
                            {statement_begin.span.begin, end.span.end}, next_node_id_++, std::move(proposition)});
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

    std::string ConcreteSyntaxTree::render() const {
        std::string result;
        for (auto const &token : tokens)
            result += token.text;
        return result;
    }

    ConcreteSyntaxTree parse_tree(std::string_view source) {
        LexedSource lexed = Lexer(source).lex();
        ConcreteSyntaxTree tree;
        tree.tokens = std::move(lexed.concrete);
        tree.ast = Parser(std::move(lexed.semantic)).document();
        tree.root.kind = ConcreteNodeKind::document;
        tree.root.range = {0, lexed.end.offset};
        tree.root.first_token = 0;
        tree.root.end_token = tree.tokens.size();

        struct DeclarationRange {
            ConcreteNodeKind kind;
            SourceSpan span;
        };
        std::vector<DeclarationRange> declarations;
        for (auto const &declaration : tree.ast.enums)
            declarations.push_back({ConcreteNodeKind::enum_declaration, declaration.span});
        for (auto const &declaration : tree.ast.proof_inductives)
            declarations.push_back({ConcreteNodeKind::proof_inductive_declaration, declaration.span});
        for (auto const &declaration : tree.ast.functions)
            declarations.push_back({ConcreteNodeKind::function_declaration, declaration.span});
        for (auto const &declaration : tree.ast.proof_functions)
            declarations.push_back({ConcreteNodeKind::proof_function_declaration, declaration.span});
        if (tree.ast.run)
            declarations.push_back({ConcreteNodeKind::run_declaration, tree.ast.run->span});
        std::sort(declarations.begin(), declarations.end(),
                  [](auto const &left, auto const &right) { return left.span.begin.offset < right.span.begin.offset; });
        for (auto const &declaration : declarations) {
            ConcreteNode node;
            node.kind = declaration.kind;
            node.range = ConcreteRange::from_span(declaration.span);
            node.first_token = tree.tokens.size();
            node.end_token = tree.tokens.size();
            for (std::size_t index = 0; index < tree.tokens.size(); ++index) {
                auto const &span = tree.tokens[index].span;
                if (span.end.offset <= node.range.begin)
                    continue;
                if (span.begin.offset >= node.range.end)
                    break;
                if (node.first_token == tree.tokens.size())
                    node.first_token = index;
                node.end_token = index + 1;
            }
            tree.root.children.push_back(std::move(node));
        }
        if (tree.render() != source)
            throw std::logic_error("concrete syntax tree lost source bytes");
        return tree;
    }

    Document parse(std::string_view source) {
        return std::move(parse_tree(source).ast);
    }

    CounterexampleWitness parse_counterexample_witness(std::string_view source) {
        LexedSource lexed = Lexer(source).lex();
        return Parser(std::move(lexed.semantic)).counterexample_witness();
    }

}  // namespace fine::syntax
