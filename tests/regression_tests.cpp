#include "pbAudioStats.h"
#include "pbAudioReaderTestHooks.h"
#include "pbNormalizeTestHooks.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace pb_audio;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool near(double a, double b, double tolerance = 0.02) {
    return std::isfinite(a) && std::isfinite(b) && std::fabs(a - b) <= tolerance;
}

void put_u16_le(std::ofstream& out, uint16_t value) {
    const char bytes[2] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff)
    };
    out.write(bytes, 2);
}

void put_u32_le(std::ofstream& out, uint32_t value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff)
    };
    out.write(bytes, 4);
}

bool write_pcm_wav(const fs::path& path, uint32_t sample_rate, uint16_t channels,
                   uint16_t bit_depth, const std::vector<float>& samples,
                   uint32_t channel_mask = 0) {
    if ((bit_depth != 8 && bit_depth != 16 && bit_depth != 24 && bit_depth != 32) || channels == 0 ||
        samples.size() % channels != 0) return false;
    const uint32_t bytes_per_sample = bit_depth / 8;
    const uint32_t data_size = static_cast<uint32_t>(samples.size() * bytes_per_sample);
    const uint32_t padding = data_size & 1u;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const uint32_t fmt_size = channel_mask == 0 ? 16u : 40u;
    out.write("RIFF", 4);
    put_u32_le(out, 20 + fmt_size + data_size + padding);
    out.write("WAVEfmt ", 8);
    put_u32_le(out, fmt_size);
    put_u16_le(out, channel_mask == 0 ? 1u : 0xFFFEu);
    put_u16_le(out, channels);
    put_u32_le(out, sample_rate);
    put_u32_le(out, sample_rate * channels * bytes_per_sample);
    put_u16_le(out, static_cast<uint16_t>(channels * bytes_per_sample));
    put_u16_le(out, bit_depth);
    if (channel_mask != 0) {
        put_u16_le(out, 22);
        put_u16_le(out, bit_depth);
        put_u32_le(out, channel_mask);
        static constexpr uint8_t pcm_guid[16] = {
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
            0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
        };
        out.write(reinterpret_cast<const char*>(pcm_guid), sizeof(pcm_guid));
    }
    out.write("data", 4);
    put_u32_le(out, data_size);
    for (float sample : samples) {
        const float clipped = std::max(-1.0f, std::min(1.0f, sample));
        if (bit_depth == 8) {
            const int value = static_cast<int>(std::lrint(clipped * 127.0f + 128.0f));
            out.put(static_cast<char>(std::max(0, std::min(255, value))));
        } else if (bit_depth == 16) {
            const int value = static_cast<int>(std::lrint(clipped * 32767.0f));
            put_u16_le(out, static_cast<uint16_t>(static_cast<int16_t>(value)));
        } else if (bit_depth == 24) {
            const int32_t value = static_cast<int32_t>(std::lrint(clipped * 8388607.0f));
            out.put(static_cast<char>(value & 0xff));
            out.put(static_cast<char>((value >> 8) & 0xff));
            out.put(static_cast<char>((value >> 16) & 0xff));
        } else {
            const int64_t value = static_cast<int64_t>(std::llround(
                static_cast<double>(clipped) * 2147483647.0));
            put_u32_le(out, static_cast<uint32_t>(static_cast<int32_t>(value)));
        }
    }
    if (padding != 0) out.put('\0');
    out.flush();
    return out.good();
}

bool write_float_wav(const fs::path& path, uint32_t sample_rate,
                     const std::vector<float>& samples) {
    const uint32_t data_size = static_cast<uint32_t>(samples.size() * sizeof(float));
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write("RIFF", 4);
    put_u32_le(out, 36 + data_size);
    out.write("WAVEfmt ", 8);
    put_u32_le(out, 16);
    put_u16_le(out, 3);
    put_u16_le(out, 1);
    put_u32_le(out, sample_rate);
    put_u32_le(out, sample_rate * sizeof(float));
    put_u16_le(out, sizeof(float));
    put_u16_le(out, 32);
    out.write("data", 4);
    put_u32_le(out, data_size);
    out.write(reinterpret_cast<const char*>(samples.data()), data_size);
    out.flush();
    return out.good();
}

