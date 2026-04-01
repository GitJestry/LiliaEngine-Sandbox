#include "lilia/app/engines/engine_registry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#include <shlobj.h>
#include <windows.h>
#endif

namespace lilia::app::engines
{
  namespace fs = std::filesystem;

  namespace
  {
    std::string trim(std::string s)
    {
      auto isSpace = [](unsigned char c)
      { return std::isspace(c) != 0; };

      while (!s.empty() && isSpace(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
      while (!s.empty() && isSpace(static_cast<unsigned char>(s.back())))
        s.pop_back();
      return s;
    }

    std::string escapeValue(const std::string &value)
    {
      std::string out;
      out.reserve(value.size());
      for (char c : value)
      {
        switch (c)
        {
        case '\\':
          out += "\\\\";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        default:
          out.push_back(c);
          break;
        }
      }
      return out;
    }

    std::string unescapeValue(const std::string &value)
    {
      std::string out;
      out.reserve(value.size());
      bool escape = false;
      for (char c : value)
      {
        if (!escape)
        {
          if (c == '\\')
          {
            escape = true;
            continue;
          }
          out.push_back(c);
          continue;
        }

        switch (c)
        {
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        default:
          out.push_back(c);
          break;
        }
        escape = false;
      }
      if (escape)
        out.push_back('\\');
      return out;
    }

    bool writeTextFileAtomic(const fs::path &path, const std::string &content)
    {
      const fs::path tmp = path.string() + ".tmp";
      {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.good())
          return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out.good())
          return false;
      }

      std::error_code ec;
      fs::rename(tmp, path, ec);
      if (!ec)
        return true;

      fs::remove(path, ec);
      ec.clear();
      fs::rename(tmp, path, ec);
      if (ec)
      {
        fs::remove(tmp, ec);
        return false;
      }
      return true;
    }

    std::map<std::string, std::string> readProperties(const fs::path &path)
    {
      std::map<std::string, std::string> out;
      std::ifstream in(path, std::ios::binary);
      if (!in.good())
        return out;

      std::string line;
      while (std::getline(in, line))
      {
        if (line.empty() || line[0] == '#')
          continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos)
          continue;

        const std::string key = trim(line.substr(0, eq));
        const std::string value = unescapeValue(line.substr(eq + 1));
        if (!key.empty())
          out[key] = value;
      }
      return out;
    }

    std::string writeProperties(const std::map<std::string, std::string> &props)
    {
      std::ostringstream os;
      for (const auto &[k, v] : props)
        os << k << '=' << escapeValue(v) << '\n';
      return os.str();
    }

    std::vector<std::string> readEscapedLines(const fs::path &path)
    {
      std::vector<std::string> out;
      std::ifstream in(path, std::ios::binary);
      if (!in.good())
        return out;

      std::string line;
      while (std::getline(in, line))
      {
        if (line.empty())
          continue;
        out.push_back(unescapeValue(line));
      }
      return out;
    }

    std::string writeEscapedLines(const std::vector<std::string> &lines)
    {
      std::ostringstream os;
      for (const auto &line : lines)
        os << escapeValue(line) << '\n';
      return os.str();
    }

    fs::path weakCanonicalOrNormal(const fs::path &p)
    {
      std::error_code ec;
      const fs::path c = fs::weakly_canonical(p, ec);
      if (ec)
        return p.lexically_normal();
      return c;
    }

    bool setExecutableBitIfNeeded(const fs::path &p)
    {
#if defined(_WIN32)
      (void)p;
      return true;
#else
      std::error_code ec;
      fs::permissions(p,
                      fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                      fs::perm_options::add,
                      ec);
      return !ec;
#endif
    }

    std::string hex64(std::uint64_t v)
    {
      std::ostringstream os;
      os << std::hex << std::setfill('0') << std::setw(16) << v;
      return os.str();
    }
  } // namespace

  EngineRegistry &EngineRegistry::instance()
  {
    static EngineRegistry g_registry;
    return g_registry;
  }

  fs::path EngineRegistry::userDataDir() const
  {
#if defined(_WIN32)
    PWSTR roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) && roaming)
    {
      fs::path base(roaming);
      CoTaskMemFree(roaming);
      return base / "Lilia";
    }

    if (const char *appdata = std::getenv("APPDATA"))
      return fs::path(appdata) / "Lilia";
    return fs::temp_directory_path() / "Lilia";
