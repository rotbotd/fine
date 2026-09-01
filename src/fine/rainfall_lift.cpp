#include "rainfall_lift.h"

#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace fine {
    namespace {

        std::string json_quote(std::string_view text) {
            std::ostringstream output;
            output << '"';
            for (unsigned char character : text) {
                switch (character) {
                case '"': output << "\\\""; break;
                case '\\': output << "\\\\"; break;
                case '\n': output << "\\n"; break;
                case '\r': output << "\\r"; break;
                case '\t': output << "\\t"; break;
                default:
                    if (character < 0x20) {
                        constexpr char digits[] = "0123456789abcdef";
                        output << "\\u00" << digits[character >> 4] << digits[character & 15];
                    }
                    else {
                        output << static_cast<char>(character);
                    }
                }
            }
            output << '"';
            return output.str();
        }

        std::string string_quote(std::string_view text) {
            return json_quote(text);  // the generated Fine string escape set is JSON's subset
        }

        std::string symbol_text(Z3_context context, Z3_symbol symbol) {
            if (!symbol)
                return "<null>";
            if (Z3_get_symbol_kind(context, symbol) == Z3_INT_SYMBOL)
                return std::to_string(Z3_get_symbol_int(context, symbol));
            char const *value = Z3_get_symbol_string(context, symbol);
            return value ? value : "";
        }

        std::string symbol_syntax(Z3_context context, Z3_symbol symbol) {
            if (!symbol)
                return "n";
            if (Z3_get_symbol_kind(context, symbol) == Z3_INT_SYMBOL)
                return "i(" + std::to_string(Z3_get_symbol_int(context, symbol)) + ")";
            return "s(" + string_quote(symbol_text(context, symbol)) + ")";
        }

        std::string sanitize(std::string_view text, std::string_view prefix) {
            std::string result(prefix);
            bool previous_underscore = !result.empty() && result.back() == '_';
            for (unsigned char character : text) {
                bool accepted = std::isalnum(character) || character == '_';
                if (accepted) {
                    result.push_back(static_cast<char>(character));
                    previous_underscore = character == '_';
                }
                else if (!previous_underscore) {
                    result.push_back('_');
                    previous_underscore = true;
                }
            }
            while (result.size() > prefix.size() && result.back() == '_')
                result.pop_back();
            if (result == prefix)
                result += "anonymous";
            return result;
        }

        std::string declaration_base(z3::func_decl const &declaration, std::string_view symbol) {
            switch (declaration.decl_kind()) {
            case Z3_OP_TRUE: return "true";
            case Z3_OP_FALSE: return "false";
            case Z3_OP_EQ: return "equal";
            case Z3_OP_DISTINCT: return "distinct";
            case Z3_OP_ITE: return "if";
            case Z3_OP_AND: return "and";
            case Z3_OP_OR: return "or";
            case Z3_OP_IFF: return "iff";
            case Z3_OP_XOR: return "xor";
            case Z3_OP_NOT: return "not";
            case Z3_OP_IMPLIES: return "implies";
            case Z3_OP_OEQ: return "observational_equal";
            case Z3_OP_LE: return "less_equal";
            case Z3_OP_GE: return "greater_equal";
            case Z3_OP_LT: return "less";
            case Z3_OP_GT: return "greater";
            case Z3_OP_ADD: return "add";
            case Z3_OP_SUB: return "subtract";
            case Z3_OP_UMINUS: return "negate";
            case Z3_OP_MUL: return "multiply";
            case Z3_OP_DIV: return "divide";
            case Z3_OP_IDIV: return "integer_divide";
            case Z3_OP_REM: return "remainder";
            case Z3_OP_MOD: return "modulo";
            case Z3_OP_TO_REAL: return "to_real";
            case Z3_OP_TO_INT: return "to_int";
            case Z3_OP_IS_INT: return "is_int";
            case Z3_OP_POWER: return "power";
            case Z3_OP_ABS: return "absolute";
            case Z3_OP_STORE: return "store";
            case Z3_OP_SELECT: return "select";
            case Z3_OP_CONST_ARRAY: return "constant_array";
            case Z3_OP_ARRAY_DEFAULT: return "array_default";
            default: return sanitize(symbol, "");
            }
        }

        std::vector<std::string> declaration_parameters(Z3_context context,
                                                        z3::func_decl const &declaration) {
            std::vector<std::string> result;
            unsigned count = Z3_get_decl_num_parameters(context, declaration);
            result.reserve(count);
            for (unsigned i = 0; i < count; ++i) {
                switch (Z3_get_decl_parameter_kind(context, declaration, i)) {
                case Z3_PARAMETER_INT:
                    result.push_back(std::to_string(Z3_get_decl_int_parameter(context, declaration, i)));
                    break;
                case Z3_PARAMETER_DOUBLE: {
                    std::ostringstream value;
                    value << Z3_get_decl_double_parameter(context, declaration, i);
                    result.push_back(value.str());
                    break;
                }
                case Z3_PARAMETER_RATIONAL:
                    result.push_back(Z3_get_decl_rational_parameter(context, declaration, i));
                    break;
                case Z3_PARAMETER_SYMBOL:
                    result.push_back(symbol_text(context,
                                                 Z3_get_decl_symbol_parameter(context, declaration, i)));
                    break;
                case Z3_PARAMETER_SORT:
                    result.push_back(Z3_sort_to_string(
                        context, Z3_get_decl_sort_parameter(context, declaration, i)));
                    break;
                case Z3_PARAMETER_AST:
                    result.push_back(Z3_ast_to_string(
                        context, Z3_get_decl_ast_parameter(context, declaration, i)));
                    break;
                case Z3_PARAMETER_FUNC_DECL:
                    result.push_back(symbol_text(
                        context, Z3_get_decl_name(
                                     context, Z3_get_decl_func_decl_parameter(context, declaration, i))));
                    break;
                case Z3_PARAMETER_INTERNAL: result.push_back("internal"); break;
                case Z3_PARAMETER_ZSTRING: result.push_back("zstring"); break;
                }
            }
            return result;
        }

        struct SortBinding {
            std::string alias;
            z3::sort sort;
        };

        struct DeclarationBinding {
            std::string alias;
            z3::func_decl declaration;
        };

        class Registry {
        public:
            explicit Registry(z3::context &context) : context_(context) {}

            std::string sort_alias(z3::sort const &sort) {
                for (SortBinding const &binding : sorts_) {
                    if (Z3_is_eq_sort(context_, binding.sort, sort))
                        return binding.alias;
                }
                std::string alias = unique(sanitize(sort.to_string(), "_s_"));
                sorts_.push_back({alias, sort});
                return alias;
            }

            std::string declaration_alias(z3::func_decl const &declaration) {
                for (DeclarationBinding const &binding : declarations_) {
                    if (Z3_is_eq_func_decl(context_, binding.declaration, declaration))
                        return binding.alias;
                }
                std::string name = symbol_text(context_, Z3_get_decl_name(context_, declaration));
                std::string base = declaration_base(declaration, name);
                for (std::string const &parameter : declaration_parameters(context_, declaration))
                    base += '_' + sanitize(parameter, "");
                std::string alias = unique(sanitize(base, "_d_"));
                declarations_.push_back({alias, declaration});
                for (unsigned i = 0; i < declaration.arity(); ++i)
                    sort_alias(declaration.domain(i));
                sort_alias(declaration.range());
                return alias;
            }

            z3::sort const &sort(std::string const &alias) const {
                for (SortBinding const &binding : sorts_) {
                    if (binding.alias == alias)
                        return binding.sort;
                }
                throw std::runtime_error("unknown Fine lift sort alias `" + alias + "`");
            }

            z3::func_decl const &declaration(std::string const &alias) const {
                for (DeclarationBinding const &binding : declarations_) {
                    if (binding.alias == alias)
                        return binding.declaration;
                }
                throw std::runtime_error("unknown Fine lift declaration alias `" + alias + "`");
            }

            std::string sorts_json() const {
                std::ostringstream output;
                output << '[';
                for (std::size_t i = 0; i < sorts_.size(); ++i) {
                    if (i)
                        output << ',';
                    SortBinding const &binding = sorts_[i];
                    output << "{\"name\":" << json_quote(binding.alias)
                           << ",\"z3_text\":" << json_quote(binding.sort.to_string())
                           << ",\"sort_kind\":" << static_cast<unsigned>(binding.sort.sort_kind())
                           << ",\"ast_id_at_observation\":" << Z3_get_ast_id(context_, binding.sort) << '}';
                }
                output << ']';
                return output.str();
            }

            std::string declarations_json() const {
                std::ostringstream output;
                output << '[';
                for (std::size_t i = 0; i < declarations_.size(); ++i) {
                    if (i)
                        output << ',';
                    DeclarationBinding const &binding = declarations_[i];
                    z3::func_decl const &declaration = binding.declaration;
                    std::vector<std::string> parameters = declaration_parameters(context_, declaration);
                    output << "{\"name\":" << json_quote(binding.alias)
                           << ",\"z3_symbol\":"
                           << json_quote(symbol_text(context_, Z3_get_decl_name(context_, declaration)))
                           << ",\"z3_declaration_text\":" << json_quote(declaration.to_string())
                           << ",\"decl_kind\":" << static_cast<unsigned>(declaration.decl_kind())
                           << ",\"parameters\":[";
                    for (std::size_t j = 0; j < parameters.size(); ++j) {
                        if (j)
                            output << ',';
                        output << json_quote(parameters[j]);
                    }
                    output << ']'
                           << ",\"ast_id_at_observation\":" << Z3_get_ast_id(context_, declaration)
                           << ",\"domain\":[";
                    for (unsigned j = 0; j < declaration.arity(); ++j) {
                        if (j)
                            output << ',';
                        output << json_quote(find_sort_alias(declaration.domain(j)));
                    }
                    output << "],\"range\":" << json_quote(find_sort_alias(declaration.range())) << '}';
                }
                output << ']';
                return output.str();
            }

        private:
            z3::context &context_;
            std::vector<SortBinding> sorts_;
            std::vector<DeclarationBinding> declarations_;
            std::set<std::string> aliases_;

            std::string unique(std::string base) {
                std::string candidate = base;
                for (unsigned suffix = 1; !aliases_.insert(candidate).second; ++suffix)
                    candidate = base + '_' + std::to_string(suffix);
                return candidate;
            }

            std::string find_sort_alias(z3::sort const &sort) const {
                for (SortBinding const &binding : sorts_) {
                    if (Z3_is_eq_sort(context_, binding.sort, sort))
                        return binding.alias;
                }
                throw std::runtime_error("Fine lift declaration references an unregistered sort");
            }
        };

        struct BoundBinding {
            std::string alias;
            z3::sort sort;
        };

        class Printer {
        public:
            Printer(z3::context &context, Registry &registry) : context_(context), registry_(registry) {}

            std::string print(z3::expr const &expression) {
                return expression_text(expression);
            }

        private:
            z3::context &context_;
            Registry &registry_;
            std::vector<BoundBinding> bound_;
            std::size_t next_bound_ = 0;

            std::string expression_text(z3::expr const &expression) {
                Z3_ast ast = expression;
                switch (Z3_get_ast_kind(context_, ast)) {
                case Z3_NUMERAL_AST:
                    return "numeral(" + string_quote(Z3_get_numeral_string(context_, ast)) + "," +
                           registry_.sort_alias(expression.get_sort()) + ')';
                case Z3_APP_AST: return application_text(expression);
                case Z3_VAR_AST: return bound_text(ast);
                case Z3_QUANTIFIER_AST: return quantifier_text(ast);
                default: throw std::runtime_error("Fine lift encountered a non-expression Z3 AST kind");
                }
            }

            std::string application_text(z3::expr const &expression) {
                std::string alias = registry_.declaration_alias(expression.decl());
                if (expression.num_args() == 0)
                    return alias;
                std::ostringstream output;
                output << alias << '(';
                for (unsigned i = 0; i < expression.num_args(); ++i) {
                    if (i)
                        output << ',';
                    output << expression_text(expression.arg(i));
                }
                output << ')';
                return output.str();
            }

            std::string bound_text(Z3_ast ast) const {
                unsigned index = Z3_get_index_value(context_, ast);
                if (index >= bound_.size())
                    throw std::runtime_error("Fine lift encountered a free de Bruijn variable");
                return bound_[bound_.size() - 1 - index].alias;
            }

            std::string pattern_text(Z3_pattern pattern) {
                std::ostringstream output;
                output << '[';
                unsigned count = Z3_get_pattern_num_terms(context_, pattern);
                for (unsigned i = 0; i < count; ++i) {
                    if (i)
                        output << ',';
                    output << expression_text(z3::expr(context_, Z3_get_pattern(context_, pattern, i)));
                }
                output << ']';
                return output.str();
            }

            std::string quantifier_text(Z3_ast ast) {
                bool lambda = Z3_is_lambda(context_, ast);
                char const *kind = lambda ? "lambda" : Z3_is_quantifier_forall(context_, ast) ? "forall" : "exists";
                unsigned count = Z3_get_quantifier_num_bound(context_, ast);
                std::size_t old_size = bound_.size();
                std::ostringstream binders;
                binders << '(';
                for (unsigned i = 0; i < count; ++i) {
                    if (i)
                        binders << ',';
                    z3::sort sort(context_, Z3_get_quantifier_bound_sort(context_, ast, i));
                    std::string alias = "_v_" + std::to_string(next_bound_++);
                    bound_.push_back({alias, sort});
                    binders << alias << '@'
                            << symbol_syntax(context_, Z3_get_quantifier_bound_name(context_, ast, i)) << ':'
                            << registry_.sort_alias(sort);
                }
                binders << ')';

                std::ostringstream output;
                output << kind << "[weight=" << Z3_get_quantifier_weight(context_, ast)
                       << ",qid=" << symbol_syntax(context_, Z3_get_quantifier_id(context_, ast))
                       << ",skid=" << symbol_syntax(context_, Z3_get_quantifier_skolem_id(context_, ast)) << ']'
                       << binders.str() << '{'
                       << expression_text(z3::expr(context_, Z3_get_quantifier_body(context_, ast))) << '}'
                       << "[patterns=[";
                unsigned patterns = Z3_get_quantifier_num_patterns(context_, ast);
                for (unsigned i = 0; i < patterns; ++i) {
                    if (i)
                        output << ',';
                    output << pattern_text(Z3_get_quantifier_pattern_ast(context_, ast, i));
                }
                output << "],nopatterns=[";
                unsigned no_patterns = Z3_get_quantifier_num_no_patterns(context_, ast);
                for (unsigned i = 0; i < no_patterns; ++i) {
                    if (i)
                        output << ',';
                    output << expression_text(
                        z3::expr(context_, Z3_get_quantifier_no_pattern_ast(context_, ast, i)));
                }
                output << "]]";
                bound_.erase(bound_.begin() + static_cast<std::ptrdiff_t>(old_size), bound_.end());
                return output.str();
            }
        };

        enum class TokenKind { identifier, number, string, punctuation, end };

        struct Token {
            TokenKind kind;
            std::string text;
            char punctuation = 0;
        };

        class Lexer {
        public:
            explicit Lexer(std::string_view source) : source_(source) {}

            Token next() {
                while (offset_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[offset_])))
                    ++offset_;
                if (offset_ == source_.size())
                    return {TokenKind::end, {}};
                char character = source_[offset_];
                if (std::isalpha(static_cast<unsigned char>(character)) || character == '_') {
                    std::size_t begin = offset_++;
                    while (offset_ < source_.size()) {
                        char next = source_[offset_];
                        if (!std::isalnum(static_cast<unsigned char>(next)) && next != '_')
                            break;
                        ++offset_;
                    }
                    return {TokenKind::identifier, std::string(source_.substr(begin, offset_ - begin))};
                }
                if (std::isdigit(static_cast<unsigned char>(character))) {
                    std::size_t begin = offset_++;
                    while (offset_ < source_.size() &&
                           std::isdigit(static_cast<unsigned char>(source_[offset_])))
                        ++offset_;
                    return {TokenKind::number, std::string(source_.substr(begin, offset_ - begin))};
                }
                if (character == '"') {
                    ++offset_;
                    std::string result;
                    while (offset_ < source_.size() && source_[offset_] != '"') {
                        char next = source_[offset_++];
                        if (next != '\\') {
                            result.push_back(next);
                            continue;
                        }
                        if (offset_ == source_.size())
                            fail("unfinished string escape");
                        char escaped = source_[offset_++];
                        switch (escaped) {
                        case '"': result.push_back('"'); break;
                        case '\\': result.push_back('\\'); break;
                        case 'b': result.push_back('\b'); break;
                        case 'f': result.push_back('\f'); break;
                        case 'n': result.push_back('\n'); break;
                        case 'r': result.push_back('\r'); break;
                        case 't': result.push_back('\t'); break;
                        case 'u': {
                            unsigned value = 0;
                            for (unsigned i = 0; i < 4; ++i) {
                                if (offset_ == source_.size())
                                    fail("unfinished Unicode escape");
                                char digit = source_[offset_++];
                                value <<= 4;
                                if (digit >= '0' && digit <= '9')
                                    value += static_cast<unsigned>(digit - '0');
                                else if (digit >= 'a' && digit <= 'f')
                                    value += static_cast<unsigned>(digit - 'a' + 10);
                                else if (digit >= 'A' && digit <= 'F')
                                    value += static_cast<unsigned>(digit - 'A' + 10);
                                else
                                    fail("invalid Unicode escape");
                            }
                            if (value > 0xff)
                                fail("generated Fine strings admit byte escapes only");
                            result.push_back(static_cast<char>(value));
                            break;
                        }
                        default: fail("unsupported string escape");
                        }
                    }
                    if (offset_ == source_.size())
                        fail("unfinished string");
                    ++offset_;
                    return {TokenKind::string, std::move(result)};
                }
                static std::string_view const punctuation = "(){}[],:=@";
                if (punctuation.find(character) == std::string_view::npos)
                    fail(std::string("unexpected character `") + character + "`");
                ++offset_;
                return {TokenKind::punctuation, {}, character};
            }

        private:
            std::string_view source_;
            std::size_t offset_ = 0;

            [[noreturn]] static void fail(std::string message) {
                throw std::runtime_error("Fine lift parse error: " + std::move(message));
            }
        };

        struct ParsedBinder {
            std::string alias;
            z3::sort sort;
        };

        class Parser {
        public:
            Parser(z3::context &context, Registry const &registry, std::string_view source)
                : context_(context), registry_(registry), lexer_(source), current_(lexer_.next()) {}

            z3::expr parse() {
                z3::expr result = expression();
                if (current_.kind != TokenKind::end)
                    fail("trailing input");
                return result;
            }

        private:
            z3::context &context_;
            Registry const &registry_;
            Lexer lexer_;
            Token current_;
            std::vector<ParsedBinder> bound_;

            [[noreturn]] static void fail(std::string message) {
                throw std::runtime_error("Fine lift parse error: " + std::move(message));
            }

            void advance() {
                current_ = lexer_.next();
            }

            void punctuation(char expected) {
                if (current_.kind != TokenKind::punctuation || current_.punctuation != expected)
                    fail(std::string("expected `") + expected + "`");
                advance();
            }

            std::string identifier() {
                if (current_.kind != TokenKind::identifier)
                    fail("expected identifier");
                std::string result = std::move(current_.text);
                advance();
                return result;
            }

            unsigned number() {
                if (current_.kind != TokenKind::number)
                    fail("expected unsigned number");
                unsigned result = static_cast<unsigned>(std::stoul(current_.text));
                advance();
                return result;
            }

            std::string string() {
                if (current_.kind != TokenKind::string)
                    fail("expected string");
                std::string result = std::move(current_.text);
                advance();
                return result;
            }

            void keyword(std::string_view expected) {
                if (identifier() != expected)
                    fail("expected `" + std::string(expected) + "`");
            }

            Z3_symbol symbol() {
                std::string kind = identifier();
                if (kind == "n")
                    return nullptr;
                punctuation('(');
                Z3_symbol result;
                if (kind == "s")
                    result = context_.str_symbol(string().c_str());
                else if (kind == "i")
                    result = context_.int_symbol(number());
                else
                    fail("unknown symbol encoding");
                punctuation(')');
                return result;
            }

            z3::expr expression() {
                if (current_.kind != TokenKind::identifier)
                    fail("expected expression");
                std::string name = identifier();
                if (name == "numeral")
                    return numeral();
                if (name == "forall" || name == "exists" || name == "lambda")
                    return quantifier(name);
                for (std::size_t i = 0; i < bound_.size(); ++i) {
                    if (bound_[i].alias == name) {
                        unsigned index = static_cast<unsigned>(bound_.size() - 1 - i);
                        return z3::expr(context_, Z3_mk_bound(context_, index, bound_[i].sort));
                    }
                }
                z3::func_decl const &declaration = registry_.declaration(name);
                std::vector<z3::expr> arguments;
                if (current_.kind == TokenKind::punctuation && current_.punctuation == '(') {
                    punctuation('(');
                    while (current_.kind != TokenKind::punctuation || current_.punctuation != ')') {
                        arguments.push_back(expression());
                        if (current_.kind == TokenKind::punctuation && current_.punctuation == ',')
                            punctuation(',');
                        else if (current_.kind != TokenKind::punctuation || current_.punctuation != ')')
                            fail("expected `,` or `)` in application");
                    }
                    punctuation(')');
                }
                std::vector<Z3_ast> raw;
                raw.reserve(arguments.size());
                for (z3::expr const &argument : arguments)
                    raw.push_back(argument);
                Z3_ast application = Z3_mk_app(context_, declaration, static_cast<unsigned>(raw.size()),
                                               raw.empty() ? nullptr : raw.data());
                context_.check_error();
                return z3::expr(context_, application);
            }

            z3::expr numeral() {
                punctuation('(');
                std::string value = string();
                punctuation(',');
                std::string sort_name = identifier();
                punctuation(')');
                return z3::expr(context_, Z3_mk_numeral(context_, value.c_str(), registry_.sort(sort_name)));
            }

            z3::expr quantifier(std::string const &kind) {
                punctuation('[');
                keyword("weight");
                punctuation('=');
                unsigned weight = number();
                punctuation(',');
                keyword("qid");
                punctuation('=');
                Z3_symbol qid = symbol();
                punctuation(',');
                keyword("skid");
                punctuation('=');
                Z3_symbol skid = symbol();
                punctuation(']');
                punctuation('(');
                std::size_t old_size = bound_.size();
                std::vector<z3::sort> sorts;
                std::vector<Z3_symbol> names;
                while (current_.kind != TokenKind::punctuation || current_.punctuation != ')') {
                    std::string alias = identifier();
                    punctuation('@');
                    Z3_symbol original = symbol();
                    punctuation(':');
                    z3::sort sort = registry_.sort(identifier());
                    bound_.push_back({alias, sort});
                    sorts.push_back(sort);
                    names.push_back(original);
                    if (current_.kind == TokenKind::punctuation && current_.punctuation == ',')
                        punctuation(',');
                    else if (current_.kind != TokenKind::punctuation || current_.punctuation != ')')
                        fail("expected `,` or `)` in binder list");
                }
                punctuation(')');
                punctuation('{');
                z3::expr body = expression();
                punctuation('}');
                punctuation('[');
                keyword("patterns");
                punctuation('=');
                punctuation('[');
                std::vector<Z3_pattern> patterns;
                while (current_.kind != TokenKind::punctuation || current_.punctuation != ']') {
                    punctuation('[');
                    std::vector<Z3_ast> terms;
                    while (current_.kind != TokenKind::punctuation || current_.punctuation != ']') {
                        terms.push_back(expression());
                        if (current_.kind == TokenKind::punctuation && current_.punctuation == ',')
                            punctuation(',');
                        else if (current_.kind != TokenKind::punctuation || current_.punctuation != ']')
                            fail("expected `,` or `]` in pattern");
                    }
                    punctuation(']');
                    patterns.push_back(Z3_mk_pattern(context_, static_cast<unsigned>(terms.size()), terms.data()));
                    if (current_.kind == TokenKind::punctuation && current_.punctuation == ',')
                        punctuation(',');
                    else if (current_.kind != TokenKind::punctuation || current_.punctuation != ']')
                        fail("expected `,` or `]` after pattern");
                }
                punctuation(']');
                punctuation(',');
                keyword("nopatterns");
                punctuation('=');
                punctuation('[');
                std::vector<Z3_ast> no_patterns;
                while (current_.kind != TokenKind::punctuation || current_.punctuation != ']') {
                    no_patterns.push_back(expression());
                    if (current_.kind == TokenKind::punctuation && current_.punctuation == ',')
                        punctuation(',');
                    else if (current_.kind != TokenKind::punctuation || current_.punctuation != ']')
                        fail("expected `,` or `]` in no-pattern list");
                }
                punctuation(']');
                punctuation(']');
                bound_.erase(bound_.begin() + static_cast<std::ptrdiff_t>(old_size), bound_.end());

                std::vector<Z3_sort> raw_sorts;
                raw_sorts.reserve(sorts.size());
                for (z3::sort const &sort : sorts)
                    raw_sorts.push_back(sort);
                Z3_ast result;
                if (kind == "lambda") {
                    if (!patterns.empty() || !no_patterns.empty() || weight != 0)
                        fail("lambda carries unsupported quantifier metadata");
                    result = Z3_mk_lambda(context_, static_cast<unsigned>(raw_sorts.size()), raw_sorts.data(),
                                          names.data(), body);
                }
                else {
                    result = Z3_mk_quantifier_ex(
                        context_, kind == "forall", weight, qid, skid, static_cast<unsigned>(patterns.size()),
                        patterns.empty() ? nullptr : patterns.data(), static_cast<unsigned>(no_patterns.size()),
                        no_patterns.empty() ? nullptr : no_patterns.data(), static_cast<unsigned>(raw_sorts.size()),
                        raw_sorts.data(), names.data(), body);
                }
                context_.check_error();
                return z3::expr(context_, result);
            }
        };

    }  // namespace

    RainfallLiftedTerm lift_rainfall_term(z3::context &context, z3::expr const &expression, bool exact_reify) {
        Registry registry(context);
        Printer printer(context, registry);
        std::string text = printer.print(expression);
        if (exact_reify) {
            Parser parser(context, registry, text);
            z3::expr reparsed = parser.parse();
            if (!Z3_is_eq_ast(context, expression, reparsed))
                throw std::runtime_error("Fine Rainfall lift/reify changed exact Z3 AST identity\nsource: " +
                                         expression.to_string() + "\nreified: " + reparsed.to_string() +
                                         "\nfine: " + text);
        }
        return {std::move(text), registry.sorts_json(), registry.declarations_json()};
    }

}  // namespace fine
