#include "midi.hpp"
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace qw {
namespace {
struct Json {
  enum Kind { Null, Number, String, Array, Object, Bool } kind = Null;
  double number = 0;
  std::string text;
  std::vector<Json> list;
  std::map<std::string, Json> fields;
  const Json &at(const char *k) const {
    auto i = fields.find(k);
    if (kind != Object || i == fields.end())
      throw std::runtime_error(std::string("Missing progression ") + k);
    return i->second;
  }
};
struct Parser {
  const std::string &s;
  size_t pos = 0, nodes = 0;
  void ws() {
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
      ++pos;
  }
  char take() {
    if (pos == s.size())
      throw std::runtime_error("Truncated progression JSON");
    return s[pos++];
  }
  void need(char c) {
    ws();
    if (take() != c)
      throw std::runtime_error("Invalid progression JSON");
  }
  unsigned hex() {
    unsigned n = 0;
    for (int i = 0; i < 4; ++i) {
      char c = take();
      n *= 16;
      if (c >= '0' && c <= '9')
        n += c - '0';
      else if (c >= 'a' && c <= 'f')
        n += c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        n += c - 'A' + 10;
      else
        throw std::runtime_error("Bad Unicode escape");
    }
    return n;
  }
  std::string str() {
    need('"');
    std::string out;
    while (true) {
      unsigned char c = take();
      if (c == '"')
        break;
      if (c < 32)
        throw std::runtime_error("Invalid JSON string");
      if (c == '\\') {
        c = take();
        switch (c) {
        case '"':
        case '\\':
        case '/':
          out += c;
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case 'u': {
          unsigned u = hex();
          if (u >= 0xd800 && u <= 0xdbff) {
            if (take() != '\\' || take() != 'u')
              throw std::runtime_error("Bad Unicode pair");
            unsigned lo = hex();
            if (lo < 0xdc00 || lo > 0xdfff)
              throw std::runtime_error("Bad Unicode pair");
            u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
          } else if (u >= 0xdc00 && u <= 0xdfff)
            throw std::runtime_error("Bad Unicode pair");
          if (u < 128)
            out += char(u);
          else if (u < 2048) {
            out += char(0xc0 | (u >> 6));
            out += char(0x80 | (u & 63));
          } else if (u < 65536) {
            out += char(0xe0 | (u >> 12));
            out += char(0x80 | ((u >> 6) & 63));
            out += char(0x80 | (u & 63));
          } else {
            out += char(0xf0 | (u >> 18));
            out += char(0x80 | ((u >> 12) & 63));
            out += char(0x80 | ((u >> 6) & 63));
            out += char(0x80 | (u & 63));
          }
          break;
        }
        default:
          throw std::runtime_error("Bad JSON escape");
        }
      } else
        out += c;
      if (out.size() > 4096)
        throw std::runtime_error("JSON text too long");
    }
    return out;
  }
  Json value(int depth = 0) {
    if (depth > 16 || ++nodes > 20000)
      throw std::runtime_error("Progression JSON limit");
    ws();
    Json j;
    if (pos >= s.size())
      throw std::runtime_error("Empty JSON");
    char c = s[pos];
    if (c == '{') {
      j.kind = Json::Object;
      ++pos;
      ws();
      if (pos < s.size() && s[pos] == '}') {
        ++pos;
        return j;
      }
      while (true) {
        auto k = str();
        need(':');
        auto v = value(depth + 1);
        if (!j.fields.emplace(k, std::move(v)).second)
          throw std::runtime_error("Duplicate JSON key");
        ws();
        char d = take();
        if (d == '}')
          break;
        if (d != ',')
          throw std::runtime_error("Invalid JSON object");
      }
      return j;
    }
    if (c == '[') {
      j.kind = Json::Array;
      ++pos;
      ws();
      if (pos < s.size() && s[pos] == ']') {
        ++pos;
        return j;
      }
      while (true) {
        j.list.push_back(value(depth + 1));
        ws();
        char d = take();
        if (d == ']')
          break;
        if (d != ',')
          throw std::runtime_error("Invalid JSON array");
      }
      return j;
    }
    if (c == '"') {
      j.kind = Json::String;
      j.text = str();
      return j;
    }
    for (auto literal : {"true", "false", "null"}) {
      std::string l = literal;
      if (s.compare(pos, l.size(), l) == 0) {
        pos += l.size();
        j.kind = l == "null" ? Json::Null : Json::Bool;
        return j;
      }
    }
    size_t begin = pos;
    if (c == '-')
      ++pos;
    if (pos == s.size() || !std::isdigit(static_cast<unsigned char>(s[pos])))
      throw std::runtime_error("Invalid JSON value");
    if (s[pos] == '0')
      ++pos;
    else
      while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])))
        ++pos;
    if (pos < s.size() && s[pos] == '.') {
      ++pos;
      size_t b = pos;
      while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])))
        ++pos;
      if (b == pos)
        throw std::runtime_error("Invalid JSON number");
    }
    if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
      ++pos;
      if (pos < s.size() && (s[pos] == '+' || s[pos] == '-'))
        ++pos;
      size_t b = pos;
      while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])))
        ++pos;
      if (b == pos)
        throw std::runtime_error("Invalid JSON exponent");
    }
    j.kind = Json::Number;
    j.number = std::stod(s.substr(begin, pos - begin));
    if (!std::isfinite(j.number))
      throw std::runtime_error("Invalid number");
    return j;
  }
};
} // namespace
Clip parse_progression(const std::string &text, const std::string &name,
                       double step, double gate) {
  if (text.size() > 1024 * 1024 || !std::isfinite(step) || step < 0.0625 ||
      step > 16 || gate <= 0 || gate > 1)
    throw std::runtime_error("Progression limits");
  Parser parser{text};
  auto root = parser.value();
  parser.ws();
  if (parser.pos != text.size())
    throw std::runtime_error("Trailing JSON data");
  const auto &p = root.at("progression");
  const auto &chords = p.at("chords");
  if (chords.kind != Json::Array || chords.list.empty() ||
      chords.list.size() > 128)
    throw std::runtime_error("Need 1-128 chords");
  Clip c;
  c.name = name;
  c.length = step * chords.list.size();
  auto key = p.fields.find("rootNote");
  if (key != p.fields.end() && key->second.kind == Json::String) {
    static const char *names[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                  "F#", "G",  "G#", "A",  "A#", "B"};
    for (int i = 0; i < 12; ++i)
      if (key->second.text == names[i])
        c.key = i;
  }
  for (size_t i = 0; i < chords.list.size(); ++i) {
    const auto &ns = chords.list[i].at("notes");
    if (ns.kind != Json::Array || ns.list.size() > 32)
      throw std::runtime_error("Chord limit is 32 notes");
    std::set<int> unique;
    for (const auto &v : ns.list) {
      if (v.kind != Json::Number || v.number != std::floor(v.number) ||
          v.number < 0 || v.number > 127)
        throw std::runtime_error("Invalid progression note");
      unique.insert(int(v.number));
    }
    for (int n : unique)
      c.notes.push_back({i * step, (i + gate) * step, uint8_t(n), 100, 0, 0});
  }
  if (c.notes.empty())
    throw std::runtime_error("Empty progression");
  return c;
}
} // namespace qw
