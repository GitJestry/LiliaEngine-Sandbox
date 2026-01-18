#include "lilia/view/ui/platform/file_dialog.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#else
#include <optional>
#endif

namespace lilia::view::ui::platform
{
  namespace
  {
#if !defined(_WIN32)
    static void stripTrailingNewlines(std::string &s)
    {
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    }

    static std::optional<std::string> popenReadAll(const std::string &cmd)
    {
      FILE *pipe = popen(cmd.c_str(), "r");
      if (!pipe)
        return std::nullopt;

      std::string out;
      char buf[1024];
      while (fgets(buf, sizeof(buf), pipe))
        out += buf;

      pclose(pipe);
      stripTrailingNewlines(out);
      if (out.empty())
        return std::nullopt;
      return out;
    }
#endif
  } // namespace

  std::optional<std::string> openExecutableFileDialog()
  {
#if defined(_WIN32)
    char fileBuf[MAX_PATH] = {0};

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter =
        "Executables\0*.exe;*.bat;*.cmd\0All Files\0*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;

    if (GetOpenFileNameA(&ofn))
      return std::string(fileBuf);

    return std::nullopt;

#elif defined(__APPLE__)
    // macOS: use osascript to open file picker (no additional deps).
    // Note: user can still pick a non-executable; installExternal will validate.
    auto path = popenReadAll(
        "osascript -e 'POSIX path of (choose file with prompt \"Select UCI engine executable\")' 2>/dev/null");
    return path;

#else
    // Linux: prefer zenity, then kdialog.
    auto path = popenReadAll("zenity --file-selection --title=\"Select UCI engine executable\" 2>/dev/null");
    if (path)
      return path;

    path = popenReadAll("kdialog --getopenfilename . \"*|Executable\" 2>/dev/null");
    return path;
#endif
  }
} // namespace lilia::view::ui::platform
