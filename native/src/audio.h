#pragma once
// Sound for games (miniaudio): wav / ogg / mp3 / flac files, sounds made by code (oscillators, noise, filters, envelopes,
// like the Web Audio sounds of BLOCKFALL), or samples from a script; 3D sounds heard from where they are (the listener is
// the camera), and volumes for the whole game and for groups (music, sfx, ui, ...).
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace pge {

// a sound made by code: renders mono samples (44.1 kHz) from a description; "" = ok, else what is wrong
std::string synthRender(const nlohmann::json& def, std::vector<float>& out, int sampleRate = 44100);

extern bool g_headlessAudio;   // tests: games make their sound without a sound card

class Audio {
public:
  // headless: no sound card, frames are pulled with mix() (tests, and machines without audio)
  explicit Audio(bool headless = false);
  ~Audio();
  bool ok() const;                   // made (with or without a sound card)
  bool hasDevice() const;
  void setFiles(std::function<std::string(const std::string&)> find) { find_ = std::move(find); }   // asset name → file

  // a named sound kept in memory: samples (mono or stereo), or a description for synthRender
  std::string make(const std::string& name, const nlohmann::json& def);
  std::string makeSamples(const std::string& name, std::vector<float> samples, int channels, int sampleRate);
  bool has(const std::string& name) const;

  // play: name of a made sound, a file ('sounds/hit.ogg'), or a description. Returns a handle (> 0) or 0 and why
  int play(const std::string& src, const nlohmann::json& opts, std::string* why = nullptr);
  int playDef(const nlohmann::json& def, const nlohmann::json& opts, std::string* why = nullptr);
  int playSamples(std::vector<float> samples, int channels, int sampleRate, const nlohmann::json& opts, std::string* why = nullptr);
  void set(int h, const nlohmann::json& opts);
  void stop(int h, float fadeSec = 0);
  void stopAll(const std::string& bus = "");
  bool playing(int h) const;
  void setVolume(const std::string& bus, float v);   // "" or "master" = everything
  float volume(const std::string& bus) const;
  void pause(bool on);                 // the game's pause menu: everything stops where it is
  // every frame: the listener (camera), sounds that follow actors, finished sounds go
  void update(const glm::vec3& earPos, const glm::vec3& earForward, std::function<bool(const std::string&, glm::vec3&)> actorPos);
  int count() const;                   // sounds alive
  void mix(float* stereoOut, int frames);   // headless: the next frames of the mix
  struct Impl;
private:
  std::unique_ptr<Impl> d; std::function<std::string(const std::string&)> find_;
};
}
