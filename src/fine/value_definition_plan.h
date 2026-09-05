#pragma once

#include "parser.h"

#include <vector>

namespace fine::elaboration {

    using ValueDefinitionGroup = std::vector<syntax::FunctionDecl const *>;

    // Body dependencies form definition groups.  A singleton may recurse into
    // itself; a larger group is checked as one mutually recursive unit before
    // any of its native definitions are installed.
    std::vector<ValueDefinitionGroup> value_definition_groups(std::vector<syntax::FunctionDecl> const &functions);

}  // namespace fine::elaboration
