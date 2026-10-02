#pragma once
#include "midi.hpp"
#include <array>
#include <functional>
#include <memory>
namespace qw {
enum Direction { Forward, Reverse, PingPong };
enum Transform { Transpose, FitScale, DegreeMap };
struct Settings {
  bool enabled = true, loop = true, follow = false, latch = true;
  int speed = 3, direction = 0, channel = 0, shift = 0, source = 0, target = 0;
  int source_scale = 0, target_scale = 0, transform = 0, anchor = 60,
      input_channel = 0;
};
constexpr double speeds[] = {0.25,    0.5,     0.75,    1,       1.5,
                             2,       3,       4,       4.0 / 3, 5.0 / 4,
                             7.0 / 4, 2.0 / 3, 4.0 / 5, 4.0 / 7};
const std::vector<int> &scale(int id);
int map_pitch(int pitch, const Settings &, int live_shift);
struct Event {
  double time;
  uint32_t id;
  bool on;
};
struct Prepared {
  Clip clip;
  std::array<std::vector<Event>, 2> events;
  explicit Prepared(Clip c);
};
using Sink = std::function<void(double, int, int, int,
                                int)>; // beat,status,channel,pitch,velocity
class Player {
  struct Lane {
    std::shared_ptr<const Prepared> clip;
    Settings cfg;
    double phase = 0;
    std::vector<int> active;
    int live = 0;
    std::vector<int> held;
  };
  std::array<Lane, 4> lanes_;
  std::array<std::array<unsigned, 128>, 16> refs_{};
  Sink sink_;
  bool running_ = false;
  double expected_ = 0;
  void release(int lane, int id, double at);
  void flush(int lane, double at);

public:
  explicit Player(Sink sink) : sink_(std::move(sink)) {}
  void load(int lane, std::shared_ptr<const Prepared>);
  std::shared_ptr<const Prepared> clip(int lane) const {
    return lanes_.at(lane).clip;
  }
  const Settings &settings(int lane) const { return lanes_.at(lane).cfg; }
  void configure(int lane, const Settings &);
  void input(int channel, int note, bool on);
  void panic();
  void process(double start, double end, bool playing);
};
} // namespace qw
