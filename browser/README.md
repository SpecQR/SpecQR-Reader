# SpecQR Reader のブラウザー接続層

このディレクトリは、C++17 Reader をブラウザーで使用するための接続層です。`src/` の 5 個の `.cpp` と `bridge.cpp` を Emscripten でコンパイルし、実際の `decode_grid()` または `read_image()` を呼び出します。QR の解析を別のデコーダーやサーバーへ置き換えるものではありません。

`trial/` には画面、Worker、画像入力処理、固定サンプル、検査コード、ライセンスと起動説明を含めています。ソースチェックアウトには、生成物 `trial/assets/specqr.mjs` と `trial/assets/specqr.wasm` を含めていません。まず次の手順でビルドしてください。すでにこの 2 ファイルを含む試験版フォルダーがある場合は、[起動と利用の説明](trial/はじめに.md) から始められます。

## Emscripten 6.0.12 の準備

Wasm のビルドには **Emscripten 6.0.12** を使用します。[Emscripten 公式の導入手順](https://emscripten.org/docs/getting_started/downloads.html) に従って、公式の emsdk を任意の作業用ディレクトリに用意してください。emsdk のディレクトリで、バージョンを固定してインストール・選択します。

```sh
./emsdk install 6.0.12
./emsdk activate 6.0.12
```

以下の例は macOS の Bash または zsh を想定しています。`/absolute/path/to/emsdk` は用意した SDK の絶対パスに置き換えます。環境を読み込む処理は括弧内のサブシェルで実行するため、シェルの起動ファイルや永続的な `PATH` の変更は不要です。

## 試験版のビルドと起動

SpecQR Reader のリポジトリのルートから実行します。

```sh
(
  . /absolute/path/to/emsdk/emsdk_env.sh
  em++ --version
  EMXX="$(command -v em++)" bash browser/build.sh wasm browser/trial/assets
)
python3 browser/trial/serve.py
```

`em++ --version` が 6.0.12 であることを確認してください。`build.sh` も [固定バージョン](emscripten-version.txt) と照合し、異なる版を拒否します。SDK のコミットと検証に使用した Node.js などは [ツールチェーン情報](toolchain.json) に記録しています。出力は `browser/trial/assets/specqr.mjs` と `browser/trial/assets/specqr.wasm` です。`build.sh` の出力先を省略すると `browser/build/` になるため、試験画面用には上記のように出力先を明示します。ES module の default export は `createSpecQRModule` ファクトリーです。

サーバーに必要なのは Python 3 の標準ライブラリだけです。表示された `http://127.0.0.1:8765/` を同じコンピューターのブラウザーで開いてください。使用中のポートは `python3 browser/trial/serve.py --port 8766` のように変更できます。サーバーは IPv4 のループバックにのみバインドし、終了は Control+C です。`file://` で HTML を直接開く方法には対応しません。

読み取りは Web Worker 内で実行します。停止すると Worker と Wasm インスタンスを破棄し、再実行時に新しく作り直します。Reader のデコード自体は、ファイルシステム、ネットワーク、追加のスレッドを使用しません。ブラウザーがローカル HTTP サーバーから画面、サンプル、Wasm を読み込む通信は必要です。

PNG/JPEG の展開にはブラウザーの画像機能と Canvas を使用し、白背景に合成した RGB バイト列を `read_image()` に渡します。この画像展開処理と QR の解析は別です。色管理と画像の向きはブラウザーの処理に従います。アプリケーションでの自動縮小は行いません。制限と操作は [試験版の説明](trial/はじめに.md)、同梱通知文は [ライセンスと来歴](trial/THIRD_PARTY-Reader.md) を参照してください。

## 例外処理とメモリー

`-fexceptions` と `-sDISABLE_EXCEPTION_CATCHING=0` により C++ の例外処理を保ちます。`-sABORTING_MALLOC=0`、初期メモリー 32 MiB、上限 512 MiB でビルドします。Reader の公開 API は `noexcept` ではなく、メモリー確保失敗を表す結果の構築中にも `std::bad_alloc` が伝播する場合があります。接続層はこの例外も捕捉し、追加のメモリー確保が不要な `resource_limit` の JSON を返します。予期しない C++ 例外は `adapter_error` です。

入力と結果のバッファーを解放しても、Wasm の線形メモリーの容量は縮小しません。繰り返し実行の検査では、所有するバッファーの解放と、確保済みヒープ容量の推移を区別してください。Worker の終了時には Wasm インスタンスごと破棄します。これらの仕組みは、ブラウザー全体のメモリー不足やプロセス終了からの復帰を保証するものではありません。

## ABI と所有権

関数宣言は [bridge.h](bridge.h) にあります。JavaScript から呼ぶ関数名には先頭に `_` を付けます。全数値引数は wasm32 の符号なし 32 ビット整数です。JavaScript 側では呼び出し前に、整数であることと `0 <= value <= 4294967295` を確認します。範囲外の数値を Wasm に渡すと、接続層で検証する前に桁が折り返されます。

1. `specqr_alloc(byteLength)` で入力を確保します。同時に保持できる入力は 1 個です。0 バイト、64 MiB 超過、既存の入力がある場合、確保失敗の場合は null を返します。
2. メモリー拡張に備え、確保後の `module.HEAPU8` に入力をコピーします。
3. `specqr_decode(kind, pointer, byteLength, width, height, stride, channels, flags, maxCandidates, maxSymbols, maxGridAttempts)` を呼びます。`kind=0` は 0/1 のモジュールグリッド、`kind=1` は 8 ビットのグレースケールまたは RGB です。グリッドでは `width=N`、`stride` が 1 行のバイト数で、`height` と `channels` は未使用です。画像は最後の行の実データまで存在すれば、末尾のパディングを省略できます。
4. 戻り値はモジュールが所有する UTF-8 JSON のポインターです。`UTF8ToString` で JavaScript 側へコピーします。
5. `finally` で `specqr_free(pointer)` と `specqr_release_result()` を呼びます。次の `specqr_decode()` も前回の結果を解放します。

`flags` のビット 0 は診断、ビット 1 は `try_invert`、ビット 2 は `strict_padding`、ビット 3 は鏡像の試行無効化です。その他のビットは `invalid_input` です。各処理量制限の 0 は既定値を使用し、正の値は上書きします。`maxSymbols=1` や `maxGridAttempts=1` で、シンボルを取得した後に探索を打ち切るケースを検査できます。

`specqr_live_input_bytes()` と `specqr_result_size()` は、現在保持している入力と結果のバイト数を返します。不正な入力ポインターと確保領域を超える長さは、Reader を呼ぶ前に拒否します。`specqr_free()` に異なるポインターを渡しても解放しません。

JSON は CLI の `runtime_ms`、`repeat`、`peak_rss_kib` を除いた内容です。`bytes_hex`、別名の `hex`、全セグメント、ECI、FNC1、Structured Append、四隅、トレース、`work_limit_reached` を保持します。`ok=true` と `work_limit_reached=true` が同時に返る場合は、取得したシンボルがあっても探索が完了していません。`error`、`symbols`、`work_limit_reached` を一緒に確認してください。

カメラ、文字コードの自動変換、Structured Append の再結合、公開ホスティングは含めていません。デコードした内容を HTML として解釈したり、URL やコマンドとして実行したりしません。画像認識の範囲は既存の Reader と同じで、Mac の検証結果を iPhone/iPad の動作確認には扱いません。

## ネイティブ比較と例外検査

リポジトリのルートで、C++17 コンパイラーを使って比較用バイナリーをビルドします。`CXX` を指定しない場合は `c++` を使用します。

```sh
bash browser/build.sh native browser/build
bash browser/build.sh contract browser/build
mkdir -p browser/build/evidence
./browser/build/bridge-contract fixtures/grids/segno-v01-H-m0.grid \
  > browser/build/evidence/native-fault-injection.ndjson
```

Wasm 版の例外検査は、同じ SDK と、その環境で選択された Node.js を使用します。

```sh
(
  . /absolute/path/to/emsdk/emsdk_env.sh
  EMXX="$(command -v em++)" bash browser/build.sh wasm-contract browser/build
  node browser/build/wasm-contract.cjs /fixture.grid \
    > browser/build/evidence/wasm-fault-injection.ndjson
)
```

`native-bridge` は同じ ABI と JSON 出力処理をネイティブ C++ で動かします。呼び出し形式は次のとおりです。

```text
native-bridge grid|image RAW WIDTH HEIGHT STRIDE CHANNELS FLAGS MAXCAND MAXSYM MAXATTEMPTS [REPEAT]
```

標準出力には最後の JSON、標準エラーには繰り返し回数と終了時の入力・結果バイト数を出力します。終了コードは成功が 0、Reader の不成功が 2、呼び出し形式やファイルのエラーが 3 です。

`bridge-contract` は実際の `specqr_decode()` に対し、グローバルな `operator new` による確保を 512 段階で失敗させます。Reader 内と JSON 作成中の両方を対象とします。確保失敗の注入、テスト用の `main`、埋め込みフィクスチャ用の仮想ファイルシステムは、検査専用バイナリーにだけ含めています。製品用の `specqr.wasm` には含めません。結果の件数は各実行の出力で確認してください。

固定入力の通常・診断結果、CLI との比較、反復、ブラウザーの画像展開や停止・復帰の検査は [検証ハーネスの説明](trial/tests/README.md) を参照してください。
