# pbAudioStats

High-performance C++17 audio analysis library and CLI with ITU-R BS.1770-4 loudness measurement, true peak detection, RMS analysis, and audio normalization. Its supported behavior is covered by the public deterministic regression suite; selected true-peak and loudness cases are also checked with independent tools.

[日本語版 README](README_ja.md)

## Highlights

- **Tested measurement behavior** — BS.1770-4 / EBU R 128 loudness, EBU Tech 3342 LRA, and SOX-compatible RMS, with explicit numeric tolerances in the public regression suite.
- **Extreme performance** — AVX2 (x86-64) and NEON (ARM64) hand-tuned kernels with a streaming block-FIR true-peak scanner, fully batched K-weighting, and O(1)/sample loudness aggregation.
- **Robust I/O** — native WAV/AIFF readers/writers (PCM 8/16/24/32-bit, IEEE float 32/64-bit, `WAVE_FORMAT_EXTENSIBLE`, AIFF/AIFC incl. `sowt`), MP3 via dr_mp3, full UTF-8 / Japanese filename support on Windows.
- **Parallel batch processing** — scales across all CPU cores for analysis, with a memory-budgeted admission scheme for normalization.

## Features

- **BS.1770-4 Loudness Measurement**
  - Integrated Loudness (LUFS)
  - Short-term Loudness Maximum (LUFS)
  - Momentary Loudness Maximum (LUFS)
  - Loudness Range LRA (LU)
- **True Peak Detection** — 4× oversampling with a 96-tap Kaiser-windowed-sinc prototype (24 taps per phase). The same scanner is used for memory and streaming paths.
- **Sample Peak** (dBFS)
- **RMS Analysis** — SOX-compatible exponential-moving-average implementation (min / max / average)
- **Audio Normalization** — Peak, True Peak, Integrated / Short-term / Momentary Loudness, RMS min/max/average targets
- **Multi-format Support** — WAV, AIFF/AIFC, MP3
- **Cross-platform** — Windows (x64, AVX2) and macOS (Apple Silicon / Intel)

## Format Support

| Container / codec | Read | Normalized output |
|---|---|---|
| WAV PCM 8/16/24/32-bit | Yes | Yes, explicit 8/16/24/32-bit integer PCM |
| WAV IEEE float 32/64-bit | Yes | Float32 or explicit integer PCM; 64-bit input is never silently narrowed |
| WAV `WAVE_FORMAT_EXTENSIBLE` | Yes, including channel mask | Yes when a representable channel layout is retained |
| AIFF/AIFC PCM 8/16/24/32-bit (`sowt` read) | Yes | AIFF integer PCM 8/16/24/32-bit |
| AIFF/AIFC float/compressed codecs | No | No; convert first or use WAV Float32 output |
| MP3 | Yes via dr_mp3 | No; choose `.wav`, `.aif`, or `.aiff` |

Unknown output extensions are rejected before writing.

## Requirements

- C++17 compatible compiler
- CMake 3.16+
- **Windows**: MSVC 2022, **CPU with AVX2 + FMA** (Intel Haswell 2013+ / AMD Excavator 2015+). The binary detects unsupported CPUs at startup and prints a diagnostic instead of crashing.
- **macOS**: Clang/GCC (Apple Silicon NEON, or x86-64 AVX2)

## Building

### Windows

```batch
build_win.bat Release
```

or manually:

```batch
mkdir build_win && cd build_win
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

### macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
cmake --build build -j
```

Universal binary:

```bash
cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
cmake --build build --config Release
```

## CLI Usage

```bash
# Analyze a single file (integrated loudness + true peak)
pbAudioStats -i -tp input.wav

# Analyze a directory with every column
pbAudioStats -f -fe -fea -sr -bt -ch -tm -du -i -s -m -l -pk -tp -rn -rm -ra ./audio_folder/

# Export results to UTF-8 CSV (with BOM, Excel-friendly)
pbAudioStats -i -tp -pk ./audio_folder/ results.csv

# Normalize to -23 LUFS (integrated)
pbAudioStats -norm-i:-23.0 input.wav output.wav

# Normalize a whole folder to -1 dBTP (mirrors the input tree into the output dir)
pbAudioStats -norm-tp:-1.0 ./in_folder/ ./out_folder/

# Limit worker threads
pbAudioStats -j8 -i ./audio_folder/
```

With no analysis options and no `-norm-*`, every column is emitted. If the output path ends in `.csv`, results are written as UTF-8 CSV (with BOM) instead of stdout.

### Analysis Options

