# pbAudioStats

ITU-R BS.1770-4 ラウドネス測定・トゥルーピーク検出・RMS 解析・オーディオ正規化を備えた、高性能な C++17 オーディオ解析ライブラリ／CLI。対応範囲は公開された決定的回帰試験で検証し、一部のトゥルーピーク／ラウドネス信号は独立ツールとも照合しています。

[English README](README.md)

## ハイライト

- **試験された測定挙動** — BS.1770-4 / EBU R 128 ラウドネス、EBU Tech 3342 LRA、SOX 互換 RMS を、公開回帰試験の明示的な数値許容差で検証。
- **極限のパフォーマンス** — AVX2 (x86-64) / NEON (ARM64) の手書きカーネル。ストリーミング型ブロック FIR トゥルーピークスキャナ、完全バッチ化された K 特性フィルタ、O(1)/サンプルのラウドネス集計。
- **堅牢な I/O** — ネイティブ WAV/AIFF 読み書き（PCM 8/16/24/32bit、IEEE float 32/64bit、`WAVE_FORMAT_EXTENSIBLE`、AIFF/AIFC（`sowt` 含む））、dr_mp3 による MP3、Windows での完全な UTF-8 / 日本語ファイル名対応。
- **並列バッチ処理** — 解析は全 CPU コアにスケール。正規化はメモリバジェットによる流量制御付き。

## 機能

- **BS.1770-4 ラウドネス測定**
  - 統合ラウドネス (LUFS)
  - 短時間ラウドネス最大値 (LUFS)
  - 瞬時ラウドネス最大値 (LUFS)
  - ラウドネスレンジ LRA (LU)
- **トゥルーピーク検出** — 4 倍オーバーサンプリング、96 タップの Kaiser 窓 sinc プロトタイプ（位相あたり24タップ）。メモリ／ストリーミング経路で同じスキャナを使用。
- **サンプルピーク** (dBFS)
- **RMS 解析** — SOX 互換の指数移動平均実装（最小／最大／平均）
- **オーディオ正規化** — Peak / True Peak / 統合・短時間・瞬時ラウドネス / RMS 最小・最大・平均 をターゲットに指定可能
- **マルチフォーマット対応** — WAV、AIFF/AIFC、MP3
- **クロスプラットフォーム** — Windows (x64, AVX2) / macOS (Apple Silicon・Intel)

## 対応形式

| コンテナ／コーデック | 読み取り | 正規化出力 |
|---|---|---|
| WAV PCM 8/16/24/32-bit | 対応 | 8/16/24/32-bit integer PCMを明示指定可能 |
| WAV IEEE float 32/64-bit | 対応 | Float32または明示したinteger PCM。64-bit入力を暗黙に縮小しない |
| WAV `WAVE_FORMAT_EXTENSIBLE` | channel maskを含め対応 | 表現可能なchannel layoutを保持して出力 |
| AIFF/AIFC PCM 8/16/24/32-bit（`sowt`読み取り） | 対応 | AIFF integer PCM 8/16/24/32-bit |
| AIFF/AIFC float／圧縮codec | 非対応 | 非対応。事前変換またはWAV Float32出力を使用 |
| MP3 | dr_mp3で対応 | 非対応。`.wav`、`.aif`、`.aiff`を指定 |

未知の出力拡張子は書き込み前に拒否します。

## 必要環境

- C++17 対応コンパイラ
- CMake 3.16 以上
- **Windows**: MSVC 2022、**AVX2 + FMA 対応 CPU**（Intel Haswell 2013 年以降 / AMD Excavator 2015 年以降）。非対応 CPU では起動時に検出してクラッシュせず診断メッセージを表示します。
- **macOS**: Clang/GCC（Apple Silicon NEON、または x86-64 AVX2）

## ビルド

### Windows

```batch
build_win.bat Release
```

または手動で:

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

ユニバーサルバイナリ:

```bash
cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
cmake --build build --config Release
```

## CLI の使い方

```bash
# 単一ファイルを解析（統合ラウドネス＋トゥルーピーク）
pbAudioStats -i -tp input.wav

# フォルダを全カラムで解析
pbAudioStats -f -fe -fea -sr -bt -ch -tm -du -i -s -m -l -pk -tp -rn -rm -ra ./audio_folder/

# UTF-8 CSV（BOM 付き、Excel 対応）に出力
pbAudioStats -i -tp -pk ./audio_folder/ results.csv

# -23 LUFS（統合）に正規化
pbAudioStats -norm-i:-23.0 input.wav output.wav

# フォルダ全体を -1 dBTP に正規化（入力ツリーを出力ディレクトリに再現）
pbAudioStats -norm-tp:-1.0 ./in_folder/ ./out_folder/

# ワーカースレッド数を指定
pbAudioStats -j8 -i ./audio_folder/
```

解析オプションも `-norm-*` も指定しない場合は全カラムを出力します。出力パスが `.csv` で終わる場合は、標準出力ではなく UTF-8 CSV（BOM 付き）に書き出します。

### 解析オプション

