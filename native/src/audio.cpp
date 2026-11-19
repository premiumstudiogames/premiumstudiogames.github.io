#include "audio.h"
#include "log.h"
#include <miniaudio.h>
#include <map>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace pge {
using json = nlohmann::json;
bool g_headlessAudio = false;

/* ------------------------------------------------------------------ sounds made by code */
// a value over time: a number, [from, to] over the voice, or points [[t, v], [t, v, 'exp'], ...] (like Web Audio's
// setValueAtTime / linearRampToValueAtTime / exponentialRampToValueAtTime; t in seconds from the voice's start)
struct Curve {
  std::vector<float> t, v; std::vector<char> ex;
  bool parse(const json& j, float len, bool expDefault, std::string& why, const char* what) {
    t.clear(); v.clear(); ex.clear();
    if (j.is_number()) { t = {0}; v = {j.get<float>()}; ex = {0}; return true; }
    if (j.is_array() && j.size() == 2 && j[0].is_number() && j[1].is_number()) { t = {0, len}; v = {j[0].get<float>(), j[1].get<float>()}; ex = {0, (char)(expDefault && v[0] > 0 && v[1] > 0)}; return true; }
    if (j.is_array() && !j.empty()) { for (auto& p : j) { if (!p.is_array() || p.size() < 2 || !p[0].is_number() || !p[1].is_number()) { why = std::string(what) + ": points are [time, value] or [time, value, 'exp']"; return false; }
        t.push_back(p[0].get<float>()); v.push_back(p[1].get<float>()); ex.push_back(p.size() > 2 && p[2].is_string() && p[2].get<std::string>() == "exp"); }
      for (size_t i = 1; i < t.size(); i++) if (t[i] < t[i - 1]) { why = std::string(what) + ": point times must go up"; return false; }
      return true; }
    why = std::string(what) + ": a number, [from, to] or [[time, value], ...]"; return false;
  }
  float at(float x) const {
    if (t.empty()) return 0; if (x <= t[0]) return v[0];
    for (size_t i = 1; i < t.size(); i++) if (x < t[i]) { float a = (x - t[i - 1]) / std::max(1e-6f, t[i] - t[i - 1]);
        if (ex[i] && v[i - 1] > 0 && v[i] > 0) return v[i - 1] * std::pow(v[i] / v[i - 1], a); return v[i - 1] + (v[i] - v[i - 1]) * a; }
    return v.back();
  }
};
static float polyBlep(float t, float dt) { if (t < dt) { t /= dt; return t + t - t * t - 1; } if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; } return 0; }
struct Biquad {
  int type = 0; float q = 1; Curve f; double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  void coeffs(float fc, int sr) {   // Web Audio's BiquadFilterNode (Q in dB for lowpass / highpass)
    fc = std::clamp(fc, 10.f, sr * 0.49f); double w = 6.283185307179586 * fc / sr, c = std::cos(w), s = std::sin(w);
    double al = type <= 1 ? s / (2 * std::pow(10.0, q / 20.0)) : s / (2 * std::max(1e-4f, q)), a0 = 1 + al;
    if (type == 0) { b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; }
    else if (type == 1) { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; }
    else if (type == 2) { b0 = al; b1 = 0; b2 = -al; }
    else { b0 = 1; b1 = -2 * c; b2 = 1; }
    b0 /= a0; b1 /= a0; b2 /= a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
  }
  float run(float x) { double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return (float)y; }
};

