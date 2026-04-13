#pragma once
#include "lilia/chess/move.hpp"
#include "lilia/chess/compiler.hpp"

namespace lilia::engine
{
  namespace detail
  {
    LILIA_ALWAYS_INLINE void insertion_sort_desc(int *score, chess::Move *moves, int n)
    {
      for (int i = 1; i < n; ++i)
      {
        const int s = score[i];
        const chess::Move m = moves[i];
        int j = i - 1;

        while (j >= 0 && score[j] < s)
        {
          score[j + 1] = score[j];
          moves[j + 1] = moves[j];
          --j;
        }

        score[j + 1] = s;
        moves[j + 1] = m;
      }
    }
  }

  // descending sort on parallel arrays
  // small n: insertion sort
  // larger n: shell sort with Ciura gaps
  LILIA_ALWAYS_INLINE void sort_by_score_desc(int *score, chess::Move *moves, int n)
  {
    if (n <= 1)
      return;

    if (n <= 16)
    {
      detail::insertion_sort_desc(score, moves, n);
      return;
    }

    static constexpr int gaps[] = {57, 23, 10, 4, 1};

    for (int gap : gaps)
    {
      if (gap >= n)
        continue;

      for (int i = gap; i < n; ++i)
      {
        const int s = score[i];
        const chess::Move m = moves[i];
        int j = i;

        while (j >= gap && score[j - gap] < s)
        {
          score[j] = score[j - gap];
          moves[j] = moves[j - gap];
          j -= gap;
        }

        score[j] = s;
        moves[j] = m;
      }
    }
  }
}
