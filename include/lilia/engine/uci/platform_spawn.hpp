#pragma once
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/types.h>
#endif

namespace lilia::engine::uci
{
  struct SpawnedProcess
  {
#if defined(_WIN32)
    HANDLE process{nullptr};
    HANDLE thread{nullptr};
    HANDLE stdinWrite{nullptr};
    HANDLE stdoutRead{nullptr};
#else
    pid_t pid{-1};
    int stdinFd{-1};
    int stdoutFd{-1};
#endif
  };

  bool spawnWithPipes(const std::string &exePath, SpawnedProcess &out, std::string *outError = nullptr);
  void terminateProcess(SpawnedProcess &p);

} // namespace lilia::engine::uci