| オプション | 説明 | 単位 |
|--------|-------------|------|
| `-f` | ファイル名（拡張子なし） | - |
| `-fe` | ファイル名（拡張子あり） | - |
| `-fea` | フルパス | - |
| `-sr` | サンプルレート | Hz |
| `-bt` | ビット深度 | bits |
| `-ch` | チャンネル数 | - |
| `-tm` | 総時間 | HH:MM:SS.mmm |
| `-du` | 長さ | 秒 |
| `-i` | 統合ラウドネス | LUFS |
| `-s` | 短時間ラウドネス最大値 | LUFS |
| `-m` | 瞬時ラウドネス最大値 | LUFS |
| `-l` | ラウドネスレンジ (LRA) | LU |
| `-pk` | サンプルピーク | dBFS |
| `-tp` | トゥルーピーク | dBTP |
| `-rn` | RMS 最小 | dB |
| `-rm` | RMS 最大 | dB |
| `-ra` | RMS 平均 | dB |

### 正規化オプション

解析オプションとは排他です。必要なゲインを計算し、同一ディレクトリの一時ファイルを介して原子的に書き出します。クリップするターゲット、非有限値、無音に対する到達不能ターゲットは、出力先を置換せず拒否します。**MP3 出力は非対応** — MP3 を正規化する場合は `.wav` または `.aiff` の出力パスを指定してください。

| オプション | 説明 | 例 |
|--------|-------------|---------|
| `-norm-pk:<値>` | Peak に正規化 | `-norm-pk:-1.0` |
| `-norm-tp:<値>` | True Peak に正規化 | `-norm-tp:-1.0` |
| `-norm-i:<値>` | 統合ラウドネスに正規化 | `-norm-i:-23.0` |
| `-norm-s:<値>` | 短時間最大に正規化 | `-norm-s:-18.0` |
| `-norm-m:<値>` | 瞬時最大に正規化 | `-norm-m:-18.0` |
| `-norm-rn:<値>` | RMS 最小に正規化 | `-norm-rn:-12.0` |
| `-norm-rm:<値>` | RMS 最大に正規化 | `-norm-rm:-12.0` |
| `-norm-ra:<値>` | RMS 平均に正規化 | `-norm-ra:-20.0` |

### その他のオプション

| オプション | 説明 |
|--------|-------------|
| `-j<N>` | ワーカースレッド数（既定: 全コア） |
| `-h` | ヘルプ表示 |

### 終了コード

| コード | 意味 |
|------|---------|
| `0` | 成功 |
| `1` | 使い方・引数・I/O エラー |
| `2` | 完了したが一部ファイルの読み込みに失敗（プレースホルダ値を出力） |
| `3` | CPU が必要な AVX2/FMA 命令セットに非対応 |

## ライブラリの使い方

```cpp
#include "pbAudioStats.h"
#include <iostream>
using namespace pb_audio;

int main() {
    // ファイルを解析
    AudioStats stats = analyze("input.wav");

    std::cout << "Integrated: " << stats.integrated_loudness << " LUFS\n";
    std::cout << "True Peak:  " << stats.true_peak << " dBTP\n";
    std::cout << "RMS Avg:    " << stats.rms_average << " dB\n";

    // -23 LUFS に正規化
    Normalizer::normalize_and_save(
        "input.wav", "output.wav",
        Normalizer::Target::Integrated, -23.0);

    return 0;
}
```

主な API（`lib/include/pbAudioStats.h`）:

- `analyze(path)` / `analyze(AudioData&, source_path)` — フル解析。`AudioStats` を返す。
- `analyze(path, AnalysisOptions)` — 選択したDSPのみ実行。全falseならサンプルをdecodeせずmetadataのみ解析。
- `AudioReader::load(path)` / `open_stream(path)` — メモリ展開／ストリーム。
- `LoudnessMeter::measure / measure_with_rms / measure_stream` — ラウドネス（+RMS、ストリーミング経路では +トゥルーピーク）。
- `TruePeakMeter::measure(audio[, known_sample_peak])` — 正確な 4 倍オーバーサンプリングトゥルーピーク。
- `RMSMeter::measure` — SOX 互換 RMS。
- `Normalizer::calculate_gain / apply_gain / normalize_and_save`。`normalize_and_save_detailed`は構造化エラーを返す。

`AnalysisOptions::memory_budget_bytes`の既定値は64 MiBで、保持するdecode済みFloat32 payloadと固定work bufferを基準にします。Full analysisは見積もり超過時にstreamへ切り替わり、MP3のfull analysisは常にstreamです。測定手順と参考値は[BENCHMARKS.md](BENCHMARKS.md)を参照してください。

## 技術仕様

### BS.1770-4 準拠

| 項目 | 仕様 |
|------|---------------|
| K 特性 | ハイシェルフ + ハイパス biquad フィルタ |
| 基準レート | 48 kHz（他レートは再量子化、偏差 ≤0.0011 dB） |
| 瞬時 | 400 ms ブロック、75% オーバーラップ |
| 短時間 | 3000 ms ブロック、67% オーバーラップ |
| 絶対ゲート | -70 LUFS |
| 相対ゲート | アンゲート平均から -10 LU |
| LRA | 10〜95 パーセンタイル、-20 LU 相対ゲート（EBU Tech 3342） |
| トゥルーピーク | 4倍、96タップprototype／位相あたり24タップ、Kaiser sinc（β=9.6）、終端flush |
| チャンネル重み | モノ/ステレオ/3.0 = 1.0、サラウンド +1.5 dB、LFE 除外（4.0〜7.1 レイアウト） |