bool write_float64_wav(const fs::path& path, uint32_t sample_rate,
                       const std::vector<double>& samples) {
    const uint32_t data_size = static_cast<uint32_t>(samples.size() * sizeof(double));
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write("RIFF", 4);
    put_u32_le(out, 36 + data_size);
    out.write("WAVEfmt ", 8);
    put_u32_le(out, 16);
    put_u16_le(out, 3);
    put_u16_le(out, 1);
    put_u32_le(out, sample_rate);
    put_u32_le(out, sample_rate * sizeof(double));
    put_u16_le(out, sizeof(double));
    put_u16_le(out, 64);
    out.write("data", 4);
    put_u32_le(out, data_size);
    out.write(reinterpret_cast<const char*>(samples.data()), data_size);
    out.flush();
    return out.good();
}

std::vector<uint8_t> read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

AudioData constant_audio(uint64_t frames, float amplitude, uint32_t sample_rate = 48000,
                         uint16_t channels = 1) {
    AudioData audio;
    audio.sample_rate = sample_rate;
    audio.channels = channels;
    audio.bit_depth = 32;
    audio.total_frames = frames;
    audio.samples.assign(static_cast<size_t>(frames * channels), amplitude);
    return audio;
}

AudioData sine_audio(double seconds, float amplitude, uint16_t channels = 2) {
    AudioData audio;
    audio.sample_rate = 48000;
    audio.channels = channels;
    audio.bit_depth = 32;
    audio.total_frames = static_cast<uint64_t>(std::llround(seconds * audio.sample_rate));
    audio.samples.resize(static_cast<size_t>(audio.total_frames * channels));
    for (uint64_t frame = 0; frame < audio.total_frames; ++frame) {
        const float value = amplitude * static_cast<float>(
            std::sin(2.0 * 3.141592653589793 * 1000.0 * frame / audio.sample_rate));
        for (uint16_t channel = 0; channel < channels; ++channel) {
            audio.samples[static_cast<size_t>(frame * channels + channel)] = value;
        }
    }
    return audio;
}

void test_rms_boundary_and_consistency() {
    constexpr double expected = -6.020599913;
    for (uint32_t rate : {44100u, 48000u, 96000u}) {
        for (double window_ms : {10.0, 50.0, 100.0}) {
            const uint64_t boundary = static_cast<uint64_t>(5.0 * window_ms / 1000.0 * rate);
            for (uint16_t channels : {1u, 2u}) {
                for (int offset : {-1, 0, 1}) {
                    const uint64_t frames = static_cast<uint64_t>(
                        static_cast<int64_t>(boundary) + offset);
                    AudioData audio = constant_audio(frames, 0.5f, rate, channels);
                    const auto single = RMSMeter::measure(audio, window_ms);
                    const std::string context = " rate=" + std::to_string(rate) +
                        " window=" + std::to_string(window_ms) +
                        " channels=" + std::to_string(channels) +
                        " frames=" + std::to_string(frames);
                    check(near(single.min_db, expected, 0.05),
                          "RMS minimum must remain stable at settling boundary" + context);
                    check(near(single.max_db, expected, 0.05),
                          "RMS maximum must remain stable at settling boundary" + context);
                    check(near(single.average_db, expected, 0.01),
                          "RMS average must remain stable at settling boundary" + context);
                    check(single.valid, "valid RMS input must report success" + context);

                    const auto combined = LoudnessMeter::measure_with_rms(audio, window_ms);
                    check(near(combined.rms_min, single.min_db, 1.0e-9),
                          "single/batch RMS minimum mismatch" + context);
                    check(near(combined.rms_max, single.max_db, 1.0e-9),
                          "single/batch RMS maximum mismatch" + context);
                    check(near(combined.rms_average, single.average_db, 1.0e-9),
                          "single/batch RMS average mismatch" + context);
                }
            }
        }
    }
}

