#pragma once

#include "parser.h"

#include <vector>

namespace fine::elaboration {

    std::vector<syntax::FunctionDecl const *>
    value_definition_order(std::vector<syntax::FunctionDecl> const &functions);

}  // namespace fine::elaboration