チャンネルベースのモノ/ステレオ/5.1 測定について ITU-R **BS.1770-5** に準拠（これらのレイアウトでは BS.1770-4 からアルゴリズム変更なし）。

### 対応測定範囲とポリシー

- sampleとtargetはfinite値必須。memory上のsample絶対値が10,000（+80 dBFS）を超える入力は対応範囲外として拒否します。
- loudness histogramは+100 LUFSまで対応。4秒未満は互換性のため既定で反復し、libraryでは`ShortAudioPolicy::DoNotRepeat`を選べます。`short_audio_repeated`で適用結果を確認できます。
- True Peak回帰試験は48 kHz、1～20 kHz、4位相、fade、EOF impulseを対象とし、解析波形／独立参照に対する許容差は最悪過小評価0.30 dB、過大評価0.50 dBです。
- WAV channel maskからBS.1770 roleを割り当て、LFEは除外、surroundは+1.5 dBです。layout metadataなしはchannel数fallbackを用います。WAV出力は`keep_channel_roles`がtrueなら表現可能なmaskを保持し、AIFF出力ではlayout metadataを合成しません。

### 出力精度とmetadata方針

- integer変換は最近傍丸めです。TPDF ditherはAPI上に予約されていますが現在はunsupportedとして拒否し、暗黙には適用しません。
- 正規化はsample rate、channel数、frame数、WAVで表現可能なchannel maskを保持します。それ以外のmetadata chunkはコピーしません。
- RIFF／FORM payloadは偶数byte境界へpadし、pad byteはchunk payload sizeに含めません。

### SOX 互換

| 項目 | 仕様 |
|------|---------------|
| RMS 方式 | 指数移動平均 |
| 時定数 | 50 ms（既定） |
| 整定時間 | 5 × 時定数 × サンプルレート |

### パフォーマンス設計

- **トゥルーピーク**: AVX2/NEON FMA カーネルによるストリーミング型ブロック FIR。実行中のピークを理論上超え得ないチャンクを L1 ノルム上界で厳密に枝刈り（結果は厳密に不変）。
- **ラウドネス**: フィルタ状態をレジスタに常駐させたチャンクバッチ K 特性処理。ブロック集計はパーティション別セグメント和で O(1)/サンプルに削減。
- **RMS / ピーク**: 共有の単一サンプルピークスキャンがラウドネス計とトゥルーピークのシードを兼用。SOX EMA はチャンネル別にバッチ化。
- **デノーマル**: x86 ではハードウェア FTZ/DAZ により IIR ホットパスからサンプル毎のフラッシュ分岐を除去。
- **バッチ**: workerが共有atomic indexから次のファイルを取得。正規化はRAM byte budgetでadmissionを制御。
- **ビルド**: `/fp:contract`（MSVC）/ `-ffp-contract=fast`（GCC/Clang）で FMA 縮約（丸め 1 回 = *より高精度*）。補償加算を壊さないよう `/fp:fast` は不使用。

## ライセンスとリリース

本プロジェクトはProprietaryです。dr_mp3およびそのminimp3由来部分にはupstreamのpermissive／public-domain条件が適用されます。詳細は[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)を参照してください。

release artifactにはversion、source commit、build条件、SHA-256 checksumを[RELEASING.md](RELEASING.md)の手順で添付します。

## バージョン履歴

| バージョン | 日付 | 変更点 |
|---------|------|---------|
| 1.0.0 | 2026-01-06 | 初版 |
| 1.1.1 | 2026-05-02 | プロジェクト名を pbAudioStats に変更 |
| 1.2.0 | 2026-05-02 | 精度＋パフォーマンス刷新（BS.1770-4 4倍オーバーサンプリング、SIMD ビット深度変換） |
| 1.3.0 | 2026-06-12 | 極限最適化: ストリーミング型ブロック FIR トゥルーピーク（AVX2/NEON + L1 枝刈り + 終端フラッシュ）、完全バッチ化ラウドネス/RMS、O(1) ブロック集計、FTZ/DAZ、全コアスケジューリング、メモリバジェット正規化。正確性修正: 32bit PCM フルスケールオーバーフロー、ストリーミング経路の正確なトゥルーピーク、`WAVE_FORMAT_EXTENSIBLE` 対応、UTF-8 / 日本語ファイル名、PCM 書き込みの四捨五入化、AVX2 CPU ガード、堅牢なヘッダ解析。 |
| 1.4.0 | 2026-11-10 | 安全性・移植性改善: 原子的な正規化と構造化エラー／明示的な出力精度、finite値・resource検証、channel layout対応loudness、選択解析とdecode後memory budget／MP3 streaming、True Peak・RMS境界精度改善、公開回帰・consumer test、cross-platform release check、install可能なCMake package、resource benchmark、形式・測定範囲の明文化。 |
