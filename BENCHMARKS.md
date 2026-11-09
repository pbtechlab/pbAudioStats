# Resource benchmark

`pbAudioStats_memory_benchmark` is opt-in (`-DPB_BUILD_BENCHMARKS=ON`) and is intentionally excluded from correctness CTest runs. The manual `Resource benchmark` workflow generates deterministic 48 kHz stereo Float32 audio and prints elapsed time plus process peak RSS for:

- full analysis retaining decoded samples;
- full analysis with a zero-byte budget, forcing the streaming path;
- the current load/measure/write normalizer;
- a two-pass streaming design probe (measure on pass 1, decode/apply gain on pass 2).

The two-pass probe does not encode an output file, so its elapsed time is a lower bound for a production two-pass writer. It is used to quantify the memory/I/O tradeoff, not as a correctness substitute for normalization tests.

## Local reference run

2026-11-10, Windows x64, MinGW GCC 15.1 Release/LTO, 30 seconds stereo Float32 WAV (2,880,000 samples):

| Mode | Elapsed | Peak RSS |
|---|---:|---:|
| Analysis, decoded samples retained | 0.0498 s | 16,617,472 bytes |
| Analysis, forced streaming | 0.0392 s | 8,802,304 bytes |
| Current normalizer, including Int32 WAV write | 0.0357 s | 20,533,248 bytes |
| Two-pass stream design probe, no output encode | 0.0395 s | 8,749,056 bytes |

This run shows the intended tradeoff: streaming roughly halves peak RSS for this input, while a production two-pass normalizer would pay a second decode plus output encoding. Results are environment-specific; release manifests must report their own machine/compiler/input and must not reuse these values as universal performance claims.
