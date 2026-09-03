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

    struct ConcreteRange {
        std::size_t begin = 0;
        std::size_t end = 0;

        static ConcreteRange from_span(SourceSpan span) {
            return {span.begin.offset, span.end.offset};
        }

        static ConcreteRange empty_at(std::size_t offset) {
            return {offset, offset};
        }
    };

    enum class ConcreteTokenKind { whitespace, line_comment, identifier, integer, symbol };

    struct ConcreteToken {
        ConcreteTokenKind kind = ConcreteTokenKind::whitespace;
        std::string text;
        SourceSpan span;
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

    // The syntax itself is two-level. ValueType and ProofType are not variants
    // of one common source type, and the runtime has no reason to add a proof
    // case when elaborating either structure.
    struct ValueType {
        enum class Kind { integer, boolean, enumeration };

        Kind kind = Kind::integer;
        std::string name;
        SourceSpan span;
    };

    struct ExplicitProofArgument {
        SourceSpan span;
        std::string coeffect;
        std::string proof;
    };

    struct ValueExpr {
        enum class Kind { name, integer, boolean, call, equal, match };

        Kind kind = Kind::name;
        SourceSpan span;
        std::size_t node_id = 0;
        std::string name;
        std::string integer_text;
        bool boolean_value = false;
        std::vector<ValueExpr> elements;
        std::vector<ExplicitProofArgument> using_proofs;
        // A match keeps its scrutinee in elements[0] and one body per arm in
        // elements[1..]. Constructor names and binders remain source syntax;
        // the elaborator owns their Z3 recognizer/accessor identities.
        std::vector<std::string> match_constructors;
        std::vector<std::vector<std::string>> match_binders;
        std::vector<SourceSpan> match_arm_spans;
        // End of the call's closing parenthesis. An implicit coeffect
        // materialization inserts ` using [...]` at this exact byte offset.
        std::size_t call_argument_end = 0;
    };

    struct ProofType {
        enum class Kind { identity, inductive };

        Kind kind = Kind::identity;
        SourceSpan span;
        std::size_t node_id = 0;
        std::string name;
        std::vector<ValueExpr> arguments;
        ValueType carrier;
        ValueExpr left;
        ValueExpr right;
    };

    struct ProofExpr {
        enum class Kind { name, reflexivity, application, match, hole };

        Kind kind = Kind::name;
        SourceSpan span;
        std::size_t node_id = 0;
        std::string name;
        ValueExpr value;
        // Proof constructors keep their static indices in brackets and their
        // structural children in parentheses. Proof functions instead use
        // ordinary value arguments in parentheses and named virtual evidence
        // through `using`, exactly as value-level coeffects do.
        bool constructor_style = false;
        std::vector<ValueExpr> value_arguments;
        std::vector<ProofExpr> proof_arguments;
        std::vector<std::string> using_coeffects;
        std::vector<ProofExpr> using_proofs;
        std::vector<SourceSpan> using_spans;
        std::size_t call_argument_end = 0;
        // Proof-family patterns preserve static value binders and proof-field
        // binders separately. The scrutinee is intentionally a local proof
        // name in this first eliminator slice.
        std::string matched_proof;
        std::vector<std::string> match_constructors;
        std::vector<std::vector<std::string>> match_value_binders;
        std::vector<std::vector<std::string>> match_proof_binders;
        std::vector<SourceSpan> match_arm_spans;
        std::vector<ProofExpr> match_bodies;
    };

    struct ValueParameter {
        SourceSpan span;
        std::string name;
        ValueType type;
    };

    struct CoeffectParameter {
        SourceSpan span;
        std::string name;
        ProofType type;
    };

    struct FunctionDecl {
        SourceSpan span;
        std::size_t node_id = 0;
        std::string name;
        std::vector<ValueParameter> parameters;
        ValueType result_type;
        std::vector<CoeffectParameter> coeffects;
        std::vector<ValueExpr> ensures;
        ValueExpr body;
    };

    // Value parameters are static indices for the proof signature. A call uses
    // ordinary value arguments and obtains virtual proof parameters through
    // the same named `using`/lexical-search coeffect boundary as a value call.
    struct ProofFunctionDecl {
        SourceSpan span;
        std::size_t node_id = 0;
        std::string name;
        std::vector<ValueParameter> parameters;
        std::vector<CoeffectParameter> proof_parameters;
        std::optional<std::string> induction_parameter;
        ProofType result_type;
        bool has_body = false;
        ProofExpr body;
    };

    struct ProofConstructorDecl {
        SourceSpan span;
        std::size_t node_id = 0;
        std::string name;
        std::vector<ValueParameter> parameters;
        std::vector<CoeffectParameter> proof_parameters;
        ProofType result_type;
    };

    struct ProofInductiveDecl {
        SourceSpan span;
        std::size_t node_id = 0;
        std::string name;
        std::vector<ValueParameter> indices;
        std::vector<ProofConstructorDecl> constructors;
    };

    struct EnumConstructorDecl {
        SourceSpan span;
        std::string name;
        std::vector<ValueType> fields;
    };

    struct EnumDecl {
        SourceSpan span;
        std::size_t node_id = 0;
        std::string name;
        std::vector<EnumConstructorDecl> constructors;
    };

    struct LetDecl {
        SourceSpan span;
        std::size_t node_id = 0;
        std::string name;
        ValueType type;
        ValueExpr value;
    };

    struct ProofDecl {
        SourceSpan span;
        std::size_t node_id = 0;
        std::string name;
        ProofType type;
        ProofExpr value;
    };

    struct AssertDecl {
        SourceSpan span;
        std::size_t node_id = 0;
        ValueExpr proposition;
    };

    using RunStatement = std::variant<LetDecl, ProofDecl, AssertDecl>;

    struct RunDecl {
        SourceSpan span;
        std::size_t node_id = 0;
        std::string name;
        std::vector<RunStatement> statements;
    };

    struct Document {
        std::vector<EnumDecl> enums;
        std::vector<ProofInductiveDecl> proof_inductives;
        std::vector<FunctionDecl> functions;
        std::vector<ProofFunctionDecl> proof_functions;
        std::optional<RunDecl> run;
    };

    enum class ConcreteNodeKind {
        document,
        enum_declaration,
        proof_inductive_declaration,
        function_declaration,
        proof_function_declaration,
        run_declaration,
    };

    // The concrete tree owns every source byte. Its declaration nodes provide
    // stable edit ranges while the existing AST remains the semantic view.
    // Trivia between declarations belongs to the document root; trivia inside
    // a declaration lies within that declaration's concrete range.
    struct ConcreteNode {
        ConcreteNodeKind kind = ConcreteNodeKind::document;
        ConcreteRange range;
        std::size_t first_token = 0;
        std::size_t end_token = 0;
        std::vector<ConcreteNode> children;
    };

    struct ConcreteSyntaxTree {
        std::vector<ConcreteToken> tokens;
        ConcreteNode root;
        Document ast;

        std::string render() const;
        ConcreteRange range(SourceSpan span) const {
            return ConcreteRange::from_span(span);
        }
    };

    ConcreteSyntaxTree parse_tree(std::string_view source);
    Document parse(std::string_view source);

}  // namespace fine::syntax