| Option | Description | Unit |
|--------|-------------|------|
| `-f` | File name (without extension) | - |
| `-fe` | File name (with extension) | - |
| `-fea` | Full file path | - |
| `-sr` | Sample rate | Hz |
| `-bt` | Bit depth | bits |
| `-ch` | Number of channels | - |
| `-tm` | Total time | HH:MM:SS.mmm |
| `-du` | Duration | seconds |
| `-i` | Integrated Loudness | LUFS |
| `-s` | Short-term Loudness Maximum | LUFS |
| `-m` | Momentary Loudness Maximum | LUFS |
| `-l` | Loudness Range (LRA) | LU |
| `-pk` | Sample Peak | dBFS |
| `-tp` | True Peak | dBTP |
| `-rn` | RMS Minimum | dB |
| `-rm` | RMS Maximum | dB |
| `-ra` | RMS Average | dB |

### Normalization Options

Mutually exclusive with analysis options. The gain needed to hit the target is computed and written atomically through a sibling temporary file. A target that would clip, is non-finite, or is unreachable for silent input is rejected without replacing the destination. **MP3 output is not supported** — normalizing an MP3 requires a `.wav` or `.aiff` output path.

| Option | Description | Example |
|--------|-------------|---------|
| `-norm-pk:<value>` | Normalize to Peak | `-norm-pk:-1.0` |
| `-norm-tp:<value>` | Normalize to True Peak | `-norm-tp:-1.0` |
| `-norm-i:<value>` | Normalize to Integrated Loudness | `-norm-i:-23.0` |
| `-norm-s:<value>` | Normalize to Short-term Max | `-norm-s:-18.0` |
| `-norm-m:<value>` | Normalize to Momentary Max | `-norm-m:-18.0` |
| `-norm-rn:<value>` | Normalize to RMS Min | `-norm-rn:-12.0` |
| `-norm-rm:<value>` | Normalize to RMS Max | `-norm-rm:-12.0` |
| `-norm-ra:<value>` | Normalize to RMS Average | `-norm-ra:-20.0` |

### Other Options

| Option | Description |
|--------|-------------|
| `-j<N>` | Number of worker threads (default: all cores) |
| `-h` | Show help |

### Exit Codes

| Code | Meaning |
|------|---------|
| `0` | Success |
| `1` | Usage / argument / I/O error |
| `2` | Completed, but one or more files failed to load (placeholder values emitted) |
| `3` | CPU does not support the required AVX2/FMA instruction set |

## Library Usage

```cpp
#include "pbAudioStats.h"
#include <iostream>
using namespace pb_audio;

int main() {
    // Analyze a file
    AudioStats stats = analyze("input.wav");

    std::cout << "Integrated: " << stats.integrated_loudness << " LUFS\n";
    std::cout << "True Peak:  " << stats.true_peak << " dBTP\n";
    std::cout << "RMS Avg:    " << stats.rms_average << " dB\n";

    // Normalize to -23 LUFS
    Normalizer::normalize_and_save(
        "input.wav", "output.wav",
        Normalizer::Target::Integrated, -23.0);

    return 0;
}
```

Key API surface (`lib/include/pbAudioStats.h`):

- `analyze(path)` / `analyze(AudioData&, source_path)` — full analysis, returns `AudioStats`.
- `analyze(path, AnalysisOptions)` — selected DSP only; all-false options perform metadata-only parsing without decoding samples.
- `AudioReader::load(path)` / `open_stream(path)` — decode to memory or stream.
- `LoudnessMeter::measure / measure_with_rms / measure_stream` — loudness (+RMS, +true peak in the streaming path).
- `TruePeakMeter::measure(audio[, known_sample_peak])` — exact 4× oversampled true peak.
- `RMSMeter::measure` — SOX-compatible RMS.
- `Normalizer::calculate_gain / apply_gain / normalize_and_save`; `normalize_and_save_detailed` reports a structured error.

`AnalysisOptions::memory_budget_bytes` defaults to 64 MiB for the retained decoded Float32 payload plus fixed work buffers. Full analysis streams above that estimate, and MP3 full analysis always streams. Resource/performance methodology and sample measurements are in [BENCHMARKS.md](BENCHMARKS.md).

## Technical Specifications

### BS.1770-4 Compliance

