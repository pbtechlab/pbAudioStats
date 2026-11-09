#ifndef PB_AUDIO_STATS_RMS_CORE_H
#define PB_AUDIO_STATS_RMS_CORE_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#if defined(PB_SIMD_AVX2)
#include <immintrin.h>
#endif

namespace pb_audio {

struct DenormalGuard {
#if defined(PB_SIMD_AVX2)
    unsigned int old_csr;
    DenormalGuard() : old_csr(_mm_getcsr()) {
        _mm_setcsr(old_csr | 0x8040u);
    }
    ~DenormalGuard() { _mm_setcsr(old_csr); }
#else
    DenormalGuard() = default;
#endif
    DenormalGuard(const DenormalGuard&) = delete;
    DenormalGuard& operator=(const DenormalGuard&) = delete;
};

struct RmsChannelState {
    double avg_sigma_x2 = 0.0;
    double max_sigma_x2 = 0.0;
    double min_sigma_x2 = std::numeric_limits<double>::max();
    double sum_sq = 0.0;
    uint64_t sample_count = 0;
    bool tracked = false;
};

struct RmsResult {
    std::vector<RmsChannelState> states;
    double min_db = -96.0;
    double max_db = -96.0;
    double average_db = -96.0;
    bool silent = false;
};

class RmsCore {
public:
    RmsCore(uint32_t sample_rate, uint16_t channels, double window_ms);
    void process(const float* samples, size_t frames, uint64_t start_index);
    void finalize(uint64_t measured_frames);
    RmsResult result() const;

    uint64_t tc_samples() const { return tc_samples_; }
    bool settling_fallback() const { return settling_fallback_; }

private:
    double sample_rate_;
    uint16_t channels_;
    double mult_;
    double one_minus_mult_;
    uint64_t tc_samples_;
    bool settling_fallback_ = false;
    std::vector<RmsChannelState> states_;
};

}  // namespace pb_audio

#endif