void test_invalid_memory_inputs() {
    AudioData audio = constant_audio(64, 0.25f);
    const std::vector<float> original = audio.samples;
    check(!Normalizer::apply_gain(audio, std::numeric_limits<double>::infinity()),
          "infinite gain must be rejected");
    check(audio.samples == original, "rejected gain must not change samples");
    audio.samples.back() = std::numeric_limits<float>::quiet_NaN();
    check(!Normalizer::apply_gain(audio, 1.0), "NaN sample must be rejected");
    check(!RMSMeter::measure(audio).valid, "RMS API must reject a NaN sample");
    check(!LoudnessMeter::measure(audio).valid, "loudness API must reject a NaN sample");
    check(std::isnan(TruePeakMeter::measure(audio)), "true-peak API must reject a NaN sample");
    check(!analyze(audio, "nan-memory.wav").valid, "analysis API must reject a NaN sample");

    AudioData malformed = constant_audio(64, 0.25f);
    malformed.total_frames = 65;
    const auto rms = RMSMeter::measure(malformed);
    check(rms.min_db == -96.0 && rms.max_db == -96.0 && rms.average_db == -96.0,
          "inconsistent frame count must be rejected");

    const auto invalid_window = RMSMeter::measure(constant_audio(64, 0.25f),
        std::numeric_limits<double>::quiet_NaN());
    check(invalid_window.min_db == -96.0, "NaN RMS window must be rejected");

    for (size_t count : {31u, 32u, 33u, 65535u, 65536u, 65537u}) {
        AudioData boundary = constant_audio(count, 0.25f);
        boundary.samples.back() = std::numeric_limits<float>::quiet_NaN();
        const std::string context = " sample-count=" + std::to_string(count);
        check(!RMSMeter::measure(boundary).valid, "RMS SIMD/chunk boundary rejection" + context);
        check(!LoudnessMeter::measure(boundary).valid,
              "loudness SIMD/chunk boundary rejection" + context);
        check(std::isnan(TruePeakMeter::measure(boundary)),
              "true-peak SIMD/chunk boundary rejection" + context);
        check(!analyze(boundary, "nan-boundary.wav").valid,
              "analysis SIMD/chunk boundary rejection" + context);
    }

    AudioData invalid_rate = constant_audio(64, 0.25f);
    invalid_rate.sample_rate = 0;
    check(!analyze(invalid_rate, "invalid-rate.wav").valid, "zero sample rate must fail");
    AudioData invalid_roles = constant_audio(64, 0.25f, 48000, 2);
    invalid_roles.channel_roles = {ChannelRole::Left};
    check(!analyze(invalid_roles, "invalid-roles.wav").valid,
          "channel-role count mismatch must fail");
}

