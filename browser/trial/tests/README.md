# ブラウザー版の検証ハーネス

同梱の `samples/manifest.json` は、固定した 6 個のグリッドと 6 枚の画像を検査対象とします。通常・診断の各モードで合計 14 個のシンボルを期待し、別途 PNG、JPEG、透過 PNG の 3 種類の画像展開を検査します。ファイルの SHA-256 と独立したバイト列・メタデータの期待値を照合し、必要なフィクスチャや実行モードが欠けた場合も失敗にします。

この説明のコマンドは、すべて SpecQR Reader のリポジトリのルートで実行します。ソースに含まれる `browser/README.md` のビルド手順に従い、Emscripten 6.0.12 で `browser/trial/assets/` に Wasm を生成してください。生成済み試験版だけを持っている場合は、対応するソースも別途用意してください。ネイティブ比較には C++17 コンパイラーを、実行には SDK の環境で選択した Node.js を使用します。通常の検査ではフィクスチャの再生成やエンコーダーのインストールは不要です。

## ネイティブ版と Wasm 版の比較

```sh
bash browser/build.sh native browser/build
make -j2 all
(
  . /absolute/path/to/emsdk/emsdk_env.sh
  node --version
  node browser/trial/tests/run-parity.mjs "$PWD/browser/build/native-bridge" \
    browser/build/evidence/wasm
  node browser/trial/tests/compare-cli.mjs "$PWD/build/specqr-reader" \
    browser/build/evidence/wasm/native-observed.json \
    browser/build/evidence/cli-comparison.json
  node browser/trial/tests/test-harness-controls.mjs \
    browser/build/evidence/harness-controls.json
)
```

`/absolute/path/to/emsdk` は使用する SDK の絶対パスに置き換えてください。括弧内だけで SDK の環境を読み込むため、恒久的な `PATH` の変更は不要です。

`run-parity.mjs` は、同じ C++ Reader と接続層のネイティブ版・Wasm 版を比較します。JSON は `runtime_ms`、`repeat`、`peak_rss_kib` 以外を比較し、`bytes_hex`、セグメント、ECI、FNC1、Structured Append、四隅、トレース、処理量制限の状態を含めます。同じ実装同士の比較だけでは独立した正しさの検証にならないため、固定フィクスチャの独立した期待値も照合します。

検査は、各フィクスチャの通常・診断結果、境界ケース、200 回の反復、明示的な入力・結果バッファーの解放を対象とします。Wasm の線形メモリーは縮小しないため、ウォームアップ後の容量が増えないことも確認します。これは、すべてのアロケーターを網羅したリークの証明ではありません。

比較先の `tests/native-expected.json` は、追跡対象の固定期待値として読み取るだけで、実行結果によって上書きしません。指定した出力先に `parity.json`、個別の結果、今回観測した `native-observed.json` を保存します。後者には対象ソースと実行バイナリーのハッシュも含めます。`compare-cli.mjs` はその観測結果を使い、既存 CLI の JSON 出力と接続層の一致を検査します。`test-harness-controls.mjs` は、欠落や重複などで検証範囲が減った場合に、ハーネスが拒否することを確認します。

## 実際のブラウザーでの検査

ビルド済みの試験画面と同じローカルサーバーを起動します。

```sh
python3 browser/trial/serve.py
```

同じコンピューターで `http://127.0.0.1:8765/tests/?label=chrome` または `http://127.0.0.1:8765/tests/?label=safari` を開き、「検証を実行」を押します。ポートを変えた場合は URL も合わせてください。ラベルは結果の識別用であり、ブラウザーを自動的に選択したり、その種類を証明したりするものではありません。

固定フィクスチャ、画像の展開、透明度、停止と再起動、Worker の障害からの復帰、共有レンダラーでの非信頼文字列の表示を検査します。QR の復号は、実際の Reader Wasm が Worker 内で行います。PNG、JPEG、透過 PNG はブラウザーの Canvas で RGB に変換し、そのバイト列と SHA-256 を結果に記録します。

完了後に「JSON をダウンロード」を押すと、ラベルに応じた `specqr-browser-chrome.json` などを保存できます。画面では省略している RGB の全バイト列も、保存する JSON には含まれます。画面上のテキストをコピーした結果では、次の RGB 比較に必要なデータが不足します。

通常の `serve.py` はアップロードや検査結果の保存 POST を受け付けません。結果はダウンロードして保存してください。既存の検証用サーバーが同一オリジンの `POST /__evidence/<label>` に対応する場合に限り、URL に `&collector=1` を明示すると自動保存を試みます。この収集機能は通常のローカル試用に必要ありません。

## ブラウザーが展開した RGB のネイティブ比較

ダウンロードした JSON の絶対パスを指定します。Node.js が利用できる SDK の環境で実行してください。

```sh
node browser/trial/tests/compare-browser-rgb.mjs "$PWD/browser/build/native-bridge" \
  /absolute/path/to/specqr-browser-chrome.json \
  browser/build/evidence/chrome-rgb
```

ブラウザーが展開した RGB の長さとハッシュを照合し、その同じバイト列をネイティブ Reader に渡します。圧縮画像を別のコーデックで展開し直す比較ではありません。各入力のバイト列、ネイティブ結果、`comparison.json` を指定した出力先に保存します。

ブラウザーの正確な製品名・バージョン、OS、ソースと Wasm の識別情報を、結果とともに記録してください。user agent だけでは詳細なバージョンを確定できない場合があります。Mac での検査は iPhone/iPad の動作確認ではありません。既存 Reader の画像認識限界も引き継ぎます。

## 固定入力からの派生画像の再生成

通常の検査には不要な、開発者向けの手順です。`source-inputs.json` と `raster-manifest.json` に記録したハッシュを照合し、グリッド・PNM から生バイト列と PNG、JPEG、透過 PNG を別のディレクトリへ生成します。JPEG の生成には macOS の `/usr/bin/sips` を使います。

```sh
python3 browser/trial/tests/prepare-fixtures.py browser/trial/samples \
  --output /absolute/path/to/new-reader-samples
```

出力先には、入力ディレクトリの外側にある、まだ存在しないディレクトリを指定してください。元の同梱ファイルは上書きしません。JPEG などの再生成物を差し替える場合は、生成環境とハッシュの変化を確認し、期待値の変更を別途レビューしてください。

`crash-worker.js` は Worker の実行時エラーからの復帰を検査するための専用ファイルです。通常の Reader 画面からは読み込みません。
