# pbAudioStats 改善監査・タスクリスト

作成日: 2026-11-09
対象: v1.3.0 / HEAD `75b5705`
監査モデル: `gpt-6-astra`（ultra）

## この文書の扱い

- 本文書は、改善候補を実行順に管理するための未着手タスクリストである。
- 監査時点では、ソース修正、コミット、プッシュを実施していない。
- 再現信号、補助プログラム、独立ビルドは一時領域だけに作成した。
- pbAudioStats の責務は、オフライン測定・正規化ライブラリ／CLIとして維持する。
- pbAudioCore の新規実装は本監査の対象外とする。
- 優先度は、P0＝正常入力でも原本を破損し得る問題、P1＝正確性・安全性・ビルド成立性、P2＝性能・利用性・配布改善を表す。

## 結論

最優先は正規化の安全性である。通常の有限なWAVと有限な目標値でも、RMS最小値への正規化によって無音区間がフルスケールへ変わるケースを再現した。CLIは出力先省略時に入力ファイルを直接上書きするため、原本破損につながる。

ほかにも、AIFF出力のサンプルレート半減、RMS境界条件、WAVE_FORMAT_EXTENSIBLEのチャンネル配置無視、非有限値の正常扱い、クリーンcheckoutでのCMake失敗を再現した。既存の精度検証はすべて合格したが、writer、正規化安全性、異常入力、公開API境界を十分に覆っていない。

## 検証結果の概要

| 検証 | 結果 |
|---|---|
| 既存Releaseの精度ハーネス | `ALL CHECKS PASSED` |
| Git管理ファイルだけでCMake configure | `tools/stage_bench.cpp` 不在で失敗 |
| 不足ファイルだけ補ったMSVC 19.44 Releaseビルド | 成功 |
| 新ビルドの精度ハーネス | `ALL CHECKS PASSED` |
| AIFF正規化 | 48 kHz／10秒が24 kHz／20秒になった |
| 目標値 `nan`／`inf`／`1e308` | 終了コード0で破損出力を生成 |
| 4ch WAV_EXTENSIBLE、mask `0xF` | FCとLFEのラウドネス重み誤りを再現 |
| `add_subdirectory` 組み込み | include path不正でビルド失敗 |
| 10分192 kbps MP3 | 入力13.734 MiBに対しピークWorking Set約446.4 MiB |

## 全体チェックリスト

- [x] AUD-01 P0: RMS正規化による音声破損を防止する
- [x] AUD-02 P1: RMS整定時間ちょうどの境界条件を修正する
- [x] AUD-03 P1: AIFFライターのサンプルレート半減を修正する
- [x] AUD-04 P1: 非有限値と異常な公開API入力を拒否する
- [x] AUD-05 P1: 正規化出力を安全に確定し、失敗時に原本を保持する
- [x] AUD-06 P1: 実際のチャンネル配置に基づいて測定する
- [x] AUD-07 P1: Git checkoutだけで構成・ビルド可能にする
- [x] AUD-08 P1: 公開可能な回帰試験と手動release gateを整備する
- [x] AUD-09 P2: 要求された測定項目だけを計算する
- [x] AUD-10 P2: 圧縮サイズ依存のメモリ見積もりを改める
- [x] AUD-11 P2: 高ラウドネスと短時間音声の測定仕様を明確にする
- [x] AUD-12 P2: True Peakの高域・位相依存誤差を精査する
- [x] AUD-13 P2: CMake組み込みとインストール利用を成立させる
- [x] AUD-14 P2: 書き出し形式と精度保持方針を明示する
- [x] AUD-15 P2: README・配布物の保証範囲を実装と試験に合わせる

## P0・P1: 先に対応する項目

### AUD-01｜P0: RMS正規化による音声破損を防止する

- [x] 単独RMSと一括解析のRMS計算核を共通化する。
- [x] 無音、極小値、到達不能な目標の扱いを仕様化する。
- [x] `target_value`、`gain_db`、線形ゲインの有限性と表現可能範囲を検証する。
- [x] 失敗時は書き込み前に中止し、理由を返す。
- [x] 単独、一括、ストリーミング、正規化で値と判定を一致させる。

**再現:** 48 kHz、ステレオ、16 bit、60秒。先頭1秒を振幅0.5、残り59秒を無音としたWAV。通常解析は `RMS Min=-96.00 / Max=-6.02 / Avg=-23.80`。`-norm-rn:-20` は終了コード0だが、出力の先頭と59秒地点がともに `+32767`、出力平均RMSは約0 dBとなった。