void test_normalization_safety(const fs::path& root) {
    check(detail::normalizer_output_size_supported_for_testing(1024, 32, false),
          "small WAV output size must be accepted");
    check(!detail::normalizer_output_size_supported_for_testing(
              (std::numeric_limits<uint32_t>::max() / 4ull) + 1ull, 32, false),
          "WAV output beyond RIFF size limit must be rejected before open");
    check(!detail::normalizer_output_size_supported_for_testing(
              (std::numeric_limits<uint32_t>::max() / 4ull) + 1ull, 32, true),
          "AIFF output beyond FORM size limit must be rejected before open");

    const uint32_t rate = 48000;
    const uint16_t channels = 2;
    const uint64_t frames = rate * 4ull;
    std::vector<float> samples(static_cast<size_t>(frames * channels), 0.0f);
    for (uint64_t frame = 0; frame < rate; ++frame) {
        samples[static_cast<size_t>(frame * channels)] = 0.5f;
        samples[static_cast<size_t>(frame * channels + 1)] = 0.5f;
    }
    const fs::path input = root / "rms-silence.wav";
    check(write_pcm_wav(input, rate, channels, 16, samples), "create RMS safety input");
    const auto before = read_bytes(input);

    auto memory_audio = AudioReader::load(input.u8string());
    auto stream = AudioReader::open_stream(input.u8string());
    check(memory_audio != nullptr && stream != nullptr, "open memory and stream RMS input");
    if (memory_audio && stream) {
        const auto memory_result = LoudnessMeter::measure_with_rms(*memory_audio);
        const auto stream_result = LoudnessMeter::measure_stream(*stream);
        check(near(memory_result.rms_min, stream_result.rms_min, 1.0e-9),
              "memory/stream RMS minimum mismatch");
        check(near(memory_result.rms_max, stream_result.rms_max, 1.0e-9),
              "memory/stream RMS maximum mismatch");
        check(near(memory_result.rms_average, stream_result.rms_average, 1.0e-9),
              "memory/stream RMS average mismatch");
    }

    check(!Normalizer::normalize_and_save(input.u8string(), input.u8string(),
                                           Normalizer::Target::RMSMin, -20.0),
          "unreachable RMS minimum target must fail");
    check(read_bytes(input) == before, "failed in-place normalization must preserve original bytes");

    const fs::path existing = root / "existing.wav";
    {
        std::ofstream out(existing, std::ios::binary | std::ios::trunc);
        out.write("sentinel", 8);
    }
    const auto sentinel = read_bytes(existing);
    check(!Normalizer::normalize_and_save(input.u8string(), existing.u8string(),
                                           Normalizer::Target::Peak,
                                           std::numeric_limits<double>::quiet_NaN()),
          "NaN normalization target must fail");
    check(read_bytes(existing) == sentinel, "invalid target must not truncate existing output");

    const auto huge = Normalizer::normalize_and_save_detailed(
        input.u8string(), existing.u8string(), Normalizer::Target::Peak, 1.0e308);
    check(!huge && huge.error == Normalizer::Error::UnreachableTarget && !huge.message.empty(),
          "huge target must return a detailed unreachable-target error");
    check(read_bytes(existing) == sentinel, "huge target must not truncate existing output");

    const fs::path nan_input = root / "nan-input.wav";
    std::vector<float> nan_samples(64, 0.25f);
    nan_samples.back() = std::numeric_limits<float>::quiet_NaN();
    check(write_float_wav(nan_input, rate, nan_samples), "create NaN float WAV");
    const auto nan_result = Normalizer::normalize_and_save_detailed(
        nan_input.u8string(), existing.u8string(), Normalizer::Target::Peak, -1.0);
    check(!nan_result && nan_result.error == Normalizer::Error::InvalidAudio,
          "NaN float WAV must return InvalidAudio");
    check(read_bytes(existing) == sentinel, "NaN float WAV must preserve existing output");

    const fs::path chunk_nan_input = root / "chunk-nan-input.wav";
    std::vector<float> chunk_nan_samples(65537, 0.25f);
    chunk_nan_samples.back() = std::numeric_limits<float>::quiet_NaN();
    check(write_float_wav(chunk_nan_input, rate, chunk_nan_samples),
          "create chunk-boundary NaN float WAV");
    auto chunk_nan_stream = AudioReader::open_stream(chunk_nan_input.u8string());
    check(chunk_nan_stream != nullptr, "open chunk-boundary NaN stream");
    if (chunk_nan_stream) {
        check(!LoudnessMeter::measure_stream(*chunk_nan_stream).valid,
              "stream API must reject NaN across a 65536-frame chunk boundary");
    }

    const fs::path unknown = root / "output.xyz";
    check(!Normalizer::normalize_and_save(input.u8string(), unknown.u8string(),
                                           Normalizer::Target::Peak, -1.0),
          "unknown output extension must fail");
    check(!fs::exists(unknown), "unknown extension must not create output");

    const fs::path large_input = root / "large-writer-input.wav";
    std::vector<float> large_samples((1u << 20) + 1u, 0.25f);
    check(write_pcm_wav(large_input, rate, 1, 16, large_samples),
          "create multi-chunk writer input");

    Normalizer::OutputOptions cancel_options;
    int cancel_checks = 0;
    cancel_options.cancel_requested = [&cancel_checks]() { return ++cancel_checks >= 3; };
    const auto cancel_result = Normalizer::normalize_and_save_detailed(
        large_input.u8string(), existing.u8string(), Normalizer::Target::Peak,
        -6.0, cancel_options);
    check(!cancel_result && cancel_result.error == Normalizer::Error::Cancelled,
          "mid-write cancellation must return Cancelled");
    check(read_bytes(existing) == sentinel, "mid-write cancellation must preserve output");

    detail::set_normalizer_write_failure_after_chunks_for_testing(1);
    const auto write_failure = Normalizer::normalize_and_save_detailed(
        large_input.u8string(), existing.u8string(), Normalizer::Target::Peak, -6.0);
    detail::set_normalizer_write_failure_after_chunks_for_testing(-1);
    check(!write_failure && write_failure.error == Normalizer::Error::WriteFailed,
          "injected partial write must return WriteFailed");
    check(read_bytes(existing) == sentinel, "partial write failure must preserve output");

    size_t temp_count = 0;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.path().filename().u8string().find("existing.tmp-") == 0) ++temp_count;
    }
    check(temp_count == 0, "failed writes must not leave sibling temporary files");
}

