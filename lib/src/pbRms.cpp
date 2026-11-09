/*
 * pbRms.cpp - RMS Measurement (SOX compatible)
 * Uses exponential moving average with 50ms time constant (SOX default)
 *
 * SOX uses exponential smoothing for RMS Peak/Trough:
 *   mult = exp(-1 / (time_constant * sample_rate))
 *   avg_sigma_x2 = avg_sigma_x2 * mult + (1 - mult) * sample^2
 *
 * SOX Overall calculation:
 * - RMS lev dB = overall RMS across all channels (sum of squares / total samples)
 * - RMS Pk dB = max across all channels' max_sigma_x2
 * - RMS Tr dB = min across all channels' min_sigma_x2
 *
 * NOTE: RMS Min values are output as-is (no floor restriction).
 * Only -inf is floored to -96 dB for SOX compatibility.
 *
 * AUD-01 / AUD-02 fixes:
 * - This file holds the single implementation of the RMS core (RmsCore). The
 *   loudness path in pbLoudness.cpp uses the same core, so analysis and
 *   normalization can no longer disagree about RMS values.
 * - Denormal flush + zero-energy guard: a channel state that never accumulated
 *   energy is reported as "not tracked" instead of as an astronomically small
 *   value. Previously such a value produced ~-3200 dB, which the normalizer
 *   turned into +inf gain and consequently full-scale samples in silent regions.
 * - The exact settling boundary no longer registers an unmeasured state as a
 *   trough: only states that actually reached the settling time participate in
 *   min/max, and inputs shorter than the settling time use the SOX fallback.
 */

#include "pbAudioStats.h"
#include "pbRmsCore.h"
#include "pbSimd.h"
#include "pbValidation.h"
#include <cmath>
#include <limits>
#include <algorithm>
#include <vector>

