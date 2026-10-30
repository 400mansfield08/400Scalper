#pragma once
// 400Scalper — global user preferences (shared by every instance, not saved in presets).
// Currently: the UI accent color. Stored as a tiny text file:
//   Windows: %APPDATA%\400Scalper\settings.txt
//   macOS:   ~/Library/Application Support/400Scalper/settings.txt
//   Linux:   ~/.config/400Scalper/settings.txt

#include <string>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <cctype>

namespace scalper::settings {

inline const char* kDefaultAccent = "#FF6A13";

inline std::filesystem::path dir()
{
#if defined(_WIN32)
    if (const char* a = std::getenv ("APPDATA")) return std::filesystem::path (a) / "400Scalper";
#elif defined(__APPLE__)
    if (const char* h = std::getenv ("HOME")) return std::filesystem::path (h) / "Library" / "Application Support" / "400Scalper";
#else
    if (const char* x = std::getenv ("XDG_CONFIG_HOME")) return std::filesystem::path (x) / "400Scalper";
    if (const char* h = std::getenv ("HOME")) return std::filesystem::path (h) / ".config" / "400Scalper";
#endif
    return std::filesystem::temp_directory_path() / "400Scalper";
}

inline bool isHexColor (const std::string& s)
{
    if (s.size() != 7 || s[0] != '#') return false;
    for (size_t i = 1; i < 7; ++i) if (! std::isxdigit ((unsigned char) s[i])) return false;
    return true;
}

inline std::string loadAccent()
{
    std::ifstream f (dir() / "settings.txt");
    std::string line;
    while (std::getline (f, line))
    {
        if (line.rfind ("accent=", 0) == 0)
        {
            auto v = line.substr (7);
            while (! v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
            if (isHexColor (v)) return v;
        }
    }
    return kDefaultAccent;
}

inline void saveAccent (const std::string& hex)
{
    if (! isHexColor (hex)) return;
    std::error_code ec;
    std::filesystem::create_directories (dir(), ec);
    std::ofstream f (dir() / "settings.txt", std::ios::trunc);
    f << "accent=" << hex << "\n";
}

} // namespace scalper::settings
