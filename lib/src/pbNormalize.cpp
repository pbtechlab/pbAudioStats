/*
 * pbNormalize.cpp - Audio Normalization
 * Supports Peak, True Peak, Integrated Loudness, Short-term, Momentary, RMS
 *
 * WAV/AIFF: Native implementation (no external dependencies)
 *
 * Performance notes:
 * - normalize_and_save computes ONLY the metric the target needs (a peak-only
 *   normalization no longer runs K-weighting, LRA histograms and the
 *   oversampled true-peak pass).
 * - The gain is fused into the SIMD float->PCM conversion in the writers
 *   (one pass instead of gain pass + clip pass + scalar conversion pass).
 * - Output is converted and written in chunks (no file-sized staging buffer).
 *
 * Accuracy notes:
 * - float->PCM conversion rounds to nearest (ties-to-even), matching SoX;
 *   the previous implementation truncated (up to 1 LSB biased error).
 * - The 32-bit PCM path scales in double precision; the previous float-based
 *   scaling overflowed full-scale +1.0 samples to INT32_MIN.
 */

#include "pbAudioStats.h"
#include "pbSimd.h"
#include "pbValidation.h"
#include "pbNormalizeTestHooks.h"
#include <cmath>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <vector>
#include <limits>
#include <chrono>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace pb_audio {

namespace {
std::atomic<int> g_fail_after_chunks{-1};

bool output_size_supported(uint64_t samples, uint16_t bit_depth, bool aiff,
                           bool wav_extensible = false) {
    if (bit_depth != 8 && bit_depth != 16 && bit_depth != 24 && bit_depth != 32) return false;
    const uint64_t bytes_per_sample = bit_depth / 8;
    if (samples > std::numeric_limits<uint64_t>::max() / bytes_per_sample) return false;
    const uint64_t bytes = samples * bytes_per_sample;
    const uint64_t padding = bytes & 1u;
    const uint64_t overhead = aiff ? 46ull : (wav_extensible ? 60ull : 36ull);
    return bytes + padding <= 0xFFFFFFFFull - overhead;
}
}

namespace detail {
void set_normalizer_write_failure_after_chunks_for_testing(int chunks) {
    g_fail_after_chunks.store(chunks, std::memory_order_relaxed);
}

bool normalizer_output_size_supported_for_testing(unsigned long long samples,
                                                  unsigned short bit_depth,
                                                  bool aiff) {
    return output_size_supported(samples, bit_depth, aiff);
}
}  // namespace detail

// ============================================================================
// Gain Calculation
// ============================================================================

