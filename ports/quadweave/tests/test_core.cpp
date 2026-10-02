#include "player.hpp"
#include "ring.hpp"
#include "state.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace qw;
struct Message {
  double at;
  int st, ch, n, v;
};
static void be(std::vector<uint8_t> &b, unsigned n, int bytes) {
  while (bytes--)
    b.push_back((n >> (8 * bytes)) & 255);
}
static std::vector<uint8_t> midi(const std::vector<uint8_t> &track) {
  std::vector<uint8_t> b;
  be(b, 0x4d546864, 4);
  be(b, 6, 4);
  be(b, 0, 2);
  be(b, 1, 2);
  be(b, 96, 2);
  be(b, 0x4d54726b, 4);
  be(b, track.size(), 4);
  b.insert(b.end(), track.begin(), track.end());
  return b;
}
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (...) {
    caught = true;
  }
  assert(caught);
}
int main() {
  auto b = midi({0, 0x90, 60, 100, 0, 64, 100, 96, 0x80, 60, 0, 0, 64, 0, 0,
                 0xff, 0x2f, 0});
  auto c = parse_midi(b, "test.mid");
  assert(c.notes.size() == 2 && c.length == 1 && c.notes[1].pitch == 64);
  auto pedal =
      parse_midi(midi({0,  0x90, 60, 100,  0,  0xb0, 64, 127,  48,   0x80,
                       60, 0,    48, 0xb0, 64, 0,    0,  0xff, 0x2f, 0}),
                 "pedal");
  assert(pedal.notes[0].end == 1);
  for (size_t n = 0; n < b.size(); ++n)
    rejects([&] {
      parse_midi(std::vector<uint8_t>(b.begin(), b.begin() + n), "bad");
    });
  auto smpte = b;
  smpte[12] = 0x80;
  rejects([&] { parse_midi(smpte, "smpte"); });
  auto prog = parse_progression(
      R"({"progression":{"name":"Test","chords":[{"notes":[60,64,67]},{"notes":[62,65,69]}]}})",
      "test.progression", 2);
  assert(prog.notes.size() == 6 && prog.length == 4 &&
         prog.notes[3].start == 2 && std::abs(prog.notes[0].end - 1.8) < 1e-9);
  rejects([] {
    parse_progression(R"({"progression":{"chords":[{"notes":[300]}]}})", "bad");
  });
  rejects([] {
    parse_progression(R"({"progression":{"chords":[{"notes":[60.5]}]}})",
                      "bad");
  });
  rejects([] {
    parse_progression(R"({"progression":{"chords":[{"notes":[60]}]}})junk)",
                      "bad");
  });
  Settings s;
  s.source = 0;
  s.target = 2;
  assert(map_pitch(64, s, 0) == 66);
  s.transform = DegreeMap;
  s.target_scale = 1;
  assert(map_pitch(60, s, 0) == 62 && map_pitch(64, s, 0) == 65 &&
         map_pitch(67, s, 0) == 69);
  s.target_scale = 4;
  assert(map_pitch(60, s, 0) == -1);
  s.transform = FitScale;
  s.target_scale = 1;
  assert(map_pitch(64, s, 0) == 65);
  std::vector<Message> out;
  Player p([&](double at, int st, int ch, int n, int v) {
    out.push_back({at, st, ch, n, v});
  });
  Clip melody;
  melody.length = 4;
  melody.notes = {
      {0, .5, 60, 100, 0, 0}, {1, 1.5, 62, 100, 0, 0}, {3, 4, 64, 100, 0, 0}};
  auto clip = std::make_shared<Prepared>(melody);
  for (int i = 0; i < 4; ++i) {
    p.load(i, clip);
    Settings x;
    x.channel = i;
    x.speed = i == 0 ? 1 : i == 1 ? 3 : i == 2 ? 5 : 10;
    x.direction = i == 2 ? Reverse : Forward;
    p.configure(i, x);
  }
  for (int i = 0; i < 160; ++i)
    p.process(i * .05, (i + 1) * .05, true);
  int counts[4] = {};
  for (auto &m : out)
    if (m.st == 0x90)
      ++counts[m.ch];
  assert(counts[0] == 3 && counts[1] == 6 && counts[2] == 12 &&
         counts[3] == 11);
  p.panic();
  int balance[4][128] = {};
  for (auto &m : out)
    balance[m.ch][m.n] += m.st == 0x90 ? 1 : -1;
  for (auto &row : balance)
    for (int n : row)
      assert(n == 0);
  // Reverse mirrors note intervals, not raw MIDI event order.
  out.clear();
  Player rev([&](double at, int st, int ch, int n, int v) {
    out.push_back({at, st, ch, n, v});
  });
  rev.load(0, clip);
  Settings r;
  r.direction = Reverse;
  rev.configure(0, r);
  rev.process(0, .1, true);
  assert(out[0].n == 64 && out[0].st == 0x90);
  rev.panic();
  // Incoming root changes never change the release pitch of an existing note.
  out.clear();
  Player live([&](double at, int st, int ch, int n, int v) {
    out.push_back({at, st, ch, n, v});
  });
  live.load(0, clip);
  Settings ls;
  ls.follow = true;
  ls.channel = 7;
  live.configure(0, ls);
  live.process(0, .1, true);
  live.input(0, 64, true);
  live.process(.1, .6, true);
  assert(out.back().st == 0x80 && out.back().n == 60 && out.back().ch == 7);
  live.process(.6, 1.1, true);
  assert(out.back().n == 66);
  live.panic();
  // Key-map collisions are reference counted.
  Clip collision;
  collision.length = 2;
  collision.notes = {{0, 1, 60, 100, 0, 0}, {0, 1.5, 61, 100, 0, 0}};
  out.clear();
  Player cp([&](double at, int st, int ch, int n, int v) {
    out.push_back({at, st, ch, n, v});
  });
  cp.load(0, std::make_shared<Prepared>(collision));
  Settings cs;
  cs.transform = FitScale;
  cp.configure(0, cs);
  cp.process(0, .5, true);
  assert(out.size() == 1);
  cp.process(.5, 1.25, true);
  assert(out.size() == 1);
  cp.process(1.25, 1.75, true);
  assert(out.size() == 2 && out.back().st == 0x80);
  // Ratio clocks: a 1-beat pulse at 5/4 and 7/4 has 5 and 7 attacks over 4
  // beats.
  Clip pulse;
  pulse.length = 1;
  pulse.notes = {{0, .2, 60, 100, 0, 0}};
  out.clear();
  Player ratios([&](double at, int st, int ch, int n, int v) {
    out.push_back({at, st, ch, n, v});
  });
  for (int i = 0; i < 2; ++i) {
    ratios.load(i, std::make_shared<Prepared>(pulse));
    Settings rs;
    rs.channel = i;
    rs.speed = i ? 10 : 9;
    ratios.configure(i, rs);
  }
  for (int i = 0; i < 400; ++i)
    ratios.process(i * .01, (i + 1) * .01, true);
  int a = 0, z = 0;
  for (auto &m : out)
    if (m.st == 0x90)
      (m.ch ? a : z)++;
  assert(a == 7 && z == 5);
  ratios.panic();
  // Ping-pong alternates complete forward/reverse phrases.
  out.clear();
  Player pp([&](double at, int st, int ch, int n, int v) {
    out.push_back({at, st, ch, n, v});
  });
  pp.load(0, clip);
  Settings ps;
  ps.direction = PingPong;
  pp.configure(0, ps);
  for (int i = 0; i < 81; ++i)
    pp.process(i * .1, (i + 1) * .1, true);
  bool reverseStart = false;
  for (auto &m : out)
    if (m.st == 0x90 && std::abs(m.at - 4) < 1e-8 && m.n == 64)
      reverseStart = true;
  assert(reverseStart);
  pp.panic();
  StateWriter w;
  save_clip(w, clip);
  StateReader rr{w.bytes.data(), w.bytes.size()};
  auto loaded = restore_clip(rr);
  assert(loaded->clip.notes.size() == 3 && rr.left == 0);
  for (size_t n = 0; n < w.bytes.size(); ++n)
    rejects([&] {
      StateReader tr{w.bytes.data(), n};
      restore_clip(tr);
    });
  auto dir = std::filesystem::temp_directory_path() / "qw-browser-test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "sub");
  std::ofstream(dir / "A.mid").put('x');
  std::ofstream(dir / "B.PROGRESSION").put('x');
  std::ofstream(dir / "ignore.txt").put('x');
  std::filesystem::create_symlink("/", dir / "escape");
  auto entries = browse(canonical(dir.string()), dir.string());
  assert(entries.size() == 3 && entries[0].directory);
  rejects([&] { browse(canonical(dir.string()), "/"); });
  std::filesystem::remove_all(dir);
  Ring<int, 32> queue;
  std::thread producer([&] {
    for (int i = 0; i < 10000; ++i)
      while (!queue.push(i))
        std::this_thread::yield();
  });
  for (int i = 0; i < 10000; ++i) {
    int got = -1;
    while (!queue.pop(got))
      std::this_thread::yield();
    assert(got == i);
  }
  producer.join();
  std::cout << "PASS: SMF, progression, reverse, ping-pong, four speeds, 5:7, "
               "key/scale, note ownership, state, browser, queue\n";
}
