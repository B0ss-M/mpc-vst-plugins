#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace qw {
struct Note {
  double start, end;
  uint8_t pitch, velocity, channel;
  uint16_t part;
};
struct Clip {
  std::string name;
  double length = 4;
  int ppq = 480, format = 0, numerator = 4, denominator = 4;
  int key = -1, minor = 0, filtered = 0;
  std::vector<Note> notes;
};
Clip parse_midi(const std::vector<uint8_t> &data, const std::string &name);
Clip parse_progression(const std::string &, const std::string &,
                       double step = 1, double gate = 0.9);
Clip load_clip(const std::string &, double step = 1);
Clip load_midi(const std::string &path);
struct Entry {
  std::string name, path;
  bool directory;
};
std::vector<Entry> browse(const std::string &root,
                          const std::string &directory);
std::string canonical(const std::string &path);
bool within(const std::string &root, const std::string &path);
} // namespace qw
