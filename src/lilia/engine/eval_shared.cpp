#include "lilia/engine/eval_shared.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace lilia::engine {
namespace {
struct EvalParamStorage {
  EvalParams current{};
  EvalParams defaults{};
};

EvalParamStorage& storage() {
  static EvalParamStorage instance{};
  return instance;
}
}  // namespace

EvalParams& eval_params() { return storage().current; }
const EvalParams& default_eval_params() { return storage().defaults; }

void reset_eval_params() { storage().current = storage().defaults; }

std::span<const EvalParamEntry> eval_param_entries() {
  static std::vector<EvalParamEntry> entries;
  if (entries.empty()) {
    auto& params = eval_params();
    const auto& defaults = default_eval_params();
#define REGISTER_SCALAR(field, defField, label) \
    entries.emplace_back(EvalParamEntry{label, &field, defField});
#define REGISTER_ARRAY(field, defField, label)                                           \
    for (size_t idx = 0; idx < field.size(); ++idx) {                                    \
      entries.emplace_back(EvalParamEntry{std::string(label) + "[" + std::to_string(idx) + "]", \
                                           &field[idx], defField[idx]});                   \
    }

#define EVAL_PARAM_SCALAR(name, default_value) REGISTER_SCALAR(params.name, defaults.name, #name)
#define EVAL_PARAM_ARRAY(name, size, ...) REGISTER_ARRAY(params.name, defaults.name, #name)
#include "lilia/engine/eval_params.inc"
#undef EVAL_PARAM_SCALAR
#undef EVAL_PARAM_ARRAY
#undef REGISTER_SCALAR
#undef REGISTER_ARRAY
  }
  return entries;
}

std::vector<int> get_eval_param_values() {
  std::vector<int> values;
  const auto& entries = eval_param_entries();
  values.reserve(entries.size());
  for (const auto& entry : entries) values.push_back(*entry.value);
  return values;
}

std::vector<int> get_default_eval_param_values() {
  std::vector<int> values;
  const auto& entries = eval_param_entries();
  values.reserve(entries.size());
  for (const auto& entry : entries) {
    // Recover default via pointer arithmetic by mapping name again.
    values.push_back(entry.default_value);
  }
  return values;
}

void set_eval_param_values(std::span<const int> values) {
  const auto& entries = eval_param_entries();
  if (values.size() != entries.size()) {
    throw std::invalid_argument("Parameter count mismatch when setting eval params");
  }
  for (size_t i = 0; i < entries.size(); ++i) {
    *entries[i].value = values[i];
  }
}

}  // namespace lilia::engine