**原因候補:** 単独RMSにデノーマル対策がなく極小値を返す。線形ゲインが`float`の無限大となり、`0 * inf`のNaNがAVX2 clampで最大値に変わる。

**対象:** `lib/src/pbRms.cpp:34`、`lib/src/pbLoudness.cpp:95`、`lib/src/pbNormalize.cpp:330`、`lib/src/pbNormalize.cpp:367`、`lib/src/pbNormalize.cpp:368`、`lib/include/pbSimd.h:593`、`cli/main.cpp:555`

**完了条件:** 再現入力の無音部分が非ゼロにならない。到達不能時は出力・原本を変更しない。全測定経路のRMS値と判定が一致する。

**依存:** AUD-02、AUD-04、AUD-05。**工数:** 中。**リスク:** RMS Minの極小値仕様変更による互換性。

### AUD-02｜P1: RMS整定時間ちょうどの境界条件を修正する

- [x] 未計測状態を明示的に追跡するか、整定時間以下をfallback対象にする。
- [x] 単独RMSと一括解析の二重実装を統一する。
- [x] `tc_samples-1 / tc_samples / tc_samples+1` の回帰試験を追加する。

**再現:** 48 kHz、モノラル、振幅0.5一定。

| フレーム数 | RMS Min | RMS Max | RMS Avg |
|---:|---:|---:|---:|
| 11999 | -6.02 | -6.02 | -6.02 |
| 12000 | -96.00 | -96.00 | -6.02 |
| 12001 | -6.05 | -6.05 | -6.02 |

12000フレームに `-norm-rm:-20` を実行すると、全サンプルが `+32767` となった。

**対象:** `lib/src/pbRms.cpp:83`、`lib/src/pbRms.cpp:97`、`lib/src/pbLoudness.cpp:757`

**完了条件:** 複数のサンプルレート、時定数、チャンネル数で境界前後を検証し、境界だけ約90 dB低下しない。解析と正規化の値が一致する。

**依存:** AUD-01。**工数:** 小。**リスク:** 低。SoXにも同境界の異常があるため、SoXの値を無条件に期待値としない。

### AUD-03｜P1: AIFFライターのサンプルレート半減を修正する

- [x] 80 bit拡張浮動小数点の明示的整数ビット、仮数、指数の対応を修正する。
- [x] 44100／48000／96000 Hzのwriter round-trip試験を追加する。
- [x] 内部Readerと独立ツールの両方で検証する。

**再現:** 48000 Hz／10秒のWAVを `-norm-pk:-1` でAIFF化すると、ffprobeとpbAudioStatsの双方が24000 Hz／20秒と判定する。現行フィールドは `400e5dc0000000000000`、48000 Hzの正しい値は `400ebb80000000000000`。

**対象:** `lib/src/pbNormalize.cpp:131`～`140`、`lib/src/pbNormalize.cpp:284`、`lib/src/pbAudioReader.cpp:87`～`100`

**完了条件:** WAV→AIFF、AIFF→AIFFでサンプルレート、フレーム数、再生時間を維持する。

**依存:** AUD-08。**工数:** 小。**リスク:** 低。

### AUD-04｜P1: 非有限値と異常な公開API入力を拒否する

- [x] 正規化目標、ゲイン、サンプル、`window_ms` の有限性と有効範囲を検証する。
- [x] `frames * channels` のoverflowと`AudioData::samples.size()`の整合性を検証する。
- [x] sample rate、channels、総フレーム数に資源上限を設ける。
- [x] 失敗理由を返せる結果型を設計する。
- [x] 入力検証をdecodeまたは既存ピーク走査へ融合し、追加走査を避ける。

**再現:** `-norm-pk:nan`、`-norm-pk:inf`、`-norm-pk:1e308` はすべて終了コード0で、出力先頭サンプルが `+32767`。float WAV内のNaNも成功扱いとなる。末尾NaNの32サンプル入力ではSample Peak=`-inf`、33サンプルでは0 dBとなり、SIMD幅で結果が変わる。

**対象:** `cli/main.cpp:221`～`225`、`lib/src/pbNormalize.cpp:361`～`368`、`lib/src/pbAudioReader.cpp:234`～`238`、`lib/include/pbSimd.h:34`～`69`、`lib/src/pbAnalyze.cpp:129`、`lib/src/pbRms.cpp:72`～`75`、`lib/include/pbAudioStats.h:26`～`31`

**完了条件:** NaN、±Inf、SIMD幅前後、chunk境界、不一致フレーム数、異常rate／channels／windowで成功扱い、出力変更、範囲外アクセスがない。

