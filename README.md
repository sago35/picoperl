# picoperl

Raspberry Pi Pico (RP2040) 上で動く小さな Perl サブセットのインタプリタと、
それを使った自作キーボード用ファームウェアです。
pico-sdk + TinyUSB の上に載っていて、キーマトリクスのスキャンやキーマップは
**Perl スクリプトで書きます** (PRK Firmware の PicoRuby の Perl 版のような位置付け)。

ファームウェアを書き込むと `PICOPERL` という USB ドライブが見えます。
そこにある `/main.pl` が起動時に実行され、`/lib` 以下のモジュールを `use` / `require`
で読み込みます。ファイルを保存すると自動的に再起動して新しいコードが動きます。

```
PICOPERL/
  main.pl                     ← 起動時に実行 (キーマップはここ)
  lib/PicoPerl/Keyboard.pm    ← マトリクススキャン・デバウンス・レイヤー
  lib/PicoPerl/Keycodes.pm    ← MO()/TG() などのヘルパ (KC_* 定数はファームウェア内蔵の表から生成)
  README.TXT
```

## できること

- USB HID キーボード (boot protocol、6KRO) + USB CDC シリアルコンソール + USB マスストレージ
- GPIO の入出力、フラッシュ上の FAT ファイルシステム (1 MiB、`fs_read`/`fs_write` からも使える)
- Perl のモジュール機構: `package`、`use Module qw(...)`、`require`、`@EXPORT`/`@EXPORT_OK`
  (Exporter 相当を内蔵)、`use constant`、`__PACKAGE__`
- `KC_A` などのキーコード定数 (QMK 準拠、202 個) は `src/pp_keycodes.c` の C の表から
  **使われた名前だけ**実行時に生成されるので RAM をほとんど使いません
  (`keycode_value("KC_A")` / `keycode_name(4)` でも引けます。PC の perl で動かすときは
  `PicoPerl::KeycodesTable` が同じ表を提供します)
- 付属ライブラリ `PicoPerl::Keyboard`: 行/列ピンとキーマップを渡すだけで動く。
  デバウンス、モディファイア、`MO(n)`/`TG(n)` レイヤー、`KC_TRNS`、Caps Lock LED、
  `KC_BOOTSEL` (UF2 ブートローダへ)、コールバック (`on_report`, `on_press`, `on_release`, `on_idle`)

## main.pl の例

```perl
use strict;
use PicoPerl::Keycodes;
use PicoPerl::Keyboard;

keyboard_run(
    rows   => [2, 3],          # 行: 出力 (アクティブ Low)
    cols   => [4, 5, 6],       # 列: 入力 (内部プルアップ)、ダイオードは row -> col
    keymap => [
        [ [ KC_A,    KC_B, MO(1)    ],      # layer 0
          [ KC_LSFT, KC_C, KC_ENTER ] ],
        [ [ KC_1,    KC_2, KC_TRNS  ],      # layer 1
          [ KC_TRNS, KC_3, KC_BOOTSEL ] ],
    ],
    debounce_ms => 5,
    on_report => sub { my ($mod, @keys) = @_; printf("mod=%02x keys=@keys\n", $mod) },
);
```

自分でループを回したい場合は `keyboard_init(%opts)` の後に `keyboard_step()` を呼びます
(デバウンス済み状態が変わると 1 を返す)。

`keyboard_run` の主なオプション:

| オプション | 説明 |
|---|---|
| `diode => 'row2col'` / `'col2row'` | 駆動する側 (既定: 行を駆動して列を読む) |
| `active => 'low'` / `'high'` | `'low'`: 駆動ピンを Low、入力はプルアップ (既定)。`'high'`: 駆動ピンを High、入力はプルダウン |
| `direct_keys => { GPIO => keycode }` | マトリクスに入っていない単独スイッチ (プルアップ、アクティブ Low) |
| `encoder => { a => GPIO, b => GPIO, cw => keycode, ccw => keycode, precision => 4 }` | ロータリーエンコーダ (1 クリックごとに cw/ccw をタップ) |
| `debounce_ms`, `settle_us`, `scan_interval_ms`, `caps_led` | タイミングと Caps Lock LED |
| `on_report`, `on_press`, `on_release`, `on_encoder`, `on_idle` | コールバック |

