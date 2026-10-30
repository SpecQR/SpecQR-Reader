# 試験版のライセンスと第三者由来の情報

新規に作成した SpecQR Reader のコードと文書には、[MIT ライセンス](LICENSE-SpecQR-Reader.txt) を適用し、`Copyright (c) 2026 SpecQR contributors` の帰属表示を使用します。以下の第三者通知文は、それぞれの適用範囲を維持します。プロジェクトの MIT ライセンスで置き換えるものではありません。

## Wasm のビルドに伴う通知文

ブラウザー版は、C++17 Reader を Emscripten 6.0.12 でビルドします。生成する JavaScript と Wasm に関連するツールチェーン・ランタイムの通知文を、このフォルダーに保持しています。

- [Emscripten のライセンス本文](LICENSE-Emscripten.txt)
- [libc++ のライセンス本文](LICENSE-libcxx.txt)
- [libc++abi のライセンス本文](LICENSE-libcxxabi.txt)
- [compiler-rt のライセンス本文](LICENSE-compiler-rt.txt)
- [musl のライセンス本文と帰属表示](LICENSE-musl.txt)

この SDK の既定のメモリーアロケーターである dlmalloc は、上流の `system/lib/dlmalloc.c` 冒頭で、Doug Lea によるパブリックドメインの実装として記載され、CC0 を参照しています。

Reader の QR 解析に、第三者の QR デコーダーを組み込んでいません。PNG/JPEG の展開は利用するブラウザーの画像機能に委ねており、この試験版に別の画像コーデックやカメラ実装を同梱していません。ソースチェックアウトでは `assets/specqr.mjs` と `assets/specqr.wasm` は未生成です。生成物を含む試験版を配布する場合も、これらの通知文を保持してください。

## 固定サンプルの来歴

同梱の `samples/` には、テスト用に指定したバイト列を符号化した 6 個のモジュールグリッドと、それらを配置した 6 枚の PNM 画像があります。別途、ブラウザーの画像展開を確認する PNG、JPEG、透過 PNG を含めています。写真コーパスや比較用デコーダーのバイナリーは含めていません。

グリッドの生成元には qrcode 8.2、Segno 1.6.6、ZXing Java 3.5.3、libqrencode 4.1.1 が含まれます。これらのエンコーダーの実装や配布物を試験版に同梱しているわけではありません。各サンプルの元コミット、元ファイル、生成元、期待するバイト列とメタデータ、SHA-256 は以下に記録しています。

- [グリッドの原典と独立した期待値](samples/source-inputs.json)
- [PNM 画像の配置と画素ハッシュ](samples/raster-manifest.json)
- [同梱サンプルと画像展開用データの一覧・ハッシュ](samples/manifest.json)

JSON 内の元ファイルのパスは、原典のリポジトリ内の位置を示します。この試験版フォルダー内の配置は `samples/manifest.json` で確認してください。固定サンプルからの展開・派生画像の作成手順は `tests/prepare-fixtures.py` にあります。

Reader の QR パラメーター表は qrcode 8.2 から抽出し、Segno 1.6.6 と照合した数値を使用しています。その出典とサンプルの生成元に関する通知文として、次の原文を保持しています。

- [qrcode 8.2 の通知文](samples/qrcode-8.2-LICENSE.txt)：BSD-3-Clause の本文と、継承された MIT の帰属表示。
- [Segno 1.6.6 の通知文](samples/segno-1.6.6-LICENSE.txt)：BSD-3-Clause の本文。

規格の規定本文は再配布していません。QR Code Model 2 の全条件への適合や、形式的な正しさを主張するものではありません。
