#include "EmbeddedEmulatorEngine.h"
extern "C" {
#include "engine.h"
}

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr int kControlCount = 24;
constexpr int kAudioBlock = 128;
constexpr int kDefaults[kControlCount] = {
    0, 64, 0, 0, 0, 64, 64, 0, 127, 0, 127, 64,
    127, 64, 0, 0, 5, 5, 127, 11, 5, 5, 127, 11
};
constexpr const char *kKeys[kControlCount] = {
    "glide", "lfo_freq", "wmod_src_mix", "pmod_osc_b", "pmod_filt_env",
    "osc_a_freq", "osc_b_freq", "osc_b_fine", "filt_cutoff", "filt_env_amt",
    "mix_osc_b", "osc_b_pw", "mix_osc_a", "osc_a_pw", "mix_noise",
    "filt_resonance", "filt_attack", "filt_decay", "filt_sustain",
    "filt_release", "amp_attack", "amp_decay", "amp_sustain", "amp_release"
};

struct Instance {
    std::unique_ptr<ves::EmbeddedEmulatorEngine> emulator;
    std::array<std::atomic<int>, kControlCount> controls;
    std::array<int, kControlCount> last_sent;
    char temp_root[64]{};
    char cfg_path[96]{};
    char nvram_path[96]{};

    Instance() {
        last_sent.fill(-1);
        for (int i = 0; i < kControlCount; ++i)
            controls[static_cast<std::size_t>(i)].store(kDefaults[i], std::memory_order_relaxed);
    }
};