std::string synthRender(const json& def0, std::vector<float>& out, int sr) {
  out.clear();
  json def = def0; if (!def.is_object()) return "a sound description is an object";
  json voices = def.contains("voices") ? def["voices"] : json::array({def});
  if (!voices.is_array() || voices.empty()) return "voices: a list of voices";
  if (voices.size() > 64) return "at most 64 voices";
  // how long: the given duration, or the end of the last voice (+ the echo's tail)
  float total = def.value("duration", 0.f), endMax = 0;
  for (auto& v : voices) { if (!v.is_object()) return "each voice is an object"; float st = v.value("start", 0.f), ln = v.value("length", v.value("duration", 0.f));
    if (ln <= 0) { float fromGain = 0; if (v.contains("gain") && v["gain"].is_object()) { auto& g = v["gain"]; fromGain = g.value("attack", 0.f) + g.value("decay", 0.f) + g.value("hold", 0.f) + g.value("release", 0.f); }
      else if (v.contains("gain") && v["gain"].is_array() && !v["gain"].empty() && v["gain"].back().is_array()) fromGain = v["gain"].back()[0].get<float>();
      ln = total > 0 ? total - st : fromGain > 0 ? fromGain : 0.5f; }
    endMax = std::max(endMax, st + ln); }
  json echo = def.value("echo", json());
  float tail = echo.is_object() ? echo.value("time", 0.25f) * 6 : 0;
  if (total <= 0) total = endMax + tail;
  if (!(total > 0) || total > 30) return "a sound made by code lasts up to 30 seconds";
  out.assign((size_t)std::ceil(total * sr), 0.f);
  uint32_t seed = def.value("seed", 12345u);
  for (auto& v : voices) {
    std::string why; std::string type = v.value("type", v.value("wave", std::string("sine")));
    int wave = type == "sine" ? 0 : type == "square" ? 1 : type == "sawtooth" || type == "saw" ? 2 : type == "triangle" ? 3 : type == "noise" || type == "white" ? 4 : type == "pink" ? 5 : type == "brown" ? 6 : -1;
    if (wave < 0) return "type: sine, square, sawtooth, triangle, noise (white), pink or brown";
    float st = std::max(0.f, v.value("start", 0.f)), ln = v.value("length", v.value("duration", 0.f));
    Curve freq, gain;
    if (ln <= 0) { float fromGain = 0; if (v.contains("gain") && v["gain"].is_object()) { auto& g = v["gain"]; fromGain = g.value("attack", 0.f) + g.value("decay", 0.f) + g.value("hold", 0.f) + g.value("release", 0.f); }
      else if (v.contains("gain") && v["gain"].is_array() && !v["gain"].empty() && v["gain"].back().is_array()) fromGain = v["gain"].back()[0].get<float>();
      ln = def.value("duration", 0.f) > 0 ? def.value("duration", 0.f) - st : fromGain > 0 ? fromGain : 0.5f; }
    std::string curve = v.value("curve", std::string());
    if (!freq.parse(v.value("freq", json(440)), ln, curve != "linear", why, "freq")) return why;
    // gain: a number, points, or an envelope { attack, decay, sustain, hold, release, peak }
    if (v.contains("gain") && v["gain"].is_object()) { auto& g = v["gain"]; float A = g.value("attack", 0.005f), Dc = g.value("decay", 0.f), S = g.value("sustain", 1.f), R = g.value("release", 0.05f), P = g.value("peak", 1.f);
      float H = g.contains("hold") ? g["hold"].get<float>() : std::max(0.f, ln - A - Dc - R);
      bool ex = g.value("curve", std::string()) == "exp";
      gain.t = {0, A, A + Dc, A + Dc + H, A + Dc + H + R}; gain.v = {0, P, P * S, P * S, 0}; gain.ex = {0, 0, ex, 0, ex};
      if (ex) { gain.v[4] = 0.0001f; } }
    else if (!gain.parse(v.value("gain", json(1)), ln, false, why, "gain")) return why;
    gain.ex.resize(gain.t.size(), 0);
    float vol = v.value("volume", 1.f), detune = v.value("detune", 0.f), duty = std::clamp(v.value("duty", 0.5f), 0.01f, 0.99f);
    float vibR = 0, vibD = 0; if (v.contains("vibrato") && v["vibrato"].is_object()) { vibR = v["vibrato"].value("rate", 5.f); vibD = v["vibrato"].value("depth", 20.f); }
    std::vector<Biquad> filters;
    json fl = v.value("filter", json()); if (fl.is_object()) fl = json::array({fl});
    if (fl.is_array()) for (auto& f : fl) { if (!f.is_object()) continue; Biquad b; std::string ft = f.value("type", std::string("lowpass"));
      b.type = ft == "lowpass" ? 0 : ft == "highpass" ? 1 : ft == "bandpass" ? 2 : ft == "notch" ? 3 : -1; if (b.type < 0) return "filter type: lowpass, highpass, bandpass or notch";
      b.q = f.value("q", f.value("Q", 1.f)); if (!b.f.parse(f.value("freq", json(1000)), ln, f.value("curve", std::string()) != "linear", why, "filter freq")) return why; filters.push_back(b); }
    uint32_t rs = seed ^ (uint32_t)(std::hash<std::string>()(v.dump()) & 0x7fffffff); if (v.contains("seed")) rs = v["seed"].get<uint32_t>();
    auto rnd = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (rs & 0xffffff) / 8388607.5f - 1.f; };
    double ph = 0; float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, brown = 0;
    size_t i0 = (size_t)(st * sr), n = (size_t)(ln * sr);
    for (size_t k = 0; k < n && i0 + k < out.size(); k++) {
      float t = k / (float)sr, s = 0;
      if ((k & 31) == 0) for (auto& b : filters) b.coeffs(b.f.at(t), sr);
      if (wave <= 3) {
        float f = freq.at(t) * std::pow(2.f, (detune + (vibD ? vibD * std::sin(6.2831853f * vibR * t) : 0)) / 1200.f), dt = std::clamp(f / sr, 0.f, 0.5f);
        float p = (float)ph;
        if (wave == 0) s = std::sin(6.2831853f * p);
        else if (wave == 1) { s = (p < duty ? 1.f : -1.f) + polyBlep(p, dt) - polyBlep(std::fmod(p + 1 - duty, 1.f), dt); }
        else if (wave == 2) s = 2 * p - 1 - polyBlep(p, dt);
        else s = 1 - 4 * std::abs(p - 0.5f) - 0;   // triangle (-1 .. 1 starting at -1 like Web Audio's phase)
        if (wave == 3) s = -s;
        ph += dt; ph -= std::floor(ph);
      } else {
        float w = rnd();
        if (wave == 4) s = w;
        else if (wave == 5) { b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.969f * b2 + w * 0.153852f; b3 = 0.8665f * b3 + w * 0.3104856f; b4 = 0.55f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.016898f;
          s = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f) * 0.11f; b6 = w * 0.115926f; }
        else { brown = (brown + 0.02f * w) / 1.02f; s = brown * 3.5f; }
      }
      for (auto& b : filters) s = b.run(s);
      out[i0 + k] += s * gain.at(t) * vol;
    }
  }
  if (echo.is_object()) { float et = std::clamp(echo.value("time", 0.25f), 0.01f, 2.f), fb = std::clamp(echo.value("feedback", 0.35f), 0.f, 0.95f), mixv = std::clamp(echo.value("mix", 0.35f), 0.f, 1.f);
    size_t dly = (size_t)(et * sr); std::vector<float> wet(out.size(), 0.f);
    for (size_t i = dly; i < out.size(); i++) wet[i] = (out[i - dly] + wet[i - dly]) * fb;
    for (size_t i = 0; i < out.size(); i++) out[i] += wet[i] * mixv / std::max(0.01f, fb); }
  float master = def.value("volume", 1.f);
  for (float& s : out) s = std::clamp(s * master, -1.f, 1.f);
  return "";
}

