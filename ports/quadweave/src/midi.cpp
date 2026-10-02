#include "midi.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <dirent.h>
#include <fstream>
#include <map>
#include <stdexcept>
#include <sys/stat.h>
namespace qw {
namespace {
constexpr size_t MAX_BYTES = 8 * 1024 * 1024, MAX_EVENTS = 100000;
struct Reader {
  const std::vector<uint8_t> &d;
  size_t p, end;
  uint8_t byte() {
    if (p >= end)
      throw std::runtime_error("Truncated MIDI");
    return d[p++];
  }
  uint32_t num(int n) {
    uint32_t v = 0;
    while (n--)
      v = (v << 8) | byte();
    return v;
  }
  uint32_t vlq() {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      auto c = byte();
      v = (v << 7) | (c & 127);
      if (!(c & 128))
        return v;
    }
    throw std::runtime_error("Invalid MIDI VLQ");
  }
  void skip(size_t n) {
    if (n > end - p)
      throw std::runtime_error("Invalid MIDI length");
    p += n;
  }
};
struct Raw {
  uint64_t tick;
  unsigned order;
  int part, channel, type, a, b;
};
} // namespace
Clip parse_midi(const std::vector<uint8_t> &bytes, const std::string &name) {
  if (bytes.size() > MAX_BYTES)
    throw std::runtime_error("File exceeds 8 MiB");
  Reader r{bytes, 0, bytes.size()};
  if (r.num(4) != 0x4d546864)
    throw std::runtime_error("Not a MIDI file");
  auto h = r.num(4);
  if (h < 6)
    throw std::runtime_error("Bad MIDI header");
  Clip c;
  c.name = name;
  c.format = r.num(2);
  int tracks = r.num(2);
  c.ppq = r.num(2);
  if (c.format > 1 || (c.format == 0 && tracks != 1))
    throw std::runtime_error("Only MIDI formats 0/1");
  if (tracks < 1 || tracks > 64)
    throw std::runtime_error("Track limit is 64");
  if (!c.ppq || (c.ppq & 0x8000))
    throw std::runtime_error("Only PPQ timing supported");
  r.skip(h - 6);
  std::vector<Raw> events;
  uint64_t endtick = 0;
  unsigned order = 0;
  for (int t = 0; t < tracks; ++t) {
    if (r.num(4) != 0x4d54726b)
      throw std::runtime_error("Expected MIDI track");
    auto len = r.num(4);
    if (len > r.end - r.p)
      throw std::runtime_error("Truncated MIDI track");
    Reader q{bytes, r.p, r.p + len};
    r.skip(len);
    uint64_t tick = 0;
    int running = 0;
    bool eot = false;
    while (q.p < q.end) {
      if (++order > MAX_EVENTS)
        throw std::runtime_error("Event limit is 100000");
      tick += q.vlq();
      if (tick > uint64_t(c.ppq) * 4 * 100000)
        throw std::runtime_error("MIDI duration limit");
      int status = q.byte(), a = -1;
      if (status < 128) {
        a = status;
        status = running;
        if (!status)
          throw std::runtime_error("Invalid running status");
      }
      if (status == 255) {
        running = 0;
        int type = q.byte();
        auto n = q.vlq();
        if (n > q.end - q.p)
          throw std::runtime_error("Bad meta event");
        if (type == 0x2f) {
          if (n)
            throw std::runtime_error("Bad end-of-track");
          eot = true;
          q.p = q.end;
        } else {
          if (type == 0x58 && n == 4 && tick == 0) {
            c.numerator = bytes[q.p];
            int pow = bytes[q.p + 1];
            if (!c.numerator || pow > 6)
              throw std::runtime_error("Invalid meter");
            c.denominator = 1 << pow;
          }
          if (type == 0x59 && n == 2 && tick == 0) {
            int sf = int(int8_t(bytes[q.p]));
            int mi = bytes[q.p + 1];
            if (sf >= -7 && sf <= 7 && mi <= 1) {
              c.key = ((sf * 7 + (mi ? 9 : 0)) % 12 + 12) % 12;
              c.minor = mi;
            }
          }
          q.skip(n);
        }
      } else if (status == 0xf0 || status == 0xf7) {
        running = 0;
        auto n = q.vlq();
        q.skip(n);
        ++c.filtered;
      } else if (status >= 0x80 && status < 0xf0) {
        running = status;
        if (a < 0)
          a = q.byte();
        int type = status >> 4;
        int b = (type == 12 || type == 13) ? 0 : q.byte();
        if (a > 127 || b > 127)
          throw std::runtime_error("Invalid MIDI data");
        if (type == 8 || type == 9 || (type == 11 && a == 64))
          events.push_back({tick, order, t, status & 15, type, a, b});
        else
          ++c.filtered;
      } else
        throw std::runtime_error("Unsupported MIDI status");
    }
    if (!eot)
      throw std::runtime_error("Missing end-of-track");
    endtick = std::max(endtick, tick);
  }
  std::stable_sort(events.begin(), events.end(),
                   [](const Raw &a, const Raw &b) { return a.tick < b.tick; });
  // Match notes by source part/channel/pitch; sustain is channel-wide in MIDI.
  std::map<int, std::deque<size_t>> held;
  std::array<bool, 16> pedal{};
  std::array<std::vector<size_t>, 16> sustained;
  for (const auto &e : events) {
    auto &queue = held[(e.part * 16 + e.channel) * 128 + e.a];
    double beat = double(e.tick) / c.ppq;
    if (e.type == 11) {
      pedal[e.channel] = e.b >= 64;
      if (!pedal[e.channel]) {
        for (auto i : sustained[e.channel])
          c.notes[i].end = beat;
        sustained[e.channel].clear();
      }
    } else if (e.type == 9 && e.b) {
      queue.push_back(c.notes.size());
      c.notes.push_back({beat, -1, uint8_t(e.a), uint8_t(e.b),
                         uint8_t(e.channel), uint16_t(e.part)});
    } else {
      if (queue.empty())
        throw std::runtime_error("Unmatched note-off");
      auto i = queue.front();
      queue.pop_front();
      if (pedal[e.channel])
        sustained[e.channel].push_back(i);
      else
        c.notes[i].end = beat;
    }
  }
  for (auto &h : held)
    if (!h.second.empty())
      throw std::runtime_error("Missing note-off");
  double finish = double(endtick) / c.ppq;
  for (auto &h : sustained)
    for (auto i : h)
      c.notes[i].end = finish;
  c.notes.erase(std::remove_if(c.notes.begin(), c.notes.end(),
                               [](const Note &n) { return n.end <= n.start; }),
                c.notes.end());
  if (c.notes.empty())
    throw std::runtime_error("No playable notes");
  c.length = finish;
  if (c.length <= 0)
    throw std::runtime_error("Empty clip");
  std::stable_sort(
      c.notes.begin(), c.notes.end(),
      [](const Note &a, const Note &b) { return a.start < b.start; });
  return c;
}
Clip load_midi(const std::string &path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f)
    throw std::runtime_error("Cannot open MIDI");
  auto n = f.tellg();
  if (n < 0 || n > std::streamoff(MAX_BYTES))
    throw std::runtime_error("File exceeds 8 MiB");
  std::vector<uint8_t> d(static_cast<size_t>(n));
  f.seekg(0);
  if (!f.read(reinterpret_cast<char *>(d.data()), n))
    throw std::runtime_error("Read failed");
  return parse_midi(d, path.substr(path.find_last_of('/') + 1));
}
Clip load_clip(const std::string &path, double step) {
  auto ext = path.substr(path.find_last_of('.') == std::string::npos
                             ? path.size()
                             : path.find_last_of('.'));
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (ext != ".progression")
    return load_midi(path);
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f)
    throw std::runtime_error("Cannot open progression");
  auto n = f.tellg();
  if (n < 0 || n > 1024 * 1024)
    throw std::runtime_error("Progression exceeds 1 MiB");
  std::string text(static_cast<size_t>(n), '\0');
  f.seekg(0);
  if (!f.read(&text[0], n))
    throw std::runtime_error("Read failed");
  return parse_progression(text, path.substr(path.find_last_of('/') + 1), step);
}
std::string canonical(const std::string &path) {
  char b[PATH_MAX];
  if (!realpath(path.c_str(), b))
    throw std::runtime_error("Folder not found");
  return b;
}
bool within(const std::string &root, const std::string &p) {
  return p == root ||
         (p.size() > root.size() && p.compare(0, root.size(), root) == 0 &&
          (root == "/" || p[root.size()] == '/'));
}
std::vector<Entry> browse(const std::string &root, const std::string &path) {
  auto dir = canonical(path);
  if (!within(root, dir))
    throw std::runtime_error("Outside MIDI folder");
  DIR *d = opendir(dir.c_str());
  if (!d)
    throw std::runtime_error("Cannot read folder");
  std::vector<Entry> out;
  try {
    while (auto *e = readdir(d)) {
      std::string n = e->d_name;
      if (n.empty() || n[0] == '.')
        continue;
      auto full = dir + "/" + n;
      struct stat st{};
      if (lstat(full.c_str(), &st) || S_ISLNK(st.st_mode))
        continue;
      bool folder = S_ISDIR(st.st_mode);
      auto ext = n.substr(n.find_last_of('.') == std::string::npos
                              ? n.size()
                              : n.find_last_of('.'));
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (folder || (S_ISREG(st.st_mode) && (ext == ".mid" || ext == ".midi" ||
                                             ext == ".progression")))
        out.push_back({n, full, folder});
      if (out.size() > 4096)
        throw std::runtime_error("Folder exceeds 4096 entries");
    }
    closedir(d);
  } catch (...) {
    closedir(d);
    throw;
  }
  std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
    return a.directory != b.directory ? a.directory > b.directory
                                      : a.name < b.name;
  });
  return out;
}
} // namespace qw
