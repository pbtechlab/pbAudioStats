#ifndef PB_AUDIO_STATS_NORMALIZE_TEST_HOOKS_H
#define PB_AUDIO_STATS_NORMALIZE_TEST_HOOKS_H

namespace pb_audio::detail {

// Internal regression-test hook. -1 disables failure injection; 0 fails the
// next sample chunk; N fails after N chunks have been written.
void set_normalizer_write_failure_after_chunks_for_testing(int chunks);
bool normalizer_output_size_supported_for_testing(unsigned long long samples,
                                                  unsigned short bit_depth,
                                                  bool aiff);

}  // namespace pb_audio::detail

#endif
