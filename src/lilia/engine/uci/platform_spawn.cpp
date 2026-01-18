#include "lilia/engine/uci/platform_spawn.hpp"

#include <cstring>

#if defined(_WIN32)

namespace lilia::engine::uci
{
  bool spawnWithPipes(const std::string &exePath, SpawnedProcess &out, std::string *outError)
  {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE childStdoutRd = nullptr, childStdoutWr = nullptr;
    HANDLE childStdinRd = nullptr, childStdinWr = nullptr;

    if (!CreatePipe(&childStdoutRd, &childStdoutWr, &sa, 0))
    {
      if (outError)
        *outError = "CreatePipe(stdout) failed.";
      return false;
    }
    if (!SetHandleInformation(childStdoutRd, HANDLE_FLAG_INHERIT, 0))
    {
      if (outError)
        *outError = "SetHandleInformation(stdout) failed.";
      CloseHandle(childStdoutRd);
      CloseHandle(childStdoutWr);
      return false;
    }

    if (!CreatePipe(&childStdinRd, &childStdinWr, &sa, 0))
    {
      if (outError)
        *outError = "CreatePipe(stdin) failed.";
      CloseHandle(childStdoutRd);
      CloseHandle(childStdoutWr);
      return false;
    }
    if (!SetHandleInformation(childStdinWr, HANDLE_FLAG_INHERIT, 0))
    {
      if (outError)
        *outError = "SetHandleInformation(stdin) failed.";
      CloseHandle(childStdoutRd);
      CloseHandle(childStdoutWr);
      CloseHandle(childStdinRd);
      CloseHandle(childStdinWr);
      return false;
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = childStdinRd;
    si.hStdOutput = childStdoutWr;
    si.hStdError = childStdoutWr;

    PROCESS_INFORMATION pi{};
    std::string cmd = "\"" + exePath + "\"";

    BOOL ok = CreateProcessA(
        nullptr,
        cmd.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);

    // Parent no longer needs these inherited ends
    CloseHandle(childStdinRd);
    CloseHandle(childStdoutWr);

    if (!ok)
    {
      if (outError)
        *outError = "CreateProcess failed.";
      CloseHandle(childStdoutRd);
      CloseHandle(childStdinWr);
      return false;
    }

    out.process = pi.hProcess;
    out.thread = pi.hThread;
    out.stdinWrite = childStdinWr;
    out.stdoutRead = childStdoutRd;
    return true;
  }

  void terminateProcess(SpawnedProcess &p)
  {
    if (p.stdinWrite)
    {
      CloseHandle(p.stdinWrite);
      p.stdinWrite = nullptr;
    }
    if (p.stdoutRead)
    {
      CloseHandle(p.stdoutRead);
      p.stdoutRead = nullptr;
    }
    if (p.process)
    {
      TerminateProcess(p.process, 0);
      CloseHandle(p.process);
      p.process = nullptr;
    }
    if (p.thread)
    {
      CloseHandle(p.thread);
      p.thread = nullptr;
    }
  }
} // namespace lilia::engine::uci
#else

#include <cerrno>
#include <csignal> // SIGKILL, kill()
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace lilia::engine::uci
{
  bool spawnWithPipes(const std::string &exePath, SpawnedProcess &out, std::string *outError)
  {
    int inPipe[2]{-1, -1};
    int outPipe[2]{-1, -1};

    if (pipe(inPipe) != 0)
    {
      if (outError)
        *outError = "pipe(stdin) failed.";
      return false;
    }
    if (pipe(outPipe) != 0)
    {
      if (outError)
        *outError = "pipe(stdout) failed.";
      close(inPipe[0]);
      close(inPipe[1]);
      return false;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
      if (outError)
        *outError = "fork failed.";
      close(inPipe[0]);
      close(inPipe[1]);
      close(outPipe[0]);
      close(outPipe[1]);
      return false;
    }

    if (pid == 0)
    {
      // Child
      (void)dup2(inPipe[0], STDIN_FILENO);
      (void)dup2(outPipe[1], STDOUT_FILENO);
      (void)dup2(outPipe[1], STDERR_FILENO);

      close(inPipe[0]);
      close(inPipe[1]);
      close(outPipe[0]);
      close(outPipe[1]);

      execl(exePath.c_str(), exePath.c_str(), (char *)nullptr);
      _exit(127);
    }

    // Parent
    close(inPipe[0]);  // parent writes to inPipe[1]
    close(outPipe[1]); // parent reads from outPipe[0]

    out.pid = pid;
    out.stdinFd = inPipe[1];
    out.stdoutFd = outPipe[0];

    return true;
  }

  void terminateProcess(SpawnedProcess &p)
  {
    if (p.stdinFd != -1)
    {
      close(p.stdinFd);
      p.stdinFd = -1;
    }
    if (p.stdoutFd != -1)
    {
      close(p.stdoutFd);
      p.stdoutFd = -1;
    }

    if (p.pid > 0)
    {
      // First try a polite terminate; many UCI engines exit on stdin close anyway,
      // but this makes shutdown deterministic.
      (void)kill(p.pid, SIGTERM);

      // Reap if it exits quickly; otherwise force kill.
      int status = 0;
      pid_t r = waitpid(p.pid, &status, WNOHANG);
      if (r == 0)
      {
        // Still running -> hard kill and wait.
        (void)kill(p.pid, SIGKILL);
        (void)waitpid(p.pid, &status, 0);
      }
      else if (r < 0)
      {
        // ESRCH means it already died; still attempt to reap just in case.
        if (errno != ESRCH)
        {
          (void)waitpid(p.pid, &status, 0);
        }
      }

      p.pid = -1;
    }
  }
} // namespace lilia::engine::uci

#endif
