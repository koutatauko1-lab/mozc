// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "renderer/win32/dopagaki_sound.h"

#include <mmreg.h>
#include <wil/com.h>
#include <windows.h>
#include <xaudio2.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "protocol/config.pb.h"
#include "renderer/win32/dopagaki_sound_synth.h"
#include "renderer/win32/dopagaki_state.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

// The number of sounds that can play at the same time.
constexpr size_t kVoiceCount = 16;
// Audio is turned off after this many failures to start it.
constexpr int kMaxFailures = 3;

void Log(const wchar_t* message, HRESULT hr) {
  wchar_t buffer[160] = {};
  ::swprintf_s(buffer, std::size(buffer), L"[dopagaki] sound %ls hr=0x%08lX\n",
               message, static_cast<unsigned long>(hr));
  ::OutputDebugStringW(buffer);
}

// Notified when the audio device is lost (e.g. headphones are unplugged).
class EngineCallback final : public IXAudio2EngineCallback {
 public:
  void STDMETHODCALLTYPE OnProcessingPassStart() override {}
  void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
  void STDMETHODCALLTYPE OnCriticalError(HRESULT /*error*/) override {
    failed_ = true;
  }
  bool failed() const { return failed_; }

 private:
  std::atomic<bool> failed_ = false;
};

}  // namespace

class DopagakiSound::Engine {
 public:
  struct Slot {
    IXAudio2SourceVoice* voice = nullptr;
    // Owned here until the voice finishes playing it.
    std::vector<float> samples;
  };

  Engine() = default;
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  ~Engine() {
    for (Slot& slot : slots) {
      if (slot.voice != nullptr) {
        slot.voice->DestroyVoice();
      }
    }
    if (mastering_voice != nullptr) {
      mastering_voice->DestroyVoice();
    }
    if (xaudio2) {
      xaudio2->UnregisterForCallbacks(&callback);
      xaudio2->StopEngine();
    }
  }

  bool Initialize() {
    // The default flags and processor.
    HRESULT hr = ::XAudio2Create(xaudio2.put());
    if (FAILED(hr)) {
      Log(L"XAudio2Create failed", hr);
      return false;
    }
    xaudio2->RegisterForCallbacks(&callback);
    hr = xaudio2->CreateMasteringVoice(&mastering_voice);
    if (FAILED(hr)) {
      Log(L"CreateMasteringVoice failed", hr);
      return false;
    }
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = 1;
    format.nSamplesPerSec = kDopagakiSampleRate;
    format.wBitsPerSample = 32;
    format.nBlockAlign = sizeof(float);
    format.nAvgBytesPerSec = kDopagakiSampleRate * sizeof(float);
    for (Slot& slot : slots) {
      hr = xaudio2->CreateSourceVoice(&slot.voice, &format);
      if (FAILED(hr)) {
        Log(L"CreateSourceVoice failed", hr);
        return false;
      }
      slot.voice->Start(0);
    }
    return true;
  }

  // Returns false if the samples could not be played because of an error.
  bool Play(std::vector<float> samples) {
    for (Slot& slot : slots) {
      XAUDIO2_VOICE_STATE state = {};
      slot.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
      if (state.BuffersQueued > 0) {
        continue;
      }
      slot.samples = std::move(samples);
      XAUDIO2_BUFFER buffer = {};
      buffer.Flags = XAUDIO2_END_OF_STREAM;
      buffer.AudioBytes =
          static_cast<UINT32>(slot.samples.size() * sizeof(float));
      buffer.pAudioData = reinterpret_cast<const BYTE*>(slot.samples.data());
      const HRESULT hr = slot.voice->SubmitSourceBuffer(&buffer);
      if (FAILED(hr)) {
        Log(L"SubmitSourceBuffer failed", hr);
        return false;
      }
      return true;
    }
    // All voices are busy. This sound is skipped.
    return true;
  }

  bool failed() const { return callback.failed(); }

 private:
  wil::com_ptr_nothrow<IXAudio2> xaudio2;
  IXAudio2MasteringVoice* mastering_voice = nullptr;
  std::array<Slot, kVoiceCount> slots;
  EngineCallback callback;
};

DopagakiSound::DopagakiSound()
    : random_engine_(std::random_device()()), distribution_(0.0, 1.0) {}

DopagakiSound::~DopagakiSound() { Shutdown(); }

void DopagakiSound::Shutdown() { engine_.reset(); }

bool DopagakiSound::EnsureEngine() {
  if (engine_ && !engine_->failed()) {
    return true;
  }
  // The audio device was lost (e.g. headphones were unplugged). Start again
  // with the current default device.
  engine_.reset();
  if (disabled_) {
    return false;
  }
  auto engine = std::make_unique<Engine>();
  if (!engine->Initialize()) {
    ++failure_count_;
    if (failure_count_ >= kMaxFailures) {
      Log(L"is turned off after repeated failures", E_FAIL);
      disabled_ = true;
    }
    return false;
  }
  engine_ = std::move(engine);
  return true;
}

void DopagakiSound::Play(DopagakiSoundSet sound_set, DopagakiSoundKind kind,
                         DopagakiRank rank) {
  if (sound_set == config::Config::DopagakiConfig::MUTE) {
    return;
  }
  if (!EnsureEngine()) {
    return;
  }
  std::vector<float> samples = SynthesizeDopagakiSound(
      sound_set, kind, static_cast<int>(rank),
      [this] { return distribution_(random_engine_); });
  if (samples.empty()) {
    return;
  }
  if (!engine_->Play(std::move(samples))) {
    engine_.reset();
  }
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
