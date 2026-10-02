#pragma once
#include <array>
#include <string>
#ifndef QW_NO_ALSA
#include <alsa/asoundlib.h>
#endif
namespace qw {
// Owned exclusively by the playback worker. No ALSA call is made by the audio
// callback.
class Output {
#ifndef QW_NO_ALSA
  snd_seq_t *seq_ = nullptr;
  int port_ = -1;
#endif
  std::array<std::array<bool, 128>, 16> sounding_{};

public:
  bool failed = false;
  std::string status;
  Output() {
#ifdef QW_NO_ALSA
    status = "Host test: MIDI sink";
#else
    if (snd_seq_open(&seq_, "default", SND_SEQ_OPEN_OUTPUT, SND_SEQ_NONBLOCK) <
        0) {
      status = "ALSA unavailable";
      return;
    }
    snd_seq_set_client_name(seq_, "QUADWEAVE");
    port_ = snd_seq_create_simple_port(
        seq_, "MIDI Out", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    status = port_ < 0 ? "ALSA port failed"
                       : "ALSA " + std::to_string(snd_seq_client_id(seq_)) +
                             ":" + std::to_string(port_);
#endif
  }
  bool send(int st, int ch, int note, int vel) {
    (void)vel;
#ifndef QW_NO_ALSA
    if (!seq_ || port_ < 0)
      return false;
    snd_seq_event_t e;
    snd_seq_ev_clear(&e);
    snd_seq_ev_set_source(&e, port_);
    snd_seq_ev_set_subs(&e);
    snd_seq_ev_set_direct(&e);
    if (st == 0x90)
      snd_seq_ev_set_noteon(&e, ch, note, vel);
    else
      snd_seq_ev_set_noteoff(&e, ch, note, 0);
    if (snd_seq_event_output_direct(seq_, &e) < 0) {
      failed = true;
      return false;
    }
#endif
    sounding_[ch][note] = st == 0x90;
    return true;
  }
  void recover() {
    failed = false;
    for (int ch = 0; ch < 16; ++ch)
      for (int n = 0; n < 128; ++n)
        if (sounding_[ch][n])
          send(0x80, ch, n, 0);
  }
  ~Output() {
    recover();
#ifndef QW_NO_ALSA
    if (seq_)
      snd_seq_close(seq_);
#endif
  }
};
} // namespace qw