bool is_directory(const char *path) {
    struct stat info {};
    return path != nullptr && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

std::string resolve_rom_path(const char *data_dir) {
    const char *configured = std::getenv("MAME_ROMS_DIR");
    if (is_directory(configured))
        return configured;

    if (data_dir != nullptr && *data_dir != '\0') {
        char candidate[512];
        if (std::snprintf(candidate, sizeof(candidate), "%s/roms", data_dir) < static_cast<int>(sizeof(candidate))
            && is_directory(candidate))
            return candidate;
    }

    return "/sdcard/MAME/roms";
}

int key_index(const char *key) {
    if (key == nullptr)
        return -1;
    for (int i = 0; i < kControlCount; ++i)
        if (std::strcmp(key, kKeys[i]) == 0)
            return i;
    return -1;
}

void send_control_cc(Instance *instance, int index, int value) {
    const std::uint8_t message[3] = {
        0xb0,
        static_cast<std::uint8_t>(20 + index),
        static_cast<std::uint8_t>(value)
    };
    instance->emulator->sendMidiBytes(message, sizeof(message));
}

void *create_instance(const char *data_dir) {
    std::unique_ptr<Instance> instance(new (std::nothrow) Instance());
    if (!instance)
        return nullptr;

    std::strcpy(instance->temp_root, "/tmp/mpc-prophet5-XXXXXX");
    if (mkdtemp(instance->temp_root) == nullptr)
        return nullptr;

    std::snprintf(instance->cfg_path, sizeof(instance->cfg_path), "%s/cfg", instance->temp_root);
    std::snprintf(instance->nvram_path, sizeof(instance->nvram_path), "%s/nvram", instance->temp_root);
    if (mkdir(instance->cfg_path, 0700) != 0 || mkdir(instance->nvram_path, 0700) != 0) {
        rmdir(instance->cfg_path);
        rmdir(instance->nvram_path);
        rmdir(instance->temp_root);
        return nullptr;
    }

    ves::EmbeddedEmulatorEngineSettings settings;
    settings.rom_path = resolve_rom_path(data_dir);
    settings.cfg_path = instance->cfg_path;
    settings.nvram_path = instance->nvram_path;
    settings.driver_name = "prophet5r30";
    settings.sample_rate = 44100;
    settings.retrofit_midi_input_option = "ves_virtual_midiin";
    settings.state_operations_allowed = false;
    settings.enable_layout_plugin = false;

    instance->emulator.reset(new (std::nothrow) ves::EmbeddedEmulatorEngine(std::move(settings)));
    if (!instance->emulator) {
        rmdir(instance->cfg_path);
        rmdir(instance->nvram_path);
        rmdir(instance->temp_root);
        return nullptr;
    }
    instance->emulator->start();
    return instance.release();
}

void destroy_instance(void *opaque) {
    Instance *instance = static_cast<Instance *>(opaque);
    if (instance == nullptr)
        return;
    if (instance->emulator) {
        instance->emulator->sendMidiPanic();
        instance->emulator->stopAndJoin(std::chrono::seconds(5));
        instance->emulator.reset();
    }
    rmdir(instance->cfg_path);
    rmdir(instance->nvram_path);
    rmdir(instance->temp_root);
    delete instance;
}

void midi_input(void *opaque, const std::uint8_t *message, int length) {
    Instance *instance = static_cast<Instance *>(opaque);
    if (instance != nullptr && instance->emulator != nullptr && message != nullptr && length > 0)
        instance->emulator->sendMidiBytes(message, static_cast<std::size_t>(length));
}

void set_parameter(void *opaque, const char *key, const char *value) {
    Instance *instance = static_cast<Instance *>(opaque);
    if (instance == nullptr || key == nullptr || value == nullptr)
        return;

    const int index = key_index(key);
    if (index >= 0) {
        char *end = nullptr;
        const long parsed = std::strtol(value, &end, 10);
        if (end != value)
            instance->controls[static_cast<std::size_t>(index)].store(
                static_cast<int>(std::max(0L, std::min(127L, parsed))), std::memory_order_release);
        return;
    }

    if (std::strcmp(key, "state") == 0) {
        const char *cursor = value;
        for (int i = 0; i < kControlCount && *cursor != '\0'; ++i) {
            char *end = nullptr;
            const long parsed = std::strtol(cursor, &end, 10);
            if (end == cursor)
                break;
            instance->controls[static_cast<std::size_t>(i)].store(
                static_cast<int>(std::max(0L, std::min(127L, parsed))), std::memory_order_release);
            cursor = *end == ',' ? end + 1 : end;
        }
    }
}

int get_parameter(void *opaque, const char *key, char *buffer, int capacity) {
    Instance *instance = static_cast<Instance *>(opaque);
    if (instance == nullptr || key == nullptr || buffer == nullptr || capacity <= 0)
        return 0;

    const int index = key_index(key);
    if (index >= 0)
        return std::snprintf(buffer, static_cast<std::size_t>(capacity), "%d",
            instance->controls[static_cast<std::size_t>(index)].load(std::memory_order_acquire));

    if (std::strcmp(key, "state") == 0) {
        int used = 0;
        for (int i = 0; i < kControlCount && used < capacity; ++i) {
            const int written = std::snprintf(buffer + used, static_cast<std::size_t>(capacity - used),
                i == 0 ? "%d" : ",%d",
                instance->controls[static_cast<std::size_t>(i)].load(std::memory_order_acquire));
            if (written < 0 || written >= capacity - used)
                return 0;
            used += written;
        }
        return used;
    }

    if ((std::strcmp(key, "engine_status") == 0 || std::strcmp(key, "engine_status_display") == 0)
        && instance->emulator != nullptr) {
        const auto &diagnostics = instance->emulator->diagnostics();
        int status = 0;
        const char *label = "Starting";
        if (diagnostics.machine_running.load(std::memory_order_acquire)) {
            status = 1;
            label = "Ready";
        } else if (diagnostics.machine_exited.load(std::memory_order_acquire) != 0) {
            status = 2;
            const auto diagnostic = instance->emulator->startupDiagnostic();
            const std::string message = diagnostic.summary.empty() ? "Emulator stopped" : diagnostic.summary;
            if (std::strcmp(key, "engine_status_display") == 0)
                return std::snprintf(buffer, static_cast<std::size_t>(capacity), "%s", message.c_str());
            return std::snprintf(buffer, static_cast<std::size_t>(capacity), "%d", status);
        }
        if (std::strcmp(key, "engine_status_display") == 0)
            return std::snprintf(buffer, static_cast<std::size_t>(capacity), "%s", label);
        return std::snprintf(buffer, static_cast<std::size_t>(capacity), "%d", status);
    }
    return 0;
}

void render_audio(void *opaque, std::int16_t *output, int frames) {
    Instance *instance = static_cast<Instance *>(opaque);
    if (output == nullptr || frames <= 0)
        return;
    if (instance == nullptr || instance->emulator == nullptr) {
        std::memset(output, 0, static_cast<std::size_t>(frames) * 2 * sizeof(*output));
        return;
    }

    for (int i = 0; i < kControlCount; ++i) {
        const int value = instance->controls[static_cast<std::size_t>(i)].load(std::memory_order_acquire);
        if (value != instance->last_sent[static_cast<std::size_t>(i)]) {
            send_control_cc(instance, i, value);
            instance->last_sent[static_cast<std::size_t>(i)] = value;
        }
    }

    std::array<ves::StereoFrame, kAudioBlock> block{};
    int offset = 0;
    while (offset < frames) {
        const int request = std::min(kAudioBlock, frames - offset);
        const std::size_t received = instance->emulator->readAudioFrames(block.data(), static_cast<std::size_t>(request));
        for (int i = 0; i < request; ++i) {
            const ves::StereoFrame frame = i < static_cast<int>(received) ? block[static_cast<std::size_t>(i)] : ves::StereoFrame{};
            const float left = std::max(-1.0f, std::min(0.999969f, frame.left));
            const float right = std::max(-1.0f, std::min(0.999969f, frame.right));
            output[2 * (offset + i)] = static_cast<std::int16_t>(std::lrintf(left * 32768.0f));
            output[2 * (offset + i) + 1] = static_cast<std::int16_t>(std::lrintf(right * 32768.0f));
        }
        offset += request;
    }
}

const mpc_engine_t kEngine = {
    create_instance,
    destroy_instance,
    midi_input,
    set_parameter,
    get_parameter,
    render_audio,
    nullptr
};

} // namespace

extern "C" const mpc_engine_t *mpc_engine(void) {
    return &kEngine;
}
