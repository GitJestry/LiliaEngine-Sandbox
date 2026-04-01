#include "lilia/app/engines/uci_engine_process.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace std::chrono_literals;

namespace lilia::app::engines
{
  namespace
  {
    bool startsWith(const std::string &s, const char *prefix)
    {
      return s.rfind(prefix, 0) == 0;
    }

    std::string trimCrlf(std::string s)
    {
      while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
      return s;
    }

    std::vector<std::string> tokenize(const std::string &line)
    {
      std::vector<std::string> out;
      std::string cur;
      bool inQuotes = false;
      bool escape = false;

      for (char c : line)
      {
        if (escape)
        {
          cur.push_back(c);
          escape = false;
          continue;
        }

        if (inQuotes && c == '\\')
        {
          escape = true;
          continue;
        }

        if (c == '"')
        {
          inQuotes = !inQuotes;
          continue;
        }

        if (!inQuotes && std::isspace(static_cast<unsigned char>(c)))
        {
          if (!cur.empty())
          {
            out.push_back(cur);
            cur.clear();
          }
          continue;
        }

        cur.push_back(c);
      }

      if (!cur.empty())
        out.push_back(cur);

      return out;
    }

    bool isOptionKeyword(const std::string &tok)
    {
      return tok == "default" || tok == "min" || tok == "max" || tok == "var";
    }

    std::string joinTokens(const std::vector<std::string> &tokens, std::size_t from, std::size_t to)
    {
      if (from >= to || from >= tokens.size())
        return {};

      std::ostringstream os;
      for (std::size_t i = from; i < to; ++i)
      {
        if (i != from)
          os << ' ';
        os << tokens[i];
      }
      return os.str();
    }

    std::string quoteIfNeeded(const std::string &value)
    {
      if (!value.empty())
      {
        bool needsQuotes = false;
        for (char c : value)
        {
          if (std::isspace(static_cast<unsigned char>(c)) || c == '"' || c == '\\')
          {
            needsQuotes = true;
            break;
          }
        }
        if (!needsQuotes)
          return value;
      }

      std::string out;
      out.push_back('"');
      for (char c : value)
      {
        if (c == '"' || c == '\\')
          out.push_back('\\');
        out.push_back(c);
      }
      out.push_back('"');
      return out;
    }

    bool parseIntValue(const std::string &s, int &out)
    {
      try
      {
        std::size_t pos = 0;
        int v = std::stoi(s, &pos, 10);
        if (pos != s.size())
          return false;
        out = v;
        return true;
      }
      catch (...)
      {
        return false;
      }
    }

#if defined(_WIN32)
    std::wstring utf8ToWide(const std::string &s)
    {
      if (s.empty())
        return L"";
      const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
      if (len <= 0)
        return L"";

      std::wstring out;
      out.resize(static_cast<std::size_t>(len - 1));
      MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
      return out;
    }
#endif
  } // namespace

  struct UciEngineProcess::Impl
  {
#if defined(_WIN32)
    HANDLE process{nullptr};
    HANDLE thread{nullptr};
    HANDLE stdinWrite{nullptr};
    HANDLE stdoutRead{nullptr};
#else
    pid_t pid{-1};
    int stdinWrite{-1};
    int stdoutRead{-1};
#endif
    std::string readBuffer;
  };

  void UciEngineProcess::ImplDeleter::operator()(Impl *p) noexcept
  {
    delete p;
  }

  UciEngineProcess::~UciEngineProcess()
  {
    stop();
  }

  bool UciEngineProcess::start(const std::string &exePath)
  {
    std::filesystem::path p(exePath);
    const std::filesystem::path workDir = p.has_parent_path() ? p.parent_path() : std::filesystem::current_path();
    return start(exePath, workDir.string());
  }

  bool UciEngineProcess::start(const std::string &exePath, const std::string &workingDirectory)
  {
    stop();
    if (!platformStart(exePath, workingDirectory))
      return false;

    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_lines.clear();
      m_bestmoves.clear();
    }