**依存:** AUD-01、AUD-08。**工数:** 中。**リスク:** 特殊値仕様とAPI互換性。

### AUD-05｜P1: 正規化出力を安全に確定し、失敗時に原本を保持する

- [x] 形式、サイズ、ゲインの事前検証をファイルopenより前に完了する。
- [x] 出力先と同じディレクトリの一時ファイルへ書き込む。
- [x] `flush`、`close`、ヘッダー整合性を確認後に置換する。
- [x] 失敗時は元ファイルを保持する。
- [x] サイズ上限、途中書き込み失敗、キャンセルの注入試験を追加する。

**現状:** 出力省略時は原本を直接上書きする。writerはサイズ上限検査より前に`ofstream`を開くため、検査失敗でも既存出力を切り詰め得る。最終flush／close成功も確認しない。

**対象:** `cli/main.cpp:555`、`lib/src/pbNormalize.cpp:195`、`lib/src/pbNormalize.cpp:205`、`lib/src/pbNormalize.cpp:252`、`lib/src/pbNormalize.cpp:261`、`lib/src/pbNormalize.cpp:242`～`244`、`lib/src/pbNormalize.cpp:296`～`298`

**完了条件:** すべての失敗注入試験で原本ハッシュが変わらない。正常時は一時ファイルが残らず、終了コードと保存状態が一致する。

**依存:** AUD-01、AUD-04。**工数:** 中。**リスク:** Windowsの置換・共有ロック条件。

### AUD-06｜P1: 実際のチャンネル配置に基づいて測定する

- [x] `AudioData`と`StreamInfo`にチャンネル配置情報を保持する。
- [x] チャンネル役割に基づいてBS.1770重みを決める。
- [x] maskなし入力の従来推定を明示的fallbackにする。
- [x] 保存時にもチャンネル配置を保持する。

**再現:** mask=`0xF`、FL／FR／FC／LFEの4ch WAV。FC単独はpbAudioStats=-17.8 LUFS、ffmpeg=-19.3 LUFS。LFE単独はpbAudioStats=-17.8 LUFS、ffmpeg=-70.0 LUFS。

**対象:** `lib/src/pbAudioReader.cpp:153`～`157`、`lib/src/pbLoudness.cpp:244`～`267`、`lib/include/pbAudioStats.h:26`、`lib/include/pbAudioStats.h:39`

**完了条件:** quad、3.1、5.1 side／back、7.1の各チャンネル単独信号で重みを検証する。LFEが寄与せず、memory／stream／保存後で配置が維持される。

**依存:** AUD-08、公開API変更。**工数:** 中。**リスク:** public struct互換性と配置不明ファイルの扱い。

### AUD-07｜P1: Git checkoutだけで構成・ビルド可能にする

- [x] `stage_bench`を既定OFFのビルドオプション下でだけ登録するか、公開可能なソースを管理対象にする。
- [x] クリーンclone／Git archiveからのconfigureをrelease前検証に追加する。
- [x] ベンチON時に資産不足なら明確な診断を出す。

**再現:** Git管理ファイルだけを複製してCMake configureすると、`tools/stage_bench.cpp`不在で終了コード1となる。`EXCLUDE_FROM_ALL`はconfigure時のソース存在要件をなくさない。

**対象:** `CMakeLists.txt:147`、`.gitignore:50`～`51`

**完了条件:** クリーンcheckoutからベンチOFFでconfigure／Release buildが成功する。

**依存:** なし。**工数:** 小。**リスク:** 低。

### AUD-08｜P1: 公開可能な回帰試験と手動release gateを整備する

- [x] 小さく決定的な信号をテスト時に生成する公開スイートを作る。
- [x] ライブラリの未丸め値を直接検証する。
- [x] writer round-trip、異常入力、正規化、memory／stream一致を検証する。
- [x] 必要ケース欠落や参照ツール失敗を成功扱いにしない。
- [x] Windows MSVC、macOS ARM64、対象x86環境で実行する。
- [x] 大容量ベンチは正確性テストと別target／手動jobにする。

**現状:** 検証ハーネスとデータはローカル限定で、Git管理されたテストとrelease gateがない。既存ハーネスはAUD-01～07の不具合を検出できなかった。READMEの“bit-stable”に対し、golden比較はCLI表示値に最大0.0151の許容差を持つ。

**対象:** `.gitignore:34`～`35`、`.gitignore:50`～`51`、`CMakeLists.txt`、`README.md:9`

