#pragma once
#include <cstdint>

namespace lilia {
namespace model {
class Position;
}

namespace engine {

class Evaluator final {
 public:
  Evaluator() noexcept;
  ~Evaluator() noexcept;

  // evaluation in cp in the view of the one who's turn it is
  int evaluate(model::Position& pos) const;

  // Eval- & Pawn-Caches clearing
  void clearCaches() const noexcept;

  Evaluator(const Evaluator&) = delete;
  Evaluator& operator=(const Evaluator&) = delete;
  Evaluator(Evaluator&&) = delete;
  Evaluator& operator=(Evaluator&&) = delete;

 private:
  struct Impl;
  mutable Impl* m_impl = nullptr;
};

}  // namespace engine
}  // namespace lilia
