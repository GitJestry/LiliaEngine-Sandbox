#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "lilia/model/analysis/config/start_config.hpp"

class UciEngineProcess
{
public:
  struct Id
  {
    std::string name, author;
  };

  UciEngineProcess() = default;
  ~UciEngineProcess(); // out-of-line semantics via custom deleter (safe with incomplete Impl)

  UciEngineProcess(const UciEngineProcess &) = delete;
  UciEngineProcess &operator=(const UciEngineProcess &) = delete;
  UciEngineProcess(UciEngineProcess &&) = delete;
  UciEngineProcess &operator=(UciEngineProcess &&) = delete;

  bool start(const std::string &exePath);
  void stop();

  bool uciHandshake(Id &outId, std::vector<lilia::config::UciOption> &outOptions);

  void setOption(const std::string &name, const lilia::config::UciValue &v);
  void newGame();

  void position(const std::string &fen, const std::vector<std::string> &movesUci);
  void goTime(int wtimeMs, int btimeMs, int wincMs, int bincMs);
  void goFixedMovetime(int movetimeMs);
  void goFixedDepth(int depth);
  void stopSearch();

  std::string waitBestmove();

  static bool parseUciOptionLine(const std::string &line, lilia::config::UciOption &out);
  static std::string serializeOptionLine(const lilia::config::UciOption &opt);

private:
  void sendLine(const std::string &line);
  void readerLoop();

  bool platformStart(const std::string &exePath);
  void platformStop();

  bool platformWrite(const std::string &s);
  bool platformReadLine(std::string &outLine);

private:
  std::thread m_reader;
  std::atomic_bool m_running{false};

  std::mutex m_mtx;
  std::condition_variable m_cvLines;
  std::condition_variable m_cvBest;

  std::deque<std::string> m_lines;
  std::deque<std::string> m_bestmoves;

  struct Impl;

  struct ImplDeleter
  {
    void operator()(Impl *p) noexcept; // defined in platform .cpp where Impl is complete
  };

  std::unique_ptr<Impl, ImplDeleter> m_impl;
};
