#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "alsa.hpp"
#include "params.h"
#include "player.hpp"
#include "plugin_dir.h"
#include "ring.hpp"
#include "state.hpp"
#include "vst2_abi.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
namespace {
constexpr int FIELDS = 15;
int id(const char *k) {
  for (int i = 0; i < NPARAMS; ++i)
    if (!std::strcmp(PARAMS[i].key, k))
      return i;
  return -1;
}
struct Frame {
  double start, end;
  bool playing;
};
struct Input {
  int channel, note;
  bool on;
};
struct Request {
  int action, index, lane, part, channel, step;
  unsigned epoch;
};
struct Loaded {
  std::shared_ptr<const qw::Prepared> clip;
  int lane;
  bool preview;
  unsigned epoch;
};
struct Plugin {
  AEffect fx{};
  audioMasterCallback master;
  std::array<std::atomic<float>, NPARAMS> params;
  std::array<std::atomic<bool>, NPARAMS> triggers;
  qw::Ring<Frame, 256> frames;
  qw::Ring<Input, 256> inputs;
  std::atomic<bool> quit{false}, overflow{false}, suspend{false};
  std::atomic<unsigned> epoch{0};
  std::thread worker, loader;
  std::mutex core, io, text;
  std::condition_variable wake;
  std::deque<Request> requests;
  std::deque<Loaded> loaded;
  std::array<std::string, NPARAMS> display;
  std::array<std::shared_ptr<const qw::Prepared>, 4> beforePreview;
  std::array<bool, 4> previewing{};
  qw::Output output;
  std::unique_ptr<qw::Player> player;
  std::vector<uint8_t> chunk;
  std::string root;
  double sr = 44100;
  int uiFrames = 0;
  explicit Plugin(audioMasterCallback m) : master(m) {
    static_assert(std::atomic<float>::is_always_lock_free,
                  "Need lock-free VST controls");
    for (int i = 0; i < NPARAMS; ++i) {
      params[i].store(PARAMS[i].def);
      triggers[i] = false;
    }
    char here[512];
    root = mpc_plugin_dir(here, sizeof here)
               ? std::string(here) + "/quadweave-midi"
               : "./quadweave-midi";
    if (const char *p = std::getenv("QUADWEAVE_MIDI_ROOT"))
      root = p;
    player.reset(new qw::Player([this](double, int st, int ch, int n, int v) {
      output.send(st, ch, n, v);
    }));
    worker = std::thread([this] { work(); });
    try {
      loader = std::thread([this] { files(); });
    } catch (...) {
      quit = true;
      worker.join();
      throw;
    }
  }
  ~Plugin() {
    quit = true;
    wake.notify_all();
    if (loader.joinable())
      loader.join();
    if (worker.joinable())
      worker.join();
  }
  int value(int i) const {
    auto &p = PARAMS[i];
    float n = params[i].load();
    return int(
        std::lround(p.nopts ? n * (p.nopts - 1) : p.min + n * (p.max - p.min)));
  }
  int value(const char *k) const { return value(id(k)); }
  void say(const char *k, std::string s) {
    std::lock_guard<std::mutex> l(text);
    display[id(k)] = std::move(s);
  }
  qw::Settings cfg(int t) const {
    int b = t * FIELDS;
    qw::Settings c;
    c.enabled = value(b);
    c.speed = value(b + 1);
    c.direction = value(b + 2);
    c.channel = value(b + 3) - 1;
    c.shift = value(b + 4);
    c.source = value(b + 5);
    c.target = value(b + 6);
    c.source_scale = value(b + 7);
    c.target_scale = value(b + 8);
    c.transform = value(b + 9);
    c.follow = value(b + 10);
    c.anchor = value(b + 11);
    c.input_channel = value(b + 12);
    c.latch = value(b + 13);
    c.loop = value(b + 14);
    return c;
  }
  void enqueue(int a) {
    std::lock_guard<std::mutex> l(io);
    if (requests.size() < 32) {
      requests.push_back({a, value("selected"), value("destination"),
                          value("part"), value("file_channel"),
                          value("chord_step"), epoch.load()});
      wake.notify_one();
    }
  }
  void work() {
    say("output", output.status);
    auto last = std::chrono::steady_clock::now();
    while (!quit) {
      for (auto key : {"up", "refresh", "open", "load", "preview"})
        if (triggers[id(key)].exchange(false))
          enqueue(id(key));
      std::deque<Loaded> ready;
      {
        std::lock_guard<std::mutex> l(io);
        ready.swap(loaded);
      }
      {
        std::lock_guard<std::mutex> l(core);
        for (auto &r : ready)
          if (r.epoch == epoch.load()) {
            if (r.preview && !previewing[r.lane]) {
              beforePreview[r.lane] = player->clip(r.lane);
              previewing[r.lane] = true;
            }
            if (!r.preview) {
              previewing[r.lane] = false;
              beforePreview[r.lane].reset();
            }
            player->load(r.lane, r.clip);
            say(("clip" + std::to_string(r.lane)).c_str(), r.clip->clip.name);
            say("status", std::string(r.preview ? "Preview " : "Loaded ") +
                              r.clip->clip.name);
          }
        if (triggers[id("stop_preview")].exchange(false)) {
          ++epoch;
          for (int i = 0; i < 4; ++i)
            if (previewing[i]) {
              player->load(i, beforePreview[i]);
              previewing[i] = false;
              say(("clip" + std::to_string(i)).c_str(),
                  beforePreview[i] ? beforePreview[i]->clip.name : "Empty");
              beforePreview[i].reset();
            }
        }
        for (int i = 0; i < 4; ++i)
          player->configure(i, cfg(i));
        bool reset = overflow.exchange(false) || suspend.exchange(false) ||
                     triggers[id("panic")].exchange(false);
        if (reset) {
          Frame discard;
          while (frames.pop(discard)) {
          };
          Input mi;
          while (inputs.pop(mi)) {
          };
          player->panic();
          output.recover();
          say("status", "Stopped / reset");
        }
        Input mi;
        while (inputs.pop(mi))
          player->input(mi.channel, mi.note, mi.on);
        Frame f;
        std::array<Frame, 8> batch;
        size_t count = 0;
        bool backlog = false;
        while (frames.pop(f)) {
          if (count < batch.size())
            batch[count++] = f;
          else
            backlog = true;
        }
        if (count)
          last = std::chrono::steady_clock::now();
        if (backlog) {
          player->panic();
          say("status", "MIDI backlog: reset");
        } else
          for (size_t i = 0; i < count; ++i)
            player->process(batch[i].start, batch[i].end, batch[i].playing);
        if (std::chrono::steady_clock::now() - last >
            std::chrono::milliseconds(250))
          player->panic();
        if (output.failed) {
          player->panic();
          output.recover();
          say("output", "ALSA send failed");
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::lock_guard<std::mutex> l(core);
    player->panic();
    player.reset(new qw::Player([](double, int, int, int, int) {}));
  }
  void files() {
    std::string directory;
    std::vector<qw::Entry> entries;
    auto scan = [&] {
      if (directory.empty()) {
        root = qw::canonical(root);
        directory = root;
      }
      entries = qw::browse(root, directory);
      say("folder", directory);
      say("status", std::to_string(entries.size()) + " files/folders");
    };
    try {
      scan();
    } catch (const std::exception &e) {
      say("status", e.what());
    }
    int shown = -1;
    while (!quit) {
      Request req{};
      bool have = false;
      {
        std::unique_lock<std::mutex> l(io);
        wake.wait_for(l, std::chrono::milliseconds(40),
                      [&] { return quit || !requests.empty(); });
        if (quit)
          break;
        if (!requests.empty()) {
          req = requests.front();
          requests.pop_front();
          have = true;
        }
      }
      if (have)
        try {
          if (req.action == id("refresh"))
            scan();
          else if (req.action == id("up")) {
            if (directory != root) {
              directory = directory.substr(0, directory.find_last_of('/'));
              if (directory.empty())
                directory = "/";
              scan();
            }
          } else {
            if (req.index < 0 || size_t(req.index) >= entries.size())
              throw std::runtime_error("Select a file or folder");
            auto entry = entries[req.index];
            auto path = qw::canonical(entry.path);
            if (!qw::within(root, path))
              throw std::runtime_error("Outside MIDI folder");
            if (req.action == id("open")) {
              if (!entry.directory)
                throw std::runtime_error("Use Load for MIDI");
              directory = path;
              scan();
            } else {
              if (entry.directory)
                throw std::runtime_error("Open folder first");
              auto c = qw::load_clip(path, std::pow(2.0, req.step - 4));
              c.notes.erase(std::remove_if(
                                c.notes.begin(), c.notes.end(),
                                [&](const qw::Note &n) {
                                  return (req.part && n.part != req.part - 1) ||
                                         (req.channel &&
                                          n.channel != req.channel - 1);
                                }),
                            c.notes.end());
              if (c.notes.empty())
                throw std::runtime_error("No notes in selected part");
              auto clip = std::make_shared<qw::Prepared>(std::move(c));
              std::lock_guard<std::mutex> l(io);
              if (loaded.size() < 8)
                loaded.push_back(
                    {clip, req.lane, req.action == id("preview"), req.epoch});
            }
          }
          shown = -1;
        } catch (const std::exception &e) {
          say("status", e.what());
        }
      int selection = value("selected");
      if (shown != selection) {
        shown = selection;
        int page = (selection / 8) * 8;
        for (int i = 0; i < 8; ++i) {
          int at = page + i;
          std::string s;
          if (size_t(at) < entries.size())
            s = (at == selection ? "> " : "  ") + std::to_string(at) + " " +
                (entries[at].directory ? "[DIR] " : "") + entries[at].name;
          say(("row" + std::to_string(i)).c_str(), s);
        }
      }
    }
  }
  intptr_t save(void *p) {
    std::lock_guard<std::mutex> l(core);
    qw::StateWriter w;
    w.u32(0x51575331);
    w.u32(1);
    w.u32(4 * FIELDS);
    for (int i = 0; i < 4 * FIELDS; ++i)
      w.real(params[i].load());
    for (int i = 0; i < 4; ++i)
      qw::save_clip(w, previewing[i] ? beforePreview[i] : player->clip(i));
    chunk = std::move(w.bytes);
    *reinterpret_cast<void **>(p) = chunk.data();
    return chunk.size();
  }
  intptr_t restore(const void *p, size_t n) {
    if (!p || n > 16 * 1024 * 1024)
      return 0;
    try {
      qw::StateReader r{static_cast<const uint8_t *>(p), n};
      if (r.u32() != 0x51575331 || r.u32() != 1 || r.u32() != 4 * FIELDS)
        return 0;
      std::array<float, 4 * FIELDS> ps;
      for (auto &v : ps) {
        v = r.real();
        if (v < 0 || v > 1)
          return 0;
      }
      std::array<std::shared_ptr<const qw::Prepared>, 4> clips;
      for (auto &c : clips)
        c = qw::restore_clip(r);
      if (r.left)
        return 0;
      std::lock_guard<std::mutex> l(core);
      ++epoch;
      player->panic();
      for (int i = 0; i < 4 * FIELDS; ++i)
        params[i] = ps[i];
      for (int i = 0; i < 4; ++i) {
        player->configure(i, cfg(i));
        player->load(i, clips[i]);
        previewing[i] = false;
        beforePreview[i].reset();
        say(("clip" + std::to_string(i)).c_str(),
            clips[i] ? clips[i]->clip.name : "Empty");
      }
      suspend = true;
      return 1;
    } catch (...) {
      return 0;
    }
  }
};
void copy(void *p, const std::string &s, size_t n) {
  if (p) {
    std::strncpy(static_cast<char *>(p), s.c_str(), n - 1);
    static_cast<char *>(p)[n - 1] = 0;
  }
}
void set(AEffect *e, int32_t i, float v) {
  if (i < 0 || i >= NPARAMS || !std::isfinite(v))
    return;
  auto &w = *static_cast<Plugin *>(e->object);
  if (PARAMS[i].string_display)
    return;
  v = std::max(0.f, std::min(1.f, v));
  if (PARAMS[i].momentary) {
    if (v > 0.5f)
      w.triggers[i] = true;
  } else
    w.params[i] = v;
}
float get(AEffect *e, int32_t i) {
  if (i < 0 || i >= NPARAMS)
    return 0;
  return static_cast<Plugin *>(e->object)->params[i].load();
}
void process(AEffect *e, float **, float **out, int32_t n) {
  auto &w = *static_cast<Plugin *>(e->object);
  if (n <= 0)
    return;
  if (out)
    for (int ch = 0; ch < 2; ++ch)
      if (out[ch])
        std::fill(out[ch], out[ch] + n, 0.f);
  auto *t = reinterpret_cast<VstTimeInfo *>(
      w.master(e, audioMasterGetTime, 0, (1 << 9) | (1 << 10), nullptr, 0));
  bool valid = t && (t->flags & (1 << 9)) && (t->flags & (1 << 10)) &&
               std::isfinite(t->ppqPos) && std::isfinite(t->tempo) &&
               t->tempo > 0;
  if (valid) {
    double end = t->ppqPos + n * t->tempo / (60 * w.sr);
    if (!w.frames.push({t->ppqPos, end, bool(t->flags & 2)}))
      w.overflow = true;
  } else
    w.suspend = true;
  if ((w.uiFrames += n) >= int(w.sr / 4)) {
    w.uiFrames = 0;
    w.master(e, audioMasterUpdateDisplay, 0, 0, nullptr, 0);
  }
}
void accumulate(AEffect *e, float **, float **, int32_t n) {
  process(e, nullptr, nullptr, n);
}
intptr_t dispatch(AEffect *e, int32_t op, int32_t index, intptr_t v, void *p,
                  float opt) {
  auto &w = *static_cast<Plugin *>(e->object);
  switch (op) {
  case effClose:
    delete &w;
    return 1;
  case effOpen:
    return 1;
  case effGetPlugCategory:
    return 2;
  case effGetEffectName:
  case effGetProductString:
    copy(p, PLUG_NAME, 32);
    return 1;
  case effGetVendorString:
    copy(p, PLUG_VENDOR, 32);
    return 1;
  case effGetVendorVersion:
    return PLUG_VERSION;
  case effGetVstVersion:
    return 2400;
  case effSetSampleRate:
    if (std::isfinite(opt) && opt > 0)
      w.sr = opt;
    return 1;
  case effSetBlockSize:
    return 1;
  case effMainsChanged:
    if (!v)
      w.suspend = true;
    return 1;
  case effCanBeAutomated:
    return index >= 0 && index < 4 * FIELDS;
  case effGetParamName:
    if (index >= 0 && index < NPARAMS)
      copy(p, PARAMS[index].name, 32);
    return 1;
  case effGetParamLabel:
    copy(p, "", 8);
    return 1;
  case effGetParamDisplay:
    if (index >= 0 && index < NPARAMS) {
      auto &d = PARAMS[index];
      if (d.string_display) {
        std::lock_guard<std::mutex> l(w.text);
        copy(p, w.display[index], 24);
      } else if (d.nopts)
        copy(p, d.opts[w.value(index)], 24);
      else
        copy(p, std::to_string(w.value(index)), 24);
    }
    return 1;
  case effGetChunk:
    return p ? w.save(p) : 0;
  case effSetChunk:
    return v > 0 ? w.restore(p, size_t(v)) : 0;
  case effProcessEvents:
    if (p) {
      auto *ev = static_cast<VstEvents *>(p);
      if (ev->numEvents < 0 || ev->numEvents > 4096) {
        w.overflow = true;
        return 0;
      }
      for (int i = 0; i < ev->numEvents; ++i)
        if (ev->events[i] && ev->events[i]->type == 1 &&
            ev->events[i]->byteSize >= int(sizeof(VstMidiEvent))) {
          auto *m = reinterpret_cast<VstMidiEvent *>(ev->events[i]);
          int st = m->midiData[0] & 0xf0;
          if (st == 0x90 || st == 0x80) {
            if (!w.inputs.push({m->midiData[0] & 15, m->midiData[1],
                                st == 0x90 && m->midiData[2] != 0}))
              w.overflow = true;
          } else if (st == 0xb0 &&
                     (m->midiData[1] == 120 || m->midiData[1] == 123))
            w.suspend = true;
        }
    }
    return 1;
  case effCanDo:
    return p && (!std::strcmp(static_cast<char *>(p), "receiveVstEvents") ||
                 !std::strcmp(static_cast<char *>(p), "receiveVstMidiEvent") ||
                 !std::strcmp(static_cast<char *>(p), "receiveVstTimeInfo"))
               ? 1
               : -1;
  default:
    return 0;
  }
}
} // namespace
extern "C" __attribute__((visibility("default"))) AEffect *
VSTPluginMain(audioMasterCallback master) {
  if (!master)
    return nullptr;
  try {
    auto *w = new Plugin(master);
    auto &e = w->fx;
    e.magic = 0x56737450;
    e.dispatcher = dispatch;
    e.process = accumulate;
    e.processReplacing = process;
    e.setParameter = set;
    e.getParameter = get;
    e.numParams = NPARAMS;
    e.numOutputs = 2;
    e.flags = effFlagsCanReplacing | effFlagsIsSynth | effFlagsProgramChunks;
    e.uniqueID = PLUG_UID;
    e.version = PLUG_VERSION;
    e.object = w;
    return &e;
  } catch (...) {
    return nullptr;
  }
}