**完了条件:** `ctest --test-dir <build> -C Release --output-on-failure` がクリーン環境で動き、AUD-01～07の再現ケースが必須テストになる。

**依存:** AUD-07。**工数:** 中～大。**リスク:** プラットフォーム間許容差とgolden更新ルール。

## P2: 性能・設計・配布改善

### AUD-09｜P2: 要求された測定項目だけを計算する

- [x] `AnalysisOptions`または測定フラグを公開APIへ追加する。
- [x] metadata-only、peak-only、loudness、RMS、True Peakを依存関係に応じて実行する。
- [x] 現在の全解析APIを互換wrapperとして維持する。
- [x] 選択測定と全測定の値を照合する回帰試験を追加する。

**現状:** `-sr`だけを指定しても、ピーク、True Peak、ラウドネス、RMSをすべて計算する。

**参考測定:** 新Release、10分24 bit WAV、各4回の中央値は `-sr`のみ1178.3 ms、`-i`のみ1296.55 ms、全列1166.05 ms。共有環境の参考値だが、項目選択が計算量へ反映されていないことは実装からも明確である。

**対象:** `cli/main.cpp:513`、`lib/src/pbAnalyze.cpp:69`～`93`、`lib/include/pbAudioStats.h:208`～`212`

**完了条件:** metadata-onlyが音声全体をdecode・DSP走査しない。全項目解析の性能を悪化させない。

**依存:** AUD-08。**工数:** 中。**リスク:** 共通走査の分割による全測定性能低下。

### AUD-10｜P2: 圧縮サイズ依存のメモリ見積もりを改める

- [x] 展開後フレーム数、channels、`sizeof(float)`、作業バッファ、再確保ピークから見積もる。
- [x] MP3の原則stream化または展開後サイズによる経路選択を比較する。
- [x] 解析側にもメモリ予算を導入する。
- [x] 正規化の二段階stream処理と現方式の性能を比較する。

**再現:** 10分192 kbps MP3は入力13.734 MiBだが、新ReleaseのピークWorking Setは約446.4 MiB。32 MiBの切替判定は圧縮ファイルサイズを使用しており、展開メモリ上限にならない。

**対象:** `lib/src/pbAnalyze.cpp:147`～`148`、`lib/src/pbAudioReader.cpp:734`～`748`、`cli/main.cpp:494`～`497`、`cli/main.cpp:622`～`624`

**完了条件:** MP3、8 bit、多チャンネルを並列処理してピークRSSと設定予算の関係を確認し、スループットと最大メモリを併記する。

**依存:** AUD-08、AUD-09。**工数:** 中。**リスク:** 再読込I/Oとスループットのトレードオフ。

### AUD-11｜P2: 高ラウドネスと短時間音声の測定仕様を明確にする

- [x] 対応入力範囲に足るヒストグラム設計へ変更するか、範囲外を明示的に失敗させる。
- [x] 短時間音声の反復を互換モードとして明示する。
- [x] 非反復測定との選択と結果ステータスを検討する。
- [x] 上限付近と0.4／3／4秒前後の試験を追加する。

**再現:** 48 kHz、997 Hz、5秒、float32ステレオ。振幅2.0でI=5.0／S=M=6.0、ffmpeg I=6.0。振幅10.0でI=-70.0／S=M=20.0となり、静かな信号ではないのに無音相当になる。

**対象:** `lib/src/pbLoudness.cpp:55`、`lib/src/pbLoudness.cpp:289`～`294`、`lib/src/pbLoudness.cpp:334`～`343`、`lib/src/pbLoudness.cpp:844`～`868`

**完了条件:** gain変更に対するLUFSの整合性を保ち、対応範囲外なら明確に失敗する。短時間ポリシーをREADMEとAPIへ記載する。

**依存:** AUD-04、AUD-08。**工数:** 中。**リスク:** golden値と既存の短時間挙動。

### AUD-12｜P2・要精査: True Peakの高域・位相依存誤差を精査する

- [x] 周波数×位相sweep、fade、EOF境界の試験を追加する。
- [x] 独立補間器と理論波形に対する最悪過小評価を測る。
- [x] 必要ならtap数、窓、passband設計を比較する。
- [x] 正規化後も独立測定器で検証する。

**観測:** 48 kHz、mono float32、16 kHz正弦波、位相0、1秒、先頭末尾100 samples線形fade。現CLIは-0.38 dBTP、現ソース直接ビルドは-0.377794 dBTP。ffmpegとSoXの192 kHz補間後ピークは0.0 dB。既存の対ffmpeg許容0.3 dBを超える。

**対象:** `lib/src/pbTruePeakScanner.h:63`～`89`

