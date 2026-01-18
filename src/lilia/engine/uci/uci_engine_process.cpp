#include "lilia/engine/uci/uci_engine_process.hpp"

#include <sstream>
#include <chrono>

using namespace std::chrono_literals;

static bool starts_with(const std::string &s, const char *pfx)
{
  return s.rfind(pfx, 0) == 0;
}

UciEngineProcess::~UciEngineProcess()
{
  stop();
}

bool UciEngineProcess::start(const std::string &exePath)
{
  stop();
  if (!platformStart(exePath))
    return false;

  m_running.store(true);
  m_reader = std::thread([this]
                         { readerLoop(); });
  return true;
}

void UciEngineProcess::stop()
{
  if (!m_running.exchange(false))
    return;

  // best-effort graceful shutdown
  sendLine("quit");
  platformStop();

  if (m_reader.joinable())
    m_reader.join();

  std::lock_guard lk(m_mtx);
  m_lines.clear();
  m_bestmoves.clear();
}

void UciEngineProcess::sendLine(const std::string &line)
{
  // UCI requires \n; many engines tolerate \r\n.
  platformWrite(line + "\n");
}

void UciEngineProcess::readerLoop()
{
  while (m_running.load())
  {
    std::string line;
    if (!platformReadLine(line))
      break;

    // Normalize CRLF
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
      line.pop_back();

    {
      std::lock_guard lk(m_mtx);
      m_lines.push_back(line);
      if (starts_with(line, "bestmove "))
        m_bestmoves.push_back(line);
    }
    m_cvLines.notify_all();
    m_cvBest.notify_all();
  }
}

static bool wait_for_prefix(std::deque<std::string> &q, const char *pfx, std::string &out)
{
  for (auto it = q.begin(); it != q.end(); ++it)
  {
    if (starts_with(*it, pfx))
    {
      out = *it;
      q.erase(it);
      return true;
    }
  }
  return false;
}

bool UciEngineProcess::uciHandshake(Id &outId, std::vector<lilia::config::UciOption> &outOptions)
{
  outId = {};
  outOptions.clear();

  sendLine("uci");

  auto deadline = std::chrono::steady_clock::now() + 3s;

  for (;;)
  {
    std::unique_lock lk(m_mtx);
    m_cvLines.wait_until(lk, deadline, [&]
                         { return !m_lines.empty() || !m_running.load(); });

    while (!m_lines.empty())
    {
      std::string line = std::move(m_lines.front());
      m_lines.pop_front();

      if (starts_with(line, "id name "))
        outId.name = line.substr(std::string("id name ").size());
      else if (starts_with(line, "id author "))
        outId.author = line.substr(std::string("id author ").size());
      else if (starts_with(line, "option "))
      {
        lilia::config::UciOption opt;
        if (parseUciOptionLine(line, opt))
          outOptions.push_back(std::move(opt));
      }
      else if (line == "uciok")
      {
        lk.unlock();
        sendLine("isready");

        // wait readyok
        auto readyDeadline = std::chrono::steady_clock::now() + 2s;
        for (;;)
        {
          std::unique_lock lk2(m_mtx);
          m_cvLines.wait_until(lk2, readyDeadline, [&]
                               { return !m_lines.empty() || !m_running.load(); });
          while (!m_lines.empty())
          {
            std::string l2 = std::move(m_lines.front());
            m_lines.pop_front();
            if (l2 == "readyok")
              return true;
          }
          if (std::chrono::steady_clock::now() > readyDeadline)
            return false;
        }
      }
    }

    if (std::chrono::steady_clock::now() > deadline)
      return false;
  }
}

void UciEngineProcess::setOption(const std::string &name, const lilia::config::UciValue &v)
{
  std::ostringstream os;
  os << "setoption name " << name << " value ";
  if (std::holds_alternative<bool>(v))
    os << (std::get<bool>(v) ? "true" : "false");
  else if (std::holds_alternative<int>(v))
    os << std::get<int>(v);
  else
    os << std::get<std::string>(v);
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
    for (auto &m : movesUci)
      os << " " << m;
  }
  sendLine(os.str());
}