`scripts/main.pl` のデフォルトは **zero-kb02** (TinyGo Keeb Tour、Waveshare RP2040-Zero) 向けです:
列 GPIO5-8 を High に駆動、行 GPIO9-11 をプルダウンで読む 3x4 マトリクス、エンコーダ GPIO3/4、
エンコーダのボタン GPIO2。ファームウェアは `-DPICO_BOARD=waveshare_rp2040_zero` でビルドします
(ジョイスティックは ADC 未対応のため使えません)。

自作モジュールは `lib/Foo/Bar.pm` に置いて `use Foo::Bar;` で読み込めます:

```perl
package Foo::Bar;
use strict;
use Exporter 'import';      # picoperl では無視され、@EXPORT を内蔵の仕組みで処理する
our @EXPORT = qw(hello);
sub hello { print "hello from ", __PACKAGE__, "\n" }
1;
```

## ディレクトリ構成 (リポジトリ)

```
src/          Perl サブセットインタプリタ本体 (ポータブルな C99、依存なし)
host/         PC 用 CLI (テストと GPIO シミュレーション)
firmware/     pico-sdk + TinyUSB + FatFs のファームウェア (CMake)
scripts/      デフォルトの main.pl
lib/          デフォルトのモジュール (PicoPerl::Keyboard, PicoPerl::Keycodes, PicoPerl::KeycodesTable)
tools/        embed.pl: 初回起動時にドライブへ書き出すデフォルトファイルをファームウェアに埋め込む
test/         本物の perl と出力を突き合わせるテスト
```

## ビルド

### ファームウェア

