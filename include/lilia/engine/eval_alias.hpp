#pragma once

#include "lilia/engine/eval_shared.hpp"

#define LILIA_EVAL_PARAM_REF(name) (::lilia::engine::eval_params().name)
#include "lilia/engine/eval_param_aliases.inc"

#define ROOK_BEHIND_PASSER_HALF (ROOK_BEHIND_PASSER / 2)
#define ROOK_BEHIND_PASSER_THIRD (ROOK_BEHIND_PASSER / 3)
#define ROOK_PASSER_PROGRESS_MULT ROOK_BEHIND_PASSER_THIRD
