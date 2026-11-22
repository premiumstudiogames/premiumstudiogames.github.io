#pragma once
// Small OS helpers for the editor: file dialogs, opening folders, starting programs.
#include <string>
#include <vector>
namespace pge {
// returns "" when cancelled (or when the OS has no dialog: the caller then offers a path field)
std::string openFileDialog(const std::string& title, const std::string& filterName, const std::string& pattern);
bool hasFileDialogs();
void openInExplorer(const std::string& path);
bool launchProgram(const std::string& exe, const std::vector<std::string>& args, std::string* err = nullptr);
std::string exeSuffix();   // ".exe" on Windows
// the console version (Premium Games console mode): its own system - no Windows file dialogs or Explorer
void setConsoleSystem(bool on);
bool consoleSystem();
}
