#if !defined(_WIN32)

#include "lilia/engine/uci/uci_engine_process.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <string>
#include <thread>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct UciEngineProcess::Impl
{
  pid_t pid{-1};
  int stdinWrite{-1}; // parent -> child stdin
  int stdoutRead{-1}; // child stdout/stderr -> parent
  std::string readBuf;
};

void UciEngineProcess::ImplDeleter::operator()(Impl *p) noexcept
{
  delete p;
}

static void closeFd(int &fd)
{
  if (fd >= 0)
  {
    ::close(fd);
    fd = -1;
  }
}

bool UciEngineProcess::platformStart(const std::string &exePath)
{
  platformStop();
  m_impl.reset(new Impl()); // IMPORTANT: not make_unique (would create wrong deleter type)

  int inPipe[2] = {-1, -1};  // child reads [0], parent writes [1]
  int outPipe[2] = {-1, -1}; // parent reads [0], child writes [1]

  if (::pipe(inPipe) != 0)
    return false;

  if (::pipe(outPipe) != 0)
  {
    ::close(inPipe[0]);
    ::close(inPipe[1]);
    return false;
  }

  pid_t pid = ::fork();
  if (pid < 0)
  {
    ::close(inPipe[0]);
    ::close(inPipe[1]);
    ::close(outPipe[0]);
    ::close(outPipe[1]);
    return false;
  }

  if (pid == 0)
  {
    // Child
    ::dup2(inPipe[0], STDIN_FILENO);
    ::dup2(outPipe[1], STDOUT_FILENO);
    ::dup2(outPipe[1], STDERR_FILENO);

    ::close(inPipe[0]);
    ::close(inPipe[1]);
    ::close(outPipe[0]);
    ::close(outPipe[1]);

    char *const argv[] = {const_cast<char *>(exePath.c_str()), nullptr};
    ::execv(exePath.c_str(), argv);
    _exit(127);
  }

  // Parent
  ::close(inPipe[0]);
  ::close(outPipe[1]);

  m_impl->pid = pid;
  m_impl->stdinWrite = inPipe[1];
  m_impl->stdoutRead = outPipe[0];

  return true;
}

void UciEngineProcess::platformStop()
{
  if (!m_impl)
    return;

  closeFd(m_impl->stdinWrite);
  closeFd(m_impl->stdoutRead);

  if (m_impl->pid > 0)
  {
    int status = 0;
    for (int i = 0; i < 50; ++i)
    {
      pid_t r = ::waitpid(m_impl->pid, &status, WNOHANG);
      if (r == m_impl->pid)
      {
        m_impl->pid = -1;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (m_impl->pid > 0)
    {
      ::kill(m_impl->pid, SIGKILL);
      ::waitpid(m_impl->pid, &status, 0);
      m_impl->pid = -1;
    }
  }

  m_impl.reset();
}

bool UciEngineProcess::platformWrite(const std::string &s)
{
  if (!m_impl || m_impl->stdinWrite < 0)
    return false;

  const char *p = s.data();
  size_t remaining = s.size();

  while (remaining > 0)
  {
    ssize_t n = ::write(m_impl->stdinWrite, p, remaining);
    if (n < 0)
    {
      if (errno == EINTR)
        continue;
      return false;
    }
    p += n;
    remaining -= (size_t)n;
  }
  return true;
}

bool UciEngineProcess::platformReadLine(std::string &outLine)
{
  outLine.clear();
  if (!m_impl || m_impl->stdoutRead < 0)
    return false;

  for (;;)
  {
    auto pos = m_impl->readBuf.find('\n');
    if (pos != std::string::npos)
    {
      outLine = m_impl->readBuf.substr(0, pos + 1);
      m_impl->readBuf.erase(0, pos + 1);
      return true;
    }

    char tmp[4096];
    ssize_t n = ::read(m_impl->stdoutRead, tmp, sizeof(tmp));
    if (n < 0)
    {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (n == 0)
    {
      if (!m_impl->readBuf.empty())
      {
        outLine = std::move(m_impl->readBuf);
        m_impl->readBuf.clear();
        return true;
      }
      return false;
    }

    m_impl->readBuf.append(tmp, tmp + n);
  }
}

#endif // !_WIN32
