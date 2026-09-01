#pragma once

#include "c++/z3++.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fine {

class RainfallRecorder;

struct SynthesisSelection {
    std::size_t grammar_size;
    z3::expr term;
    z3::expr ground_instance;
    z3::expr label;
};

struct SynthesisResult {
    z3::expr assembled;
    z3::expr witness;
    std::vector<SynthesisSelection> selections;
    std::vector<std::size_t> core_indices;

    SynthesisResult(z3::expr assembled, z3::expr witness)
        : assembled(std::move(assembled)), witness(std::move(witness)) {}
};

// A fair, size-stratified refutation synthesizer for the first
// single-invocation QF-LIA slice. Candidate terms use Fine's fixed built-in
// integer grammar. It is a semi-decision procedure: no hidden size ceiling is
// imposed, so an unrealizable specification need not terminate.
class RefutationSynthesizer {
public:
    RefutationSynthesizer(z3::context& context, std::string declaration_name,
                          std::vector<z3::expr> parameters,
                          z3::expr result_placeholder, z3::expr specification,
                          RainfallRecorder* rainfall = nullptr,
                          std::vector<z3::expr> grammar_inputs = {},
                          bool arm_scope = false);

    SynthesisResult run();

private:
    z3::context& context_;
    std::string declaration_name_;
    std::vector<z3::expr> parameters_;
    z3::expr result_placeholder_;
    z3::expr specification_;
    RainfallRecorder* rainfall_;
    std::vector<z3::expr> grammar_inputs_;
    bool arm_scope_ = false;
};

} // namespace fine
