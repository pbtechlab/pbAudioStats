#pragma once

#include <cstdint>

namespace pb_audio::detail {

// Regression-test instrumentation. Production behavior is unchanged; the
// counter lets tests prove that metadata-only analysis never decodes samples.
void reset_decoded_frame_count();
uint64_t decoded_frame_count();

}  // namespace pb_audio::detail
