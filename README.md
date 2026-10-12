# SpecQR Reader 0.1.1

SpecQR Reader is a dependency-free C++17 reader for QR Code Model 2. It decodes caller-sampled module grids and searches grayscale or RGB images for planar symbols. It returns exact payload bytes and raw segment/control metadata, including ECI, FNC1 and Structured Append information.

English entry points: [build commands](#build-and-test), [API contract and examples](docs/api.md), [limitations](docs/limitations.md) and [validation evidence](docs/validation.md). Detailed documentation is primarily in Japanese.

SpecQR Reader は、QR Code Model 2 を対象とする実験段階の Reader です。新規に作成したコードと文書を MIT ライセンスで公開しています。入力サイズと処理量に上限を設け、独立した実装によるフィクスチャ確認とネイティブ環境での移植性検証を行っています。ただし、QR 仕様全体への適合、形式的な正しさ、あらゆる写真からの確実な読み取りを保証するものではありません。

公開済みの基準コミットは [`c7dfc6918d621d45830701b62c7a75ccd2ba965f`](https://github.com/SpecQR/SpecQR-Reader/commit/c7dfc6918d621d45830701b62c7a75ccd2ba965f) です。この基準ソースでは、[GitHub Actions の実行](https://github.com/SpecQR/SpecQR-Reader/actions/runs/38248190279) と公開リポジトリからの新規クローン検証が成功しています。今回の日本語文書変更に対する検証とは区別し、詳しい範囲を [検証記録](docs/validation.md) に記載しています。

ライブラリが使用するのは C++17 標準ライブラリだけです。C++17 を選ぶことで、インタープリターや画像コーデックに依存せず、画像処理の中核を移植して一般的なネイティブアプリケーションへ組み込めます。一方、バッファーの寿命管理とメモリー確保失敗への対処は呼び出し側の責任です。任意のコマンドラインアダプターは PNM 画像とテキスト形式のグリッドを読み取ります。PNG/JPEG の読み込み、カメラ接続、文字列変換、Structured Append の再結合は、アプリケーション側のアダプターで行います。Reed–Solomon 訂正は Reader の内部構成要素であり、独立した ECC API ではありません。

文書の基本言語は日本語です。英語の短い概要、正式名称、API 識別子、コード例を維持し、空白の細則はリンゴとデスクに合わせます。[言語・表記方針](docs/project-language.md) を参照してください。

<a id="build-and-test"></a>
## ビルドとテスト

C++17 コンパイラー、Make、Python 3 がある環境では、次を実行します。

```sh
make -j2 all
make test
./build/consumer fixtures/grids/segno-v01-H-m0.grid
./build/specqr-reader grid fixtures/grids/segno-v01-H-m0.grid
```

`make test-core` は Python を使わず、独立した C++ テストを実行します。`make test-gate` は Python 標準ライブラリだけを使い、評価ハーネスの対照テストを実行します。`make test-writing` は表記チェッカーのテストと文書の表記検査を実行します。`make test` は、この 3 種類を実行します。同梱フィクスチャのデータは固定されており、これらのテストで参照データをダウンロードしたり再生成したりすることはありません。任意の `make test-reference` には、別途保管している過去の完全なコーパスと参照アダプターが必要です。このソースパッケージには含まれていません。

CMake 3.16 以降を使う場合は、次を実行します。

```sh
cmake -S . -B out -DCMAKE_BUILD_TYPE=Release -DSPECQR_BUILD_TESTS=ON
cmake --build out --config Release --parallel
(cd out && ctest -C Release --output-on-failure)
cmake --install out --config Release --prefix "$PWD/install"
```

CMake ではテストを明示的に有効にします。`SPECQR_BUILD_TOOLS=OFF` と `SPECQR_BUILD_EXAMPLES=OFF` を指定すると、それぞれ任意のコマンドラインツールとサンプルを無効にできます。インストールされるパッケージ名は `SpecQRReader`、インポートするターゲット名は `SpecQR::specqr_reader` です。

```cmake
find_package(SpecQRReader 0.1 REQUIRED CONFIG)
add_executable(my_reader main.cpp)
target_link_libraries(my_reader PRIVATE SpecQR::specqr_reader)
```

利用側プロジェクトの構成時に、`CMAKE_PREFIX_PATH` をインストール先のプレフィックスへ設定します。公開ヘッダーは `<specqr/reader.hpp>` です。

複数のプラットフォームで共通の CI ドライバーは、CMake テストの構成とビルド、評価ハーネスの対照テスト、ライブラリのインストールを行います。さらに、インストール先を移動し、ソースツリーから独立した利用側プログラムをビルドします。

```sh
python3 tests/run_ci.py --work-dir /absolute/path/to/new-ci-work
```

Python の実行ファイル名は、使用するプラットフォームに合わせてください。作業ディレクトリには、まだ存在しない、ソースツリーの外側のパスを指定します。CI の設定だけでは実行の証拠になりません。実際に確認したコミットと検証範囲は [検証記録](docs/validation.md) を参照してください。

対応するコンパイラーとランタイムでは、`make sanitize` により、有限個の中核テストを AddressSanitizer と UndefinedBehaviorSanitizer の下で実行します。リーク検査は Darwin 以外では既定で有効、Darwin では報告済みの Apple ランタイムの制約により既定で無効です。必要に応じて `SANITIZER_DETECT_LEAKS=0` または `1` で上書きできます。サニタイザーを使用できるかどうかは、選択したツールチェーンに依存します。

## Reader の使い方

`decode_grid()` は、明モジュールを `0`、暗モジュールを `1` とする、モジュール当たり 1 バイトの入力を受け取ります。クワイエットゾーンは含めません。QR Code Model 2 の一辺は 21～177 モジュールで、4 モジュール刻みです。`read_image()` は、行が上から下へ並ぶ 8 ビットのグレースケール画像、または RGB 成分が交互に並ぶ画像バッファーを受け取り、画像内の各位置にあるシンボルの一覧を返します。入力バッファーの所有権は呼び出し側にあり、呼び出し終了後にライブラリが保持することはありません。

ペイロードは Unicode 文字列ではなく、バイト列です。セグメント境界と ECI 割り当てを保持します。FNC1 の英数字モードにおける `%` のエスケープも、そのままのバイト列として残し、解釈をアプリケーションへ委ねます。[API の契約](docs/api.md) と [利用例](examples/consumer.cpp) を参照してください。

画像の結果は、`error`、`symbols`、`work_limit_reached` を一緒に確認してください。成功した結果でも探索が未完了の場合があり、メモリー確保に関連する `resource_limit` の結果に有効な部分結果のシンボルが残る場合もあります。成功したことも、処理量制限のフラグが偽であることも、全シンボルの検出を保証しません。これらの API は `noexcept` ではなく、メモリー確保に失敗すると `std::bad_alloc` が伝播する場合があります。

コマンドラインアダプターは、バイナリーデータを損なわずに表現する JSON を出力します。

```sh
./build/specqr-reader image input.pgm --trace
./build/specqr-reader image input.ppm --invert --repeat 3
```

画像入力は、最大サンプル値が 255 のバイナリー P5/P6 PNM に限ります。アダプターは `--trace`、`--invert`、`--strict-padding`、`--repeat N` に対応しています。グリッドファイルは、先頭行に一辺の長さを十進数で記し、その後に指定した行数ちょうどの `0`/`1` の文字列を並べます。終了ステータス 0 は成功、2 は拒否、資源の枯渇、入力エラーのいずれかを表します。部分結果のシンボルと処理量フラグは、必ず JSON で確認してください。ペイロードは `bytes_hex` で表現し、読み取った URL やコマンドを実行することはありません。

## 対応範囲と検証根拠

ソースパッケージには、決定的な単体テスト、公開 API テスト、不正入力のスモークテスト、評価ハーネスの対照テストを含めています。より広い受け入れ検証では、独立したエンコーダーとデコーダー、および来歴と権利表示を付けて別途保管した写真を使用しました。同梱のテスト群は、その検証根拠のうち再現可能な一部分であり、完全な比較コーパスの代替ではありません。

画像の測定結果には、追加の合成陽性画像 80 件中、完全一致 73 件、出力なし 7 件、および以前の開発用写真 50 件中、完全一致 47 件、出力なし 3 件が含まれます。これらの有限の検査では、誤った出力も余分な出力もありませんでした。合成画像で成功した探索の一部は、処理量の上限に達していました。これらの件数から一般的な再現率や誤復号率は求められません。[制約と測定条件](docs/limitations.md) を参照してください。

受け入れレビューでは、Mac と Windows の検証根拠を、実行時コード、ヘッダー、CLI の全 9 ファイルのハッシュと独立に照合しました。Windows の検証は、Windows ARM64 のエミュレーション下で動く x64 GCC ビルドを用いており、ネイティブの Windows ARM64 ABI は未検証です。以前にクラウド実行環境で発生したダウンロード失敗は、転送の失敗であり、ネイティブ環境でのデコーダー実行の失敗ではありません。受け入れレビューでアーカイブを確認した後は、未解決の受け入れ条件には数えていません。[検証記録](docs/validation.md) に範囲と検証元を記載しています。

[MIT ライセンス](LICENSE) は、既存の `Copyright (c) 2026 SpecQR contributors` の帰属表示を使用し、新規に作成したコードと文書に適用します。個別に適用される権利表示と生成データの来歴は、[第三者由来の情報](THIRD_PARTY.md) を参照してください。
