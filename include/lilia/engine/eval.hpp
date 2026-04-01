#pragma once
#include <cstdint>

namespace lilia::engine
{
  class SearchPosition;
  class Evaluator final
  {
  public:
    Evaluator() noexcept;
    ~Evaluator() noexcept;

    // evaluation in cp in the view of white
    int evaluate(const SearchPosition &pos) const;

    // Eval- & Pawn-Caches clearing
    void clearCaches() const noexcept;

    Evaluator(const Evaluator &) = delete;
    Evaluator &operator=(const Evaluator &) = delete;
    Evaluator(Evaluator &&) = delete;
    Evaluator &operator=(Evaluator &&) = delete;

  private:
    struct Impl;
    mutable Impl *m_impl = nullptr;
  };

}