void test_aiff_rates_and_padding(const fs::path& root) {
    for (uint32_t rate : {44100u, 48000u, 96000u}) {
        std::vector<float> samples(rate / 20u, 0.25f);
        const fs::path input = root / ("rate-" + std::to_string(rate) + ".wav");
        const fs::path output = root / ("rate-" + std::to_string(rate) + ".aiff");
        check(write_pcm_wav(input, rate, 1, 16, samples), "create AIFF rate input");
        check(Normalizer::normalize_and_save(input.u8string(), output.u8string(),
                                              Normalizer::Target::Peak, -6.0),
              "WAV to AIFF normalization must succeed");
        auto decoded = AudioReader::load(output.u8string());
        check(decoded && decoded->sample_rate == rate, "AIFF sample rate round-trip");
        check(decoded && decoded->total_frames == samples.size(), "AIFF frame count round-trip");
    }

    const fs::path tiny_wav = root / "tiny-in.wav";
    const fs::path padded_wav = root / "tiny-out.wav";
    const fs::path padded_aiff = root / "tiny-out.aiff";
    check(write_pcm_wav(tiny_wav, 48000, 1, 8, {0.5f}), "create one-sample WAV");
    check(Normalizer::normalize_and_save(tiny_wav.u8string(), padded_wav.u8string(),
                                         Normalizer::Target::Peak, -6.0),
          "one-sample WAV normalization");
    check(Normalizer::normalize_and_save(tiny_wav.u8string(), padded_aiff.u8string(),
                                         Normalizer::Target::Peak, -6.0),
          "one-sample AIFF normalization");
    check(fs::exists(padded_wav) && fs::file_size(padded_wav) % 2 == 0,
          "WAV odd payload must be padded");
    check(fs::exists(padded_aiff) && fs::file_size(padded_aiff) % 2 == 0,
          "AIFF odd payload must be padded");
}

void test_channel_layouts(const fs::path& root) {
    const std::vector<std::vector<ChannelRole>> layouts = {
        {ChannelRole::Left, ChannelRole::Right,
         ChannelRole::LeftSurround, ChannelRole::RightSurround},
        {ChannelRole::Left, ChannelRole::Right, ChannelRole::Center, ChannelRole::LFE},
        {ChannelRole::Left, ChannelRole::Right, ChannelRole::Center, ChannelRole::LFE,
         ChannelRole::LeftSurround, ChannelRole::RightSurround},
        {ChannelRole::Left, ChannelRole::Right, ChannelRole::Center, ChannelRole::LFE,
         ChannelRole::LeftRearSurround, ChannelRole::RightRearSurround},
        {ChannelRole::Left, ChannelRole::Right, ChannelRole::Center, ChannelRole::LFE,
         ChannelRole::LeftRearSurround, ChannelRole::RightRearSurround,
         ChannelRole::LeftSurround, ChannelRole::RightSurround}
    };

    for (const auto& roles : layouts) {
        double reference = std::numeric_limits<double>::quiet_NaN();
        for (size_t active = 0; active < roles.size(); ++active) {
            AudioData audio;
            audio.sample_rate = 48000;
            audio.channels = static_cast<uint16_t>(roles.size());
            audio.bit_depth = 32;
            audio.total_frames = 48000;
            audio.channel_roles = roles;
            audio.samples.assign(static_cast<size_t>(audio.total_frames * audio.channels), 0.0f);
            for (uint64_t frame = 0; frame < audio.total_frames; ++frame) {
                audio.samples[static_cast<size_t>(frame * audio.channels + active)] =
                    0.1f * std::sin(2.0 * 3.141592653589793 * 1000.0 * frame / audio.sample_rate);
            }
            const auto measured = LoudnessMeter::measure(audio);
            check(measured.valid, "known channel layout must be measurable");
            const ChannelRole role = roles[active];
            if (role == ChannelRole::LFE) {
                check(measured.integrated <= -69.9, "LFE must not contribute to BS.1770 loudness");
            } else if (role == ChannelRole::LeftSurround ||
                       role == ChannelRole::RightSurround ||
                       role == ChannelRole::LeftRearSurround ||
                       role == ChannelRole::RightRearSurround) {
                check(std::isfinite(reference) && near(measured.integrated - reference, 1.5, 0.05),
                      "surround channel must receive +1.5 dB BS.1770 weighting");
            } else if (!std::isfinite(reference)) {
                reference = measured.integrated;
            } else {
                check(near(measured.integrated, reference, 0.02),
                      "front channels must use equal BS.1770 weighting");
            }
        }
    }

    const uint32_t mask_3_1 = 0x0000000Fu;
    const uint16_t channels = 4;
    const uint64_t frames = 48000;
    std::vector<float> samples(static_cast<size_t>(frames * channels), 0.0f);
    for (uint64_t frame = 0; frame < frames; ++frame) {
        samples[static_cast<size_t>(frame * channels + 2)] =
            0.1f * std::sin(2.0 * 3.141592653589793 * 1000.0 * frame / 48000.0);
    }
    const fs::path input = root / "layout-3.1.wav";
    const fs::path output = root / "layout-3.1-normalized.wav";
    check(write_pcm_wav(input, 48000, channels, 16, samples, mask_3_1),
          "create WAVE_FORMAT_EXTENSIBLE 3.1 input");

    const std::vector<ChannelRole> expected = {
        ChannelRole::Left, ChannelRole::Right, ChannelRole::Center, ChannelRole::LFE};
    auto memory = AudioReader::load(input.u8string());
    auto stream = AudioReader::open_stream(input.u8string());
    check(memory && memory->channel_roles == expected, "memory reader must preserve channel mask");
    check(stream && stream->info.channel_roles == expected, "stream reader must preserve channel mask");

    check(Normalizer::normalize_and_save(input.u8string(), output.u8string(),
                                         Normalizer::Target::Peak, -6.0),
          "normalize WAVE_FORMAT_EXTENSIBLE input");
    auto saved = AudioReader::load(output.u8string());
    check(saved && saved->channel_roles == expected,
          "normalized WAV must preserve the channel mask");

    AudioData fallback = constant_audio(48000, 0.1f, 48000, 4);
    check(fallback.channel_roles.empty() && LoudnessMeter::measure(fallback).valid,
          "maskless input must retain the legacy channel-count fallback");
}

