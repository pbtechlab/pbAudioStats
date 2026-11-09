#ifndef PB_AUDIO_STATS_VALIDATION_H
#define PB_AUDIO_STATS_VALIDATION_H

#include "pbAudioStats.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace pb_audio::detail {

struct AudioValidation {
    bool valid = false;
    float sample_peak = 0.0f;
};

inline AudioValidation validate_audio_data(const AudioData& audio) {
    AudioValidation result;
    if (audio.sample_rate == 0 || audio.sample_rate > 768000 ||
        audio.channels == 0 || audio.channels > 64 ||
        audio.total_frames == 0 || audio.total_frames > (1ull << 31) ||
        audio.total_frames > std::numeric_limits<uint64_t>::max() / audio.channels) {
        return result;
    }

    const uint64_t expected = audio.total_frames * audio.channels;
    if (expected > (1ull << 32) || expected != audio.samples.size()) return result;
    if (!audio.channel_roles.empty() && audio.channel_roles.size() != audio.channels) return result;

    for (float sample : audio.samples) {
        if (!std::isfinite(sample)) return result;
        result.sample_peak = std::max(result.sample_peak, std::fabs(sample));
    }
    // The loudness histogram and filters are specified through +100 LUFS.
    // Reject implausibly large float samples explicitly instead of saturating
    // a histogram bin and returning a misleading value.
    if (result.sample_peak > 10000.0f) return result;
    result.valid = true;
    return result;
}

inline bool validate_stream_info(const StreamInfo& info) {
    return info.sample_rate > 0 && info.sample_rate <= 768000 &&
           info.channels > 0 && info.channels <= 64 &&
           info.total_frames <= (1ull << 31) &&
           (info.channel_roles.empty() || info.channel_roles.size() == info.channels);
}

}  // namespace pb_audio::detail

#endif
