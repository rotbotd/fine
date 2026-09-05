#include "value_recursion.h"

#include "elaboration_internal.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace fine::elaboration {

    namespace {

        struct Graph {
            std::size_t from;
            std::size_t to;
            std::size_t rows;
            std::size_t columns;
            std::vector<SizeRelation> edges;

            auto key() const {
                return std::tuple{from, to, rows, columns, edges};
            }
        };

        SizeRelation compose_relation(SizeRelation left, SizeRelation right) {
            if (left == SizeRelation::unknown || right == SizeRelation::unknown)
                return SizeRelation::unknown;
            if (left == SizeRelation::decreasing || right == SizeRelation::decreasing)
                return SizeRelation::decreasing;
            return SizeRelation::nonincreasing;
        }

        SizeRelation strongest(SizeRelation left, SizeRelation right) {
            return static_cast<unsigned>(left) >= static_cast<unsigned>(right) ? left : right;
        }

        Graph compose(Graph const &left, Graph const &right) {
            Graph result{left.from, right.to, left.rows, right.columns,
                         std::vector<SizeRelation>(left.rows * right.columns, SizeRelation::unknown)};
            for (std::size_t row = 0; row < left.rows; ++row)
                for (std::size_t column = 0; column < right.columns; ++column)
                    for (std::size_t middle = 0; middle < left.columns; ++middle) {
                        SizeRelation path = compose_relation(left.edges[row * left.columns + middle],
                                                             right.edges[middle * right.columns + column]);
                        result.edges[row * result.columns + column] =
                            strongest(result.edges[row * result.columns + column], path);
                    }
            return result;
        }

    }  // namespace

    SizeChangeSummary require_size_change_termination(std::vector<syntax::FunctionDecl const *> const &group,
                                                      std::vector<SizeChangeCall> const &calls) {
        std::map<syntax::FunctionDecl const *, std::size_t> member;
        for (std::size_t i = 0; i < group.size(); ++i)
            member.emplace(group[i], i);

        std::vector<Graph> closure;
        std::set<decltype(Graph{}.key())> seen;
        for (auto const &call : calls) {
            Graph graph{member.at(call.caller),
                        member.at(call.callee),
                        call.relation.size(),
                        call.relation.empty() ? 0 : call.relation.front().size(),
                        {}};
            for (auto const &row : call.relation)
                graph.edges.insert(graph.edges.end(), row.begin(), row.end());
            if (seen.insert(graph.key()).second)
                closure.push_back(std::move(graph));
        }

        // Saturation is finite: each matrix cell has three values and group
        // arities are fixed.  Compose every path summary until no new summary
        // remains, then apply the exact idempotent-loop SCT criterion.
        for (std::size_t cursor = 0; cursor < closure.size(); ++cursor) {
            std::size_t snapshot = closure.size();
            for (std::size_t other = 0; other < snapshot; ++other) {
                if (closure[cursor].to == closure[other].from) {
                    Graph product = compose(closure[cursor], closure[other]);
                    if (seen.insert(product.key()).second)
                        closure.push_back(std::move(product));
                }
                if (closure[other].to == closure[cursor].from) {
                    Graph product = compose(closure[other], closure[cursor]);
                    if (seen.insert(product.key()).second)
                        closure.push_back(std::move(product));
                }
            }
        }

        std::size_t idempotent_loops = 0;
        for (auto const &graph : closure) {
            if (graph.from != graph.to)
                continue;
            Graph square = compose(graph, graph);
            if (square.edges != graph.edges)
                continue;
            ++idempotent_loops;
            bool strict_diagonal = false;
            for (std::size_t i = 0; i < std::min(graph.rows, graph.columns); ++i)
                strict_diagonal = strict_diagonal || graph.edges[i * graph.columns + i] == SizeRelation::decreasing;
            if (!strict_diagonal) {
                std::ostringstream names;
                for (std::size_t i = 0; i < group.size(); ++i) {
                    if (i)
                        names << ", ";
                    names << group[i]->name;
                }
                reject(group[graph.from]->span, "recursive value-function group `" + names.str() +
                                                    "` has a repeatable call cycle with no structural descent");
            }
        }
        return {calls.size(), closure.size(), idempotent_loops};
    }

}  // namespace fine::elaboration