namespace pb_audio {

// Floor value for -inf only (SOX compatibility)
static constexpr double INF_FLOOR_DB = -96.0;
static constexpr double RMS_FLOOR_POWER = 2.511886431509582e-10;  // -96 dB

// Flush denormals on non-AVX2 targets so the EMA decay tail cannot drift into
// a subnormal value that is neither zero nor a meaningful measurement.
static inline void rms_flush_denormal(double& x) {
#if !defined(PB_SIMD_AVX2)
    if (std::fabs(x) < 1.0e-30) x = 0.0;
#else
    (void)x;  // FTZ/DAZ is enabled for the whole measurement window
#endif
}

// ============================================================================
// Shared RMS core
// ============================================================================

RmsCore::RmsCore(uint32_t sample_rate, uint16_t channels, double window_ms)
    : sample_rate_(static_cast<double>(sample_rate)),
      channels_(channels),
      states_(channels) {
    const double time_constant = window_ms / 1000.0;
    mult_ = std::exp(-1.0 / (time_constant * sample_rate_));
    one_minus_mult_ = 1.0 - mult_;
    tc_samples_ = static_cast<uint64_t>(5.0 * time_constant * sample_rate_);
}

void RmsCore::process(const float* samples, size_t frames, uint64_t start_index) {
    if (frames == 0 || channels_ == 0) return;

    // Split at the settling boundary so the inner loops are branch-free.
    size_t pre = 0;
    if (start_index < tc_samples_) {
        uint64_t remaining = tc_samples_ - start_index;
        pre = static_cast<size_t>(std::min<uint64_t>(remaining, static_cast<uint64_t>(frames)));
    }

    const int nch = static_cast<int>(channels_);
    for (int ch = 0; ch < nch; ++ch) {
        RmsChannelState& st = states_[ch];
        double avg = st.avg_sigma_x2;
        double mx = st.max_sigma_x2;
        double mn = st.min_sigma_x2;
        double ssq = st.sum_sq;
        uint64_t cnt = st.sample_count;
        const float* src = samples + ch;

        size_t f = 0;
        for (; f < pre; ++f) {
            double s = static_cast<double>(src[f * nch]);
            if (!std::isfinite(s)) continue;   // AUD-04: non-finite samples are skipped
            double sq = s * s;
            ssq += sq;
            ++cnt;
            avg = avg * mult_ + one_minus_mult_ * sq;
            rms_flush_denormal(avg);
        }
        for (; f < frames; ++f) {
            double s = static_cast<double>(src[f * nch]);
            if (!std::isfinite(s)) continue;
            double sq = s * s;
            ssq += sq;
            ++cnt;
            avg = avg * mult_ + one_minus_mult_ * sq;
            rms_flush_denormal(avg);
            if (avg > mx) mx = avg;
            // A trough is only meaningful once energy has actually been seen.
            if (avg >= RMS_FLOOR_POWER && avg < mn) mn = avg;
        }

        st.avg_sigma_x2 = avg;
        st.max_sigma_x2 = mx;
        st.min_sigma_x2 = mn;
        st.sum_sq = ssq;
        st.sample_count = cnt;
        if (cnt >= tc_samples_) st.tracked = true;
    }
}

void RmsCore::finalize(uint64_t measured_frames) {
    if (measured_frames == 0 || channels_ == 0) return;

    const bool shorter_than_settling = measured_frames <= tc_samples_;
    settling_fallback_ = shorter_than_settling;

    for (uint16_t ch = 0; ch < channels_; ++ch) {
        RmsChannelState& st = states_[ch];
        if (shorter_than_settling) {
            // SOX behaviour: below the settling time, peak and trough are the
            // overall average power of that channel.
            double avg_power = (st.sample_count > 0)
                ? st.sum_sq / static_cast<double>(st.sample_count)
                : 0.0;
            st.max_sigma_x2 = avg_power;
            st.min_sigma_x2 = avg_power;
            st.tracked = true;
        } else if (!st.tracked) {
            // Never reached the settling time for this channel: keep it out of
            // the min/max decision instead of reporting a denormal trough.
            st.tracked = false;
        }
    }
}

RmsResult RmsCore::result() const {
    RmsResult r;
    r.states = states_;

    double total_sum_sq = 0.0;
    uint64_t total_sample_count = 0;
    for (uint16_t ch = 0; ch < channels_; ++ch) {
        total_sum_sq += states_[ch].sum_sq;
        total_sample_count += states_[ch].sample_count;
    }

    if (total_sample_count > 0 && total_sum_sq > 0.0) {
        double avg_rms = std::sqrt(total_sum_sq / static_cast<double>(total_sample_count));
        r.average_db = 20.0 * std::log10(avg_rms);
    }

    double overall_max = 0.0;
    double overall_min = std::numeric_limits<double>::max();
    bool any_min = false;
    for (uint16_t ch = 0; ch < channels_; ++ch) {
        const RmsChannelState& st = states_[ch];
        if (st.max_sigma_x2 > overall_max) overall_max = st.max_sigma_x2;
        if (st.tracked && st.min_sigma_x2 > 0.0 &&
            st.min_sigma_x2 < std::numeric_limits<double>::max()) {
            if (!any_min || st.min_sigma_x2 < overall_min) {
                overall_min = st.min_sigma_x2;
                any_min = true;
            }
        }
    }

    if (overall_max > 0.0) r.max_db = 20.0 * std::log10(std::sqrt(overall_max));
    if (any_min) {
        double min_db = 20.0 * std::log10(std::sqrt(overall_min));
        r.min_db = std::isfinite(min_db) ? std::max(INF_FLOOR_DB, min_db)
                                         : INF_FLOOR_DB;
    }

    r.silent = (total_sum_sq == 0.0);
    return r;
}

// ============================================================================
// RMS Meter Implementation (uses the shared core)
// ============================================================================

RMSMeter::Result RMSMeter::measure(const AudioData& audio, double window_ms) {
    Result result;
    result.min_db = INF_FLOOR_DB;
    result.max_db = INF_FLOOR_DB;
    result.average_db = INF_FLOOR_DB;

    const auto validation = detail::validate_audio_data(audio);
    if (!validation.valid || !std::isfinite(window_ms) || window_ms <= 0.0) {
        return result;
    }

    DenormalGuard denormal_guard;

    RmsCore core(audio.sample_rate, audio.channels, window_ms);
    core.process(audio.samples.data(), audio.total_frames, 0);
    core.finalize(audio.total_frames);

    const RmsResult r = core.result();
    result.min_db = r.min_db;
    result.max_db = r.max_db;
    result.average_db = r.average_db;
    result.valid = true;
    return result;
}

} // namespace pb_audio