    m_running.store(true);
    m_reader = std::thread([this]
                           { readerLoop(); });
    return true;
  }

  void UciEngineProcess::stop()
  {
    const bool wasRunning = m_running.exchange(false);
    if (wasRunning)
      sendLine("quit");

    platformStop();

    if (m_reader.joinable())
      m_reader.join();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_lines.clear();
    m_bestmoves.clear();
  }

  void UciEngineProcess::sendLine(const std::string &line)
  {
    (void)platformWrite(line + "\n");
  }

  void UciEngineProcess::readerLoop()
  {
    while (m_running.load())
    {
      std::string line;
      if (!platformReadLine(line))
        break;

      line = trimCrlf(std::move(line));

      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lines.push_back(line);
        if (startsWith(line, "bestmove "))
          m_bestmoves.push_back(line);
      }

      m_cvLines.notify_all();
      m_cvBest.notify_all();
    }

    m_running.store(false);
    m_cvLines.notify_all();
    m_cvBest.notify_all();
  }

  bool UciEngineProcess::uciHandshake(Id &outId, std::vector<domain::analysis::config::UciOption> &outOptions)
  {
    outId = {};
    outOptions.clear();

    sendLine("uci");
    const auto deadline = std::chrono::steady_clock::now() + 3s;

    for (;;)
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cvLines.wait_until(lock, deadline, [this]
                           { return !m_lines.empty() || !m_running.load(); });

      while (!m_lines.empty())
      {
        std::string line = std::move(m_lines.front());
        m_lines.pop_front();

        if (startsWith(line, "id name "))
        {
          outId.name = line.substr(std::string("id name ").size());
        }
        else if (startsWith(line, "id author "))
        {
          outId.author = line.substr(std::string("id author ").size());
        }
        else if (startsWith(line, "option "))
        {
          domain::analysis::config::UciOption opt;
          if (parseUciOptionLine(line, opt))
            outOptions.push_back(std::move(opt));
        }
        else if (line == "uciok")
        {
          lock.unlock();
          sendLine("isready");

          const auto readyDeadline = std::chrono::steady_clock::now() + 2s;
          for (;;)
          {
            std::unique_lock<std::mutex> readyLock(m_mutex);
            m_cvLines.wait_until(readyLock, readyDeadline, [this]
                                 { return !m_lines.empty() || !m_running.load(); });

            while (!m_lines.empty())
            {
              std::string readyLine = std::move(m_lines.front());
              m_lines.pop_front();
              if (readyLine == "readyok")
                return true;
            }

            if (std::chrono::steady_clock::now() >= readyDeadline)
              return false;
          }
        }
      }

      if (std::chrono::steady_clock::now() >= deadline)
        return false;
    }
  }

  void UciEngineProcess::setOption(const std::string &name, const domain::analysis::config::UciValue &value)
  {
    std::ostringstream os;
    os << "setoption name " << name;

    if (std::holds_alternative<bool>(value))
      os << " value " << (std::get<bool>(value) ? "true" : "false");
    else if (std::holds_alternative<int>(value))
      os << " value " << std::get<int>(value);
    else
      os << " value " << std::get<std::string>(value);

    sendLine(os.str());
  }

  void UciEngineProcess::newGame()
  {
    sendLine("ucinewgame");
    sendLine("isready");
  }

  void UciEngineProcess::position(const std::string &fen, const std::vector<std::string> &movesUci)
  {
    std::ostringstream os;
    os << "position fen " << fen;
    if (!movesUci.empty())
    {
      os << " moves";
      for (const auto &move : movesUci)
        os << ' ' << move;
    }
    sendLine(os.str());
  }

  void UciEngineProcess::goTime(int wtimeMs, int btimeMs, int wincMs, int bincMs)
  {
    std::ostringstream os;
    os << "go wtime " << wtimeMs << " btime " << btimeMs << " winc " << wincMs << " binc " << bincMs;
    sendLine(os.str());
  }

  void UciEngineProcess::goFixedMovetime(int movetimeMs)
  {
    std::ostringstream os;
    os << "go movetime " << movetimeMs;
    sendLine(os.str());
  }

  void UciEngineProcess::goFixedDepth(int depth)
  {
    std::ostringstream os;
    os << "go depth " << depth;
    sendLine(os.str());
  }

  void UciEngineProcess::stopSearch()
  {
    sendLine("stop");
  }

  std::string UciEngineProcess::waitBestmove()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cvBest.wait(lock, [this]
                  { return !m_bestmoves.empty() || !m_running.load(); });

    if (m_bestmoves.empty())
      return {};

    std::string line = std::move(m_bestmoves.front());
    m_bestmoves.pop_front();
    return line;
  }

  bool UciEngineProcess::parseUciOptionLine(const std::string &line, domain::analysis::config::UciOption &out)
  {
    if (!startsWith(line, "option "))
      return false;

    const std::vector<std::string> tokens = tokenize(line);
    if (tokens.size() < 5 || tokens[0] != "option")
      return false;

    std::size_t namePos = std::string::npos;
    std::size_t typePos = std::string::npos;
    for (std::size_t i = 1; i < tokens.size(); ++i)
    {
      if (tokens[i] == "name" && namePos == std::string::npos)
        namePos = i;
      else if (tokens[i] == "type" && typePos == std::string::npos)
        typePos = i;
    }

    if (namePos == std::string::npos || typePos == std::string::npos || typePos <= namePos + 1 || typePos + 1 >= tokens.size())
      return false;

    out = {};
    out.name = joinTokens(tokens, namePos + 1, typePos);

    using Type = domain::analysis::config::UciOption::Type;
    const std::string &typeToken = tokens[typePos + 1];
    if (typeToken == "check")
      out.type = Type::Check;
    else if (typeToken == "spin")
      out.type = Type::Spin;
    else if (typeToken == "combo")
      out.type = Type::Combo;
    else if (typeToken == "string")
      out.type = Type::String;
    else if (typeToken == "button")
      out.type = Type::Button;
    else
      return false;

    std::size_t i = typePos + 2;
    while (i < tokens.size())
    {
      const std::string &tok = tokens[i];

      if (tok == "default" || tok == "var")
      {
        std::size_t j = i + 1;
        while (j < tokens.size() && !isOptionKeyword(tokens[j]))
          ++j;

        const std::string value = joinTokens(tokens, i + 1, j);
        if (tok == "default")
        {
          if (out.type == Type::Check)
          {
            out.defaultBool = (value == "true" || value == "1");
          }
          else if (out.type == Type::Spin)
          {
            int v = 0;
            if (parseIntValue(value, v))
              out.defaultInt = v;
          }
          else if (out.type != Type::Button)
          {
            out.defaultStr = value;
          }
        }
        else
        {
          out.vars.push_back(value);
        }

        i = j;
        continue;
      }

      if (tok == "min" || tok == "max")
      {
        if (i + 1 >= tokens.size())
          break;
        int v = 0;
        if (parseIntValue(tokens[i + 1], v))
        {
          if (tok == "min")
            out.min = v;
          else
            out.max = v;
        }
        i += 2;
        continue;
      }

      ++i;
    }

    return true;
  }

  std::string UciEngineProcess::serializeOptionLine(const domain::analysis::config::UciOption &opt)
  {
    using Type = domain::analysis::config::UciOption::Type;

    std::ostringstream os;
    os << "option name " << quoteIfNeeded(opt.name) << " type ";

    switch (opt.type)
    {
    case Type::Check:
      os << "check default " << (opt.defaultBool ? "true" : "false");
      break;
    case Type::Spin:
      os << "spin default " << opt.defaultInt << " min " << opt.min << " max " << opt.max;
      break;
    case Type::Combo:
      os << "combo default " << quoteIfNeeded(opt.defaultStr);
      for (const auto &v : opt.vars)
        os << " var " << quoteIfNeeded(v);
      break;
    case Type::String:
      os << "string default " << quoteIfNeeded(opt.defaultStr);
      break;
    case Type::Button:
      os << "button";
      break;
    }

    return os.str();
  }

  bool UciEngineProcess::platformStart(const std::string &exePath, const std::string &workingDirectory)
  {
    platformStop();
    m_impl.reset(new Impl());

#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE childStdoutRead = nullptr;
    HANDLE childStdoutWrite = nullptr;
    HANDLE childStdinRead = nullptr;
    HANDLE childStdinWrite = nullptr;

    if (!CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 0))
      return false;
    if (!SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0))
    {
      CloseHandle(childStdoutRead);
      CloseHandle(childStdoutWrite);
      return false;
    }

    if (!CreatePipe(&childStdinRead, &childStdinWrite, &sa, 0))
    {
      CloseHandle(childStdoutRead);
      CloseHandle(childStdoutWrite);
      return false;
    }
    if (!SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0))
    {
      CloseHandle(childStdoutRead);
      CloseHandle(childStdoutWrite);
      CloseHandle(childStdinRead);
      CloseHandle(childStdinWrite);
      return false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childStdinRead;
    si.hStdOutput = childStdoutWrite;
    si.hStdError = childStdoutWrite;

    PROCESS_INFORMATION pi{};

    const std::wstring wExe = utf8ToWide(exePath);
    std::wstring cmd = L"\"" + wExe + L"\"";
    std::vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(L'\0');

    const std::wstring wCwd = utf8ToWide(workingDirectory);

    const BOOL ok = CreateProcessW(
        wExe.c_str(),
        cmdLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        wCwd.empty() ? nullptr : wCwd.c_str(),
        &si,
        &pi);

    CloseHandle(childStdoutWrite);
    CloseHandle(childStdinRead);

    if (!ok)
    {
      CloseHandle(childStdoutRead);
      CloseHandle(childStdinWrite);
      m_impl.reset();
      return false;
    }

    m_impl->process = pi.hProcess;
    m_impl->thread = pi.hThread;
    m_impl->stdinWrite = childStdinWrite;
    m_impl->stdoutRead = childStdoutRead;
    return true;
#else
    int stdinPipe[2]{-1, -1};
    int stdoutPipe[2]{-1, -1};

    if (::pipe(stdinPipe) != 0)
    {
      m_impl.reset();
      return false;
    }
    if (::pipe(stdoutPipe) != 0)
    {
      ::close(stdinPipe[0]);
      ::close(stdinPipe[1]);
      m_impl.reset();
      return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0)
    {
      ::close(stdinPipe[0]);
      ::close(stdinPipe[1]);
      ::close(stdoutPipe[0]);
      ::close(stdoutPipe[1]);
      m_impl.reset();
      return false;
    }

    if (pid == 0)
    {
      ::dup2(stdinPipe[0], STDIN_FILENO);
      ::dup2(stdoutPipe[1], STDOUT_FILENO);
      ::dup2(stdoutPipe[1], STDERR_FILENO);

      ::close(stdinPipe[0]);
      ::close(stdinPipe[1]);
      ::close(stdoutPipe[0]);
      ::close(stdoutPipe[1]);

      if (!workingDirectory.empty())
        (void)::chdir(workingDirectory.c_str());

      char *const argv[] = {const_cast<char *>(exePath.c_str()), nullptr};
      ::execv(exePath.c_str(), argv);
      _exit(127);
    }

    ::close(stdinPipe[0]);
    ::close(stdoutPipe[1]);

    m_impl->pid = pid;
    m_impl->stdinWrite = stdinPipe[1];
    m_impl->stdoutRead = stdoutPipe[0];
    return true;
#endif
  }

  void UciEngineProcess::platformStop()
  {
    if (!m_impl)
      return;

#if defined(_WIN32)
    if (m_impl->stdinWrite)
    {
      CloseHandle(m_impl->stdinWrite);
      m_impl->stdinWrite = nullptr;
    }

    if (m_impl->process)
    {
      DWORD wait = WaitForSingleObject(m_impl->process, 400);
      if (wait == WAIT_TIMEOUT)
      {
        TerminateProcess(m_impl->process, 1);
        WaitForSingleObject(m_impl->process, 200);
      }
    }

    if (m_impl->stdoutRead)
    {
      CloseHandle(m_impl->stdoutRead);
      m_impl->stdoutRead = nullptr;
    }
    if (m_impl->thread)
    {
      CloseHandle(m_impl->thread);
      m_impl->thread = nullptr;
    }
    if (m_impl->process)
    {
      CloseHandle(m_impl->process);
      m_impl->process = nullptr;
    }
#else
    if (m_impl->stdinWrite >= 0)
    {
      ::close(m_impl->stdinWrite);
      m_impl->stdinWrite = -1;
    }
    if (m_impl->stdoutRead >= 0)
    {
      ::close(m_impl->stdoutRead);
      m_impl->stdoutRead = -1;
    }

    if (m_impl->pid > 0)
    {
      int status = 0;
      for (int i = 0; i < 40; ++i)
      {
        const pid_t r = ::waitpid(m_impl->pid, &status, WNOHANG);
        if (r == m_impl->pid)
        {
          m_impl->pid = -1;
          break;
        }
        std::this_thread::sleep_for(10ms);
      }

      if (m_impl->pid > 0)
      {
        (void)::kill(m_impl->pid, SIGTERM);
        std::this_thread::sleep_for(100ms);
        if (::waitpid(m_impl->pid, &status, WNOHANG) == 0)
        {
          (void)::kill(m_impl->pid, SIGKILL);
          (void)::waitpid(m_impl->pid, &status, 0);
        }
        m_impl->pid = -1;
      }
    }
#endif

    m_impl.reset();
  }

  bool UciEngineProcess::platformWrite(const std::string &text)
  {
    if (!m_impl)
      return false;

#if defined(_WIN32)
    if (!m_impl->stdinWrite)
      return false;

    DWORD writtenTotal = 0;
    while (writtenTotal < text.size())
    {
      DWORD justWritten = 0;
      const DWORD toWrite = static_cast<DWORD>(text.size() - writtenTotal);
      if (!WriteFile(m_impl->stdinWrite, text.data() + writtenTotal, toWrite, &justWritten, nullptr))
        return false;
      writtenTotal += justWritten;
    }
    return true;
#else
    if (m_impl->stdinWrite < 0)
      return false;

    const char *data = text.data();
    std::size_t remaining = text.size();
    while (remaining > 0)
    {
      const ssize_t n = ::write(m_impl->stdinWrite, data, remaining);
      if (n < 0)
      {
        if (errno == EINTR)
          continue;
        return false;
      }
      data += n;
      remaining -= static_cast<std::size_t>(n);
    }
    return true;
#endif
  }

  bool UciEngineProcess::platformReadLine(std::string &outLine)
  {
    outLine.clear();
    if (!m_impl)
      return false;

    for (;;)
    {
      const auto pos = m_impl->readBuffer.find('\n');
      if (pos != std::string::npos)
      {
        outLine = m_impl->readBuffer.substr(0, pos + 1);
        m_impl->readBuffer.erase(0, pos + 1);
        return true;
      }

#if defined(_WIN32)
      if (!m_impl->stdoutRead)
        return false;

      char buffer[4096];
      DWORD read = 0;
      const BOOL ok = ReadFile(m_impl->stdoutRead, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr);
      if (!ok || read == 0)
      {
        if (!m_impl->readBuffer.empty())
        {
          outLine = std::move(m_impl->readBuffer);
          m_impl->readBuffer.clear();
          return true;
        }
        return false;
      }
      m_impl->readBuffer.append(buffer, buffer + read);
#else
      if (m_impl->stdoutRead < 0)
        return false;

      char buffer[4096];
      const ssize_t n = ::read(m_impl->stdoutRead, buffer, sizeof(buffer));
      if (n < 0)
      {
        if (errno == EINTR)
          continue;
        return false;
      }
      if (n == 0)
      {
        if (!m_impl->readBuffer.empty())
        {
          outLine = std::move(m_impl->readBuffer);
          m_impl->readBuffer.clear();
          return true;
        }
        return false;
      }
      m_impl->readBuffer.append(buffer, buffer + n);
#endif
    }
  }
}