/* ------------------------------------------------------------------ the mixer */
struct Clip { std::vector<float> s; int ch = 1, rate = 44100; };
struct Voice {
  int h = 0; ma_sound snd{}; ma_audio_buffer_ref ref{}; bool hasRef = false, spatial = false, loop = false, pausedByUs = false, fading = false; std::shared_ptr<Clip> clip; std::string bus, attach;
};
struct Audio::Impl {
  ma_engine eng{}; bool ok = false, device = false;
  std::map<std::string, std::unique_ptr<ma_sound_group>> buses; std::map<std::string, float> busVol;
  std::map<std::string, std::shared_ptr<Clip>> clips;
  std::map<int, std::unique_ptr<Voice>> voices; int next = 1; bool paused = false;
  ~Impl() { voices.clear(); for (auto& [k, g] : buses) ma_sound_group_uninit(g.get()); buses.clear(); if (ok) ma_engine_uninit(&eng); }
  ma_sound_group* bus(const std::string& name0) {
    std::string name = name0.empty() ? "sfx" : name0; auto it = buses.find(name); if (it != buses.end()) return it->second.get();
    auto g = std::make_unique<ma_sound_group>(); if (ma_sound_group_init(&eng, 0, nullptr, g.get()) != MA_SUCCESS) return nullptr;
    auto bv = busVol.find(name); if (bv != busVol.end()) ma_sound_group_set_volume(g.get(), bv->second);
    ma_sound_group* r = g.get(); buses[name] = std::move(g); return r; }
  void freeVoice(Voice& v) { ma_sound_uninit(&v.snd); if (v.hasRef) ma_audio_buffer_ref_uninit(&v.ref); }
};

