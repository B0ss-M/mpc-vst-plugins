#include "params.h"
#include "vst2_abi.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
extern "C" AEffect *VSTPluginMain(audioMasterCallback);
static VstTimeInfo timeInfo{};
static intptr_t host(AEffect *, int32_t op, int32_t, intptr_t, void *, float) {
  return op == audioMasterGetTime ? reinterpret_cast<intptr_t>(&timeInfo) : 0;
}
static int index(const char *k) {
  for (int i = 0; i < NPARAMS; ++i)
    if (!std::strcmp(PARAMS[i].key, k))
      return i;
  assert(false);
  return -1;
}
static void wait() {
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
}
int main() {
  auto dir = std::filesystem::temp_directory_path() / "qw-host-test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directory(dir);
  std::ofstream(dir / "test.progression")
      << R"({"progression":{"chords":[{"notes":[60,64,67]},{"notes":[62,65,69]}]}})";
  setenv("QUADWEAVE_MIDI_ROOT", dir.c_str(), 1);
  AEffect *a = VSTPluginMain(host), *b = VSTPluginMain(host);
  assert(a && b && a != b && a->uniqueID == PLUG_UID &&
         a->numParams == NPARAMS);
  wait();
  a->setParameter(a, index("load"), 1);
  wait();
  wait();
  char display[64]{};
  a->dispatcher(a, effGetParamDisplay, index("clip0"), 0, display, 0);
  assert(std::string(display) == "test.progression");
  timeInfo.tempo = 120;
  timeInfo.flags = 2 | (1 << 9) | (1 << 10);
  float l[128], r[128];
  float *out[] = {l, r};
  for (int i = 0; i < 50; ++i) {
    timeInfo.ppqPos = i * 128.0 * 2 / 44100;
    a->processReplacing(a, nullptr, out, 128);
    for (float f : l)
      assert(f == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
  }
  a->setParameter(a, 1, 1);
  assert(a->getParameter(a, 1) == 1 && b->getParameter(b, 1) != 1);
  void *chunk = nullptr;
  auto n = a->dispatcher(a, effGetChunk, 0, 0, &chunk, 0);
  assert(n > 0 && chunk);
  std::vector<unsigned char> saved(static_cast<unsigned char *>(chunk),
                                   static_cast<unsigned char *>(chunk) + n);
  assert(b->dispatcher(b, effSetChunk, 0, n, saved.data(), 0) == 1);
  assert(b->getParameter(b, 1) == 1);
  b->dispatcher(b, effGetParamDisplay, index("clip0"), 0, display, 0);
  assert(std::string(display) == "test.progression");
  auto bad = saved;
  bad[0] = 0;
  assert(b->dispatcher(b, effSetChunk, 0, n, bad.data(), 0) == 0);
  assert(b->dispatcher(b, effSetChunk, 0, n - 1, saved.data(), 0) == 0);
  for (float &f : l)
    f = .25;
  for (float &f : r)
    f = .5;
  a->process(a, nullptr, out, 128);
  assert(l[0] == .25 && r[0] == .5);
  a->dispatcher(a, effMainsChanged, 0, 0, nullptr, 0);
  wait();
  a->dispatcher(a, effClose, 0, 0, nullptr, 0);
  b->dispatcher(b, effClose, 0, 0, nullptr, 0);
  std::filesystem::remove_all(dir);
  std::cout << "PASS: VST ABI, async progression browser/load, transport, "
               "chunks, isolation, legacy audio, shutdown\n";
}
