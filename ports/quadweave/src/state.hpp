#pragma once
#include "player.hpp"
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace qw {
// Fixed-width little-endian fields; no raw pointers, padding or native structs
// in project chunks.
struct StateWriter {
  std::vector<uint8_t> bytes;
  void u32(uint32_t v) {
    for (int i = 0; i < 4; ++i)
      bytes.push_back(uint8_t(v >> (8 * i)));
  }
  void real(double v) {
    uint64_t n;
    std::memcpy(&n, &v, 8);
    u32(uint32_t(n));
    u32(uint32_t(n >> 32));
  }
  void str(const std::string &s) {
    u32(s.size());
    bytes.insert(bytes.end(), s.begin(), s.end());
  }
};
struct StateReader {
  const uint8_t *p;
  size_t left;
  uint32_t u32() {
    if (left < 4)
      throw std::runtime_error("Truncated state");
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
      v |= uint32_t(*p++) << (8 * i);
    left -= 4;
    return v;
  }
  double real() {
    uint64_t n = u32();
    n |= uint64_t(u32()) << 32;
    double v;
    std::memcpy(&v, &n, 8);
    if (!std::isfinite(v))
      throw std::runtime_error("Invalid state number");
    return v;
  }
  std::string str() {
    auto n = u32();
    if (n > 4096 || n > left)
      throw std::runtime_error("Invalid state text");
    std::string s(reinterpret_cast<const char *>(p), n);
    p += n;
    left -= n;
    return s;
  }
};
inline void save_clip(StateWriter &w,
                      const std::shared_ptr<const Prepared> &p) {
  w.u32(p ? 1 : 0);
  if (!p)
    return;
  w.str(p->clip.name);
  w.real(p->clip.length);
  w.u32(p->clip.notes.size());
  for (auto &n : p->clip.notes) {
    w.real(n.start);
    w.real(n.end);
    w.u32(n.pitch);
    w.u32(n.velocity);
    w.u32(n.channel);
    w.u32(n.part);
  }
}
inline std::shared_ptr<const Prepared> restore_clip(StateReader &r) {
  auto has = r.u32();
  if (has > 1)
    throw std::runtime_error("Invalid clip flag");
  if (!has)
    return {};
  Clip c;
  c.name = r.str();
  c.length = r.real();
  auto count = r.u32();
  if (count > 100000 || size_t(count) * 32 > r.left)
    throw std::runtime_error("Invalid state count");
  for (unsigned i = 0; i < count; ++i) {
    double a = r.real(), b = r.real();
    auto p = r.u32(), v = r.u32(), ch = r.u32(), t = r.u32();
    if (p > 127 || v > 127 || ch > 15 || t > 63)
      throw std::runtime_error("Invalid state note");
    c.notes.push_back({a, b, uint8_t(p), uint8_t(v), uint8_t(ch), uint16_t(t)});
  }
  return std::make_shared<Prepared>(std::move(c));
}
} // namespace qw