**完了条件:** 対応周波数域と許容誤差を明示し、その範囲で満たす。

**依存:** AUD-08。**工数:** 中～大。**リスク:** 速度と既存goldenへの影響。現時点では規格不適合と断定しない。

### AUD-13｜P2: CMake組み込みとインストール利用を成立させる

- [x] `CMAKE_SOURCE_DIR`をtarget／サブプロジェクト安全なパスへ置き換える。
- [x] `BUILD_INTERFACE`／`INSTALL_INTERFACE`を設定する。
- [x] `target_compile_features(... cxx_std_17)`を使用する。
- [x] extを必要なtargetだけのPRIVATE依存にする。
- [x] exported targetとCMake config／versionファイルを提供する。
- [x] CLI、テスト、benchmarkの構築を選択可能にする。

**再現:** consumerから `add_subdirectory` するとconfigureは成功するが、ライブラリbuildは全cppで `pbAudioStats.h` 不在のC1083となる。

**対象:** `CMakeLists.txt:61`、`CMakeLists.txt:83`～`86`、`CMakeLists.txt:120`～`122`、`CMakeLists.txt:153`～`164`

**完了条件:** `add_subdirectory` consumerと、別prefixへinstall後の`find_package` consumerをrelease前検証でコンパイルする。親プロジェクトのruntime・出力先を変更しない。

**依存:** AUD-07、AUD-08。**工数:** 中。**リスク:** MSVC `/MT`方針と利用者側runtimeの整合性。

### AUD-14｜P2: 書き出し形式と精度保持方針を明示する

- [x] 出力形式、PCM／float、bit depthを明示的オプションにする。
- [x] 未知拡張子を拒否する。
- [x] RIFF／FORMのpaddingとchunk sizeを正しく扱う。
- [x] 量子化、dither、メタデータ、channel mask保持方針を文書化する。
- [x] 奇数・偶数フレームと全bit depthのround-trip試験を追加する。

**再現:** 1フレーム、mono、8 bit WAVを正規化するとWAVは45 bytes、AIFFは55 bytesでpaddingされない。`.xyz`を出力先にしても成功し、WAVヘッダーを書き出す。64 bit入力は16 bitへfallbackする。

**対象:** `lib/src/pbNormalize.cpp:148`～`151`、`lib/src/pbNormalize.cpp:207`、`lib/src/pbNormalize.cpp:222`、`lib/src/pbNormalize.cpp:242`、`lib/src/pbNormalize.cpp:265`、`lib/src/pbNormalize.cpp:296`、`lib/src/pbNormalize.cpp:370`～`375`

**完了条件:** 独立decoderでもヘッダーとサンプル値が一致し、未知拡張子は書き込み前に失敗する。

**依存:** AUD-03、AUD-05、AUD-06、AUD-08。**工数:** 中。**リスク:** 既存の暗黙変換との互換性。

### AUD-15｜P2: README・配布物の保証範囲を実装と試験に合わせる

- [x] 読み取り形式と書き出し形式を分けた対応表を作る。
- [x] CPU、チャンネル配置、短時間測定、非有限値、正規化の制約を記載する。
- [x] “bit-stable”などの精度表現を公開試験の保証範囲に合わせる。
- [x] 実際の並列実装に合わせて“work-stealing”表現を修正する。
- [x] ビルドスクリプトの完了表示パスを実出力先に合わせる。
- [x] 利用条件と第三者コンポーネント情報を整理する。
- [x] リリースへversion、commit、ビルド条件、checksumを添える。

**対象:** `README.md:9`、`README.md:11`、`README.md:206`、`README.md:211`、`README_ja.md`、`build_win.bat`、`cli/main.cpp:507`

**完了条件:** READMEの全ビルド例、CLI例、library例をクリーン環境で検証する。未検証の構成を動作確認済みと書かない。

**依存:** AUD-07、AUD-08、AUD-13、AUD-14。**工数:** 小～中。**リスク:** 低。

## 推奨実施順序

1. AUD-01～05で、正常入力の破損と原本保護を解決する。
2. AUD-07～08で、クリーンビルドと回帰試験を恒久化する。
3. AUD-06、AUD-11～12で、測定の対応範囲と精度を固める。
4. AUD-09～10で、要求項目に応じた計算量と最大メモリを改善する。
5. AUD-13～15で、ライブラリ利用、書き出し仕様、配布資料を整える。

追加機能やpbAudioCoreへの拡張より先に、現在の測定・正規化機能を安全かつ再現可能な状態へする価値が高い。