double Normalizer::calculate_gain(const AudioStats& stats, Target target, double target_value) {
    if (!stats.valid) return std::numeric_limits<double>::quiet_NaN();
    double current_value = 0.0;

    switch (target) {
        case Target::Peak:
            current_value = stats.sample_peak;
            break;
        case Target::TruePeak:
            current_value = stats.true_peak;
            break;
        case Target::Integrated:
            current_value = stats.integrated_loudness;
            break;
        case Target::ShorttermMax:
            current_value = stats.shortterm_max;
            break;
        case Target::MomentaryMax:
            current_value = stats.momentary_max;
            break;
        case Target::RMSMin:
            current_value = stats.rms_min;
            break;
        case Target::RMSMax:
            current_value = stats.rms_max;
            break;
        case Target::RMSAverage:
            current_value = stats.rms_average;
            break;
    }

    if (!std::isfinite(target_value) || !std::isfinite(current_value)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double gain = target_value - current_value;
    return std::isfinite(gain) ? gain : std::numeric_limits<double>::quiet_NaN();
}

// ============================================================================
// Apply Gain
// ============================================================================

bool Normalizer::apply_gain(AudioData& audio, double gain_db) {
    if (!std::isfinite(gain_db)) return false;
    for (float sample : audio.samples) {
        if (!std::isfinite(sample)) return false;
    }
    if (gain_db == 0.0) return true;

    const double linear = db_to_linear(gain_db);
    if (!std::isfinite(linear) || linear <= 0.0 ||
        linear > static_cast<double>(std::numeric_limits<float>::max()) ||
        linear < static_cast<double>(std::numeric_limits<float>::min())) {
        return false;
    }
    float linear_gain = static_cast<float>(linear);
    simd::apply_gain_and_clip(audio.samples.data(), audio.samples.size(),
                              linear_gain, -1.0f, 1.0f);
    return true;
}

// ============================================================================
// Path / endian helpers
// ============================================================================

static fs::path utf8_path(const std::string& utf8) {
#if defined(_WIN32)
    return fs::u8path(utf8);
#else
    return fs::path(utf8);
#endif
}

static void write_u16_le(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}

static void write_u32_le(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

static void write_u32_be(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

static void write_u16_be(uint8_t* p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

// Write IEEE 754 extended precision (80-bit)
static void write_extended_be(uint8_t* p, double value) {
    if (value == 0.0) {
        memset(p, 0, 10);
        return;
    }

    int sign = (value < 0) ? 1 : 0;
    if (sign) value = -value;

    int exponent;
    double mantissa = std::frexp(value, &exponent);
    exponent += 16382;

    long double significand = std::ldexp(static_cast<long double>(mantissa), 1);
    const long double two_to_63 = std::ldexp(1.0L, 63);
    uint64_t mant = static_cast<uint64_t>(significand * two_to_63);

    p[0] = (sign << 7) | ((exponent >> 8) & 0x7F);
    p[1] = exponent & 0xFF;
    for (int i = 0; i < 8; i++) {
        p[2 + i] = (mant >> (56 - i * 8)) & 0xFF;
    }
}

// ============================================================================
// Chunked sample conversion + write (gain fused into the SIMD kernels)
// ============================================================================

static bool is_supported_bit_depth(uint16_t bit_depth) {
    return bit_depth == 8 || bit_depth == 16 || bit_depth == 24 || bit_depth == 32;
}

static uint32_t wave_mask_from_roles(const std::vector<ChannelRole>& roles,
                                     uint16_t channels) {
    if (roles.size() != channels) return 0;
    uint32_t mask = 0;
    for (ChannelRole role : roles) {
        uint32_t bit = 0;
        switch (role) {
            case ChannelRole::Left: bit = 0x00000001u; break;
            case ChannelRole::Right: bit = 0x00000002u; break;
            case ChannelRole::Center: bit = 0x00000004u; break;
            case ChannelRole::LFE: bit = 0x00000008u; break;
            case ChannelRole::LeftRearSurround: bit = 0x00000010u; break;
            case ChannelRole::RightRearSurround: bit = 0x00000020u; break;
            case ChannelRole::LeftSurround: bit = 0x00000200u; break;
            case ChannelRole::RightSurround: bit = 0x00000400u; break;
            case ChannelRole::Unknown: return 0;
        }
        if ((mask & bit) != 0) return 0;
        mask |= bit;
    }
    return mask;
}

static bool write_samples_chunked(std::ofstream& file, const AudioData& audio,
                                  uint16_t bit_depth, float gain, bool big_endian,
                                   bool aiff_signed_8bit,
                                   const std::function<bool()>& cancel_requested,
                                   bool& cancelled) {
    const size_t num_samples = audio.samples.size();
    const int bytes_per_sample = bit_depth / 8;

    constexpr size_t CHUNK_SAMPLES = 1u << 20;  // up to 4MB output per chunk
    std::vector<uint8_t> buf(std::min(num_samples, CHUNK_SAMPLES) * bytes_per_sample);

    size_t done = 0;
    while (done < num_samples) {
        if (cancel_requested && cancel_requested()) {
            cancelled = true;
            return false;
        }
        int remaining = g_fail_after_chunks.load(std::memory_order_relaxed);
        if (remaining == 0) return false;
        if (remaining > 0) g_fail_after_chunks.fetch_sub(1, std::memory_order_relaxed);

        size_t n = std::min(CHUNK_SAMPLES, num_samples - done);
        const float* src = audio.samples.data() + done;
        switch (bit_depth) {
            case 8:
                if (aiff_signed_8bit) simd::convert_float_to_s8(src, buf.data(), n, gain);
                else                  simd::convert_float_to_u8(src, buf.data(), n, gain);
                break;
            case 16:
                simd::convert_float_to_i16(src, buf.data(), n, gain, big_endian);
                break;
            case 24:
                simd::convert_float_to_i24(src, buf.data(), n, gain, big_endian);
                break;
            case 32:
                simd::convert_float_to_i32(src, buf.data(), n, gain, big_endian);
                break;
        }
        file.write(reinterpret_cast<const char*>(buf.data()),
                   static_cast<std::streamsize>(n * bytes_per_sample));
        if (!file.good()) return false;
        done += n;
    }
    return true;
}

static bool write_float32_samples_chunked(
        std::ofstream& file, const AudioData& audio, float gain,
        const std::function<bool()>& cancel_requested, bool& cancelled) {
    constexpr size_t CHUNK_SAMPLES = 1u << 20;
    std::vector<uint8_t> buffer(std::min(audio.samples.size(), CHUNK_SAMPLES) * 4u);
    size_t done = 0;
    while (done < audio.samples.size()) {
        if (cancel_requested && cancel_requested()) {
            cancelled = true;
            return false;
        }
        int remaining = g_fail_after_chunks.load(std::memory_order_relaxed);
        if (remaining == 0) return false;
        if (remaining > 0) g_fail_after_chunks.fetch_sub(1, std::memory_order_relaxed);

        const size_t count = std::min(CHUNK_SAMPLES, audio.samples.size() - done);
        for (size_t i = 0; i < count; ++i) {
            const float value = audio.samples[done + i] * gain;
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            write_u32_le(buffer.data() + i * 4u, bits);
        }
        file.write(reinterpret_cast<const char*>(buffer.data()),
                   static_cast<std::streamsize>(count * 4u));
        if (!file.good()) return false;
        done += count;
    }
    return true;
}

// ============================================================================
// Write WAV File (native implementation)
// ============================================================================

static bool write_wav(const std::string& filepath, const AudioData& audio,
                      uint16_t bit_depth, float gain,
                      const std::function<bool()>& cancel_requested,
                      bool& cancelled, bool keep_channel_roles,
                      Normalizer::OutputOptions::Encoding encoding) {
    const size_t num_samples = audio.samples.size();
    const uint32_t channel_mask = keep_channel_roles
        ? wave_mask_from_roles(audio.channel_roles, audio.channels)
        : 0;
    const bool extensible = channel_mask != 0;
    if (!output_size_supported(num_samples, bit_depth, false, extensible)) return false;
    const int bytes_per_sample = bit_depth / 8;
    const uint64_t data_size64 =
        static_cast<uint64_t>(num_samples) * static_cast<uint64_t>(bytes_per_sample);

    // RIFF sizes are 32-bit; refuse to write a silently-corrupt >4GB header.
    if (data_size64 > 0xFFFFFFFFull - 36ull) return false;
    const uint32_t data_size = static_cast<uint32_t>(data_size64);
    const uint32_t padding = data_size & 1u;
    const uint32_t fmt_size = extensible ? 40u : 16u;
    const uint64_t file_size64 = 20ull + fmt_size + data_size + padding;
    if (file_size64 > 0xFFFFFFFFull) return false;
    const uint32_t file_size = static_cast<uint32_t>(file_size64);

    std::ofstream file(utf8_path(filepath), std::ios::binary | std::ios::trunc);
    if (!file) return false;

    uint8_t buf[4];

    // RIFF header
    file.write("RIFF", 4);
    write_u32_le(buf, file_size);
    file.write((char*)buf, 4);
    file.write("WAVE", 4);

    // fmt chunk
    file.write("fmt ", 4);
    write_u32_le(buf, fmt_size);
    file.write((char*)buf, 4);

    const uint16_t base_format = encoding == Normalizer::OutputOptions::Encoding::Float32
        ? 3u : 1u;
    write_u16_le(buf, extensible ? 0xFFFEu : base_format);
    file.write((char*)buf, 2);
    write_u16_le(buf, audio.channels);
    file.write((char*)buf, 2);
    write_u32_le(buf, audio.sample_rate);
    file.write((char*)buf, 4);
    uint32_t byte_rate = audio.sample_rate * audio.channels * bytes_per_sample;
    write_u32_le(buf, byte_rate);
    file.write((char*)buf, 4);
    uint16_t block_align = audio.channels * bytes_per_sample;
    write_u16_le(buf, block_align);
    file.write((char*)buf, 2);
    write_u16_le(buf, bit_depth);
    file.write((char*)buf, 2);

    if (extensible) {
        write_u16_le(buf, 22);  // cbSize
        file.write((char*)buf, 2);
        write_u16_le(buf, bit_depth);  // valid bits per sample
        file.write((char*)buf, 2);
        write_u32_le(buf, channel_mask);
        file.write((char*)buf, 4);
        const uint8_t subformat_guid[16] = {
            static_cast<uint8_t>(base_format), 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
            0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
        };
        file.write(reinterpret_cast<const char*>(subformat_guid), sizeof(subformat_guid));
    }

    // data chunk
    file.write("data", 4);
    write_u32_le(buf, data_size);
    file.write((char*)buf, 4);

    const bool samples_written = encoding == Normalizer::OutputOptions::Encoding::Float32
        ? write_float32_samples_chunked(file, audio, gain, cancel_requested, cancelled)
        : write_samples_chunked(file, audio, bit_depth, gain,
                                /*big_endian=*/false, /*aiff_signed_8bit=*/false,
                                cancel_requested, cancelled);
    if (!samples_written) {
        return false;
    }
    if (padding != 0) file.put('\0');
    file.flush();
    if (!file.good()) return false;
    file.close();
    return !file.fail();
}

// ============================================================================
// Write AIFF File (native implementation)
// ============================================================================

static bool write_aiff(const std::string& filepath, const AudioData& audio,
                       uint16_t bit_depth, float gain,
                       const std::function<bool()>& cancel_requested,
                       bool& cancelled) {
    const size_t num_samples = audio.samples.size();
    if (!output_size_supported(num_samples, bit_depth, true)) return false;
    const int bytes_per_sample = bit_depth / 8;
    const uint64_t ssnd_data_size64 =
        static_cast<uint64_t>(num_samples) * static_cast<uint64_t>(bytes_per_sample);

    if (ssnd_data_size64 > 0xFFFFFFFFull - 64ull) return false;  // 32-bit chunk sizes
    const uint32_t ssnd_data_size = static_cast<uint32_t>(ssnd_data_size64);
    const uint32_t padding = ssnd_data_size & 1u;
    const uint32_t ssnd_chunk_size = ssnd_data_size + 8;  // excludes padding
    const uint32_t comm_chunk_size = 18;
    const uint64_t form_size64 = 4ull + 8 + comm_chunk_size + 8 + ssnd_chunk_size + padding;
    if (form_size64 > 0xFFFFFFFFull) return false;
    const uint32_t form_size = static_cast<uint32_t>(form_size64);

    std::ofstream file(utf8_path(filepath), std::ios::binary | std::ios::trunc);
    if (!file) return false;

    // FORM header
    file.write("FORM", 4);
    uint8_t buf[10];
    write_u32_be(buf, form_size);
    file.write((char*)buf, 4);
    file.write("AIFF", 4);

    // COMM chunk
    file.write("COMM", 4);
    write_u32_be(buf, (uint32_t)comm_chunk_size);
    file.write((char*)buf, 4);
    write_u16_be(buf, audio.channels);
    file.write((char*)buf, 2);
    write_u32_be(buf, (uint32_t)audio.total_frames);
    file.write((char*)buf, 4);
    write_u16_be(buf, bit_depth);
    file.write((char*)buf, 2);
    write_extended_be(buf, (double)audio.sample_rate);
    file.write((char*)buf, 10);

    // SSND chunk
    file.write("SSND", 4);
    write_u32_be(buf, ssnd_chunk_size);
    file.write((char*)buf, 4);
    write_u32_be(buf, 0);  // offset
    file.write((char*)buf, 4);
    write_u32_be(buf, 0);  // blockSize
    file.write((char*)buf, 4);

    if (!write_samples_chunked(file, audio, bit_depth, gain,
                               /*big_endian=*/true, /*aiff_signed_8bit=*/true,
                               cancel_requested, cancelled)) {
        return false;
    }
    if (padding != 0) file.put('\0');
    file.flush();
    if (!file.good()) return false;
    file.close();
    return !file.fail();
}

static bool replace_file(const fs::path& source, const fs::path& destination) {
#if defined(_WIN32)
    return MoveFileExW(source.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    fs::rename(source, destination, ec);
    return !ec;
#endif
}

static fs::path make_temp_path(const fs::path& output) {
    static uint64_t sequence = 0;
    const uint64_t stamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string name = output.stem().u8string() + ".tmp-" +
                             std::to_string(stamp) + "-" +
                             std::to_string(++sequence) + output.extension().u8string();
    return output.parent_path() / fs::u8path(name);
}

// ============================================================================
// Normalize and Save
// ============================================================================

// Compute only the metric required by the normalization target. Loudness-,
// peak- and RMS-family targets each cost one specialized pass instead of the
// full analysis suite.
static bool measure_target_value(const AudioData& audio, Normalizer::Target target,
                                 double& out_value) {
    switch (target) {
        case Normalizer::Target::Peak: {
            float pk = simd::find_peak_abs(audio.samples.data(), audio.samples.size());
            out_value = linear_to_db(static_cast<double>(pk));
            return true;
        }
        case Normalizer::Target::TruePeak:
            out_value = TruePeakMeter::measure(audio);
            return true;
        case Normalizer::Target::Integrated:
        case Normalizer::Target::ShorttermMax:
        case Normalizer::Target::MomentaryMax: {
            auto r = LoudnessMeter::measure(audio);
            if (target == Normalizer::Target::Integrated) out_value = r.integrated;
            else if (target == Normalizer::Target::ShorttermMax) out_value = r.shortterm_max;
            else out_value = r.momentary_max;
            return true;
        }
        case Normalizer::Target::RMSMin:
        case Normalizer::Target::RMSMax:
        case Normalizer::Target::RMSAverage: {
            auto r = RMSMeter::measure(audio, 50.0);
            if (target == Normalizer::Target::RMSMin) out_value = r.min_db;
            else if (target == Normalizer::Target::RMSMax) out_value = r.max_db;
            else out_value = r.average_db;
            return true;
        }
    }
    return false;
}

bool Normalizer::normalize_and_save(const std::string& input_path,
                                    const std::string& output_path,
                                    Target target,
                                    double target_value) {
    return normalize_and_save(input_path, output_path, target, target_value, OutputOptions{});
}

bool Normalizer::normalize_and_save(const std::string& input_path,
                                    const std::string& output_path,
                                    Target target,
                                    double target_value,
                                    const OutputOptions& options) {
    return normalize_and_save_detailed(input_path, output_path, target, target_value, options).success;
}

Normalizer::Result Normalizer::normalize_and_save_detailed(
        const std::string& input_path,
        const std::string& output_path,
        Target target,
        double target_value) {
    return normalize_and_save_detailed(
        input_path, output_path, target, target_value, OutputOptions{});
}

Normalizer::Result Normalizer::normalize_and_save_detailed(
        const std::string& input_path,
        const std::string& output_path,
        Target target,
        double target_value,
        const OutputOptions& options) {
    auto fail = [](Error error, const char* message) {
        return Result{false, error, message};
    };

    if (!std::isfinite(target_value)) {
        return fail(Error::InvalidTarget, "Normalization target must be finite.");
    }
    if (options.dither) {
        return fail(Error::UnsupportedOption, "Dither is not implemented for this writer.");
    }
    if (options.cancel_requested && options.cancel_requested()) {
        return fail(Error::Cancelled, "Normalization was cancelled.");
    }

    AudioFormat out_format = AudioReader::detect_format(output_path);
    if (out_format != AudioFormat::WAV && out_format != AudioFormat::AIFF) {
        return fail(Error::UnsupportedOutputFormat,
                    "Output extension must be .wav, .aif, or .aiff.");
    }
    if (options.encoding == OutputOptions::Encoding::Float32 && out_format != AudioFormat::WAV) {
        return fail(Error::UnsupportedOption, "Float32 output is supported only for WAV.");
    }

    // Load once, then measure only what the target needs.
    auto audio = AudioReader::load(input_path);
    const auto validation = audio ? detail::validate_audio_data(*audio) : detail::AudioValidation{};
    if (!validation.valid) {
        return fail(Error::InvalidAudio,
                    "Audio metadata, frame count, or sample values are invalid.");
    }

    const uint16_t bit_depth = options.encoding == OutputOptions::Encoding::Float32
        ? 32u
        : (options.bit_depth == 0 ? audio->bit_depth : options.bit_depth);
    if (options.encoding == OutputOptions::Encoding::Float32 &&
        options.bit_depth != 0 && options.bit_depth != 32) {
        return fail(Error::UnsupportedBitDepth, "Float32 output requires a 32-bit depth.");
    }
    if (!is_supported_bit_depth(bit_depth)) {
        return fail(Error::UnsupportedBitDepth, "Output bit depth must be 8, 16, 24, or 32.");
    }

    double current_value = 0.0;
    if (!measure_target_value(*audio, target, current_value)) {
        return fail(Error::InvalidTarget, "Unknown normalization target.");
    }
    if (!std::isfinite(current_value)) {
        // Silent input (or measurement floor): a finite gain cannot reach the
        // target; the previous code amplified by +inf and wrote NaN samples.
        return fail(Error::UnreachableTarget, "The selected metric is not finite for this audio.");
    }

    const double gain_db = target_value - current_value;
    const double linear_gain = db_to_linear(gain_db);
    if (!std::isfinite(gain_db) || !std::isfinite(linear_gain) || linear_gain <= 0.0 ||
        linear_gain > static_cast<double>(std::numeric_limits<float>::max()) ||
        linear_gain < static_cast<double>(std::numeric_limits<float>::min())) {
        return fail(Error::UnreachableTarget, "Required gain is outside the supported range.");
    }

    if (static_cast<double>(validation.sample_peak) * linear_gain > 1.0 + 1.0e-7) {
        return fail(Error::UnreachableTarget,
                    "Required gain would clip, so the requested metric cannot be reached.");
    }

    const float gain = static_cast<float>(linear_gain);
    const fs::path output = utf8_path(output_path);
    const fs::path temp = make_temp_path(output);
    std::error_code cleanup_error;
    fs::remove(temp, cleanup_error);

    bool written = false;
    bool cancelled = false;

    switch (out_format) {
        case AudioFormat::AIFF:
            written = write_aiff(temp.u8string(), *audio, bit_depth, gain,
                                 options.cancel_requested, cancelled);
            break;
        case AudioFormat::WAV:
            written = write_wav(temp.u8string(), *audio, bit_depth, gain,
                                options.cancel_requested, cancelled,
                                options.keep_channel_roles, options.encoding);
            break;
        default:
            break;
    }

    if (!written) {
        fs::remove(temp, cleanup_error);
        if (cancelled) return fail(Error::Cancelled, "Normalization was cancelled.");
        return fail(Error::WriteFailed, "Failed while writing the temporary output file.");
    }

    auto verify = AudioReader::open_stream(temp.u8string());
    const bool verified = verify && verify->info.sample_rate == audio->sample_rate &&
        verify->info.channels == audio->channels &&
        verify->info.total_frames == audio->total_frames &&
        verify->info.bit_depth == bit_depth;
    verify.reset();  // release the file handle before remove/replace on Windows
    if (!verified) {
        fs::remove(temp, cleanup_error);
        return fail(Error::VerificationFailed,
                    "Temporary output metadata did not match the source audio.");
    }

    if (!replace_file(temp, output)) {
        fs::remove(temp, cleanup_error);
        return fail(Error::ReplaceFailed, "Could not atomically replace the output file.");
    }
    return Result{true, Error::None, {}};
}

} // namespace pb_audio
