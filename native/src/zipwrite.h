#pragma once
// Zip files with Unix permissions kept (a Linux or macOS game made on Windows still starts: its program is marked
// executable inside the zip, which unzip and the Mac's Archive Utility honour). Stored, not compressed (pictures and
// sounds are compressed already).
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
namespace pge {
// every file under dir, named prefix + its path inside dir; executable(rel) says which get rwxr-xr-x
bool writeZip(const std::string& zipFile, const std::string& dir, const std::string& prefix, const std::function<bool(const std::string&)>& executable, std::string* err = nullptr);
struct ZipEntry { std::string name; uint32_t mode = 0, size = 0, crc = 0; bool crcOk = false; };
bool readZip(const std::string& zipFile, std::vector<ZipEntry>& out);   // the list (and checks each file's contents)
uint32_t crc32(const void* data, size_t n, uint32_t crc = 0);
}
