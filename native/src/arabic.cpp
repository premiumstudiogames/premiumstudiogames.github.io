// Arabic shaping (Unicode presentation forms) and a small bidirectional reorder (UAX #9 rules L1-L4 for one paragraph per line):
// strong right-to-left = Arabic letters; strong left-to-right = Latin letters and digits; spaces and punctuation take the side
// of the letters around them. Brackets are mirrored in right-to-left runs.
#include "arabic.h"
#include <cstdint>
#include <algorithm>

namespace pge {

std::u32string toU32(const std::string& s) {
  std::u32string r; r.reserve(s.size());
  for (size_t i = 0; i < s.size();) { unsigned char c = (unsigned char)s[i]; char32_t cp; int n;
    if (c < 0x80) { cp = c; n = 1; } else if ((c >> 5) == 6) { cp = c & 31; n = 2; } else if ((c >> 4) == 14) { cp = c & 15; n = 3; } else if ((c >> 3) == 30) { cp = c & 7; n = 4; } else { i++; continue; }
    if (i + n > s.size()) break; for (int k = 1; k < n; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 63); r.push_back(cp); i += n; }
  return r;
}
std::string toUtf8(const std::u32string& s) {
  std::string r; r.reserve(s.size() * 2);
  for (char32_t c : s) {
    if (c < 0x80) r += (char)c;
    else if (c < 0x800) { r += (char)(0xC0 | (c >> 6)); r += (char)(0x80 | (c & 63)); }
    else if (c < 0x10000) { r += (char)(0xE0 | (c >> 12)); r += (char)(0x80 | ((c >> 6) & 63)); r += (char)(0x80 | (c & 63)); }
    else { r += (char)(0xF0 | (c >> 18)); r += (char)(0x80 | ((c >> 12) & 63)); r += (char)(0x80 | ((c >> 6) & 63)); r += (char)(0x80 | (c & 63)); }
  }
  return r;
}

// presentation forms: isolated, final, initial, medial (0 = none: the letter joins only to the letter before it)
struct Forms { char32_t ch, iso, fin, ini, med; };
static const Forms FORMS[] = {
  {0x0621, 0xFE80, 0, 0, 0}, {0x0622, 0xFE81, 0xFE82, 0, 0}, {0x0623, 0xFE83, 0xFE84, 0, 0}, {0x0624, 0xFE85, 0xFE86, 0, 0},
  {0x0625, 0xFE87, 0xFE88, 0, 0}, {0x0626, 0xFE89, 0xFE8A, 0xFE8B, 0xFE8C}, {0x0627, 0xFE8D, 0xFE8E, 0, 0}, {0x0628, 0xFE8F, 0xFE90, 0xFE91, 0xFE92},
  {0x0629, 0xFE93, 0xFE94, 0, 0}, {0x062A, 0xFE95, 0xFE96, 0xFE97, 0xFE98}, {0x062B, 0xFE99, 0xFE9A, 0xFE9B, 0xFE9C}, {0x062C, 0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0},
  {0x062D, 0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4}, {0x062E, 0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8}, {0x062F, 0xFEA9, 0xFEAA, 0, 0}, {0x0630, 0xFEAB, 0xFEAC, 0, 0},
  {0x0631, 0xFEAD, 0xFEAE, 0, 0}, {0x0632, 0xFEAF, 0xFEB0, 0, 0}, {0x0633, 0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4}, {0x0634, 0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8},
  {0x0635, 0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC}, {0x0636, 0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0}, {0x0637, 0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4}, {0x0638, 0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8},
  {0x0639, 0xFEC9, 0xFECA, 0xFECB, 0xFECC}, {0x063A, 0xFECD, 0xFECE, 0xFECF, 0xFED0}, {0x0641, 0xFED1, 0xFED2, 0xFED3, 0xFED4}, {0x0642, 0xFED5, 0xFED6, 0xFED7, 0xFED8},
  {0x0643, 0xFED9, 0xFEDA, 0xFEDB, 0xFEDC}, {0x0644, 0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0}, {0x0645, 0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4}, {0x0646, 0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8},
  {0x0647, 0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC}, {0x0648, 0xFEED, 0xFEEE, 0, 0}, {0x0649, 0xFEEF, 0xFEF0, 0, 0}, {0x064A, 0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4},
  // Persian / Urdu letters
  {0x067E, 0xFB56, 0xFB57, 0xFB58, 0xFB59}, {0x0686, 0xFB7A, 0xFB7B, 0xFB7C, 0xFB7D}, {0x0698, 0xFB8A, 0xFB8B, 0, 0}, {0x06A9, 0xFB8E, 0xFB8F, 0xFB90, 0xFB91},
  {0x06AF, 0xFB92, 0xFB93, 0xFB94, 0xFB95}, {0x06CC, 0xFBFC, 0xFBFD, 0xFBFE, 0xFBFF},
};
static const Forms* formsOf(char32_t c) { for (const Forms& f : FORMS) if (f.ch == c) return &f; return nullptr; }
static bool isMark(char32_t c) { return (c >= 0x064B && c <= 0x065F) || c == 0x0670 || (c >= 0x06D6 && c <= 0x06ED); }   // harakat: don't break joining
static bool joinsAfter(char32_t c) { if (c == 0x0640) return true; const Forms* f = formsOf(c); return f && f->ini; }   // can connect to the next letter
static bool joinsBefore(char32_t c) { if (c == 0x0640) return true; const Forms* f = formsOf(c); return f && f->fin; }  // can connect to the letter before
static bool isArabic(char32_t c) { return (c >= 0x0600 && c <= 0x06FF) || (c >= 0x0750 && c <= 0x077F) || (c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF); }
static bool isArabicLetter(char32_t c) { return isArabic(c) && !(c >= 0x0660 && c <= 0x0669) && !(c >= 0x06F0 && c <= 0x06F9) && !isMark(c) && c != 0x060C && c != 0x061B && c != 0x061F; }

bool hasRtl(const std::string& s) { for (char32_t c : toU32(s)) if (isArabicLetter(c)) return true; return false; }

// joined forms, still in logical order
static std::u32string shape(const std::u32string& in) {
  std::u32string out; out.reserve(in.size());
  auto prevLetter = [&](size_t i) -> char32_t { while (i > 0) { char32_t c = in[--i]; if (!isMark(c)) return c; } return 0; };
  auto nextLetter = [&](size_t i, size_t* at) -> char32_t { while (++i < in.size()) { char32_t c = in[i]; if (!isMark(c)) { if (at) *at = i; return c; } } return 0; };
  for (size_t i = 0; i < in.size(); i++) {
    char32_t c = in[i]; const Forms* f = formsOf(c);
    if (!f) { out += c; continue; }
    char32_t p = prevLetter(i); bool before = p && joinsAfter(p) && joinsBefore(c);
    size_t ni = 0; char32_t n = nextLetter(i, &ni);
    // lam + alef: one ligature
    if (c == 0x0644 && (n == 0x0622 || n == 0x0623 || n == 0x0625 || n == 0x0627)) {
      char32_t iso = n == 0x0622 ? 0xFEF5 : n == 0x0623 ? 0xFEF7 : n == 0x0625 ? 0xFEF9 : 0xFEFB;
      out += before ? iso + 1 : iso; for (size_t k = i + 1; k < ni; k++) out += in[k]; i = ni; continue; }
    bool after = n && joinsAfter(c) && joinsBefore(n);
    char32_t g = before && after ? f->med : before ? f->fin : after ? f->ini : f->iso;
    out += g ? g : f->iso;
  }
  return out;
}
static char32_t mirror(char32_t c) {
  switch (c) { case '(': return ')'; case ')': return '('; case '[': return ']'; case ']': return '['; case '{': return '}'; case '}': return '{'; case '<': return '>'; case '>': return '<';
    case 0x00AB: return 0x00BB; case 0x00BB: return 0x00AB; default: return c; }
}
static std::u32string reorderLine(const std::u32string& logical) {
  // harakat are left out: a left-to-right glyph renderer can't place them on their letters
  std::u32string s = shape(logical); s.erase(std::remove_if(s.begin(), s.end(), isMark), s.end()); size_t n = s.size(); if (!n) return s;
  // 0 = right-to-left letter, 1 = left-to-right letter or digit, 2 = neutral
  std::vector<int> t(n);
  for (size_t i = 0; i < n; i++) { char32_t c = s[i];
    if (isArabicLetter(c)) t[i] = 0;
    else if ((c >= '0' && c <= '9') || (c >= 0x0660 && c <= 0x0669) || (c >= 0x06F0 && c <= 0x06F9) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c < 0x0600 && c != 0xD7 && c != 0xF7)) t[i] = 1;
    else t[i] = 2; }
  // the line's direction: its first strong letter
  int base = 1; for (size_t i = 0; i < n; i++) if (t[i] != 2) { base = t[i]; break; }
  // a digit run's decimal point / separators stay with the digits (1.5, 12:30, 3,000)
  for (size_t i = 1; i + 1 < n; i++) if (t[i] == 2 && (s[i] == '.' || s[i] == ',' || s[i] == ':' || s[i] == '/') && t[i - 1] == 1 && t[i + 1] == 1) t[i] = 1;
  // neutrals: the side of the strong letters around them, else the line's direction
  for (size_t i = 0; i < n;) { if (t[i] != 2) { i++; continue; } size_t j = i; while (j < n && t[j] == 2) j++;
    int a = i > 0 ? t[i - 1] : base, b = j < n ? t[j] : base; int d = a == b ? a : base; for (size_t k = i; k < j; k++) t[k] = d; i = j; }
  // levels: right-to-left line: RTL 1, LTR 2; left-to-right line: LTR 0, RTL 1
  std::vector<int> lv(n); for (size_t i = 0; i < n; i++) lv[i] = base == 0 ? (t[i] == 0 ? 1 : 2) : (t[i] == 0 ? 1 : 0);
  std::vector<size_t> ord(n); for (size_t i = 0; i < n; i++) ord[i] = i;
  int maxL = *std::max_element(lv.begin(), lv.end());
  for (int L = maxL; L >= 1; L--) for (size_t i = 0; i < n;) { if (lv[ord[i]] < L) { i++; continue; } size_t j = i; while (j < n && lv[ord[j]] >= L) j++; std::reverse(ord.begin() + i, ord.begin() + j); i = j; }
  std::u32string out; out.reserve(n);
  for (size_t k = 0; k < n; k++) { size_t i = ord[k]; char32_t c = s[i]; out += (lv[i] & 1) ? mirror(c) : c; }
  return out;
}
std::vector<std::u32string> visualLines(const std::u32string& logical) {
  std::vector<std::u32string> r; std::u32string cur;
  for (char32_t c : logical) { if (c == '\n') { r.push_back(reorderLine(cur)); cur.clear(); } else cur += c; }
  r.push_back(reorderLine(cur)); return r;
}
std::string visualText(const std::string& utf8) {
  std::u32string u = toU32(utf8); bool any = false; for (char32_t c : u) if (isArabic(c)) { any = true; break; }
  if (!any) return utf8;   // nothing to do (fast path for English)
  auto lines = visualLines(u); std::u32string out; for (size_t i = 0; i < lines.size(); i++) { if (i) out += U'\n'; out += lines[i]; }
  return toUtf8(out);
}
}
