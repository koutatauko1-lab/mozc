// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "win32/tip/tip_dopagaki_sound.h"

#include <windows.h>

// xaudio2.h (and mmreg.h in it) needs windows.h first.
#include <xaudio2.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <random>
#include <vector>

#include "protocol/config.pb.h"
#include "renderer/win32/dopagaki_sound_synth.h"

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

using ::mozc::renderer::win32::DopagakiSoundKind;
using ::mozc::renderer::win32::DopagakiSoundSet;
using ::mozc::renderer::win32::kDopagakiSampleRate;
using ::mozc::renderer::win32::SynthesizeDopagakiSound;

// The number of sounds that can play at the same time.
constexpr size_t kVoiceCount = 16;
// Audio is turned off in this process after this many failures to start it.
constexpr int kMaxFailures = 3;

void Log(const wchar_t* message, HRESULT hr) {
  wchar_t buffer[160] = {};
  ::swprintf_s(buffer, std::size(buffer),
               L"[dopagaki] tip sound %ls hr=0x%08lX\n", message,
               static_cast<unsigned long>(hr));
  ::OutputDebugStringW(buffer);
}

// The XAudio2 engine of this process. No callback is registered, so XAudio2
// never calls into this DLL. The samples are on the process heap, so they stay
// valid even if this DLL is unloaded while a sound is playing.
class Engine {
 public:
  Engine() = default;
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  ~Engine() {
    for (Slot& slot : slots_) {
      if (slot.voice != nullptr) {
        slot.voice->DestroyVoice();
      }
      FreeSamples(&slot);
    }
    if (mastering_voice_ != nullptr) {
      mastering_voice_->DestroyVoice();
    }
    if (xaudio2_ != nullptr) {
      xaudio2_->StopEngine();
      xaudio2_->Release();
    }
  }

  bool Initialize() {
    // The default flags and processor.
    HRESULT hr = ::XAudio2Create(&xaudio2_);
    if (FAILED(hr)) {
      Log(L"XAudio2Create failed", hr);
      return false;
    }
    hr = xaudio2_->CreateMasteringVoice(&mastering_voice_);
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
    for (Slot& slot : slots_) {
      hr = xaudio2_->CreateSourceVoice(&slot.voice, &format);
      if (FAILED(hr)) {
        Log(L"CreateSourceVoice failed", hr);
        return false;
      }
      slot.voice->Start(0);
    }
    return true;
  }

  // Returns false on an error (the engine should be created again).
  bool Play(const std::vector<float>& samples) {
    for (Slot& slot : slots_) {
      XAUDIO2_VOICE_STATE state = {};
      slot.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
      if (state.BuffersQueued > 0) {
        continue;
      }
      FreeSamples(&slot);
      const size_t bytes = samples.size() * sizeof(float);
      slot.samples = ::HeapAlloc(::GetProcessHeap(), 0, bytes);
      if (slot.samples == nullptr) {
        return true;
      }
      std::memcpy(slot.samples, samples.data(), bytes);
      XAUDIO2_BUFFER buffer = {};
      buffer.Flags = XAUDIO2_END_OF_STREAM;
      buffer.AudioBytes = static_cast<UINT32>(bytes);
      buffer.pAudioData = static_cast<const BYTE*>(slot.samples);
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

 private:
  struct Slot {
    IXAudio2SourceVoice* voice = nullptr;
    void* samples = nullptr;
  };

  static void FreeSamples(Slot* slot) {
    if (slot->samples != nullptr) {
      ::HeapFree(::GetProcessHeap(), 0, slot->samples);
      slot->samples = nullptr;
    }
  }

  IXAudio2* xaudio2_ = nullptr;
  IXAudio2MasteringVoice* mastering_voice_ = nullptr;
  std::array<Slot, kVoiceCount> slots_;
};

// Guarded by |g_lock|. A raw pointer on purpose: it must not be destroyed by a
// static destructor in DllMain, where XAudio2 may deadlock.
SRWLOCK g_lock = SRWLOCK_INIT;
Engine* g_engine = nullptr;
int g_active_count = 0;
int g_failure_count = 0;

double NextRandom() {
  static std::mt19937 engine(::GetTickCount());
  static std::uniform_real_distribution<double> distribution(0.0, 1.0);
  return distribution(engine);
}

}  // namespace

void TipDopagakiSound::OnActivate() {
  ::AcquireSRWLockExclusive(&g_lock);
  ++g_active_count;
  ::ReleaseSRWLockExclusive(&g_lock);
}

void TipDopagakiSound::OnDeactivate() {
  Engine* engine_to_delete = nullptr;
  ::AcquireSRWLockExclusive(&g_lock);
  if (g_active_count > 0) {
    --g_active_count;
  }
  if (g_active_count == 0) {
    engine_to_delete = g_engine;
    g_engine = nullptr;
  }
  ::ReleaseSRWLockExclusive(&g_lock);
  // Destroying voices waits for the audio thread, so not under the lock.
  delete engine_to_delete;
}

void TipDopagakiSound::Play(DopagakiSoundSet sound_set, DopagakiSoundKind kind,
                            int rank_index) {
  if (sound_set == config::Config::DopagakiConfig::MUTE) {
    return;
  }
  ::AcquireSRWLockExclusive(&g_lock);
  if (g_engine == nullptr && g_active_count > 0 &&
      g_failure_count < kMaxFailures) {
    Engine* engine = new Engine();
    if (engine->Initialize()) {
      g_engine = engine;
    } else {
      delete engine;
      ++g_failure_count;
      if (g_failure_count == kMaxFailures) {
        Log(L"is turned off after repeated failures", E_FAIL);
      }
    }
  }
  if (g_engine != nullptr) {
    const std::vector<float> samples =
        SynthesizeDopagakiSound(sound_set, kind, rank_index, &NextRandom);
    if (!samples.empty() && !g_engine->Play(samples)) {
      // Something went wrong (e.g. the audio device was lost). Start again
      // with the next sound.
      delete g_engine;
      g_engine = nullptr;
    }
  }
  ::ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
