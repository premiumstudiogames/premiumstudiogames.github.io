#include "zipwrite.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <ctime>

namespace fs = std::filesystem;
namespace pge {
uint32_t crc32(const void* data, size_t n, uint32_t crc) {
  static uint32_t table[256]; static bool made = false;
  if (!made) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; } made = true; }
  crc = ~crc; const uint8_t* p = (const uint8_t*)data; for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8); return ~crc;
}
static void u16(std::ostream& o, uint32_t v) { char b[2] = {(char)(v & 0xff), (char)((v >> 8) & 0xff)}; o.write(b, 2); }
static void u32(std::ostream& o, uint32_t v) { char b[4] = {(char)(v & 0xff), (char)((v >> 8) & 0xff), (char)((v >> 16) & 0xff), (char)((v >> 24) & 0xff)}; o.write(b, 4); }

bool writeZip(const std::string& zipFile, const std::string& dir, const std::string& prefix, const std::function<bool(const std::string&)>& executable, std::string* err) {
  struct E { std::string name; uint32_t crc, size, offset, mode; };
  std::vector<std::pair<fs::path, std::string>> files; std::error_code ec; fs::path root = fs::u8path(dir), zp = fs::u8path(zipFile);
  for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) { if (ec) break;
    if (!it->is_regular_file(ec) || fs::equivalent(it->path(), zp, ec)) continue; std::string rel = fs::relative(it->path(), root, ec).generic_u8string(); if (!ec) files.push_back({it->path(), rel}); }
  std::sort(files.begin(), files.end(), [](auto& a, auto& b) { return a.second < b.second; });
  std::ofstream o(zp, std::ios::binary | std::ios::trunc); if (!o) { if (err) *err = "can't write " + zipFile; return false; }
  std::vector<E> es; std::vector<char> buf;
  std::time_t now = std::time(nullptr); std::tm tm{}; if (std::tm* t = std::localtime(&now)) tm = *t;   // the files' date: now (DOS format)
  uint32_t dtime = (uint32_t)(tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec / 2), ddate = (uint32_t)(std::max(tm.tm_year - 80, 0) << 9 | (tm.tm_mon + 1) << 5 | tm.tm_mday);
  for (auto& [path, rel] : files) {
    std::ifstream in(path, std::ios::binary); if (!in) continue; buf.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (buf.size() >= 0xFFFFFFFFull || (uint64_t)o.tellp() + buf.size() >= 0xFFFFFFF0ull) { if (err) *err = "the game is over 4 GB (too big for one zip)"; return false; }
    E e; e.name = prefix + rel; e.crc = crc32(buf.data(), buf.size()); e.size = (uint32_t)buf.size(); e.offset = (uint32_t)o.tellp(); e.mode = executable && executable(rel) ? 0100755 : 0100644;
    u32(o, 0x04034b50); u16(o, 20); u16(o, 0x0800); u16(o, 0); u16(o, dtime); u16(o, ddate); u32(o, e.crc); u32(o, e.size); u32(o, e.size); u16(o, (uint32_t)e.name.size()); u16(o, 0);
    o.write(e.name.data(), e.name.size()); o.write(buf.data(), buf.size()); es.push_back(e); }
  uint32_t cd = (uint32_t)o.tellp();
  for (auto& e : es) { u32(o, 0x02014b50); u16(o, (3 << 8) | 20); u16(o, 20); u16(o, 0x0800); u16(o, 0); u16(o, dtime); u16(o, ddate); u32(o, e.crc); u32(o, e.size); u32(o, e.size);
    u16(o, (uint32_t)e.name.size()); u16(o, 0); u16(o, 0); u16(o, 0); u16(o, 0); u32(o, e.mode << 16); u32(o, e.offset); o.write(e.name.data(), e.name.size()); }
  uint32_t cdSize = (uint32_t)o.tellp() - cd; if (es.size() > 0xFFFF) { if (err) *err = "too many files for one zip"; return false; }
  u32(o, 0x06054b50); u16(o, 0); u16(o, 0); u16(o, (uint32_t)es.size()); u16(o, (uint32_t)es.size()); u32(o, cdSize); u32(o, cd); u16(o, 0);
  o.close(); if (!o) { if (err) *err = "writing " + zipFile + " failed (disk full?)"; return false; } return true;
}
bool readZip(const std::string& zipFile, std::vector<ZipEntry>& out) {
  out.clear(); std::ifstream in(fs::u8path(zipFile), std::ios::binary); if (!in) return false; std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto r16 = [&](size_t p) { return p + 2 <= d.size() ? (uint32_t)d[p] | (uint32_t)d[p + 1] << 8 : 0u; }; auto r32 = [&](size_t p) { return r16(p) | r16(p + 2) << 16; };
  if (d.size() < 22) return false; size_t eocd = std::string::npos; for (size_t p = d.size() - 22; p + 1 > 0 && d.size() - p < 70000; p--) { if (r32(p) == 0x06054b50) { eocd = p; break; } if (!p) break; }
  if (eocd == std::string::npos) return false; uint32_t n = r16(eocd + 10), cd = r32(eocd + 16); size_t p = cd;
  for (uint32_t i = 0; i < n; i++) { if (r32(p) != 0x02014b50) return false; ZipEntry e; uint32_t nl = r16(p + 28), xl = r16(p + 30), cl = r16(p + 32), off = r32(p + 42);
    e.crc = r32(p + 16); e.size = r32(p + 24); e.mode = r32(p + 38) >> 16; e.name.assign((const char*)&d[p + 46], nl);
    if (r32(off) == 0x04034b50) { size_t data = off + 30 + r16(off + 26) + r16(off + 28); e.crcOk = r16(off + 8) == 0 && data + e.size <= d.size() && crc32(&d[data], e.size) == e.crc; }
    out.push_back(e); p += 46 + nl + xl + cl; }
  return true;
}
}