必要なもの: pico-sdk (2.x)、arm-none-eabi-gcc、cmake、perl (ビルド時にスクリプト埋め込みに使う)。

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cd firmware
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release        # zero-kb02 なら -DPICO_BOARD=waveshare_rp2040_zero、Pico 2 なら pico2
make -j
# → picoperl.uf2 を BOOTSEL モードの Pico にコピー
```

- 初回起動時にフラッシュ末尾 1 MiB を FAT でフォーマットし、`scripts/main.pl` と `lib/` の
  内容を書き込みます。以後はドライブ上のファイルが使われ、埋め込み版はドライブに無い
  ファイルのフォールバックです (`-DPICOPERL_SCRIPT=...` でデフォルトを差し替え可能、
  `-DPICOPERL_FS_SIZE=...` で領域サイズ変更)。
- `-DPICOPERL_FS_RESET=ON` でビルドすると**起動のたびにドライブを初期化**して内蔵ファイルを
  書き直します (uf2 だけで環境が決まるので検証向き。`-DPICOPERL_SCRIPT=scripts/diag_gpio2.pl`
  と組み合わせると GPIO2 → KC_A の診断用ファームになります)。
- シリアルコンソール (CDC) を **1200 baud で開くと BOOTSEL モードで再起動** します
  (例: `stty -F /dev/ttyACM0 1200`)。
- ファームウェアの更新でドライブの内容は消えません (UF2 は先頭側にだけ書き込まれます)。
  初期化し直したいときは `main.pl` から `fs_write` するか、`picotool erase`/nuke UF2 で消してください。

### ホスト (PC) 版

```sh
make            # build/picoperl
make test       # test/t_*.pl を perl と picoperl の両方で実行して出力を比較 (test/e_*.pl はエラーメッセージを比較)
make sim PRESSED=5:9,6:10  # scripts/main.pl を GPIO シミュレーションで実行 (駆動ピン:読み取りピン、単独ピンは "2")
```

ホスト版の `use`/`require` は スクリプトのディレクトリ、`./lib`、`<script dir>/lib`、
`<script dir>/../lib`、`$PICOPERL_LIB` を探します。

## ボード API (モジュール)

ボードの機能は Perl モジュールとして提供され、`use` したものだけが見えます
(C で実装された関数は `Gpio::_init` のような非公開名で、各モジュールが包んでいます)。

| モジュール | 使い方 |
|---|---|
| `Gpio` | `my $p = Gpio->new(2, 'input_pullup')` (`'input'`, `'input_pulldown'`, `'output'`); `$p->read`, `$p->write($v)`, `$p->high`, `$p->low`, `$p->toggle`。関数形式 `Gpio::configure($pin, $mode)`, `Gpio::get($pin)`, `Gpio::put($pin, $v)` |
| `Ws2812` | `my $ws = Ws2812->new($pin)`; `$ws->put_color({ R => .., G => .., B => .. })`; `$ws->write_raw(@grb)` (LED ごとに `0xGGRRBBxx`); `$ws->write([r,g,b], ...)`; `Ws2812::rgb($r,$g,$b)` |
| `Encoder` | `my $e = Encoder->new($a, $b, precision => 4)`; `$e->read` (前回からのクリック数), `$e->position`, `$e->read_raw` (割り込みデコード) |
| `Hid` | `Hid::keyboard_report($mod, @keys)`, `Hid::ready()`, `Hid::leds()`, `Hid::consumer_report($usage)` (0xE2 ミュート, 0xE9/0xEA 音量, 0 で離す), `Hid::consumer_ready()`。`use Hid qw(keyboard_report)` で import 可 |
| `Matrix` | `Matrix::scan(\@drive, \@read, $active_high, $settle_us)` → 0/1 の配列リファレンス (C 実装、`PicoPerl::Keyboard` が使用) |
| `Time::HiRes` | `use Time::HiRes qw(usleep sleep time)`: `usleep($us)`, `sleep(0.5)`, `time()` (起動からの秒、小数)。組み込みの `sleep` も小数秒を受け付ける |
| `Usb` | `Usb::mounted()`, `Usb::suspended()`, `Usb::remote_wakeup()`, `Usb::cdc_connected()` |
| `Board` | `Board::led($on)` (LED の無いボードでは何もしない), `Board::reboot_bootsel()`, `Board::reboot()`, `Board::heap_free()`, 定数 `Board::NAME`, `Board::LED_PIN` |
| `Fs` | `Fs::read($path)`, `Fs::write($path, $data)`, `Fs::mounted()` |
| `PicoPerl::Keycodes` | `KC_*` (QMK 名、`KC_MUTE`/`KC_VOLU` などのメディアキー含む), `MO(n)`, `TG(n)`, `KC_BOOTSEL`, `is_modifier`, `is_consumer`, `keycode_value("KC_A")`, `keycode_name(4)`。定数はファームウェア内蔵の表から使ったものだけ生成 |
| `PicoPerl::Keyboard` | `keyboard_run(%opts)` / `keyboard_init`, `keyboard_step`, `keyboard_tap`, `keyboard_layer` |

`print`, `printf`, `warn`, `die` の出力先は USB CDC シリアルと UART0 (GP0/GP1)、`<STDIN>` は CDC から 1 行読みます。
USB の処理 (`tud_task`) はインタプリタが `sleep` の中や一定ステートメントごとに自動で呼ぶので、
スクリプト側で意識する必要はありません。

## 対応している Perl の範囲

インタプリタは本物の Perl 5 との出力一致をテストで確認しています (`make test`)。

対応: スカラー/配列/ハッシュ、リファレンスと自動生成 (`$h{a}{b}[0] = 1`)、
オブジェクト (`bless`, `$obj->method`, `Class->new`, `@ISA` / `use parent`, `SUPER::`, `can`, `isa`, `ref`)、
無名配列/ハッシュ/サブルーチン、クロージャ、`my`/`our`/`local`、
`if/elsif/unless/while/until/for/foreach` と後置修飾子、ラベル付き `last/next`、
`sub`、`return`、`wantarray`、`@_`、`shift`、文字列補間 (`"$x $a[0] $h{k} $r->{k}[1] @a @{[ expr ]}"`)、
`sort`/`map`/`grep`、`split`/`join`/`sprintf`/`substr`/`index` などの組み込み関数、
`eval {}`/`die`/`$@`、`package`/`use`/`require`/`@EXPORT`、`use constant` (`\%hash` 形式も)、
`qw()`、`x` 演算子、範囲 `..`、文字列のマジックインクリメント、`__LINE__`/`__FILE__`/`__PACKAGE__`、POD。

名前解決は Perl と同じで、修飾なしで呼べるのは自パッケージのサブと `use` で import したものだけです。
`use` / `use constant` / `BEGIN` はパース時に実行されるので、import したサブは組み込み関数を
上書きできます (`use Time::HiRes qw(time)`)。`our` 変数はパッケージごとに分かれます (`$Foo::x`)。

**`use strict`** は `vars` (未宣言変数はコンパイルエラー) と `subs` (bareword の禁止) を Perl と同じ
メッセージで検査します (`no strict`、`use strict 'vars'` も可)。レキシカル変数はパース時に
(深さ, スロット) へ解決されるので、実行時に名前検索は行いません。
**`use warnings`** は主要な実行時警告を出します: `Use of uninitialized value $x in addition (+)`、
`Argument "abc" isn't numeric in ...`、`Odd number of elements in hash assignment` / `anonymous hash`
(`no warnings` はブロック単位で有効)。コンパイル時警告 (`Useless use of ...` など) はありません。

