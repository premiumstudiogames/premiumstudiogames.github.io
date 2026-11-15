#include "log.h"
#include <ctime>
#include <cstdio>

namespace pge {
static std::vector<LogLine> g_lines;
std::vector<LogLine>& logLines() { return g_lines; }
void logClear() { g_lines.clear(); }
void logMsg(const std::string& text, LogKind kind) {
  if (kind == LogKind::Info) {
    if (text.rfind("error", 0) == 0) kind = LogKind::Error;
    else if (text.rfind("warning", 0) == 0) kind = LogKind::Warn;
  }
  std::time_t t = std::time(nullptr); char buf[16]; std::strftime(buf, sizeof buf, "%H:%M:%S", std::localtime(&t));
  g_lines.push_back({buf, text, kind});
  if (g_lines.size() > 5000) g_lines.erase(g_lines.begin(), g_lines.begin() + 1000);
  std::fprintf(stdout, "[%s] %s\n", buf, text.c_str()); std::fflush(stdout);
}
}