Audio::Audio(bool headless) : d(new Impl) {
  ma_engine_config c = ma_engine_config_init();
  if (headless) { c.noDevice = MA_TRUE; c.channels = 2; c.sampleRate = 48000; }
  if (ma_engine_init(&c, &d->eng) == MA_SUCCESS) { d->ok = true; d->device = !headless; }
  else if (!headless) {   // no sound card: the game still runs (silent)
    ma_engine_config q = ma_engine_config_init(); q.noDevice = MA_TRUE; q.channels = 2; q.sampleRate = 48000;
    if (ma_engine_init(&q, &d->eng) == MA_SUCCESS) d->ok = true;
    logMsg("warning: sound: no sound device, the game is silent", LogKind::Warn);
  }
  if (d->ok) ma_engine_listener_set_world_up(&d->eng, 0, 0, 1, 0);
}
Audio::~Audio() { if (d) for (auto& [h, v] : d->voices) d->freeVoice(*v); }
bool Audio::ok() const { return d->ok; }
bool Audio::hasDevice() const { return d->device; }
bool Audio::has(const std::string& name) const { return d->clips.count(name) > 0; }

std::string Audio::make(const std::string& name, const json& def) {
  if (name.empty()) return "a name is needed";
  std::vector<float> s; std::string e = synthRender(def, s, 44100); if (!e.empty()) return e;
  return makeSamples(name, std::move(s), 1, 44100);
}
std::string Audio::makeSamples(const std::string& name, std::vector<float> samples, int ch, int rate) {
  if (ch < 1 || ch > 2) return "channels: 1 or 2"; if (rate < 4000 || rate > 192000) return "sampleRate: 4000 .. 192000";
  if (samples.size() < (size_t)ch) return "no samples"; if (samples.size() / ch > (size_t)rate * 600) return "too long (10 minutes at most)";
  auto c = std::make_shared<Clip>(); c->s = std::move(samples); c->ch = ch; c->rate = rate; d->clips[name] = c; return "";
}

