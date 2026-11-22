#include "platform.h"
#include <SDL.h>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#else
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace pge {
static bool g_console = false;
void setConsoleSystem(bool on) { g_console = on; }
bool consoleSystem() { return g_console; }
std::string exeSuffix() {
#ifdef _WIN32
  return ".exe";
#else
  return "";
#endif
}
#ifdef _WIN32
static std::wstring W(const std::string& s) { int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n > 0 ? n - 1 : 0, L'\0'); if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n); return w; }
static std::string U(const std::wstring& w) { int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string s(n > 0 ? n - 1 : 0, '\0'); if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr); return s; }
bool hasFileDialogs() { return !g_console; }
std::string openFileDialog(const std::string& title, const std::string& filterName, const std::string& pattern) {
  wchar_t file[MAX_PATH * 4] = {0};
  std::wstring filter = W(filterName + " (" + pattern + ")"); filter.push_back(L'\0'); filter += W(pattern); filter.push_back(L'\0');
  filter += L"All files (*.*)"; filter.push_back(L'\0'); filter += L"*.*"; filter.push_back(L'\0');
  std::wstring t = W(title);
  OPENFILENAMEW o{}; o.lStructSize = sizeof o; o.lpstrFile = file; o.nMaxFile = MAX_PATH * 4; o.lpstrFilter = filter.c_str(); o.lpstrTitle = t.c_str();
  o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  return GetOpenFileNameW(&o) ? U(file) : "";
}
void openInExplorer(const std::string& path) { if (g_console) return; ShellExecuteW(nullptr, L"open", W(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
bool launchProgram(const std::string& exe, const std::vector<std::string>& args, std::string* err) {
  std::wstring cmd = L"\"" + W(exe) + L"\""; for (auto& a : args) cmd += L" \"" + W(a) + L"\"";
  std::wstring dir = std::filesystem::u8path(exe).parent_path().wstring();
  STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) { if (err) *err = "Windows error " + std::to_string(GetLastError()); return false; }
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return true;
}
#else
bool hasFileDialogs() { return false; }
std::string openFileDialog(const std::string&, const std::string&, const std::string&) { return ""; }
void openInExplorer(const std::string& path) { if (g_console) return; std::string u = "file://" + path; SDL_OpenURL(u.c_str()); }
bool launchProgram(const std::string& exe, const std::vector<std::string>& args, std::string* err) {
  std::vector<char*> av; av.push_back(const_cast<char*>(exe.c_str())); for (auto& a : args) av.push_back(const_cast<char*>(a.c_str())); av.push_back(nullptr);
  pid_t pid; int r = posix_spawn(&pid, exe.c_str(), nullptr, nullptr, av.data(), environ);
  if (r != 0) { if (err) *err = "could not start " + exe; return false; }
  return true;
}
#endif
}