#elif defined(__APPLE__)
    if (const char *home = std::getenv("HOME"))
      return fs::path(home) / "Library" / "Application Support" / "Lilia";
    return fs::temp_directory_path() / "Lilia";
#else
    if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
      return fs::path(xdg) / "lilia";
    if (const char *home = std::getenv("HOME"))
      return fs::path(home) / ".local" / "share" / "lilia";
    return fs::temp_directory_path() / "lilia";
#endif
  }

  fs::path EngineRegistry::catalogDir() const
  {
    return userDataDir() / "engines" / "catalog";
  }

  fs::path EngineRegistry::artifactsDir() const
  {
    return userDataDir() / "engines" / "artifacts";
  }

  fs::path EngineRegistry::enginesDir() const
  {
    return artifactsDir();
  }

  void EngineRegistry::ensureDirectories() const
  {
    std::error_code ec;
    fs::create_directories(catalogDir(), ec);
    ec.clear();
    fs::create_directories(artifactsDir(), ec);
  }

  std::filesystem::path EngineRegistry::engineCatalogPath(const std::string &engineId) const
  {
    return catalogDir() / engineId;
  }

  std::string EngineRegistry::currentPlatformTag()
  {
#if defined(_WIN32)
    const char *os = "windows";
#elif defined(__APPLE__)
    const char *os = "macos";
#else
    const char *os = "linux";
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    const char *arch = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    const char *arch = "x64";
#elif defined(__i386__) || defined(_M_IX86)
    const char *arch = "x86";
#else
    const char *arch = "unknown";
#endif

    return std::string(os) + "-" + arch;
  }

  std::string EngineRegistry::slugify(const std::string &s)
  {
    std::string out;
    bool lastDash = false;

    for (char c : s)
    {
      const unsigned char uc = static_cast<unsigned char>(c);
      if (std::isalnum(uc))
      {
        out.push_back(static_cast<char>(std::tolower(uc)));
        lastDash = false;
      }
      else if (!lastDash)
      {
        out.push_back('-');
        lastDash = true;
      }
    }

    while (!out.empty() && out.front() == '-')
      out.erase(out.begin());
    while (!out.empty() && out.back() == '-')
      out.pop_back();

    return out.empty() ? "engine" : out;
  }

  std::string EngineRegistry::chooseIconKey(const std::string &displayName, bool builtin)
  {
    std::string lower;
    lower.reserve(displayName.size());
    for (char c : displayName)
      lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    if (lower.find("stockfish") != std::string::npos)
      return "stockfish";
    if (lower.find("lilia") != std::string::npos)
      return "lilia";
    if (lower.find("komodo") != std::string::npos)
      return "dragon";
    if (lower.find("lc0") != std::string::npos || lower.find("leela") != std::string::npos)
      return "neural";
    if (builtin)
      return "builtin";
    return "engine";
  }

  std::string EngineRegistry::fingerprintPathQuick(const fs::path &p)
  {
    std::error_code ec;

    const std::uint64_t size = [&]() -> std::uint64_t
    {
      auto s = fs::file_size(p, ec);
      if (ec)
        return 0;
      return static_cast<std::uint64_t>(s);
    }();

    ec.clear();

    const std::uint64_t time = [&]() -> std::uint64_t
    {
      auto ft = fs::last_write_time(p, ec);
      if (ec)
        return 0;
      return static_cast<std::uint64_t>(ft.time_since_epoch().count());
    }();

    std::ostringstream os;
    os << slugify(p.filename().string()) << '-'
       << std::hex << size << '-'
       << std::hex << time;
    return os.str();
  }

  std::string EngineRegistry::fingerprintFile(const fs::path &p)
  {
    std::ifstream in(p, std::ios::binary);
    if (!in.good())
      return fingerprintPathQuick(p);

    constexpr std::uint64_t offset = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;

    std::uint64_t hash = offset;
    char buffer[64 * 1024];

    while (in.good())
    {
      in.read(buffer, sizeof(buffer));
      const std::streamsize n = in.gcount();
      for (std::streamsize i = 0; i < n; ++i)
      {
        hash ^= static_cast<unsigned char>(buffer[i]);
        hash *= prime;
      }
    }

    return hex64(hash);
  }

  bool EngineRegistry::probeEngine(const fs::path &entryExecutable,
                                   const fs::path &workingDirectory,
                                   UciEngineProcess::Id &outId,
                                   std::vector<domain::analysis::config::UciOption> &outOptions,
                                   std::string *outError)
  {
    UciEngineProcess proc;
    if (!proc.start(entryExecutable.string(), workingDirectory.string()))
    {
      if (outError)
        *outError = "Failed to start engine process.";
      return false;
    }

    const bool ok = proc.uciHandshake(outId, outOptions);
    proc.stop();

    if (!ok && outError)
      *outError = "Engine did not answer a valid UCI handshake.";

    return ok;
  }

  std::optional<EngineEntry> EngineRegistry::makeEntryForPlatform(const EngineRecord &record, const std::string &platformTag)
  {
    const auto it = record.installs.find(platformTag);
    if (it == record.installs.end())
      return std::nullopt;

    const EngineInstallRecord &install = it->second;
    if (install.entryExecutable.empty())
      return std::nullopt;

    EngineEntry entry{};
    entry.ref.engineId = record.engineId;
    entry.ref.displayName = record.displayName;
    entry.ref.version = record.version;
    entry.ref.executablePath = install.entryExecutable.string();
    entry.ref.builtin = install.builtin;

    entry.id = record.uciId;
    entry.options = record.options;
    entry.iconKey = record.iconKey;
    entry.artifactId = install.artifactId;
    entry.workingDirectory = install.workingDirectory;
    entry.builtin = install.builtin;
    return entry;
  }

  void EngineRegistry::load()
  {
    m_records.clear();
    ensureDirectories();

    std::error_code ec;
    if (!fs::exists(catalogDir(), ec))
      return;

    for (const auto &dirEntry : fs::directory_iterator(catalogDir(), ec))
    {
      if (ec)
        break;
      if (!dirEntry.is_directory())
        continue;

      EngineRecord record{};
      const fs::path dir = dirEntry.path();

      const auto props = readProperties(dir / "engine.properties");
      record.engineId = props.count("engine_id") ? props.at("engine_id") : dir.filename().string();
      record.displayName = props.count("display_name") ? props.at("display_name") : record.engineId;
      record.version = props.count("version") ? props.at("version") : "unknown";
      record.iconKey = props.count("icon_key") ? props.at("icon_key") : chooseIconKey(record.displayName, false);
      record.builtin = props.count("builtin") && props.at("builtin") == "1";
      record.uciId.name = props.count("uci_name") ? props.at("uci_name") : std::string{};
      record.uciId.author = props.count("uci_author") ? props.at("uci_author") : std::string{};

      const auto optionLines = readEscapedLines(dir / "options.txt");
      for (const auto &line : optionLines)
      {
        domain::analysis::config::UciOption opt;
        if (UciEngineProcess::parseUciOptionLine(line, opt))
          record.options.push_back(std::move(opt));
      }

      for (const auto &fileEntry : fs::directory_iterator(dir, ec))
      {
        if (ec)
          break;
        if (!fileEntry.is_regular_file())
          continue;

        const std::string name = fileEntry.path().filename().string();
        if (name.rfind("install_", 0) != 0 || fileEntry.path().extension() != ".properties")
          continue;

        const auto installProps = readProperties(fileEntry.path());
        EngineInstallRecord install{};
        install.platformTag = installProps.count("platform") ? installProps.at("platform") : std::string{};
        install.artifactId = installProps.count("artifact_id") ? installProps.at("artifact_id") : std::string{};
        install.entryExecutable = installProps.count("entry_executable") ? fs::path(installProps.at("entry_executable")) : fs::path{};
        install.workingDirectory = installProps.count("working_directory") ? fs::path(installProps.at("working_directory")) : fs::path{};
        install.builtin = installProps.count("builtin") && installProps.at("builtin") == "1";

        if (!install.platformTag.empty())
          record.installs[install.platformTag] = std::move(install);
      }

      if (!record.engineId.empty())
        m_records[record.engineId] = std::move(record);
    }
  }

  void EngineRegistry::saveRecord(const EngineRecord &record) const
  {
    const fs::path dir = engineCatalogPath(record.engineId);
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::map<std::string, std::string> props;
    props["engine_id"] = record.engineId;
    props["display_name"] = record.displayName;
    props["version"] = record.version;
    props["icon_key"] = record.iconKey;
    props["builtin"] = record.builtin ? "1" : "0";
    props["uci_name"] = record.uciId.name;
    props["uci_author"] = record.uciId.author;
    (void)writeTextFileAtomic(dir / "engine.properties", writeProperties(props));

    std::vector<std::string> optionLines;
    optionLines.reserve(record.options.size());
    for (const auto &opt : record.options)
      optionLines.push_back(UciEngineProcess::serializeOptionLine(opt));
    (void)writeTextFileAtomic(dir / "options.txt", writeEscapedLines(optionLines));

    for (const auto &[platformTag, install] : record.installs)
    {
      std::map<std::string, std::string> installProps;
      installProps["platform"] = install.platformTag;
      installProps["artifact_id"] = install.artifactId;
      installProps["entry_executable"] = install.entryExecutable.string();
      installProps["working_directory"] = install.workingDirectory.string();
      installProps["builtin"] = install.builtin ? "1" : "0";

      const fs::path file = dir / ("install_" + platformTag + ".properties");
      (void)writeTextFileAtomic(file, writeProperties(installProps));
    }
  }

  void EngineRegistry::save() const
  {
    ensureDirectories();
    for (const auto &[engineId, record] : m_records)
      saveRecord(record);
  }

  void EngineRegistry::ensureBuiltin(const std::string &engineId,
                                     const std::string &displayName,
                                     const std::string &version,
                                     const fs::path &entryExecutable,
                                     const std::string &iconKey)
  {
    ensureDirectories();

    const fs::path exePath = weakCanonicalOrNormal(entryExecutable);
    if (!fs::exists(exePath))
      return;

    const std::string platform = currentPlatformTag();
    const std::string artifactId = "builtin-" + fingerprintFile(exePath);

    auto &record = m_records[engineId];
    if (record.engineId.empty())
      record.engineId = engineId;

    record.displayName = displayName;
    record.version = version;
    record.iconKey = iconKey.empty() ? chooseIconKey(displayName, true) : iconKey;
    record.builtin = true;

    EngineInstallRecord install{};
    install.platformTag = platform;
    install.artifactId = artifactId;
    install.entryExecutable = exePath;
    install.workingDirectory = exePath.parent_path();
    install.builtin = true;

    const auto existingInstallIt = record.installs.find(platform);
    const bool needProbe = existingInstallIt == record.installs.end() ||
                           existingInstallIt->second.artifactId != artifactId ||
                           record.options.empty() ||
                           record.uciId.name.empty();

    record.installs[platform] = install;

    if (needProbe)
    {
      std::string error;
      UciEngineProcess::Id probedId{};
      std::vector<domain::analysis::config::UciOption> probedOptions;
      if (probeEngine(exePath, install.workingDirectory, probedId, probedOptions, &error))
      {
        record.uciId = std::move(probedId);
        record.options = std::move(probedOptions);
      }
    }

    saveRecord(record);
  }

  std::optional<EngineEntry> EngineRegistry::installExternal(const fs::path &sourceEntryExecutable,
                                                             std::string *outError)
  {
    ensureDirectories();

    const fs::path source = weakCanonicalOrNormal(sourceEntryExecutable);
    if (!fs::exists(source) || !fs::is_regular_file(source))
    {
      if (outError)
        *outError = "Selected engine executable does not exist.";
      return std::nullopt;
    }

    const std::string platform = currentPlatformTag();
    const std::string fileFingerprint = fingerprintFile(source);
    const std::string artifactId = "artifact-" + fileFingerprint;

    for (const auto &[id, record] : m_records)
    {
      auto it = record.installs.find(platform);
      if (it != record.installs.end() && it->second.artifactId == artifactId)
        return makeEntryForPlatform(record, platform);
    }

    const fs::path artifactRoot = artifactsDir() / artifactId;
    const fs::path platformRoot = artifactRoot / platform;
    const fs::path destExecutable = platformRoot / source.filename();

    std::error_code ec;
    fs::create_directories(platformRoot, ec);
    ec.clear();
    fs::copy_file(source, destExecutable, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
      if (outError)
        *outError = "Failed to copy engine into the artifact directory.";
      return std::nullopt;
    }

    (void)setExecutableBitIfNeeded(destExecutable);

    UciEngineProcess::Id probedId{};
    std::vector<domain::analysis::config::UciOption> probedOptions;
    std::string probeError;
    if (!probeEngine(destExecutable, platformRoot, probedId, probedOptions, &probeError))
    {
      fs::remove_all(artifactRoot, ec);
      if (outError)
        *outError = probeError;
      return std::nullopt;
    }

    const std::string displayName = !probedId.name.empty() ? probedId.name : source.stem().string();
    const std::string base = slugify(!probedId.name.empty() ? probedId.name : source.stem().string());
    const std::string shortHash = fileFingerprint.substr(0, 12);
    const std::string engineId = "ext-" + base + "-" + shortHash;

    EngineRecord &record = m_records[engineId];
    record.engineId = engineId;
    record.displayName = displayName;
    record.version = "unknown";
    record.iconKey = chooseIconKey(displayName, false);
    record.builtin = false;
    record.uciId = probedId;
    record.options = std::move(probedOptions);

    EngineInstallRecord install{};
    install.platformTag = platform;
    install.artifactId = artifactId;
    install.entryExecutable = weakCanonicalOrNormal(destExecutable);
    install.workingDirectory = weakCanonicalOrNormal(platformRoot);
    install.builtin = false;
    record.installs[platform] = std::move(install);

    saveRecord(record);
    return makeEntryForPlatform(record, platform);
  }

  std::vector<EngineEntry> EngineRegistry::list() const
  {
    const std::string platform = currentPlatformTag();
    std::vector<EngineEntry> out;
    out.reserve(m_records.size());

    for (const auto &[id, record] : m_records)
    {
      auto entry = makeEntryForPlatform(record, platform);
      if (!entry)
        continue;

      std::error_code ec;
      if (!entry->ref.executablePath.empty() && !fs::exists(fs::path(entry->ref.executablePath), ec))
        continue;

      out.push_back(std::move(*entry));
    }

    std::sort(out.begin(), out.end(), [](const EngineEntry &a, const EngineEntry &b)
              {
                if (a.builtin != b.builtin)
                  return a.builtin && !b.builtin;
                return a.ref.displayName < b.ref.displayName; });
    return out;
  }

  std::optional<EngineEntry> EngineRegistry::get(const std::string &engineId) const
  {
    const auto it = m_records.find(engineId);
    if (it == m_records.end())
      return std::nullopt;
    return makeEntryForPlatform(it->second, currentPlatformTag());
  }

  domain::analysis::config::BotConfig EngineRegistry::makeDefaultBotConfig(const std::string &engineId) const
  {
    domain::analysis::config::BotConfig bot{};
    const auto entry = get(engineId);
    if (!entry)
      return bot;

    bot.engine = entry->ref;
    bot.limits.movetimeMs = 500;
    bot.limits.depth.reset();

    for (const auto &opt : entry->options)
    {
      using Type = domain::analysis::config::UciOption::Type;
      switch (opt.type)
      {
      case Type::Check:
        bot.uciValues[opt.name] = opt.defaultBool;
        break;
      case Type::Spin:
        bot.uciValues[opt.name] = opt.defaultInt;
        break;
      case Type::Combo:
      case Type::String:
        bot.uciValues[opt.name] = opt.defaultStr;
        break;
      case Type::Button:
        // Buttons are actions, not persistent config values.
        break;
      }
    }

    return bot;
  }
}
