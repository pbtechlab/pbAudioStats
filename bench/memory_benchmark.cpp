#include <pbAudioStats.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace fs = std::filesystem;
using namespace pb_audio;

namespace {

void put_u16_le(std::ofstream& out, uint16_t value) {
    const char b[2] = {static_cast<char>(value), static_cast<char>(value >> 8)};
    out.write(b, 2);
}

void put_u32_le(std::ofstream& out, uint32_t value) {
    const char b[4] = {static_cast<char>(value), static_cast<char>(value >> 8),
                       static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
    out.write(b, 4);
}

bool generate_float_wav(const fs::path& path, unsigned seconds) {
    constexpr uint32_t rate = 48000;
    constexpr uint16_t channels = 2;
    const uint64_t frames = static_cast<uint64_t>(rate) * seconds;
    const uint64_t data_size64 = frames * channels * sizeof(float);
    if (data_size64 > 0xffffffffull - 36ull) return false;
    const uint32_t data_size = static_cast<uint32_t>(data_size64);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write("RIFF", 4); put_u32_le(out, 36u + data_size);
    out.write("WAVEfmt ", 8); put_u32_le(out, 16); put_u16_le(out, 3);
    put_u16_le(out, channels); put_u32_le(out, rate);
    put_u32_le(out, rate * channels * sizeof(float));
    put_u16_le(out, channels * sizeof(float)); put_u16_le(out, 32);
    out.write("data", 4); put_u32_le(out, data_size);
    std::vector<float> chunk(8192 * channels);
    uint64_t written = 0;
    while (written < frames) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(8192, frames - written));
        for (size_t i = 0; i < n; ++i) {
            const float s = static_cast<float>(0.1 * std::sin(
                2.0 * 3.141592653589793 * 997.0 * (written + i) / rate));
            chunk[i * 2] = s;
            chunk[i * 2 + 1] = -s;
        }
        out.write(reinterpret_cast<const char*>(chunk.data()),
                  static_cast<std::streamsize>(n * channels * sizeof(float)));
        written += n;
    }
    return out.good();
}

uint64_t peak_rss_bytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS info{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))) {
        return static_cast<uint64_t>(info.PeakWorkingSetSize);
    }
    return 0;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
    return static_cast<uint64_t>(usage.ru_maxrss);
#else
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
#endif
}

template <typename Function>
int timed_run(const char* name, Function&& function) {
    const auto start = std::chrono::steady_clock::now();
    const bool ok = function();
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "mode=" << name << " success=" << (ok ? 1 : 0)
              << " elapsed_seconds=" << seconds
              << " peak_rss_bytes=" << peak_rss_bytes() << '\n';
    return ok ? 0 : 1;
}

int run_analysis(const std::string& path, uint64_t budget, const char* name) {
    return timed_run(name, [&] {
        AnalysisOptions options = AnalysisOptions::all();
        options.memory_budget_bytes = budget;
        return analyze(path, options).valid;
    });
}

int run_current_normalizer(const std::string& path, const std::string& output) {
    return timed_run("normalizer-current", [&] {
        return Normalizer::normalize_and_save_detailed(
            path, output, Normalizer::Target::Peak, -6.0).success;
    });
}

int run_two_pass_prototype(const std::string& path) {
    return timed_run("normalizer-two-pass-prototype", [&] {
        auto first = AudioReader::open_stream(path);
        if (!first) return false;
        const auto measured = LoudnessMeter::measure_stream(*first);
        if (!measured.valid || !std::isfinite(measured.loudness.sample_peak)) return false;
        const float gain = static_cast<float>(db_to_linear(-6.0 - measured.loudness.sample_peak));
        auto second = AudioReader::open_stream(path);
        if (!second) return false;
        std::vector<float> chunk(65536 * second->info.channels);
        volatile double checksum = 0.0;
        for (;;) {
            const size_t got = second->read_frames(chunk.data(), 65536);
            if (got == 0) break;
            const size_t count = got * second->info.channels;
            for (size_t i = 0; i < count; i += 1024) checksum += chunk[i] * gain;
        }
        return std::isfinite(checksum);
    });
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--generate") {
        const unsigned seconds = argc >= 4 ? static_cast<unsigned>(std::stoul(argv[3])) : 120u;
        return generate_float_wav(fs::u8path(argv[2]), seconds) ? 0 : 1;
    }
    if (argc >= 3 && std::string(argv[1]) == "--analysis-memory") {
        return run_analysis(argv[2], std::numeric_limits<uint64_t>::max(), "analysis-memory");
    }
    if (argc >= 3 && std::string(argv[1]) == "--analysis-stream") {
        return run_analysis(argv[2], 0, "analysis-stream");
    }
    if (argc >= 4 && std::string(argv[1]) == "--normalize-current") {
        return run_current_normalizer(argv[2], argv[3]);
    }
    if (argc >= 3 && std::string(argv[1]) == "--normalize-two-pass") {
        return run_two_pass_prototype(argv[2]);
    }
    std::cerr << "usage: memory_benchmark --generate FILE [SECONDS] | "
                 "--analysis-memory FILE | --analysis-stream FILE | "
                 "--normalize-current INPUT OUTPUT | --normalize-two-pass INPUT\n";
    return 2;
}
