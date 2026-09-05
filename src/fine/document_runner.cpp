#include "elaboration_internal.h"

// Document execution: declaration ordering, run scopes, local bindings, and
// assertions. Value and proof semantics belong to their respective owners.
namespace fine::elaboration {

    DocumentRunner::DocumentRunner(std::ostream &output, std::ostream *rainfall_output, SourceSnapshot const *snapshot,
                                   std::string rainfall_run, ExecutionOptions options)
        : output_(output), options_(options), values_(output_, options_, *this),
          proofs_(values_, output_, options_, *this) {
        if (rainfall_output)
            rainfall_.emplace(values_.context(), *rainfall_output, std::move(rainfall_run), snapshot);
        RainfallRecorder *rainfall = rainfall_ ? &*rainfall_ : nullptr;
        values_.set_rainfall(rainfall);
        proofs_.set_rainfall(rainfall);
        values_.connect_proofs(proofs_);
    }

    void DocumentRunner::request_materialization(syntax::ConcreteRange range, std::string text,
                                                 syntax::SourceSpan span) {
        auto [found, inserted] = materializations_.emplace(std::pair{range.begin, range.end}, text);
        if (!inserted && found->second != text)
            reject(span, "two materializations disagree at one source range");
    }
    std::vector<Materialization> DocumentRunner::materializations_so_far() const {
        std::vector<Materialization> result;
        result.reserve(materializations_.size());
        for (auto const &[range, text] : materializations_)
            result.push_back({{range.first, range.second}, text});
        return result;
    }
    ExecutionResult DocumentRunner::execute(syntax::Document const &document) {
        for (auto const &enumeration : document.enums)
            values_.declare_enum(enumeration);
        values_.record_boundary();
        for (auto const &family : document.proof_inductives)
            proofs_.declare_proof_inductive(family);
        for (auto const &function : document.proof_functions)
            proofs_.declare_proof_function(function);
        for (auto const &function : document.functions)
            values_.declare_function(function);
        if (document.run)
            execute_run(*document.run);
        result_.functions_verified = values_.functions_verified();
        result_.proof_functions_verified = proofs_.functions_verified();
        result_.proof_holes_filled = proofs_.holes_filled();
        result_.proof_holes_checkpointed = proofs_.holes_checkpointed();
        result_.coeffects_resolved = values_.coeffects_resolved() + proofs_.coeffects_resolved();
        for (auto const &[range, text] : materializations_)
            result_.materializations.push_back({{range.first, range.second}, text});
        if (rainfall_) {
            rainfall_->validate_terms();
            std::vector<std::string> dependencies;
            if (document.run)
                dependencies.push_back("run:" + document.run->name);
            rainfall_->record(
                "transition", document.run ? "proof-core.run.close" : "proof-core.document.close",
                std::move(dependencies), "fine.two-level-elaborator",
                result_.checkpoint_open
                    ? "A typed partial proof was checked through its fixed subtree; unresolved leaves remain holes"
                : document.run ? "All value terms checked; proof evidence remained virtual and every coeffect resolved"
                               : "All declarations checked without manufacturing an empty executable run",
                {RainfallRecorder::string_field("status", result_.checkpoint_open ? "checkpointed" : "verified"),
                 RainfallRecorder::number_field("functions_verified", result_.functions_verified),
                 RainfallRecorder::number_field("proof_functions_verified", result_.proof_functions_verified),
                 RainfallRecorder::number_field("proofs_formed", result_.proofs_formed),
                 RainfallRecorder::number_field("proof_holes_filled", result_.proof_holes_filled),
                 RainfallRecorder::number_field("proof_holes_checkpointed", result_.proof_holes_checkpointed),
                 RainfallRecorder::number_field("coeffects_resolved", result_.coeffects_resolved),
                 RainfallRecorder::number_field("runtime_proof_values", 0)});
        }
        if (document.run)
            output_ << (result_.checkpoint_open ? "checkpointed run: " : "verified run: ") << document.run->name
                    << '\n';
        else
            output_ << (result_.checkpoint_open ? "checkpointed definitions\n" : "verified definitions\n");
        output_ << "runtime-value-kinds: Int, Bool";
        for (auto const &name : values_.runtime_kind_names())
            output_ << ", " << name;
        output_ << '\n' << "runtime-proof-values: 0 (unrepresentable)\n";
        return result_;
    }
    void DocumentRunner::execute_run(syntax::RunDecl const &run) {
        ValueEnvironment values;
        ProofEnvironment proofs;
        std::vector<std::string> proof_order;
        std::vector<z3::expr> absorbed;
        std::size_t assertion_index = 0;
        for (auto const &statement : run.statements) {
            std::visit(
                [&](auto const &item) {
                    execute_statement(item, run.name, assertion_index, values, proofs, proof_order, absorbed);
                },
                statement);
            if (result_.checkpoint_open)
                break;
        }
    }
    void DocumentRunner::execute_statement(syntax::LetDecl const &declaration, std::string const &run, std::size_t &,
                                           ValueEnvironment &values, ProofEnvironment &proofs,
                                           std::vector<std::string> &proof_order, std::vector<z3::expr> &absorbed) {
        ensure_fresh(declaration.name, declaration.span, values, proofs);
        values_.require_known_type(declaration.type);
        ValueKind expected = kind_of(declaration.type);
        ValueTerm value = values_.elaborate_value(declaration.value, values, proofs, proof_order, absorbed, expected);
        if (value.kind != expected)
            reject(declaration.span, "value binding `" + declaration.name + "` has the wrong type");
        if (rainfall_)
            rainfall_->source_term(declaration.value.node_id, declaration.value.span, "value.expression",
                                   value.expression, "exact", {"run:" + run});
        values.emplace(declaration.name, std::move(value));
    }
    void DocumentRunner::execute_statement(syntax::ProofDecl const &declaration, std::string const &run, std::size_t &,
                                           ValueEnvironment &values, ProofEnvironment &proofs,
                                           std::vector<std::string> &proof_order, std::vector<z3::expr> &absorbed) {
        ensure_fresh(declaration.name, declaration.span, values, proofs);
        SemanticProofType expected =
            proofs_.elaborate_proof_type(declaration.type, values, proofs, proof_order, absorbed);
        std::string proof_source;
        std::string type_source;
        if (rainfall_) {
            proof_source = rainfall_->source_node(
                declaration.value.node_id, declaration.value.span,
                declaration.value.kind == syntax::ProofExpr::Kind::hole ? "proof.expression.hole" : "proof.expression");
            type_source = rainfall_->source_node(declaration.type.node_id, declaration.type.span,
                                                 declaration.type.kind == syntax::ProofType::Kind::identity
                                                     ? "proof-type.identity"
                                                     : "proof-type.inductive");
        }
        ProofEvidence evidence =
            proofs_.elaborate_any_proof(declaration.value, declaration.type, std::move(expected), values, proofs,
                                        proof_order, absorbed, declaration.name, run, proof_source, type_source);
        if (!evidence.complete) {
            if (!options_.synthesize_partial_proofs && !options_.validate_partial_proofs)
                throw std::logic_error("open proof escaped checkpoint mode");
            result_.checkpoint_open = true;
            output_ << "retained typed proof holes: " << declaration.name << " : " << print_proof_type(declaration.type)
                    << '\n';
            return;
        }
        if (rainfall_) {
            if (auto identity = std::get_if<IdentityType>(&evidence.type)) {
                std::string proposition = rainfall_->term(identity->left == identity->right, "identity-proposition");
                rainfall_->record("object", "proof.identity.form", {"run:" + run}, "fine.proof-elaborator",
                                  "Exact source proof evidence formed at the static level; no runtime term exists",
                                  {RainfallRecorder::string_field("proof", declaration.name),
                                   RainfallRecorder::string_field("formation", evidence.formation),
                                   RainfallRecorder::string_field("proof_source", proof_source),
                                   RainfallRecorder::string_field("type_source", type_source),
                                   RainfallRecorder::string_field("proof_type", print_identity(declaration.type)),
                                   RainfallRecorder::string_field("proposition", proposition),
                                   RainfallRecorder::boolean_field("runtime_value_created", false)});
            }
            else {
                rainfall_->record("object", "proof.inductive.form", {"run:" + run}, "fine.proof-elaborator",
                                  "Indexed constructor evidence formed only at the static proof level",
                                  {RainfallRecorder::string_field("proof", declaration.name),
                                   RainfallRecorder::string_field("formation", evidence.formation),
                                   RainfallRecorder::string_field("proof_source", proof_source),
                                   RainfallRecorder::string_field("type_source", type_source),
                                   RainfallRecorder::string_field("proof_type", print_proof_type(declaration.type)),
                                   RainfallRecorder::boolean_field("runtime_value_created", false)});
            }
        }
        auto [inserted, ok] = proofs.emplace(declaration.name, std::move(evidence));
        proof_order.push_back(declaration.name);
        proofs_.absorb(inserted->second, absorbed, {"run:" + run}, "local-proof",
                       proof_source.empty() ? std::nullopt : std::optional(proof_source));
        ++result_.proofs_formed;
        output_ << "formed proof: " << declaration.name << " : " << print_proof_type(declaration.type)
                << " (virtual)\n";
    }
    void DocumentRunner::execute_statement(syntax::AssertDecl const &declaration, std::string const &run,
                                           std::size_t &assertion_index, ValueEnvironment &values,
                                           ProofEnvironment &proofs, std::vector<std::string> &proof_order,
                                           std::vector<z3::expr> &absorbed) {
        ValueTerm proposition =
            values_.elaborate_value(declaration.proposition, values, proofs, proof_order, absorbed, boolean_kind());
        if (proposition.kind != boolean_kind())
            reject(declaration.span, "assertion is not Bool");
        z3::solver solver(values_.context());
        for (auto const &assumption : absorbed)
            solver.add(assumption);
        solver.add(!proposition.expression);
        if (solver.check() != z3::unsat)
            reject(declaration.span, "assertion may be false");
        output_ << "verified assertion: " << run << '.' << assertion_index++ << '\n';
        if (rainfall_) {
            rainfall_->source_term(declaration.proposition.node_id, declaration.proposition.span, "value.assertion",
                                   proposition.expression, "exact", {"run:" + run});
            rainfall_->record("transition", "assert.verify", {"run:" + run}, "fine.two-level-elaborator",
                              "Assertion refuted under all previously absorbed proof constraints",
                              {RainfallRecorder::string_field("status", "unsat")});
        }
    }

}  // namespace fine::elaboration