| Item | Specification |
|------|---------------|
| K-weighting | High Shelf + High Pass biquad filters |
| Reference rate | 48 kHz (requantized for other rates, ≤0.0011 dB deviation) |
| Momentary | 400 ms block, 75% overlap |
| Short-term | 3000 ms block, 67% overlap |
| Absolute gate | -70 LUFS |
| Relative gate | -10 LU below ungated mean |
| LRA | 10–95 percentile, -20 LU relative gate (EBU Tech 3342) |
| True peak | 4× oversampling, 96-tap prototype / 24 taps per phase, Kaiser sinc (β=9.6), end-of-stream flushed |
| Channel weights | Mono/stereo/3.0 = 1.0; surrounds +1.5 dB; LFE excluded (4.0–7.1 layouts) |

Conforms to ITU-R **BS.1770-5** for channel-based mono/stereo/5.1 measurement (no algorithmic change from BS.1770-4 for these layouts).

### Supported Measurement Range and Policies

- Samples and targets must be finite. In-memory sample magnitudes above 10,000 (+80 dBFS) are rejected as outside the supported range.
- Loudness histograms cover through +100 LUFS. Audio shorter than four seconds is repeated by default for compatibility; library callers can select `ShortAudioPolicy::DoNotRepeat`, and `short_audio_repeated` reports the applied policy.
- The true-peak regression sweep covers 48 kHz signals from 1–20 kHz at four phases, with fades and end-of-file impulses. In that tested range the allowed worst under-read is 0.30 dB and allowed overshoot is 0.50 dB relative to the analytic/reference peak.
- A WAV channel mask assigns BS.1770 roles: LFE is excluded and surround channels receive +1.5 dB. Files without layout metadata use the documented channel-count fallback. WAV output preserves a representable mask when `keep_channel_roles` is true; AIFF output does not synthesize layout metadata.

### Output Precision and Metadata Policy

- Integer conversion rounds to nearest. TPDF dither is represented in the API but currently rejected as unsupported; no dither is applied implicitly.
- Normalization preserves sample rate, channel count, frame count, and—where representable in WAV—the channel mask. Other metadata chunks are not copied.
- RIFF/FORM payloads are padded to even byte boundaries. The pad byte is excluded from the chunk payload size, as required by the container format.

### SOX Compatibility

| Item | Specification |
|------|---------------|
| RMS method | Exponential moving average |
| Time constant | 50 ms (default) |
| Settling time | 5 × time_constant × sample_rate |

### Performance Architecture

- **True peak**: streaming block-FIR with AVX2/NEON FMA kernels and a rigorous L1-norm bound that prunes chunks which provably cannot beat the running peak (exact, never changes the result).
- **Loudness**: chunk-batched K-weighting with filter state kept in registers; block aggregation reduced to O(1) per sample via per-partition segment sums.
- **RMS / peak**: single shared sample-peak scan feeds both the loudness meter and the true-peak seed; SOX EMA batched per channel.
- **Denormals**: hardware FTZ/DAZ on x86 removes per-sample flush branches from the IIR hot path.
- **Batch**: workers claim the next file from a shared atomic index; normalization admission is gated by a RAM byte budget.
- **Build**: `/fp:contract` (MSVC) / `-ffp-contract=fast` (GCC/Clang) for FMA contraction (single rounding — *more* accurate), without `/fp:fast` so compensated summation stays intact.

## License and Releases

The project is proprietary. dr_mp3 and its minimp3 basis are distributed under their upstream permissive/public-domain terms; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Release artifacts must record version, source commit, build conditions, and SHA-256 checksums as described in [RELEASING.md](RELEASING.md).

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2026-01-06 | Initial release |
| 1.1.1 | 2026-05-02 | Renamed project to pbAudioStats |
| 1.2.0 | 2026-05-02 | Accuracy + performance overhaul (BS.1770-4 4× oversampling, SIMD bit-depth conversion) |
| 1.3.0 | 2026-06-12 | Extreme optimization: streaming block-FIR true peak (AVX2/NEON + L1 pruning + EOF flush), fully batched loudness/RMS, O(1) block aggregation, FTZ/DAZ, all-core scheduling, memory-budgeted normalization. Correctness fixes: 32-bit PCM full-scale overflow, exact true peak on the streaming path, `WAVE_FORMAT_EXTENSIBLE` support, UTF-8 / Japanese filenames, round-to-nearest PCM writes, AVX2 CPU guard, robust header parsing. |
| 1.4.0 | 2026-11-10 | Safety and portability release: atomic normalization with structured errors and explicit output precision, finite/resource validation, channel-layout-aware loudness, selectable analysis with decoded-memory budgets and MP3 streaming, improved true-peak/RMS edge accuracy, public regression/consumer tests, cross-platform release checks, installable CMake package, resource benchmark, and documented format/measurement limits. |
