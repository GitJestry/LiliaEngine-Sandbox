#include "lilia/engine/uci/engine_registry.hpp"

#include <fstream>
#include <sstream>
#include <chrono>
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#endif

namespace lilia::engine::uci
{
  namespace fs = std::filesystem;

  static std::string trim(std::string s)
  {
    auto issp = [](unsigned char c)
    { return std::isspace(c); };
    while (!s.empty() && issp((unsigned char)s.front()))
      s.erase(s.begin());
    while (!s.empty() && issp((unsigned char)s.back()))
      s.pop_back();
    return s;
  }

  EngineRegistry &EngineRegistry::instance()
  {
    static EngineRegistry g;
    return g;
  }

  fs::path EngineRegistry::userDataDir() const
  {
#if defined(_WIN32)
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p)) && p)
    {
      fs::path base(p);
      CoTaskMemFree(p);
      return base / "Lilia";
    }
    // fallback
    char *appdata = std::getenv("APPDATA");
    return appdata ? (fs::path(appdata) / "Lilia") : (fs::temp_directory_path() / "Lilia");
#elif defined(__APPLE__)
    const char *home = std::getenv("HOME");
    fs::path h = home ? fs::path(home) : fs::temp_directory_path();
    return h / "Library" / "Application Support" / "Lilia";
#else
    const char *xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg)
      return fs::path(xdg) / "lilia";
    const char *home = std::getenv("HOME");
    fs::path h = home ? fs::path(home) : fs::temp_directory_path();
    return h / ".local" / "share" / "lilia";
