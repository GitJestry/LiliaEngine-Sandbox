#include "lilia/app/engines/engine_provisioner.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <system_error>

namespace lilia::app::engines
{
  namespace fs = std::filesystem;

  namespace
  {
    std::string hex64(std::uint64_t v)
    {
      std::ostringstream os;
      os << std::hex << std::setfill('0') << std::setw(16) << v;
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
  } // namespace

  EngineProvisioner::EngineProvisioner(EngineRegistry &registry)
      : m_registry(registry)
  {
  }

  std::filesystem::path EngineProvisioner::builtinStoreDir() const
  {
    return m_registry.userDataDir() / "engines" / "builtin";
  }

  std::string EngineProvisioner::currentPlatformTag()
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

  std::string EngineProvisioner::fingerprintFile(const fs::path &p)
  {
    std::ifstream in(p, std::ios::binary);
    if (!in.good())
      return {};

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

  bool EngineProvisioner::setExecutableBitIfNeeded(const fs::path &p)
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

  bool EngineProvisioner::copyFileIfChanged(const fs::path &src, const fs::path &dst)
  {
    std::error_code ec;

    const std::string srcHash = fingerprintFile(src);
    if (srcHash.empty())
      return false;

    if (fs::exists(dst, ec) && !ec && fs::is_regular_file(dst, ec) && !ec)
    {
      const std::string dstHash = fingerprintFile(dst);
      if (!dstHash.empty() && dstHash == srcHash)
      {
        (void)setExecutableBitIfNeeded(dst);
        return true;
      }
    }

    fs::create_directories(dst.parent_path(), ec);
    ec.clear();

    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec)
      return false;

    return setExecutableBitIfNeeded(dst);
  }

  void EngineProvisioner::ensureBuiltinsInstalled(const std::vector<BundledEngineInfo> &bundledEngines)
  {
    const std::string platform = currentPlatformTag();

    std::error_code ec;
    fs::create_directories(builtinStoreDir(), ec);

    for (const auto &engine : bundledEngines)
    {
      if (engine.engineId.empty() || engine.entryExecutable.empty())
        continue;

      if (!fs::exists(engine.entryExecutable, ec) || ec)
      {
        ec.clear();
        continue;
      }

      const fs::path dstDir = builtinStoreDir() / engine.engineId / platform;
      const fs::path dstExe = dstDir / engine.entryExecutable.filename();

      if (!copyFileIfChanged(engine.entryExecutable, dstExe))
        continue;

      m_registry.ensureBuiltin(engine.engineId,
                               engine.displayName,
                               engine.version,
                               weakCanonicalOrNormal(dstExe),
                               engine.iconKey);
    }
  }
} // namespace lilia::uci