void UciEngineProcess::goTime(int wtimeMs, int btimeMs, int wincMs, int bincMs)
{
  std::ostringstream os;
  os << "go wtime " << wtimeMs << " btime " << btimeMs
     << " winc " << wincMs << " binc " << bincMs;
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
  std::unique_lock lk(m_mtx);
  m_cvBest.wait(lk, [&]
                { return !m_bestmoves.empty() || !m_running.load(); });
  if (m_bestmoves.empty())
    return {};
  std::string line = std::move(m_bestmoves.front());
  m_bestmoves.pop_front();
  return line;
}

// ---- UCI option parsing ----
// Handles: option name <...> type <check|spin|combo|string|button> default ... [min/max/var]
bool UciEngineProcess::parseUciOptionLine(const std::string &line, lilia::config::UciOption &out)
{
  if (!starts_with(line, "option "))
    return false;

  const std::string keyName = "option name ";
  const std::string keyType = " type ";

  auto namePos = line.find(keyName);
  if (namePos == std::string::npos)
    return false;

  auto typePos = line.find(keyType, namePos + keyName.size());
  if (typePos == std::string::npos)
    return false;

  out = {};
  out.name = line.substr(namePos + keyName.size(), typePos - (namePos + keyName.size()));

  std::string rest = line.substr(typePos + keyType.size());
  std::istringstream iss(rest);

  std::string typeTok;
  if (!(iss >> typeTok))
    return false;

  using T = lilia::config::UciOption::Type;
  if (typeTok == "check")
    out.type = T::Check;
  else if (typeTok == "spin")
    out.type = T::Spin;
  else if (typeTok == "combo")
    out.type = T::Combo;
  else if (typeTok == "string")
    out.type = T::String;
  else if (typeTok == "button")
    out.type = T::Button;
  else
    out.type = T::String;

  // Parse remaining key/value-ish tokens
  std::string tok;
  while (iss >> tok)
  {
    if (tok == "default")
    {
      if (out.type == T::Check)
      {
        std::string v;
        if (!(iss >> v))
          break;
        out.defaultBool = (v == "true");
      }
      else if (out.type == T::Spin)
      {
        int v = 0;
        if (!(iss >> v))
          break;
        out.defaultInt = v;
      }
      else
      {
        // string/combo/button default token can contain no spaces in UCI spec
        std::string v;
        if (!(iss >> v))
          break;
        out.defaultStr = v;
      }
    }
    else if (tok == "min")
    {
      int v = 0;
      if (!(iss >> v))
        break;
      out.min = v;
    }
    else if (tok == "max")
    {
      int v = 0;
      if (!(iss >> v))
        break;
      out.max = v;
    }
    else if (tok == "var")
    {
      std::string v;
      if (!(iss >> v))
        break;
      out.vars.push_back(v);
    }
    else
    {
      // ignore unrecognized tokens
    }
  }
  return true;
}

// Serialize a cached option back to a synthetic "option ..." line for db persistence
std::string UciEngineProcess::serializeOptionLine(const lilia::config::UciOption &opt)
{
  using T = lilia::config::UciOption::Type;
  std::ostringstream os;
  os << "option name " << opt.name << " type ";
  switch (opt.type)
  {
  case T::Check:
    os << "check default " << (opt.defaultBool ? "true" : "false");
    break;
  case T::Spin:
    os << "spin default " << opt.defaultInt << " min " << opt.min << " max " << opt.max;
    break;
  case T::Combo:
    os << "combo default " << opt.defaultStr;
    for (auto &v : opt.vars)
      os << " var " << v;
    break;
  case T::String:
    os << "string default " << opt.defaultStr;
    break;
  case T::Button:
    os << "button";
    break;
  }
  return os.str();
}
