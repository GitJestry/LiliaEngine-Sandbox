#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "lilia/app/domain/analysis/config/start_config.hpp"

namespace lilia::app::engines
{
  class UciEngineProcess
  {
  public:
    struct Id
    {
      std::string name;
      std::string author;
    };

    UciEngineProcess() = default;
    ~UciEngineProcess();

    UciEngineProcess(const UciEngineProcess &) = delete;
    UciEngineProcess &operator=(const UciEngineProcess &) = delete;
    UciEngineProcess(UciEngineProcess &&) = delete;
    UciEngineProcess &operator=(UciEngineProcess &&) = delete;

    bool start(const std::string &exePath);
    bool start(const std::string &exePath, const std::string &workingDirectory);
    void stop();

    bool uciHandshake(Id &outId, std::vector<domain::analysis::config::UciOption> &outOptions);

    void setOption(const std::string &name, const domain::analysis::config::UciValue &value);
    void newGame();

    void position(const std::string &fen, const std::vector<std::string> &movesUci);
    void goTime(int wtimeMs, int btimeMs, int wincMs, int bincMs);
    void goFixedMovetime(int movetimeMs);
    void goFixedDepth(int depth);
    void stopSearch();

    std::string waitBestmove();

    static bool parseUciOptionLine(const std::string &line, domain::analysis::config::UciOption &out);
    static std::string serializeOptionLine(const domain::analysis::config::UciOption &opt);

  private:
    void sendLine(const std::string &line);
    void readerLoop();

    bool platformStart(const std::string &exePath, const std::string &workingDirectory);
    void platformStop();
    bool platformWrite(const std::string &text);
    bool platformReadLine(std::string &outLine);

  private:
    std::thread m_reader;
    std::atomic_bool m_running{false};

    std::mutex m_mutex;
    std::condition_variable m_cvLines;
    std::condition_variable m_cvBest;

    std::deque<std::string> m_lines;
    std::deque<std::string> m_bestmoves;

    struct Impl;
    struct ImplDeleter
    {
      void operator()(Impl *p) noexcept;
    };

    std::unique_ptr<Impl, ImplDeleter> m_impl;
  };
}