非対応 (今のところ): **正規表現** (`=~`, `s///`, `tr///`; `split` の第 1 引数だけは
`/,/` や `/\s+/` のような単純なパターンを受け付けます)、
ファイルハンドル I/O (`open` など。ドライブは `Fs::read`/`Fs::write` で)、`sprintf("%v")`、
`DESTROY`、`AUTOLOAD`、`local` の配列/ハッシュ、`eval STRING`、`each`、ヒアドキュメント、
`foreach` のエイリアス (単一配列に対する書き戻しで代用)。
`use Exporter; use lib; use utf8;` などは受け付けて無視します。

## メモリと安全装置

- RP2040 では 64 KiB のスタックを確保し、インタプリタが C スタックの使用量を監視して
  上限を超えると `die` します (クラッシュではなくコンソールにメッセージが出ます)。
- スクリプトが `die` で終了したりスクリプトの最後まで到達すると、LED が点滅して
  コンソールに理由を表示したまま待機し、ドライブ上のファイルが変更されると再実行します
  (ドライブと 1200 baud リセットは生きています)。
- ホストからの書き込みは 4 KiB ブロック単位でキャッシュし、書き込みが 1.5 秒止まると
  **USB 接続はそのままで** インタプリタだけを再起動して新しい `main.pl` を実行します
  (Windows がマウント直後に書く `System Volume Information` でも 1 回リロードされるだけです)。
- インタプリタはヒープから確保した 48 KiB の専用スタックで動きます (pico-sdk 既定の
  スタックは RAM 末尾の 8 KiB スクラッチ領域で、ヒープとの衝突から保護されないため)。
- RAM は 264 KiB しかないので、実行し終えたファイルトップレベルの文の AST は解放されます。
  デフォルト構成 (`main.pl` + `PicoPerl::Keyboard`) でヒープ使用量は 60 KB 程度です
  (PC 版では `PICOPERL_MEMSTAT=1 build/picoperl scripts/main.pl` で確認できます)。
- メモリが尽きた場合は `picoperl: out of memory` を表示して再起動します。
- 値は参照カウントで管理します。循環参照は解放されません。
