#include <pbAudioStats.h>

#include <iostream>

int main() {
    // Keep the public README calls compiling in both add_subdirectory and
    // installed-package consumer tests without requiring fixture files.
    if (false) {
        pb_audio::AudioStats stats = pb_audio::analyze("input.wav");
        std::cout << stats.integrated_loudness << stats.true_peak << stats.rms_average;
        pb_audio::Normalizer::normalize_and_save(
            "input.wav", "output.wav",
            pb_audio::Normalizer::Target::Integrated, -23.0);
    }

    pb_audio::AudioData audio;
    audio.sample_rate = 48000;
    audio.channels = 1;
    audio.bit_depth = 32;
    audio.total_frames = 12000;
    audio.samples.assign(12000, 0.5f);
    return pb_audio::RMSMeter::measure(audio).valid ? 0 : 1;
}
