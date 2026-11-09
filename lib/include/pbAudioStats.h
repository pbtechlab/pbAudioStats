/*
 * pbAudioStats.h - Audio Statistics Library
 * Supports: WAV, AIFF, MP3
 * Features: BS.1770-4 Loudness, True Peak, RMS, Normalization
 */

#ifndef PB_AUDIO_STATS_H
#define PB_AUDIO_STATS_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <memory>

namespace pb_audio {

// Audio format enumeration
enum class AudioFormat {
    Unknown,
    WAV,
    AIFF,
    MP3
};

// BS.1770-4 channel roles. The role, not the physical index, decides the
// K-weighting channel weight (LFE is excluded, surrounds get +1.5 dB).
enum class ChannelRole : uint8_t {
    Left = 0,
    Right,
    Center,
    LFE,
    LeftSurround,
    RightSurround,
    LeftRearSurround,
    RightRearSurround,
    Unknown          // no position information available
};

// Audio data container
struct AudioData {
    std::vector<float> samples;      // Interleaved samples
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bit_depth;
    uint64_t total_frames;

    // Per-channel role. Empty means "unknown layout" (legacy behaviour:
    // position inferred from the channel count). When a WAV channel mask or
    // an AIFF channel arrangement is known, this holds the real layout.
    std::vector<ChannelRole> channel_roles;

    double duration_seconds() const {
        return sample_rate > 0 ? (double)total_frames / sample_rate : 0.0;
    }
};

// Streamed audio info
struct StreamInfo {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bit_depth;
    uint64_t total_frames;
    std::vector<ChannelRole> channel_roles;
};

class AudioStream {
public:
    StreamInfo info = {};
    virtual ~AudioStream() = default;
    virtual size_t read_frames(float* buffer, size_t max_frames) = 0;
};

// Measurement results
struct AudioStats {
    // True when load and analysis succeeded
    bool valid = false;

    // File information
    std::string filename;            // Without extension
    std::string filename_ext;        // With extension
    std::string filepath;            // Full path
    uint32_t sample_rate;
    uint16_t bit_depth;
    uint16_t channels;
    double duration_seconds;
    std::string duration_formatted;  // HH:MM:SS.mmm

    // Loudness (BS.1770-4)
    double integrated_loudness;      // LUFS
    double shortterm_max;            // LUFS
    double momentary_max;            // LUFS
    double loudness_range;           // LU

    // Peak
    double sample_peak;              // dBFS
    double true_peak;                // dBFS

    // RMS
    double rms_min;                  // dB
    double rms_max;                  // dB
    double rms_average;              // dB
};

struct AnalysisOptions {
    bool sample_peak = false;
    bool true_peak = false;
    bool loudness = false;
    bool rms = false;
    // Maximum decoded float payload retained by the full-analysis memory
    // path. MP3 full analysis always streams. A zero value forces streaming.
    uint64_t memory_budget_bytes = 64ull * 1024ull * 1024ull;

    static AnalysisOptions all() {
        return {true, true, true, true, 64ull * 1024ull * 1024ull};
    }

    bool metadata_only() const {
        return !sample_peak && !true_peak && !loudness && !rms;
    }

    bool full_analysis() const {
        return sample_peak && true_peak && loudness && rms;
    }
};

// ============================================================================
// Audio File Reader
// ============================================================================

class AudioReader {
public:
    static AudioFormat detect_format(const std::string& filepath);
    static std::unique_ptr<AudioData> load(const std::string& filepath);
    static std::unique_ptr<AudioStream> open_stream(const std::string& filepath);

private:
    static std::unique_ptr<AudioData> load_wav(const std::string& filepath);
    static std::unique_ptr<AudioData> load_aiff(const std::string& filepath);
    static std::unique_ptr<AudioData> load_mp3(const std::string& filepath);
};

// ============================================================================
// BS.1770-4 Loudness Measurement
// ============================================================================

class LoudnessMeter {
public:
    enum class ShortAudioPolicy {
        RepeatToFourSeconds,
        DoNotRepeat
    };

    struct MeasurementOptions {
        double rms_window_ms = 50.0;
        ShortAudioPolicy short_audio_policy = ShortAudioPolicy::RepeatToFourSeconds;
    };

    struct Result {
        double integrated;       // LUFS
        double momentary_max;    // LUFS
        double shortterm_max;    // LUFS
        double range;            // LU (LRA)
        double sample_peak;      // dBFS
        bool valid = false;
        bool short_audio_repeated = false;
    };

    struct ExtendedResult {
        Result loudness;
        double rms_min;
        double rms_max;
        double rms_average;
        // True peak (dBTP). Filled by measure_stream (single-pass exact
        // BS.1770-4 oversampled peak); -inf when not computed.
        double true_peak;
        bool valid = false;
    };

