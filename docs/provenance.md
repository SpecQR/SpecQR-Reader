<a id="tables-and-synthetic-test-provenance"></a>
# 表と合成テストデータの出典

ソースパッケージは、リポジトリーに含まれる固定データから、同梱する C++ テストをビルド・実行します。外部エンコーダー、デコーダーとの比較、画像コーデックとそれらの配布物は、任意で使用する開発用の参照物です。製品や同梱するコアテストには必要ありません。ビルドとテストのコマンドは [README](../README.md) を参照してください。`make test-core` は、エンコーダーをインストールせずに、リポジトリー内の RS/FNC1 ベクトルとパディング用グリッドを検証します。

以下の SHA-256 は、変更していない生成データと記録済みの出典を識別します。これは再現性と出典の記録であり、QR 規格への適合を証明したり、対応するすべての入力をテストしたと主張したりするものではありません。データの再生成は別の任意作業です。一時的なチェックアウトで行い、ファイルを置き換える前にハッシュを比較してください。

<a id="pinned-external-generators"></a>
## 固定参照の外部生成器

| 生成器 | 厳密なバージョン・成果物 | SHA-256 |
| --- | --- | --- |
| [python-qrcode](https://github.com/lincolnloop/python-qrcode) | `qrcode-8.2-py3-none-any.whl` | `16e64e0716c14960108e85d853062c9e8bba5ca8252c0b4d0231b9df4060ff4f` |
| [Segno](https://github.com/heuer/segno/) | `segno-1.6.6-py3-none-any.whl` | `28c7d081ed0cf935e0411293a465efd4d500704072cdb039778a2ab8736190c7` |
| [ZXing Java core](https://github.com/zxing/zxing/tree/f0f1a94439db0300ff0c4618f4e0a41aa630aeac) | `core-3.5.3.jar`、コミット `f0f1a94439db0300ff0c4618f4e0a41aa630aeac` | `8d8064c1636fdaef7189dd9055c7d59950a8940a12f2293956446ec3c109fd82` |
| ZXing Java core のソース | `core-3.5.3-sources.jar`、同じコミット | `f983454400d73652ad5236413ba7eba48072e042982a84b10e1468506f4b28ae` |
| [libqrencode](https://github.com/fukuchi/libqrencode/tree/715e29fd4cd71b6e452ae0f4e36d917b43122ce8) | コミット `715e29fd4cd71b6e452ae0f4e36d917b43122ce8` の 4.1.1 ソースアーカイブ | `7777a49d6b838347ccc35dadd78e47bcf3962536da1b047ccd06a432cf796db9` |

このパッケージに出典の通知文として同梱する上流ファイルは、`third_party/notices/qrcode-8.2-LICENSE.txt`（SHA-256 `40dfb903c94ee3f789500131311186548ecba5cefd3557623d7700e2522ab994`）と `third_party/notices/segno-1.6.6-LICENSE.txt`（SHA-256 `de6c85fccf5d52902aa13dfe2dc6d2a2a106fc3419ed438f3460f0d4b76a6935`）です。適用範囲と、新規作成したプロジェクトコードの MIT ライセンスについては [THIRD_PARTY.md](../THIRD_PARTY.md) で説明しています。

<a id="factual-parameter-tables"></a>
## 仕様上の数値を収めたパラメーター表

`src/qr_tables.hpp`（SHA-256 `032dcd00ba98aa8e4dadfd214183b85dfe11742bbbc27a2b75f95dfb3dd15df7`）は、バージョン・誤り訂正レベル別のブロック数とパリティ数の組 160 件、総符号語数 40 件、ゼロ終端の位置合わせパターン座標 40 行を収めています。開発時に使った `tools/generate_qr_tables.py`（SHA-256 `be3e74081c169272cc41db296921551b6cd65eeea5af8206e017a059b1472c7c`）は、qrcode の `RS_BLOCK_TABLE` と `PATTERN_POSITION_TABLE` から数値を取り出し、Segno の `ECC` と `ALIGNMENT_POS` との一致を検証して C++ 配列を出力します。これは整形と相互照合のための補助スクリプトであり、デコーダーの依存物ではありません。過去のツールパスは開発時の役割を識別するものです。同梱していない任意ツールは、この配布物のビルドに必要ありません。

公開準備時の監査では、固定した両方の wheel 内のリテラルデータを、各実装のアルゴリズムを実行せずに独立に解析し、同梱するすべての表の値が一致することを確認しました。上流のコメントは ISO/IEC 18004:2015 の表データと Annex E を出典に挙げており、これは間接的な出典の証拠です。規格の規定本文を転載しておらず、読んだとも主張していません。

<a id="fixed-rs-blocks"></a>
## 固定 RS ブロック

`tests/rs_vectors.inc` の SHA-256 は `7c3f084e168410771bbb7629128c242977ca68175ee2872881700172f9196c7c` です。96 ブロックは、7〜30 の各パリティ数と、`parity + 1`、`parity + 17`、128、255 の 4 種類の長さを網羅しています。ペイロードのバイト `i` は `(i * 37 + parity * 19 + length) & 255` とし、qrcode 8.2 の `BitBuffer`、`RSBlock`、`create_bytes` が独立した固定の符号語出力を生成します。テストは、決定的な破損を加えた後にこれらのブロックが完全に復元されることを確認し、訂正範囲を超えた一部の結果を別途測定します。

同梱する独自の補助スクリプト `tests/generate_rs_vectors.py` は任意で使用できます。インストール済みの qrcode のバージョンが 8.2 であることを確認し、実行すると `.inc` ファイルを書き換えます。以前の `PYTHONPATH=references/python` という例は、同梱していない旧開発構成を指しています。固定参照の厳密な qrcode wheel を分離した Python 環境へ別途用意したうえで、一時的なチェックアウトから `python3 -B tests/generate_rs_vectors.py` を実行し、生成物のハッシュを上記と比較してください。このパッケージは、その参照実装を自動でインストール・取得しません。

<a id="single-grid-and-embedded-image-fixtures"></a>
## 単一グリッドと埋め込み画像のテストデータ

| テストデータ | 生成条件と期待する厳密なバイト列 | 元のテキストグリッドの SHA-256 |
| --- | --- | --- |
| `fixtures/grids/segno-v01-H-m0.grid` | Segno 1.6.6。Model 2 バージョン 1、H、マスク 0、バイトモード、ISO-8859-1 入力、`boost_error=False`。生バイト列は `c879d793d7`。明示的なフラグ付きで受け入れる余分なゼロのパディングと、厳格設定での拒否を検証。 | `97662311a655b59f54d6511cabec9cd7b5531cb6e520ca2706a847c3db867f44` |
| `tests/image_boundary_tests.cpp` に埋め込んだ行列 | qrcode 8.2。バージョン 1、L、マスク 5、`optimize=0` のバイトモード、余白なし、`fit=False`。生バイト列は `00ff014c0d`。テスト自身がグレースケール・RGB の画面、回転、鏡映、離して配置した複製を描画。 | `47e4a6b23c98c2efc97beb618d6d5217e8869a3149f14304c222c846d4ad2670` |

元の決定的な開発用生成器は `tools/generate_fixtures.py`（SHA-256 `1e4180695de305e649cda91e64377d7c0d98f534cd9cdf13d81baee78a1d0274`）でした。Segno のケースは、シード `0x53515231` によるバイト入力系列に由来します。上記の厳密な入力だけで、このケースを特定できます。広範な画像・コーパスを生成するこの過去のツールは、ソースパッケージには含みません。テキストグリッドのハッシュ対象の直列化形式は、十進数の一辺のサイズ、改行、行ごとに `0`/`1` で表した各行、末尾の改行です。公開準備時の監査では、両方の行列が元のマニフェストのハッシュと一致することを確認しました。

<a id="fnc1-vectors"></a>
## FNC1 ベクトル

`tests/fnc1_vectors.inc`（SHA-256 `3618541270b861269dae65bca54c94b98802ca547004709100dfe8a7db563204`）には、有効なシンボル 12 件と不正入力の対照 2 件が含まれます。`tests/fnc1_public_tests.cpp` は、それぞれをサンプリング済みモジュール、グレースケール画素、ストライド付き RGB 画素として検証します。期待するバイト列とセグメント・制御メタデータを埋め込んでいるため、テストの実行に Java、外部 C ライブラリー、写真ファイルは必要ありません。

FIRST 位置の 5 シンボルは、変更していない ZXing Java 3.5.3 の `Encoder.encode` で、`GS1_FORMAT=true`、誤り訂正レベル Q、マスク 3、要求バージョン 4 を指定して生成しました。混合モードのケースでは `QR_COMPACT` を有効にしており、その経路が選んだ実際のバージョンを各ベクトルに記録しています。独自の補助コードがリフレクションを通じてコンパクトエンコーダーのセグメント計画を観測しますが、セグメントのビット列、ECC、モジュールを書き込むことはありません。

SECOND 位置の 7 シンボルは、変更していない libqrencode 4.1.1 の `QRinput_setFNC1Second`、`QRinput_append`、`QRcode_encodeInput` を使用しています。バージョン 4、誤り訂正レベル Q、型を明示した入力チャンクを指定し、マスクは通常の選択処理に任せています。アプリケーションインジケーターは、下表の順に 42、9、165、99、197、222、0 です。不正入力の対照では、公開の追加操作を使い、不適切な位置または重複した SECOND ヘッダーを、外部実装による有効な ECC・行列構築で符号化しています。

| ケース | 期待結果 | 期待する Reader の生バイト列（十六進） | 元のテキストグリッドの SHA-256 |
| --- | --- | --- | --- |
| `fnc1-first-alpha-gs` | valid | `41424325313233` | `2938b1387b21a347d6938d362469ca00a47b0b583dd7e4b80175311533cfb10b` |
| `fnc1-first-alpha-percent` | valid | `52415445252535302525` | `00de5f96102bb181b6721d5e15a4fcb6bbb89eb298d4dffe144b3f91bf244121` |
| `fnc1-first-byte-percent-gs` | valid | `726177251d25257461696c` | `81b557ffc6cc61e4c84e18c624863d346330dc283be29c05449b9006caa3710b` |
| `fnc1-first-mixed` | valid | `4142253132333435363738393031323334353637383930252543441d787925` | `5d895b9b6b303b48334ee265ebe296dca1dbc9fa06e1cd33da5a571d6218dd85` |
| `fnc1-first-mixed-boundary` | valid | `41414141414141414141253132333435363738393031323334353637383930313233343536373839302561616161616161616161` | `4bfcb54580dc2313dcfffbce05b5dccee560e90309f298775fee873bb820e345` |
| `fnc1-second-alpha-gs` | valid | `41424325313233` | `b409a8ebee249de914a11bddaa1e4e5ecd0c671344c1f8cab773ab3b72b3e7b4` |
| `fnc1-second-alpha-percent` | valid | `52415445252535302525` | `d1fcef9d8059bf8737d8009b3dcc9fb9e14d7319729ef7efa2753583fd5bacd6` |
| `fnc1-second-byte-percent-gs` | valid | `726177251d25257461696c` | `5f10b85be15cbe856bf09d38cd0de542e75812a352bedf6a6fdb99ba37cb6a58` |
| `fnc1-second-mixed` | valid | `4125251d00ff2542` | `37e34cd0dfd2d76a483d6a72fe85f458acfbb20f198e2fe57a9b938ebef78465` |
| `fnc1-second-split-percent` | valid | `41252542` | `e74fa2147ff414bfbfbccc4d01d1cd631fe9dd62c3f4a615e814ae7751cb20d1` |
| `fnc1-second-double-percent-boundary` | valid | `41252525252542` | `43824583941af32261cf143143604313890613c098a04cc9a3e54a657d765d0f` |
| `fnc1-second-numeric-and-gs` | valid | `313233343536373839303132333435363738393025251d25` | `ef656a7bb49d10766dc103dabde8135479a5445f3944e1737942d75e91643b8f` |
| `fnc1-malformed-late-header` | reject | シンボルなし | `35c7ea6a2715f905dde9428077fe6a6c42a42aacc4388561943ecdcb71dded7f` |
| `fnc1-malformed-duplicate-header` | reject | シンボルなし | `b34489bf7c2ac36c2b914f3e3cdcc06dbf81d0c3d2e102858209af405242e710` |

元の開発用生成器と補助コードは、`tools/generate_fnc1_fixtures.py`（SHA-256 `630be88519d0853ef3d0cb9197a9862100b777154e0159abe1f497f7fee2981c`）、`tools/Fnc1JavaEncoder.java`（`c58291829574789ae9295ddd853b931aa902c826a14aec96f6ae36353577e028`）、`tools/fnc1_libqrencode.c`（`a2422d2883f5d4678072e9aec216e4a1c1633dbca468ff669040991ff55d42f6`）でした。これらのパスは別途保持している開発ツールを識別するもので、このパッケージがインストールする前提ソフトウェアではありません。外部成果物を検証し、独立したエンコーダーを呼び出し、セグメント計画を記録して、`.grid`、PGM、`.inc` 形式を出力します。公開準備時の監査では、保存したすべての行列と計画から `.inc` ファイルをバイト単位で再構成しました。

Reader の期待値は、リテラルの `%` バイトと、生の SECOND インジケーターをメタデータとして保持します。ZXing-C++ との比較の期待値は、そのデコーダーによるパーセントのエスケープ解釈と、文字列の SECOND 接頭辞に合わせて別途定義しています。評価器は、Reader の実際の出力を正規化していません。これらの選択した合成テストから、GS1 アプリケーションメッセージの妥当性、独立した FNC1 の実写写真の網羅性、入力全体の網羅性、形式証明は導けません。