void test_output_precision(const fs::path& root) {
    for (uint16_t depth : {8u, 16u, 24u, 32u}) {
        for (size_t frames : {1u, 2u}) {
            const fs::path input = root / ("precision-in-" + std::to_string(depth) + "-" +
                                            std::to_string(frames) + ".wav");
            std::vector<float> samples(frames, 0.5f);
            if (frames > 1) samples[1] = -0.25f;
            check(write_pcm_wav(input, 48000, 1, depth, samples), "create precision input");

            Normalizer::OutputOptions options;
            options.bit_depth = depth;
            for (const std::string& extension : {std::string(".wav"), std::string(".aiff")}) {
                const fs::path output = root / ("precision-out-" + std::to_string(depth) + "-" +
                                                 std::to_string(frames) + extension);
                const auto result = Normalizer::normalize_and_save_detailed(
                    input.u8string(), output.u8string(), Normalizer::Target::Peak, -6.0, options);
                check(result.success, "explicit integer PCM output must succeed");
                auto decoded = AudioReader::load(output.u8string());
                check(decoded && decoded->bit_depth == depth, "output bit depth must match option");
                check(decoded && decoded->total_frames == frames, "odd/even frame count round-trip");
                if (decoded && !decoded->samples.empty()) {
                    const double tolerance = depth == 8 ? 0.012 : (depth == 16 ? 0.00006 : 0.000002);
                    check(std::fabs(decoded->samples[0] - 0.5011872f) <= tolerance,
                          "round-trip sample value must match normalized peak");
                }
            }
        }
    }

    const fs::path float_input = root / "float32-source.wav";
    const fs::path float_output = root / "float32-output.wav";
    check(write_pcm_wav(float_input, 48000, 1, 16, {0.5f, -0.25f}),
          "create Float32 output source");
    Normalizer::OutputOptions float_options;
    float_options.encoding = Normalizer::OutputOptions::Encoding::Float32;
    const auto float_result = Normalizer::normalize_and_save_detailed(
        float_input.u8string(), float_output.u8string(), Normalizer::Target::Peak,
        -6.0, float_options);
    check(float_result.success, "explicit Float32 WAV output must succeed");
    auto float_decoded = AudioReader::load(float_output.u8string());
    check(float_decoded && float_decoded->bit_depth == 32, "Float32 WAV bit depth");
    check(float_decoded && near(float_decoded->samples[0], 0.5011872, 1.0e-6),
          "Float32 WAV sample value");

    const fs::path f64_input = root / "float64-source.wav";
    const fs::path f64_output = root / "float64-output.wav";
    check(write_float64_wav(f64_input, 48000, {0.5, -0.25}), "create Float64 input");
    const auto implicit = Normalizer::normalize_and_save_detailed(
        f64_input.u8string(), f64_output.u8string(), Normalizer::Target::Peak, -6.0);
    check(!implicit && implicit.error == Normalizer::Error::UnsupportedBitDepth,
          "Float64 input must not silently fall back to 16-bit output");
    Normalizer::OutputOptions explicit_24;
    explicit_24.bit_depth = 24;
    check(Normalizer::normalize_and_save_detailed(
              f64_input.u8string(), f64_output.u8string(),
              Normalizer::Target::Peak, -6.0, explicit_24).success,
          "Float64 input may be explicitly converted to 24-bit PCM");
}

