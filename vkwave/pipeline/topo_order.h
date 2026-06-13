#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace vkwave
{

/// Topologically order nodes [0, deps.size()) given each node's dependencies.
///
/// `deps[i]` lists the indices that node i depends on — every such index is
/// ordered strictly before i in the result. Among nodes with no remaining
/// dependency the smallest index is chosen first, so with no edges the result
/// is the identity order [0, 1, 2, ...] (stable / insertion-preserving).
///
/// @throws std::runtime_error if the dependency graph contains a cycle.
inline std::vector<size_t> topological_order(
  const std::vector<std::vector<size_t>>& deps)
{
  const size_t n = deps.size();

  std::vector<size_t> in_degree(n, 0);
  std::vector<std::vector<size_t>> successors(n);
  for (size_t i = 0; i < n; ++i)
  {
    for (size_t d : deps[i])
    {
      // d must come before i
      successors[d].push_back(i);
      ++in_degree[i];
    }
  }

  std::vector<size_t> ready;
  for (size_t i = 0; i < n; ++i)
    if (in_degree[i] == 0)
      ready.push_back(i);

  std::vector<size_t> order;
  order.reserve(n);
  while (order.size() < n)
  {
    if (ready.empty())
      throw std::runtime_error("topological_order: dependency cycle detected");

    // Pick the smallest ready index for deterministic, insertion-preserving order.
    size_t best = 0;
    for (size_t k = 1; k < ready.size(); ++k)
      if (ready[k] < ready[best])
        best = k;

    const size_t node = ready[best];
    ready.erase(ready.begin() + static_cast<std::ptrdiff_t>(best));
    order.push_back(node);

    for (size_t s : successors[node])
      if (--in_degree[s] == 0)
        ready.push_back(s);
  }

  return order;
}

} // namespace vkwave