#endif
  }

  fs::path EngineRegistry::enginesDir() const
  {
    return userDataDir() / "engines";
  }

  fs::path EngineRegistry::dbPath() const
  {
    return userDataDir() / "engines.db";
  }

  static void ensureDir(const fs::path &p)
  {
    std::error_code ec;
    fs::create_directories(p, ec);
  }

  // Simple stable id: filename + last_write_time count (good enough for local installs)
  std::string EngineRegistry::makeStableIdFromPath(const fs::path &p)
  {
    std::error_code ec;
    auto ft = fs::last_write_time(p, ec);
    auto fname = p.filename().string();
    std::uint64_t stamp = 0;
    if (!ec)
    {
      stamp = (std::uint64_t)ft.time_since_epoch().count();
    }
    std::ostringstream os;
    os << fname << "_" << stamp;
    std::string id = os.str();
    for (char &c : id)
      if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-'))
        c = '_';
    return id;
  }

  void EngineRegistry::load()
  {
    m_entries.clear();
    ensureDir(userDataDir());
    ensureDir(enginesDir());

    std::ifstream in(dbPath());
    if (!in.good())
      return;

    EngineEntry cur{};
    std::string curId;
    bool inBlock = false;

    auto flush = [&]()
    {
      if (!inBlock || curId.empty())
        return;
      m_entries[curId] = cur;
      cur = EngineEntry{};
      curId.clear();
      inBlock = false;
    };

    std::string line;
    while (std::getline(in, line))
    {
      line = trim(line);
      if (line.empty())
        continue;

      if (line.rfind("[engine ", 0) == 0 && line.back() == ']')
      {
        flush();
        curId = line.substr(std::string("[engine ").size());
        curId.pop_back(); // ]
        inBlock = true;
        cur.ref.engineId = curId;
        continue;
      }

      if (!inBlock)
        continue;

      auto eq = line.find('=');
      if (eq == std::string::npos)
        continue;

      std::string k = trim(line.substr(0, eq));
      std::string v = trim(line.substr(eq + 1));

      if (k == "builtin")
        cur.builtin = (v == "1");
      else if (k == "exe")
        cur.ref.executablePath = v;
      else if (k == "name")
        cur.ref.displayName = v;
      else if (k == "version")
        cur.ref.version = v;
      else if (k == "id_name")
        cur.id.name = v;
      else if (k == "id_author")
        cur.id.author = v;
      // Options are cached in a minimal format: one line per option, prefixed "opt:"
      else if (k.rfind("opt:", 0) == 0)
      {
        // v is raw UCI "option ..." line; parse it back
        lilia::config::UciOption opt;
        if (UciEngineProcess::parseUciOptionLine(v, opt))
          cur.options.push_back(std::move(opt));
      }
    }
    flush();
  }

  void EngineRegistry::save() const
  {
    ensureDir(userDataDir());
    ensureDir(enginesDir());

    std::ofstream out(dbPath(), std::ios::trunc);
    if (!out.good())
      return;

    for (const auto &[id, e] : m_entries)
    {
      out << "[engine " << id << "]\n";
      out << "builtin=" << (e.builtin ? "1" : "0") << "\n";
      out << "exe=" << e.ref.executablePath << "\n";
      out << "name=" << e.ref.displayName << "\n";
      out << "version=" << e.ref.version << "\n";
      out << "id_name=" << e.id.name << "\n";
      out << "id_author=" << e.id.author << "\n";
      for (const auto &opt : e.options)
      {
        // store as re-hydratable single line: "option name ... type ..."
        out << "opt:" << opt.name << "=" << UciEngineProcess::serializeOptionLine(opt) << "\n";
      }
      out << "\n";
    }
  }

  void EngineRegistry::ensureBuiltin(const std::string &engineId,
                                     const std::string &displayName,
                                     const std::string &version,
                                     const fs::path &exePath)
  {
    EngineEntry e{};
    e.builtin = true;
    e.ref.builtin = true;
    e.ref.engineId = engineId;
    e.ref.displayName = displayName;
    e.ref.version = version;
    e.ref.executablePath = exePath.string();

    // If not present, probe once to cache options; if present, keep existing cache.
    auto it = m_entries.find(engineId);
    if (it == m_entries.end())
    {
      UciEngineProcess proc;
      if (proc.start(e.ref.executablePath))
      {
        proc.uciHandshake(e.id, e.options);
        proc.stop();
      }
      m_entries[engineId] = std::move(e);
      save();
    }
    else
    {
      // Keep entry but refresh path/display fields (useful after updates)
      it->second.ref.executablePath = e.ref.executablePath;
      it->second.ref.displayName = displayName;
      it->second.ref.version = version;
      it->second.ref.builtin = true;
      it->second.builtin = true;
      save();
    }
  }

  std::optional<EngineEntry> EngineRegistry::installExternal(const fs::path &sourceExePath,
                                                             std::string *outError)
  {
    // Ensure we are working with an up-to-date registry snapshot.
    load();

    if (!fs::exists(sourceExePath))
    {
      if (outError)
        *outError = "Selected engine does not exist.";
      return std::nullopt;
    }

    // If this executable is already registered (builtin or external), reuse that entry.
    auto canonicalStr = [](const fs::path &p) -> std::string
    {
      std::error_code ec;
      fs::path c = fs::weakly_canonical(p, ec);
      if (ec)
        return p.lexically_normal().string();
      return c.string();
    };

    const std::string srcCanon = canonicalStr(sourceExePath);

    for (const auto &[id, existing] : m_entries)
    {
      if (existing.ref.executablePath.empty())
        continue;

      const std::string exCanon = canonicalStr(fs::path(existing.ref.executablePath));
      if (!exCanon.empty() && exCanon == srcCanon)
      {
        // Do not create a new timestamp-id entry.
        return existing;
      }
    }

    // ---- existing code continues here (copy + probe + save) ----
    ensureDir(enginesDir());

    std::string id = makeStableIdFromPath(sourceExePath);
    fs::path dstDir = enginesDir() / id;
    ensureDir(dstDir);

    fs::path dst = dstDir / sourceExePath.filename();

    std::error_code ec;
    fs::copy_file(sourceExePath, dst, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
      if (outError)
        *outError = "Failed to copy engine binary.";
      return std::nullopt;
    }

#if !defined(_WIN32)
    // ensure executable bit
    fs::permissions(dst,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add,
                    ec);
#endif

    EngineEntry e{};
    e.builtin = false;
    e.ref.builtin = false;
    e.ref.engineId = id;
    e.ref.executablePath = dst.string();
    e.ref.displayName = sourceExePath.filename().string();
    e.ref.version = "unknown";

    UciEngineProcess proc;
    if (!proc.start(e.ref.executablePath))
    {
      if (outError)
        *outError = "Failed to start engine process.";
      return std::nullopt;
    }

    if (!proc.uciHandshake(e.id, e.options))
    {
      proc.stop();
      if (outError)
        *outError = "Engine did not respond as a valid UCI engine.";
      return std::nullopt;
    }

    proc.stop();

    // Prefer the engine's reported name if available
    if (!e.id.name.empty())
      e.ref.displayName = e.id.name;

    m_entries[id] = e;
    save();
    return e;
  }

  std::vector<EngineEntry> EngineRegistry::list() const
  {
    std::vector<EngineEntry> out;
    out.reserve(m_entries.size());
    for (const auto &kv : m_entries)
      out.push_back(kv.second);
    return out;
  }

  std::optional<EngineEntry> EngineRegistry::get(const std::string &engineId) const
  {
    auto it = m_entries.find(engineId);
    if (it == m_entries.end())
      return std::nullopt;
    return it->second;
  }

  lilia::config::BotConfig EngineRegistry::makeDefaultBotConfig(const std::string &engineId) const
  {
    lilia::config::BotConfig bc{};
    auto e = get(engineId);
    if (!e)
      return bc;

    bc.engine = e->ref;

    // conservative defaults (you can tune later)
    bc.limits.movetimeMs = 500; // default fast move
    bc.limits.depth.reset();

    for (const auto &opt : e->options)
    {
      switch (opt.type)
      {
      case lilia::config::UciOption::Type::Check:
        bc.uciValues[opt.name] = opt.defaultBool;
        break;
      case lilia::config::UciOption::Type::Spin:
        bc.uciValues[opt.name] = opt.defaultInt;
        break;
      case lilia::config::UciOption::Type::Combo:
      case lilia::config::UciOption::Type::String:
      case lilia::config::UciOption::Type::Button:
      default:
        bc.uciValues[opt.name] = opt.defaultStr;
        break;
      }
    }
    return bc;
  }
} // namespace lilia::engine::uci