void test_loudness_range_and_short_policy(const fs::path& root) {
    const auto level_2 = LoudnessMeter::measure(sine_audio(5.0, 2.0f));
    const auto level_10 = LoudnessMeter::measure(sine_audio(5.0, 10.0f));
    check(level_2.valid && level_10.valid, "high-level finite audio must remain measurable");
    check(level_2.integrated > 0.0 && level_10.integrated > level_2.integrated,
          "high-level audio must not collapse to the silence sentinel");
    check(near(level_10.integrated - level_2.integrated,
               20.0 * std::log10(10.0 / 2.0), 0.05),
          "loudness must track gain above 0 dBFS");

    check(!LoudnessMeter::measure(sine_audio(1.0, 20000.0f)).valid,
          "audio above the documented +80 dBFS input limit must fail explicitly");

    for (double seconds : {0.399, 0.400, 0.401, 2.999, 3.000, 3.001, 3.999, 4.000, 4.001}) {
        AudioData audio = sine_audio(seconds, 0.1f);
        const auto compatible = LoudnessMeter::measure(audio);
        check(compatible.valid, "short compatible measurement must be valid");
        check(compatible.short_audio_repeated == (seconds < 4.0),
              "compatible short-audio repetition status");

        LoudnessMeter::MeasurementOptions strict_options;
        strict_options.short_audio_policy = LoudnessMeter::ShortAudioPolicy::DoNotRepeat;
        const auto strict = LoudnessMeter::measure(audio, strict_options);
        check(strict.valid && !strict.short_audio_repeated,
              "strict short-audio measurement must not repeat");
        if (seconds < 0.400) {
            check(strict.integrated == -70.0,
                  "strict audio shorter than one momentary block has no integrated result");
        }
        if (seconds > 0.400) {
            check(strict.integrated > -70.0,
                  "strict audio longer than one momentary block has an integrated result");
        }
    }

    const fs::path short_file = root / "short-policy.wav";
    const AudioData short_audio = sine_audio(0.5, 0.1f, 1);
    check(write_pcm_wav(short_file, short_audio.sample_rate, short_audio.channels, 16,
                        short_audio.samples), "create short stream policy input");
    auto stream = AudioReader::open_stream(short_file.u8string());
    LoudnessMeter::MeasurementOptions strict_options;
    strict_options.short_audio_policy = LoudnessMeter::ShortAudioPolicy::DoNotRepeat;
    check(stream && !LoudnessMeter::measure_stream(*stream, strict_options)
                         .loudness.short_audio_repeated,
          "stream strict policy must not repeat short audio");
}

void test_true_peak_sweep(const fs::path& root) {
    constexpr uint32_t rate = 48000;
    for (double frequency : {1000.0, 10000.0, 16000.0, 18000.0, 20000.0}) {
        for (double phase : {0.0, 0.3926990817, 0.7853981634, 1.1780972451}) {
            AudioData audio;
            audio.sample_rate = rate;
            audio.channels = 1;
            audio.bit_depth = 32;
            audio.total_frames = rate;
            audio.samples.resize(rate);
            for (uint64_t frame = 0; frame < audio.total_frames; ++frame) {
                double envelope = 1.0;
                if (frame < 100) envelope = static_cast<double>(frame) / 100.0;
                if (frame + 100 >= audio.total_frames) {
                    envelope = std::min(envelope,
                        static_cast<double>(audio.total_frames - 1 - frame) / 100.0);
                }
                audio.samples[frame] = static_cast<float>(
                    envelope * std::sin(2.0 * 3.141592653589793 * frequency * frame / rate + phase));
            }
            const double measured = TruePeakMeter::measure(audio);
            if (frequency == 16000.0 && phase == 0.0) {
                check(write_float_wav(root / "true-peak-16k-phase0.wav", rate, audio.samples),
                      "create independent true-peak reference input");
            }
            const std::string context = " frequency=" + std::to_string(frequency) +
                " phase=" + std::to_string(phase);
            check(measured >= -0.30, "true-peak high-frequency under-read" + context +
                  " measured=" + std::to_string(measured));
            check(measured <= 0.50, "true-peak interpolation overshoot" + context);
        }
    }

    AudioData eof = constant_audio(64, 0.0f);
    eof.samples.back() = 1.0f;
    const double eof_memory = TruePeakMeter::measure(eof);
    check(eof_memory >= 0.0, "EOF flush must retain a final-sample peak");

    const fs::path eof_path = root / "true-peak-eof.wav";
    check(write_float_wav(eof_path, rate, eof.samples), "create EOF true-peak input");
    auto stream = AudioReader::open_stream(eof_path.u8string());
    check(stream != nullptr, "open EOF true-peak stream");
    if (stream) {
        const double eof_stream = LoudnessMeter::measure_stream(*stream).true_peak;
        check(near(eof_stream, eof_memory, 1.0e-9), "memory/stream EOF true-peak mismatch");
    }
}

