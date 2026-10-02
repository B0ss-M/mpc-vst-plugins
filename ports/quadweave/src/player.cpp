#include "player.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace qw {
const std::vector<int> &scale(int id) {
  static const std::vector<int> scales[] = {
      {0, 2, 4, 5, 7, 9, 11},
      {0, 2, 3, 5, 7, 8, 10},
      {0, 2, 3, 5, 7, 9, 10},
      {0, 2, 4, 5, 7, 9, 10},
      {0, 2, 4, 7, 9},
      {0, 3, 5, 7, 10},
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};
  return scales[std::max(0, std::min(6, id))];
}
static int nearest(int n, int root, const std::vector<int> &s) {
  for (int d = 0; d <= 12; ++d)
    for (int sign : {-1, 1}) {
      int x = n + sign * d;
      int pc = ((x - root) % 12 + 12) % 12;
      if (std::find(s.begin(), s.end(), pc) != s.end())
        return x;
    }
  return n;
}
int map_pitch(int p, const Settings &c, int live) {
  int target = c.target + live, result = p;
  if (c.transform == DegreeMap) {
    const auto &src = scale(c.source_scale);
    const auto &dst = scale(c.target_scale);
    if (src.size() != dst.size())
      return -1;
    int snapped = nearest(p, c.source, src), rel = snapped - c.source;
    int octave = int(std::floor(rel / 12.0)), pc = rel - octave * 12;
    auto i = std::find(src.begin(), src.end(), pc) - src.begin();
    result = target + octave * 12 + dst[i] + c.shift;
  } else {
    result = p + target - c.source + c.shift;
    if (c.transform == FitScale)
      result = nearest(result, target, scale(c.target_scale));
  }
  return result < 0 || result > 127 ? -1 : result;
}
Prepared::Prepared(Clip c) : clip(std::move(c)) {
  if (!std::isfinite(clip.length) || clip.length <= 0 || clip.length > 400000 ||
      clip.notes.size() > 100000)
    throw std::runtime_error("Invalid clip limits");
  for (size_t i = 0; i < clip.notes.size(); ++i) {
    auto &n = clip.notes[i];
    if (!std::isfinite(n.start) || !std::isfinite(n.end) || n.start < 0 ||
        n.end <= n.start || n.end > clip.length || n.pitch > 127 ||
        !n.velocity || n.velocity > 127 || n.channel > 15 || n.part > 63)
      throw std::runtime_error("Invalid note");
    events[0].push_back({n.start, uint32_t(i), true});
    events[0].push_back({n.end, uint32_t(i), false});
    events[1].push_back({clip.length - n.end, uint32_t(i), true});
    events[1].push_back({clip.length - n.start, uint32_t(i), false});
  }
  for (auto &v : events) {
    std::sort(v.begin(), v.end(), [](const Event &a, const Event &b) {
      return a.time != b.time ? a.time < b.time
                              : (a.on != b.on ? !a.on : a.id < b.id);
    });
    int voices = 0;
    for (auto &e : v) {
      voices += e.on ? 1 : -1;
      if (voices > 32)
        throw std::runtime_error("Polyphony exceeds 32 notes");
    }
  }
}
void Player::release(int lane, int id, double at) {
  auto &l = lanes_[lane];
  int n = l.active[id];
  if (n < 0)
    return;
  auto &r = refs_[l.cfg.channel][n];
  if (r && !--r)
    sink_(at, 0x80, l.cfg.channel, n, 0);
  l.active[id] = -1;
}
void Player::flush(int lane, double at) {
  auto &l = lanes_[lane];
  for (size_t i = 0; i < l.active.size(); ++i)
    release(lane, i, at);
}
void Player::load(int lane, std::shared_ptr<const Prepared> clip) {
  if (lane < 0 || lane >= 4)
    throw std::runtime_error("Invalid lane");
  flush(lane, expected_);
  auto &l = lanes_[lane];
  l.clip = std::move(clip);
  l.active.assign(l.clip ? l.clip->clip.notes.size() : 0, -1);
  l.phase = 0;
}
void Player::configure(int i, const Settings &c) {
  if (i < 0 || i > 3 || c.speed < 0 || c.speed > 13 || c.direction < 0 ||
      c.direction > 2 || c.channel < 0 || c.channel > 15 || c.source < 0 ||
      c.source > 11 || c.target < 0 || c.target > 11 || c.transform < 0 ||
      c.transform > 2 || c.source_scale < 0 || c.source_scale > 6 ||
      c.target_scale < 0 || c.target_scale > 6 || c.input_channel < 0 ||
      c.input_channel > 16 || c.anchor < 0 || c.anchor > 127 || c.shift < -48 ||
      c.shift > 48)
    throw std::runtime_error("Invalid settings");
  auto &l = lanes_[i];
  if (c.channel != l.cfg.channel || c.direction != l.cfg.direction ||
      c.enabled != l.cfg.enabled || c.loop != l.cfg.loop) {
    flush(i, expected_);
    l.phase = 0;
  }
  if (c.follow != l.cfg.follow || c.input_channel != l.cfg.input_channel ||
      c.anchor != l.cfg.anchor) {
    l.live = 0;
    l.held.clear();
  }
  l.cfg = c;
}
void Player::input(int ch, int n, bool on) {
  if (n < 0 || n > 127)
    return;
  for (auto &l : lanes_)
    if (l.cfg.follow &&
        (!l.cfg.input_channel || l.cfg.input_channel == ch + 1)) {
      auto &v = l.held;
      int token = ch * 128 + n;
      v.erase(std::remove(v.begin(), v.end(), token), v.end());
      if (on)
        v.push_back(token);
      if (!v.empty())
        l.live = v.back() % 128 - l.cfg.anchor;
      else if (!l.cfg.latch)
        l.live = 0;
    }
}
void Player::panic() {
  for (int i = 0; i < 4; ++i)
    flush(i, expected_);
  running_ = false;
}
void Player::process(double start, double end, bool playing) {
  if (!std::isfinite(start) || !std::isfinite(end) || end <= start ||
      end - start > 1) {
    panic();
    return;
  }
  if (!playing) {
    panic();
    expected_ = end;
    return;
  }
  bool reset = !running_ || std::abs(start - expected_) > 1e-5;
  if (reset) {
    panic();
    for (auto &l : lanes_)
      l.phase = 0;
  }
  running_ = true;
  unsigned emitted = 0;
  for (int i = 0; i < 4; ++i) {
    auto &l = lanes_[i];
    if (!l.clip || !l.cfg.enabled)
      continue;
    double len = l.clip->clip.length, speed = speeds[l.cfg.speed],
           left = end - start, host = start;
    while (left > 1e-12) {
      if (!l.cfg.loop && l.phase >= len - 1e-10) {
        flush(i, host);
        break;
      }
      auto cycle = uint64_t(std::floor(l.phase / len + 1e-10));
      double pos = l.phase - cycle * len;
      if (pos < 0)
        pos = 0;
      double span = std::min(left, (len - pos) / speed);
      if (span <= 1e-12)
        break;
      double stop = pos + span * speed;
      int dir = l.cfg.direction == Reverse ||
                (l.cfg.direction == PingPong && (cycle % 2));
      const auto &v = l.clip->events[dir];
      auto it =
          std::lower_bound(v.begin(), v.end(), pos - 1e-10,
                           [](const Event &e, double p) { return e.time < p; });
      for (; it != v.end() && it->time < stop - 1e-10; ++it) {
        if (++emitted > 8192) {
          panic();
          expected_ = end;
          return;
        }
        double at = host + std::max(0.0, it->time - pos) / speed;
        if (!it->on)
          release(i, it->id, at);
        else {
          release(i, it->id, at);
          const auto &n = l.clip->clip.notes[it->id];
          int p = map_pitch(n.pitch, l.cfg, l.live);
          if (p >= 0) {
            l.active[it->id] = p;
            auto &r = refs_[l.cfg.channel][p];
            if (r++ == 0)
              sink_(at, 0x90, l.cfg.channel, p, n.velocity);
          }
        }
      }
      l.phase += span * speed;
      host += span;
      left -= span;
      if (stop >= len - 1e-9) {
        flush(i, host);
        l.phase = (cycle + 1) * len;
      }
    }
  }
  expected_ = end;
}
} // namespace qw