static glm::vec3 vecOf(const json& j, bool& ok) {
  ok = false; if (j.is_array() && j.size() >= 3 && j[0].is_number()) { ok = true; return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()}; }
  if (j.is_object() && j.contains("x")) { ok = true; return {j.value("x", 0.f), j.value("y", 0.f), j.value("z", 0.f)}; } return glm::vec3(0);
}
static void applyOpts(Voice& v, const json& o) {
  if (!o.is_object()) return;
  if (o.contains("volume") && o["volume"].is_number()) ma_sound_set_volume(&v.snd, std::max(0.f, o["volume"].get<float>()));
  if (o.contains("pitch") && o["pitch"].is_number()) ma_sound_set_pitch(&v.snd, std::clamp(o["pitch"].get<float>(), 0.05f, 8.f));
  if (o.contains("pan") && o["pan"].is_number()) ma_sound_set_pan(&v.snd, std::clamp(o["pan"].get<float>(), -1.f, 1.f));
  if (o.contains("loop") && o["loop"].is_boolean()) { v.loop = o["loop"].get<bool>(); ma_sound_set_looping(&v.snd, v.loop); }
  bool ok = false; glm::vec3 p = o.contains("position") ? vecOf(o["position"], ok) : glm::vec3(0);
  if (o.contains("follow") && o["follow"].is_string()) { v.attach = o["follow"]; ok = true; }
  if (ok) { v.spatial = true; ma_sound_set_spatialization_enabled(&v.snd, MA_TRUE); ma_sound_set_position(&v.snd, p.x, p.y, p.z); }
  else if (o.contains("position") && o["position"].is_null()) { v.spatial = false; v.attach.clear(); ma_sound_set_spatialization_enabled(&v.snd, MA_FALSE); }
  if (o.contains("minDistance")) ma_sound_set_min_distance(&v.snd, std::max(0.01f, o["minDistance"].get<float>()));
  if (o.contains("maxDistance")) ma_sound_set_max_distance(&v.snd, std::max(0.1f, o["maxDistance"].get<float>()));
  if (o.contains("rolloff")) ma_sound_set_rolloff(&v.snd, std::max(0.f, o["rolloff"].get<float>()));
}
static int startVoice(Audio::Impl& D, std::unique_ptr<Voice> vp, const json& o, std::string* why) {
  Voice& v = *vp;
  ma_sound_set_spatialization_enabled(&v.snd, MA_FALSE);
  ma_sound_set_attenuation_model(&v.snd, ma_attenuation_model_inverse); ma_sound_set_min_distance(&v.snd, 2.f); ma_sound_set_max_distance(&v.snd, 80.f); ma_sound_set_rolloff(&v.snd, 1.f);
  applyOpts(v, o);
  if (o.is_object() && o.contains("fadeIn") && o["fadeIn"].is_number()) { float vol = o.value("volume", 1.f); ma_sound_set_fade_in_milliseconds(&v.snd, 0, vol, (ma_uint64)(std::max(0.f, o["fadeIn"].get<float>()) * 1000)); }
  if (o.is_object() && o.contains("delay") && o["delay"].is_number() && o["delay"].get<float>() > 0) ma_sound_set_start_time_in_milliseconds(&v.snd, ma_engine_get_time_in_milliseconds(&D.eng) + (ma_uint64)(o["delay"].get<float>() * 1000));
  if (ma_sound_start(&v.snd) != MA_SUCCESS) { D.freeVoice(v); if (why) *why = "the sound could not start"; return 0; }
  if (D.paused) { ma_sound_stop(&v.snd); v.pausedByUs = true; }
  // too many at once: the oldest that doesn't loop goes
  if (D.voices.size() >= 160) for (auto it = D.voices.begin(); it != D.voices.end(); ++it) if (!it->second->loop) { D.freeVoice(*it->second); D.voices.erase(it); break; }
  int h = D.next++; v.h = h; D.voices[h] = std::move(vp); return h;
}
int Audio::play(const std::string& src, const json& o, std::string* why) {
  if (!d->ok) { if (why) *why = "no sound"; return 0; }
  auto busName = o.is_object() ? o.value("bus", std::string("sfx")) : std::string("sfx");
  auto it = d->clips.find(src);
  auto vp = std::make_unique<Voice>(); Voice& v = *vp; v.bus = busName;
  if (it != d->clips.end()) {
    v.clip = it->second; ma_audio_buffer_ref_init(ma_format_f32, (ma_uint32)v.clip->ch, v.clip->s.data(), v.clip->s.size() / v.clip->ch, &v.ref); v.ref.sampleRate = (ma_uint32)v.clip->rate; v.hasRef = true;
    if (ma_sound_init_from_data_source(&d->eng, &v.ref, 0, d->bus(busName), &v.snd) != MA_SUCCESS) { ma_audio_buffer_ref_uninit(&v.ref); if (why) *why = "the sound could not start"; return 0; }
  } else {
    std::string file = find_ ? find_(src) : src;
    ma_uint32 fl = (o.is_object() && o.value("stream", false)) ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    if (ma_sound_init_from_file(&d->eng, file.c_str(), fl, d->bus(busName), nullptr, &v.snd) != MA_SUCCESS) { if (why) *why = "can't play '" + src + "' (no such sound, or not wav / ogg / mp3 / flac)"; return 0; }
  }
  return startVoice(*d, std::move(vp), o, why);
}
int Audio::playSamples(std::vector<float> s, int ch, int rate, const json& o, std::string* why) {
  std::string name = "\x01tmp" + std::to_string(d->next); std::string e = makeSamples(name, std::move(s), ch, rate); if (!e.empty()) { if (why) *why = e; return 0; }
  int h = play(name, o, why); d->clips.erase(name); return h;   // the voice keeps the samples alive
}
int Audio::playDef(const json& def, const json& o, std::string* why) {
  std::vector<float> s; std::string e = synthRender(def, s, 44100); if (!e.empty()) { if (why) *why = e; return 0; }
  return playSamples(std::move(s), 1, 44100, o, why);
}
void Audio::set(int h, const json& o) { auto it = d->voices.find(h); if (it == d->voices.end()) return; applyOpts(*it->second, o); }
void Audio::stop(int h, float fade) {
  auto it = d->voices.find(h); if (it == d->voices.end()) return;
  if (fade > 0) { ma_sound_stop_with_fade_in_milliseconds(&it->second->snd, (ma_uint64)(fade * 1000)); it->second->loop = false; it->second->fading = true; ma_sound_set_looping(&it->second->snd, MA_FALSE); return; }
  d->freeVoice(*it->second); d->voices.erase(it);
}
void Audio::stopAll(const std::string& bus) {
  for (auto it = d->voices.begin(); it != d->voices.end();) { if (bus.empty() || it->second->bus == bus) { d->freeVoice(*it->second); it = d->voices.erase(it); } else ++it; }
}
bool Audio::playing(int h) const { auto it = d->voices.find(h); return it != d->voices.end() && (it->second->pausedByUs || !ma_sound_at_end(&it->second->snd)); }
void Audio::setVolume(const std::string& bus, float v) {
  v = std::max(0.f, v); if (!d->ok) return;
  if (bus.empty() || bus == "master") { ma_engine_set_volume(&d->eng, v); d->busVol["master"] = v; return; }
  d->busVol[bus] = v; if (ma_sound_group* g = d->bus(bus)) ma_sound_group_set_volume(g, v);
}
float Audio::volume(const std::string& bus) const { auto it = d->busVol.find(bus.empty() ? "master" : bus); return it == d->busVol.end() ? 1.f : it->second; }
void Audio::pause(bool on) {
  if (on == d->paused) return; d->paused = on;
  for (auto& [h, v] : d->voices) { if (on) { if (ma_sound_is_playing(&v->snd)) { ma_sound_stop(&v->snd); v->pausedByUs = true; } } else if (v->pausedByUs) { ma_sound_start(&v->snd); v->pausedByUs = false; } }
}
void Audio::update(const glm::vec3& ear, const glm::vec3& fwd, std::function<bool(const std::string&, glm::vec3&)> actorPos) {
  if (!d->ok) return;
  ma_engine_listener_set_position(&d->eng, 0, ear.x, ear.y, ear.z);
  glm::vec3 f = glm::length(fwd) > 1e-4f ? glm::normalize(fwd) : glm::vec3(0, 0, -1); ma_engine_listener_set_direction(&d->eng, 0, f.x, f.y, f.z);
  for (auto it = d->voices.begin(); it != d->voices.end();) { Voice& v = *it->second;
    if (!v.attach.empty()) { glm::vec3 p; if (actorPos && actorPos(v.attach, p)) ma_sound_set_position(&v.snd, p.x, p.y, p.z); else { d->freeVoice(v); it = d->voices.erase(it); continue; } }   // its actor is gone: so is the sound
    if (!v.pausedByUs && ((!v.loop && ma_sound_at_end(&v.snd)) || (v.fading && !ma_sound_is_playing(&v.snd)))) { d->freeVoice(v); it = d->voices.erase(it); continue; }
    ++it; }
}
int Audio::count() const { return (int)d->voices.size(); }
void Audio::mix(float* out, int frames) { if (!d->ok) { std::fill(out, out + frames * 2, 0.f); return; } ma_uint64 got = 0; ma_engine_read_pcm_frames(&d->eng, out, (ma_uint64)frames, &got); if ((int)got < frames) std::fill(out + got * 2, out + frames * 2, 0.f); }
}