void test_selective_analysis(const fs::path& root) {
    AudioData audio;
    audio.sample_rate = 48000;
    audio.channels = 2;
    audio.bit_depth = 32;
    audio.total_frames = 48000;
    audio.samples.resize(static_cast<size_t>(audio.total_frames) * audio.channels);
    for (uint64_t frame = 0; frame < audio.total_frames; ++frame) {
        const float value = static_cast<float>(0.25 * std::sin(
            2.0 * 3.141592653589793 * 997.0 * frame / audio.sample_rate));
        audio.samples[static_cast<size_t>(frame) * 2] = value;
        audio.samples[static_cast<size_t>(frame) * 2 + 1] = -value;
    }

    const AudioStats all = analyze(audio, "selective.wav");
    check(all.valid, "full selective-analysis reference");

    AnalysisOptions options;
    options.sample_peak = true;
    const AudioStats peak = analyze(audio, "selective.wav", options);
    check(peak.valid && near(peak.sample_peak, all.sample_peak, 1.0e-12),
          "peak-only value must match full analysis");
    check(peak.integrated_loudness == -70.0 && peak.rms_average == -96.0,
          "peak-only analysis must leave unrelated placeholders unchanged");

    options = {};
    options.true_peak = true;
    const AudioStats true_peak = analyze(audio, "selective.wav", options);
    check(true_peak.valid && near(true_peak.true_peak, all.true_peak, 1.0e-9),
          "true-peak-only value must match full analysis");

    options = {};
    options.loudness = true;
    const AudioStats loudness = analyze(audio, "selective.wav", options);
    check(loudness.valid && near(loudness.integrated_loudness,
                                 all.integrated_loudness, 1.0e-9),
          "loudness-only value must match full analysis");

    options = {};
    options.rms = true;
    const AudioStats rms = analyze(audio, "selective.wav", options);
    check(rms.valid && near(rms.rms_average, all.rms_average, 1.0e-9),
          "RMS-only value must match full analysis");

    const fs::path wav = root / "metadata-only.wav";
    check(write_float_wav(wav, audio.sample_rate, audio.samples),
          "create metadata-only test WAV");
    detail::reset_decoded_frame_count();
    const AudioStats metadata = analyze(wav.u8string(), AnalysisOptions{});
    check(metadata.valid && metadata.sample_rate == audio.sample_rate &&
          metadata.channels == 1 && near(metadata.duration_seconds, 2.0, 1.0e-12),
          "metadata-only analysis must return container metadata");
    check(detail::decoded_frame_count() == 0,
          "metadata-only analysis must not decode audio frames");

    AnalysisOptions streamed_options = AnalysisOptions::all();
    streamed_options.memory_budget_bytes = 0;
    const AudioStats streamed = analyze(wav.u8string(), streamed_options);
    const AudioStats memory = analyze(wav.u8string(), AnalysisOptions::all());
    check(streamed.valid && memory.valid,
          "memory-budget stream and memory reference must both succeed");
    check(near(streamed.integrated_loudness, memory.integrated_loudness, 1.0e-9) &&
          near(streamed.true_peak, memory.true_peak, 1.0e-9) &&
          near(streamed.rms_average, memory.rms_average, 1.0e-9),
          "memory-budget stream path must match in-memory analysis");
}

}  // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "pbAudioStats-regression";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (ec) {
        std::cerr << "Unable to create test directory: " << ec.message() << '\n';
        return 1;
    }

    test_rms_boundary_and_consistency();
    test_invalid_memory_inputs();
    test_normalization_safety(root);
    test_aiff_rates_and_padding(root);
    test_channel_layouts(root);
    test_output_precision(root);
    test_loudness_range_and_short_policy(root);
    test_true_peak_sweep(root);
    test_selective_analysis(root);

    if (failures != 0) {
        std::cerr << "Retained test artifacts at: " << root.u8string() << '\n';
        std::cerr << failures << " regression check(s) failed\n";
        return 1;
    }
    if (std::getenv("PB_KEEP_TEST_ARTIFACTS") == nullptr) fs::remove_all(root, ec);
    std::cout << "All regression checks passed\n";
    return 0;
}
