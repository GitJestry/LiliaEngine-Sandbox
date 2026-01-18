#if defined(_WIN32)

#include "lilia/engine/uci/uci_engine_process.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

struct UciEngineProcess::Impl
{
  HANDLE hProcess{NULL};
  HANDLE hThread{NULL};
  HANDLE hStdinWrite{NULL};
  HANDLE hStdoutRead{NULL};
  std::string readBuf;
};

void UciEngineProcess::ImplDeleter::operator()(Impl *p) noexcept
{
  delete p;
}
static std::wstring utf8ToWide(const std::string &s)
{
  if (s.empty())
    return L"";
  int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  if (len <= 0)
    return L"";
  std::wstring out;
  out.resize(std::size_t(len - 1));
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
  return out;
}

static std::wstring quoteWinArg(const std::wstring &p)
{
  // minimal: wrap in quotes
  return L"\"" + p + L"\"";
}

bool UciEngineProcess::platformStart(const std::string &exePath)
{
  platformStop();
  m_impl = std::make_unique<Impl>();
  m_impl.reset(new Impl());

  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = nullptr;
  sa.bInheritHandle = TRUE;

  HANDLE childStdoutRd = NULL;
  HANDLE childStdoutWr = NULL;
  HANDLE childStdinRd = NULL;
  HANDLE childStdinWr = NULL;

  // stdout/stderr pipe
  if (!CreatePipe(&childStdoutRd, &childStdoutWr, &sa, 0))
    return false;
  if (!SetHandleInformation(childStdoutRd, HANDLE_FLAG_INHERIT, 0))
    return false;

  // stdin pipe
  if (!CreatePipe(&childStdinRd, &childStdinWr, &sa, 0))
    return false;
  if (!SetHandleInformation(childStdinWr, HANDLE_FLAG_INHERIT, 0))
    return false;

  STARTUPINFOW si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = childStdinRd;
  si.hStdOutput = childStdoutWr;
  si.hStdError = childStdoutWr; // merge stderr into stdout

  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));

  std::wstring wExe = utf8ToWide(exePath);
  std::wstring cmd = quoteWinArg(wExe);

  // CreateProcess requires mutable command line buffer
  std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
  cmdBuf.push_back(L'\0');

  BOOL ok = CreateProcessW(
      /*lpApplicationName=*/nullptr,
      /*lpCommandLine=*/cmdBuf.data(),
      /*lpProcessAttributes=*/nullptr,
      /*lpThreadAttributes=*/nullptr,
      /*bInheritHandles=*/TRUE,
      /*dwCreationFlags=*/CREATE_NO_WINDOW,
      /*lpEnvironment=*/nullptr,
      /*lpCurrentDirectory=*/nullptr,
      /*lpStartupInfo=*/&si,
      /*lpProcessInformation=*/&pi);

  // Parent must close its copies of child-side pipe ends
  CloseHandle(childStdoutWr);
  CloseHandle(childStdinRd);

  if (!ok)
  {
    CloseHandle(childStdoutRd);
    CloseHandle(childStdinWr);
    return false;
  }

  m_impl->hProcess = pi.hProcess;
  m_impl->hThread = pi.hThread;
  m_impl->hStdoutRead = childStdoutRd;
  m_impl->hStdinWrite = childStdinWr;

  return true;
}

void UciEngineProcess::platformStop()
{
  if (!m_impl)
    return;

  // Closing stdin is a strong hint for the child to exit if it's stuck
  if (m_impl->hStdinWrite)
  {
    CloseHandle(m_impl->hStdinWrite);
    m_impl->hStdinWrite = NULL;
  }

  // Give it a moment to exit gracefully
  if (m_impl->hProcess)
  {
    DWORD wait = WaitForSingleObject(m_impl->hProcess, 400);
    if (wait == WAIT_TIMEOUT)
    {
      // Best-effort termination
      TerminateProcess(m_impl->hProcess, 1);
      WaitForSingleObject(m_impl->hProcess, 200);
    }
  }

  if (m_impl->hStdoutRead)
  {
    CloseHandle(m_impl->hStdoutRead);
    m_impl->hStdoutRead = NULL;
  }

  if (m_impl->hThread)
  {
    CloseHandle(m_impl->hThread);
    m_impl->hThread = NULL;
  }

  if (m_impl->hProcess)
  {
    CloseHandle(m_impl->hProcess);
    m_impl->hProcess = NULL;
  }

  m_impl.reset();
}

bool UciEngineProcess::platformWrite(const std::string &s)
{
  if (!m_impl || !m_impl->hStdinWrite)
    return false;

  const char *data = s.data();
  DWORD total = 0;
  DWORD remaining = (DWORD)s.size();

  while (remaining > 0)
  {
    DWORD written = 0;
    BOOL ok = WriteFile(m_impl->hStdinWrite, data + total, remaining, &written, nullptr);
    if (!ok)
      return false;
    total += written;
    remaining -= written;
  }
  return true;
}

bool UciEngineProcess::platformReadLine(std::string &outLine)
{
  outLine.clear();
  if (!m_impl || !m_impl->hStdoutRead)
    return false;

  // If buffer already contains a full line, return it.
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
    DWORD read = 0;
    BOOL ok = ReadFile(m_impl->hStdoutRead, tmp, (DWORD)sizeof(tmp), &read, nullptr);

    if (!ok || read == 0)
    {
      // EOF: flush remainder once
      if (!m_impl->readBuf.empty())
      {
        outLine = std::move(m_impl->readBuf);
        m_impl->readBuf.clear();
        return true;
      }
      return false;
    }

    m_impl->readBuf.append(tmp, tmp + read);
  }
}

#endif // _WIN32