    static Result measure(const AudioData& audio);
    static Result measure(const AudioData& audio, const MeasurementOptions& options);
    static ExtendedResult measure_with_rms(const AudioData& audio, double window_ms = 50.0,
                                           double known_sample_peak_linear = -1.0);
    static ExtendedResult measure_with_rms(const AudioData& audio,
                                           const MeasurementOptions& options,
                                           double known_sample_peak_linear = -1.0);
    static ExtendedResult measure_stream(AudioStream& stream, double window_ms = 50.0);
    static ExtendedResult measure_stream(AudioStream& stream,
                                         const MeasurementOptions& options);

private:
    // K-weighting filter coefficients
    struct BiquadCoeffs {
        double b0, b1, b2;
        double a1, a2;
    };

    static BiquadCoeffs calc_high_shelf(double sample_rate);
    static BiquadCoeffs calc_high_pass(double sample_rate);
};

// ============================================================================
// True Peak Measurement (ITU-R BS.1770-4 compliant)
// ============================================================================

class TruePeakMeter {
public:
    // 4x oversampling as per BS.1770-4
    static double measure(const AudioData& audio);
    // Same, but reuses an already-computed sample peak (linear, >= 0) to
    // avoid a duplicate full-buffer scan and to seed chunk pruning.
    static double measure(const AudioData& audio, double known_sample_peak_linear);

private:
    static constexpr int OVERSAMPLE_FACTOR = 4;
    static void apply_lpf(const float* input, float* output, size_t count, int channels);
};

// ============================================================================
// RMS Measurement
// ============================================================================

class RMSMeter {
public:
    struct Result {
        double min_db;           // RMS trough (SOX-compatible, no floor except -inf)
        double max_db;           // RMS peak
        double average_db;       // RMS average
        bool valid = false;
    };

    // Window size in milliseconds (50ms as per sox default)
    static Result measure(const AudioData& audio, double window_ms = 50.0);
};

// ============================================================================
// Normalizer
// ============================================================================

class Normalizer {
public:
    enum class Target {
        Peak,
        TruePeak,
        Integrated,
        ShorttermMax,
        MomentaryMax,
        RMSMin,
        RMSMax,
        RMSAverage
    };

    // Returns gain in dB needed to normalize to target.
    // Returns NaN when the target is unreachable (non-finite current value).
    static double calculate_gain(const AudioStats& stats, Target target, double target_value);

    // Apply gain to audio data (modifies in place). Refuses non-finite gain;
    // returns false without touching the data.
    static bool apply_gain(AudioData& audio, double gain_db);

    // Output format / precision policy (AUD-14).
    struct OutputOptions {
        enum class Encoding {
            IntegerPCM,
            Float32
        };

        Encoding encoding = Encoding::IntegerPCM;
        uint16_t bit_depth = 0;      // 0 = keep the input depth
        bool dither = false;         // TPDF dither when quantizing downward
        bool keep_channel_roles = true;
        std::function<bool()> cancel_requested;
    };

    enum class Error {
        None,
        UnsupportedOutputFormat,
        InvalidTarget,
        InvalidAudio,
        UnsupportedBitDepth,
        UnreachableTarget,
        WriteFailed,
        VerificationFailed,
        ReplaceFailed,
        Cancelled,
        UnsupportedOption
    };

    struct Result {
        bool success = false;
        Error error = Error::None;
        std::string message;
        explicit operator bool() const { return success; }
    };

    // Normalize and save to file. Writes to a sibling temporary file and only
    // replaces the destination after the header and payload are verified, so a
    // failure never truncates or corrupts an existing file.
    static bool normalize_and_save(const std::string& input_path,
                                   const std::string& output_path,
                                   Target target,
                                   double target_value);

    static bool normalize_and_save(const std::string& input_path,
                                   const std::string& output_path,
                                   Target target,
                                   double target_value,
                                   const OutputOptions& options);

    static Result normalize_and_save_detailed(const std::string& input_path,
                                              const std::string& output_path,
                                              Target target,
                                              double target_value);

    static Result normalize_and_save_detailed(const std::string& input_path,
                                              const std::string& output_path,
                                              Target target,
                                              double target_value,
                                              const OutputOptions& options);

    // Resource limits for hostile headers (AUD-04).
    static constexpr uint64_t kMaxTotalFrames = 1ull << 31;   // ~2.1e9 frames
    static constexpr uint64_t kMaxSamples = 1ull << 32;       // interleaved samples
};

// ============================================================================
// Main Analysis Function
// ============================================================================

// Analyze single file
AudioStats analyze(const std::string& filepath);
AudioStats analyze(const std::string& filepath, bool use_single_pass);
AudioStats analyze(const std::string& filepath, const AnalysisOptions& options);

// Analyze already-loaded audio (no disk re-read)
AudioStats analyze(const AudioData& audio, const std::string& source_path);
AudioStats analyze(const AudioData& audio, const std::string& source_path,
                   const AnalysisOptions& options);

// Utility functions
std::string format_duration(double seconds);
double linear_to_db(double linear);
double db_to_linear(double db);

} // namespace pb_audio

#endif // PB_AUDIO_STATS_H
