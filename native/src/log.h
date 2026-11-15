#pragma once
#include <string>
#include <vector>
#include <functional>

// Output Log shared by the engine and the editor.
namespace pge {
enum class LogKind { Info, Ok, Warn, Error };
struct LogLine { std::string time, text; LogKind kind; };
void logMsg(const std::string& text, LogKind kind = LogKind::Info);
std::vector<LogLine>& logLines();
void logClear();
}
