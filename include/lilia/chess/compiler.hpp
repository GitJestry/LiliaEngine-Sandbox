#pragma once

// Central compiler / optimizer hint macros for the chess core.
// Include this header anywhere low-level chess code needs them.

#if defined(_MSC_VER)
#define LILIA_DEBUGBREAK() __debugbreak()
#else
#include <csignal>
#endif

// -------------------------------------------------
// Force / prevent inlining
// -------------------------------------------------
#ifndef LILIA_ALWAYS_INLINE
#if defined(_MSC_VER)
#define LILIA_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define LILIA_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define LILIA_ALWAYS_INLINE inline
#endif
#endif

#ifndef LILIA_NOINLINE
#if defined(_MSC_VER)
#define LILIA_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define LILIA_NOINLINE __attribute__((noinline))
#else
#define LILIA_NOINLINE
#endif
#endif

#ifndef LILIA_PREFETCH_L1
#if defined(__clang__) || defined(__GNUC__)
#define LILIA_PREFETCH_L1(ptr) __builtin_prefetch((ptr), 0, 3)
#define LILIA_PREFETCHW_L1(ptr) __builtin_prefetch((ptr), 1, 3)
#elif defined(_MSC_VER)
// No direct portable MSVC equivalent for generic software prefetch.
// Leave as no-op unless you later want to use architecture-specific intrinsics.
#define LILIA_PREFETCH_L1(ptr) ((void)0)
#define LILIA_PREFETCHW_L1(ptr) ((void)0)
#else
#define LILIA_PREFETCH_L1(ptr) ((void)0)
#define LILIA_PREFETCHW_L1(ptr) ((void)0)
#endif
#endif
// -------------------------------------------------
// Debug break
// -------------------------------------------------
#ifndef LILIA_DEBUGBREAK
#if defined(_MSC_VER)
// already defined above
#elif defined(__GNUC__) || defined(__clang__)
#define LILIA_DEBUGBREAK() __builtin_trap()
#else
#define LILIA_DEBUGBREAK() std::raise(SIGTRAP)
#endif
#endif

// -------------------------------------------------
// Branch prediction hints
// -------------------------------------------------
#ifndef LILIA_LIKELY
#if defined(__GNUC__) || defined(__clang__)
#define LILIA_LIKELY(x) __builtin_expect(!!(x), 1)
#define LILIA_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LILIA_LIKELY(x) (x)
#define LILIA_UNLIKELY(x) (x)
#endif
#endif

// -------------------------------------------------
// Restrict qualifier
// -------------------------------------------------
#ifndef LILIA_RESTRICT
#if defined(__GNUC__) || defined(__clang__)
#define LILIA_RESTRICT __restrict__
#elif defined(_MSC_VER)
#define LILIA_RESTRICT __restrict
#else
#define LILIA_RESTRICT
#endif
#endif

// -------------------------------------------------
// Assumptions / unreachable
// -------------------------------------------------
#ifndef LILIA_ASSUME
#if defined(__clang__) || defined(__GNUC__)
#define LILIA_ASSUME(x)        \
  do                           \
  {                            \
    if (!(x))                  \
      __builtin_unreachable(); \
  } while (0)
#elif defined(_MSC_VER)
#define LILIA_ASSUME(x) __assume(x)
#else
#define LILIA_ASSUME(x) ((void)0)
#endif
#endif

#ifndef LILIA_UNREACHABLE
#if defined(__clang__) || defined(__GNUC__)
#define LILIA_UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
#define LILIA_UNREACHABLE() __assume(0)
#else
#define LILIA_UNREACHABLE() ((void)0)
#endif
#endif

// -------------------------------------------------
// Optional lightweight assert for chess-core debugging
// -------------------------------------------------
#ifndef NDEBUG
#ifndef LILIA_ASSERT
#define LILIA_ASSERT(x)   \
  do                      \
  {                       \
    if (!(x))             \
      LILIA_DEBUGBREAK(); \
  } while (0)
#endif
#else
#ifndef LILIA_ASSERT
#define LILIA_ASSERT(x) ((void)0)
#endif
#endif
