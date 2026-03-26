#include "lilia/uci/stockfish_downloader.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace lilia::uci
{
  namespace fs = std::filesystem;

  namespace
  {
    std::string shellQuote(const std::string &s)
    {
#if defined(_WIN32)
      std::string out = "\"";
      for (char c : s)
      {
        if (c == '"')
          out += "\"\"";
        else
          out.push_back(c);
      }
      out += "\"";
      return out;
#else
      std::string out = "'";
      for (char c : s)
      {
        if (c == '\'')
          out += "'\\''";
        else
          out.push_back(c);
      }
      out += "'";
      return out;
#endif
    }

    std::string powershellSingleQuote(const std::string &s)
    {
      std::string out = "'";
      for (char c : s)
      {
        if (c == '\'')
          out += "''";
        else
          out.push_back(c);
      }
      out += "'";
      return out;
    }

    bool runCommand(const std::string &cmd)
    {
      return std::system(cmd.c_str()) == 0;
    }

    bool fileExistsAndNonEmpty(const fs::path &p)
    {
      std::error_code ec;
      return fs::exists(p, ec) && !ec && fs::is_regular_file(p, ec) && !ec && fs::file_size(p, ec) > 0;
    }

    fs::path weakCanonicalOrNormal(const fs::path &p)
    {
      std::error_code ec;
      const fs::path c = fs::weakly_canonical(p, ec);
      if (ec)
        return p.lexically_normal();
      return c;
    }

    std::string lowerCopy(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c)
                     { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    bool looksLikeSourceFile(const fs::path &p)
    {
      const std::string ext = lowerCopy(p.extension().string());
      return ext == ".txt" || ext == ".md" || ext == ".cpp" || ext == ".c" ||
             ext == ".h" || ext == ".hpp" || ext == ".mk" || ext == ".sh" ||
             ext == ".cmake" || ext == ".json" || ext == ".yml" || ext == ".yaml";
    }
  } // namespace

  StockfishDownloader::StockfishDownloader(EngineRegistry &registry)
      : m_registry(registry)
  {
  }

  std::string StockfishDownloader::currentPlatformTag()
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

  bool StockfishDownloader::isSupportedOnCurrentPlatform()
  {
    return chooseDownloadSpec().has_value();
  }

  std::optional<StockfishDownloader::DownloadSpec> StockfishDownloader::chooseDownloadSpec()
  {
#if defined(_WIN32) && (defined(__aarch64__) || defined(_M_ARM64))
    // Compatibility-first ARM build. You can switch to dotprod later if you add CPU-feature probing.
    return DownloadSpec{
        "stockfish-windows-armv8.zip",
        "https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-windows-armv8.zip",
        "latest"};
#elif defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
    return DownloadSpec{
        "stockfish-windows-x86-64-avx2.zip",
        "https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-windows-x86-64-avx2.zip",
        "latest"};
#elif defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    return DownloadSpec{
        "stockfish-macos-m1-apple-silicon.tar",
        "https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-macos-m1-apple-silicon.tar",
        "latest"};
#elif defined(__APPLE__) && (defined(__x86_64__) || defined(_M_X64))
    return DownloadSpec{
        "stockfish-macos-x86-64-bmi2.tar",
        "https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-macos-x86-64-bmi2.tar",
        "latest"};
#elif defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
    return DownloadSpec{
        "stockfish-ubuntu-x86-64-avx2.tar",
        "https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-ubuntu-x86-64-avx2.tar",
        "latest"};
#else
    return std::nullopt;
#endif
  }

  std::filesystem::path StockfishDownloader::downloadCacheDir() const
  {
    return m_registry.userDataDir() / "engines" / "downloads" / "stockfish" / currentPlatformTag();
  }

  std::filesystem::path StockfishDownloader::installedBinaryPath() const
  {
#if defined(_WIN32)
    const char *fileName = "stockfish.exe";
#else
    const char *fileName = "stockfish";
#endif
    return m_registry.userDataDir() / "engines" / "builtin" / "stockfish" / currentPlatformTag() / fileName;
  }

  bool StockfishDownloader::downloadFile(const std::string &url,
                                         const fs::path &dst,
                                         std::string *outError)
  {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);

    const std::string curlCmd =
        "curl -L --fail --silent --show-error -o " + shellQuote(dst.string()) + " " + shellQuote(url);

    if (runCommand(curlCmd) && fileExistsAndNonEmpty(dst))
      return true;

#if defined(_WIN32)
    const std::string psCmd =
        "powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
        "\"$ProgressPreference='SilentlyContinue'; "
        "Invoke-WebRequest -UseBasicParsing -Uri " +
        powershellSingleQuote(url) +
        " -OutFile " + powershellSingleQuote(dst.string()) + "\"";

    if (runCommand(psCmd) && fileExistsAndNonEmpty(dst))
      return true;
#endif

    if (outError)
      *outError = "Failed to download Stockfish archive.";
    return false;
  }

  bool StockfishDownloader::extractArchive(const fs::path &archivePath,
                                           const fs::path &extractDir,
                                           std::string *outError)
  {
    std::error_code ec;
    fs::remove_all(extractDir, ec);
    ec.clear();
    fs::create_directories(extractDir, ec);

#if defined(_WIN32)
    const std::string cmd =
        "powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
        "\"Expand-Archive -LiteralPath " +
        powershellSingleQuote(archivePath.string()) +
        " -DestinationPath " + powershellSingleQuote(extractDir.string()) + " -Force\"";
#else
    const std::string cmd =
        "tar -xf " + shellQuote(archivePath.string()) + " -C " + shellQuote(extractDir.string());
#endif

    if (runCommand(cmd))
      return true;

    if (outError)
      *outError = "Failed to extract Stockfish archive.";
    return false;
  }

  std::optional<fs::path> StockfishDownloader::findExtractedStockfishBinary(const fs::path &extractDir)
  {
    std::error_code ec;
    if (!fs::exists(extractDir, ec))
      return std::nullopt;

    fs::path exact;
    fs::path best;
    std::uintmax_t bestSize = 0;

    for (const auto &entry : fs::recursive_directory_iterator(extractDir, ec))
    {
      if (ec)
        break;
      if (!entry.is_regular_file())
        continue;

      const fs::path p = entry.path();
      const std::string name = lowerCopy(p.filename().string());

      if (looksLikeSourceFile(p))
        continue;

      if (name == "stockfish" || name == "stockfish.exe")
      {
        exact = p;
        break;
      }

      if (name.rfind("stockfish", 0) != 0)
        continue;

      const auto size = fs::file_size(p, ec);
      if (!ec && size > bestSize)
      {
        bestSize = size;
        best = p;
      }
      ec.clear();
    }

    if (!exact.empty())
      return exact;
    if (!best.empty())
      return best;
    return std::nullopt;
  }

  bool StockfishDownloader::copyFileIfDifferent(const fs::path &src,
                                                const fs::path &dst,
                                                std::string *outError)
  {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    ec.clear();

    // Cheap skip: same size means "good enough" here because downloads are content-addressed by asset URL
    if (fs::exists(dst, ec) && !ec)
    {
      const auto srcSize = fs::file_size(src, ec);
      ec.clear();
      const auto dstSize = fs::file_size(dst, ec);
      if (!ec && srcSize == dstSize)
      {
        (void)setExecutableBitIfNeeded(dst);
        return true;
      }
    }

    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
      if (outError)
        *outError = "Failed to copy Stockfish into the shared engine store.";
      return false;
    }

    return setExecutableBitIfNeeded(dst);
  }

  bool StockfishDownloader::setExecutableBitIfNeeded(const fs::path &p)
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

  bool StockfishDownloader::ensureInstalledIfMissing(std::string *outError)
  {
    // Fast path: already registered and present.
    if (auto existing = m_registry.get("stockfish"))
    {
      std::error_code ec;
      if (!existing->ref.executablePath.empty() &&
          fs::exists(fs::path(existing->ref.executablePath), ec))
      {
        return true;
      }
    }

    // Second fast path: already present in shared store but not registered in memory yet.
    const fs::path finalBinary = installedBinaryPath();
    {
      std::error_code ec;
      if (fs::exists(finalBinary, ec) && fs::is_regular_file(finalBinary, ec))
      {
        m_registry.ensureBuiltin("stockfish", "Stockfish", "latest", weakCanonicalOrNormal(finalBinary), "stockfish");
        return true;
      }
    }

    const auto spec = chooseDownloadSpec();
    if (!spec)
    {
      if (outError)
        *outError = "Automatic Stockfish download is not supported on this platform.";
      return false;
    }

    std::error_code ec;
    fs::create_directories(downloadCacheDir(), ec);

    const fs::path archivePath = downloadCacheDir() / spec->assetName;
    const fs::path extractDir = downloadCacheDir() / "extract";

    if (!fileExistsAndNonEmpty(archivePath))
    {
      if (!downloadFile(spec->url, archivePath, outError))
        return false;
    }

    if (!extractArchive(archivePath, extractDir, outError))
    {
      // One retry: bad/corrupt archive can be deleted and fetched again.
      fs::remove(archivePath, ec);
      ec.clear();

      if (!downloadFile(spec->url, archivePath, outError))
        return false;
      if (!extractArchive(archivePath, extractDir, outError))
        return false;
    }

    const auto extractedBinary = findExtractedStockfishBinary(extractDir);
    if (!extractedBinary)
    {
      if (outError)
        *outError = "Downloaded Stockfish archive did not contain a usable engine binary.";
      return false;
    }

    if (!copyFileIfDifferent(*extractedBinary, finalBinary, outError))
      return false;

    m_registry.ensureBuiltin("stockfish",
                             "Stockfish",
                             spec->versionLabel,
                             weakCanonicalOrNormal(finalBinary),
                             "stockfish");
    return true;
  }
} // namespace lilia::uci
