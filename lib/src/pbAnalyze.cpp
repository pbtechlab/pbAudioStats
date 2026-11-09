/*
 * pbAnalyze.cpp - Main Analysis Function
 *
 * Provides three analyze() overloads:
 *   - analyze(filepath): convenience, picks the best path automatically.
 *   - analyze(filepath, use_single_pass): legacy switch (both values now
 *     behave identically; kept for API compatibility).
 *   - analyze(AudioData&, source_path): runs on already-loaded data, used by
 *     pbNormalize to avoid loading the same file twice.
 *
 * Data-flow notes:
 *   - The sample peak is scanned ONCE and shared by measure_with_rms and the
 *     true-peak meter (which also uses it to seed its pruning bound).
 *   - Full analysis uses decoded frame count, channel count, float payload,
 *     and fixed work buffers for memory admission. MP3 always streams because
 *     compressed bytes are not a useful bound on decoded memory.
 */

#include "pbAudioStats.h"
#include "pbSimd.h"
#include "pbValidation.h"

#include <filesystem>
#include <limits>

namespace fs = std::filesystem;

namespace pb_audio {

namespace {

fs::path utf8_path(const std::string& utf8) {
#if defined(_WIN32)
    return fs::u8path(utf8);
#else
    return fs::path(utf8);
#endif
}

void fill_file_info(AudioStats& stats, const std::string& source_path) {
    fs::path p = utf8_path(source_path);
    stats.filepath = source_path;
#if defined(_WIN32)
    stats.filename_ext = p.filename().u8string();
    stats.filename = p.stem().u8string();
#else
    stats.filename_ext = p.filename().string();
    stats.filename = p.stem().string();
#endif
}

void fill_invalid(AudioStats& stats) {
    stats.valid = false;
    stats.sample_rate = 0;
    stats.bit_depth = 0;
    stats.channels = 0;
    stats.duration_seconds = 0.0;
    stats.duration_formatted = "00:00:00.000";
    stats.integrated_loudness = -70.0;
    stats.shortterm_max = -70.0;
    stats.momentary_max = -70.0;
    stats.loudness_range = 0.0;
    stats.sample_peak = -100.0;
    stats.true_peak = -100.0;
    stats.rms_min = -96.0;
    stats.rms_max = -96.0;
    stats.rms_average = -96.0;
}

void fill_audio_info(AudioStats& stats, const AudioData& audio) {
    stats.sample_rate = audio.sample_rate;
    stats.bit_depth = audio.bit_depth;
    stats.channels = audio.channels;
    stats.duration_seconds = audio.duration_seconds();
    stats.duration_formatted = format_duration(stats.duration_seconds);
}

void fill_stream_info(AudioStats& stats, const StreamInfo& info) {
    stats.sample_rate = info.sample_rate;
    stats.bit_depth = info.bit_depth;
    stats.channels = info.channels;
    stats.duration_seconds = (info.sample_rate > 0 && info.total_frames > 0)
        ? static_cast<double>(info.total_frames) / info.sample_rate : 0.0;
    stats.duration_formatted = format_duration(stats.duration_seconds);
}

uint64_t estimate_full_analysis_bytes(const StreamInfo& info) {
    constexpr uint64_t work_buffers = 8ull * 1024ull * 1024ull;
    if (info.channels == 0 || info.total_frames == 0) return work_buffers;
    const uint64_t channels = info.channels;
    if (info.total_frames >
        (std::numeric_limits<uint64_t>::max() - work_buffers) /
            channels / sizeof(float)) {
        return std::numeric_limits<uint64_t>::max();
    }
    return info.total_frames * channels * sizeof(float) + work_buffers;
}

void run_analysis(AudioStats& stats, const AudioData& audio,
                  const AnalysisOptions& options) {
    const auto validation = detail::validate_audio_data(audio);
    if (!validation.valid) {
        fill_invalid(stats);
        return;
    }
    fill_audio_info(stats, audio);

    if (options.metadata_only()) {
        stats.valid = true;
        return;
    }

    const double sample_peak_linear = static_cast<double>(validation.sample_peak);
    bool valid = true;

    if (options.loudness && options.rms) {
        auto measured = LoudnessMeter::measure_with_rms(audio, 50.0, sample_peak_linear);
        stats.integrated_loudness = measured.loudness.integrated;
        stats.shortterm_max = measured.loudness.shortterm_max;
        stats.momentary_max = measured.loudness.momentary_max;
        stats.loudness_range = measured.loudness.range;
        stats.rms_min = measured.rms_min;
        stats.rms_max = measured.rms_max;
        stats.rms_average = measured.rms_average;
        valid = measured.valid;
    } else if (options.loudness) {
        auto measured = LoudnessMeter::measure(audio);
        stats.integrated_loudness = measured.integrated;
        stats.shortterm_max = measured.shortterm_max;
        stats.momentary_max = measured.momentary_max;
        stats.loudness_range = measured.range;
        valid = measured.valid;
    } else if (options.rms) {
        auto measured = RMSMeter::measure(audio);
        stats.rms_min = measured.min_db;
        stats.rms_max = measured.max_db;
        stats.rms_average = measured.average_db;
        valid = measured.valid;
    }

    if (options.sample_peak) stats.sample_peak = linear_to_db(sample_peak_linear);

    if (options.true_peak) {
        stats.true_peak = TruePeakMeter::measure(audio, sample_peak_linear);
        valid = valid && !std::isnan(stats.true_peak);
    }

    stats.valid = valid;
}

void run_stream_analysis(AudioStats& stats, AudioStream& stream) {
    stats.sample_rate = stream.info.sample_rate;
    stats.bit_depth = stream.info.bit_depth;
    stats.channels = stream.info.channels;
    stats.duration_seconds = (stats.sample_rate > 0 && stream.info.total_frames > 0)
        ? static_cast<double>(stream.info.total_frames) /
          static_cast<double>(stats.sample_rate)
        : 0.0;
    stats.duration_formatted = format_duration(stats.duration_seconds);

    auto loudness = LoudnessMeter::measure_stream(stream, 50.0);
    stats.integrated_loudness = loudness.loudness.integrated;
    stats.shortterm_max = loudness.loudness.shortterm_max;
    stats.momentary_max = loudness.loudness.momentary_max;
    stats.loudness_range = loudness.loudness.range;
    stats.sample_peak = loudness.loudness.sample_peak;

    stats.rms_min = loudness.rms_min;
    stats.rms_max = loudness.rms_max;
    stats.rms_average = loudness.rms_average;

    // Exact BS.1770-4 oversampled true peak, computed in the same streaming
    // pass (previously this path silently reported the sample peak).
    stats.true_peak = loudness.true_peak;

    stats.valid = loudness.valid && !std::isnan(stats.true_peak);
}

}  // namespace

AudioStats analyze(const AudioData& audio, const std::string& source_path) {
    return analyze(audio, source_path, AnalysisOptions::all());
}

AudioStats analyze(const AudioData& audio, const std::string& source_path,
                   const AnalysisOptions& options) {
    AudioStats stats;
    fill_file_info(stats, source_path);
    fill_invalid(stats);
    if (!detail::validate_audio_data(audio).valid) {
        fill_invalid(stats);
        return stats;
    }
    run_analysis(stats, audio, options);
    return stats;
}

AudioStats analyze(const std::string& filepath, bool /*use_single_pass*/) {
    return analyze(filepath, AnalysisOptions::all());
}

AudioStats analyze(const std::string& filepath, const AnalysisOptions& options) {
    AudioStats stats;
    fill_file_info(stats, filepath);
    fill_invalid(stats);

    if (options.metadata_only()) {
        auto stream = AudioReader::open_stream(filepath);
        if (!stream || !detail::validate_stream_info(stream->info)) return stats;
        fill_stream_info(stats, stream->info);
        stats.valid = true;
        return stats;
    }

    // Decide from the decoded representation, never the compressed/container
    // byte count. MP3 full analysis streams by default even when the file is
    // small because its expansion ratio is otherwise unbounded here.
    auto stream = options.full_analysis() ? AudioReader::open_stream(filepath) : nullptr;
    const AudioFormat format = AudioReader::detect_format(filepath);
    const bool use_stream = stream && options.full_analysis() &&
        (format == AudioFormat::MP3 ||
         estimate_full_analysis_bytes(stream->info) > options.memory_budget_bytes);

    if (use_stream) {
        run_stream_analysis(stats, *stream);
        return stats;
    }

    auto audio = AudioReader::load(filepath);
    if (!audio) {
        fill_invalid(stats);
        return stats;
    }

    run_analysis(stats, *audio, options);
    return stats;
}

AudioStats analyze(const std::string& filepath) {
    return analyze(filepath, AnalysisOptions::all());
}

}  // namespace pb_audio
