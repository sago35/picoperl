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

- USB HID キーボード (boot protocol、6KRO) + メディアキー (consumer control) + マウス + USB MIDI + USB CDC シリアルコンソール + USB マスストレージ
- GPIO の入出力、PWM (ブザー)、ADC、I2C (SSD1306 OLED)、WS2812 LED、ロータリーエンコーダ、フラッシュ上の FAT ファイルシステム (1 MiB、`Fs::read`/`Fs::write` からも使える)
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
  初期化し直したいときは `main.pl` から `Fs::write` するか、`picotool erase`/nuke UF2 で消してください。

### ホスト (PC) 版

```sh
make            # build/picoperl
make test       # test/t_*.pl を perl と picoperl の両方で実行して出力を比較 (test/e_*.pl はエラーメッセージを比較)
PICOPERL_TEST_PERL=/path/to/perl make test   # 別バージョンの perl と比較 (5.44.0 で全テスト一致を確認済み)
make sim PRESSED=5:9,6:10  # scripts/main.pl を GPIO シミュレーションで実行 (駆動ピン:読み取りピン、単独ピンは "2")
```

ホスト版の `use`/`require` は スクリプトのディレクトリ、`./lib`、`<script dir>/lib`、
`<script dir>/../lib`、`$PICOPERL_LIB` を探します。

### ブラウザ (WebAssembly) 版

ビルドせずに試すなら <https://sago35.github.io/picoperl/> を開いてください
(main に push するたびに `.github/workflows/pages.yml` がビルドして公開します)。

emscripten (emcc) が PATH にあれば、手元でもプレイグラウンドをビルドできます
(`wasm/index.html`):

```sh
make wasm                          # -> wasm/picoperl.js + wasm/picoperl.wasm (~500 KB)
make wasm-test                     # テストスイートを wasm 版で実行 (node が必要)
python3 -m http.server -d wasm     # http://localhost:8000/ を開く
```

エディタと出力に加えて、スクリプトが使ったデバイスの「ボードパネル」が現れます:
SSD1306 は canvas に描画 (アニメーションも動く)、WS2812 は色付きの LED、
PWM/Tone のブザーは Web Audio の矩形波で鳴り、HID はキー名 (`KC_A`) や
マウスパッドで表示。スクリプトが入力に使った GPIO / エンコーダ / ADC は
クリックできるボタン・ホイール・スライダーになります (`wasm/shim.c` が
ホスト版シミュレーションの pp_wasm_* フックとページを橋渡し)。

スクリプトは Web Worker 内で動くのでページは固まらず、暴走したら「停止」で
worker ごと打ち切ります。js/wasm の取得とコンパイルは初回の 1 回だけで、以後は
実行のたびにコンパイル済みモジュールから新しいインスタンスを作ります (実行時の
ネットワークアクセスなし)。`lib/` のモジュールは wasm に埋め込まれ、ファイル I/O は
メモリ上 (MEMFS)、`sleep` は ASYNCIFY でイベントループに返ります。`<STDIN>` は常に
EOF です。

## ボード API (モジュール)

ボードの機能は Perl モジュールとして提供され、`use` したものだけが見えます
(C で実装された関数は `Gpio::_init` のような非公開名で、各モジュールが包んでいます)。

| モジュール | 使い方 |
|---|---|
| `Gpio` | `my $p = Gpio->new(2, 'input_pullup')` (`'input'`, `'input_pulldown'`, `'output'`); `$p->read`, `$p->write($v)`, `$p->high`, `$p->low`, `$p->toggle`。関数形式 `Gpio::configure($pin, $mode)`, `Gpio::get($pin)`, `Gpio::put($pin, $v)`。エッジコールバック: `Gpio->new(2, 'input_pullup', on_falling => sub { ... })` / `$p->watch(rising => sub { ... })` (falling / rising / change) |
| `Ws2812` | `my $ws = Ws2812->new($pin)`; `$ws->put_color({ R => .., G => .., B => .. })`; `$ws->write_raw(@grb)` (LED ごとに `0xGGRRBBxx`); `$ws->write([r,g,b], ...)`; `Ws2812::rgb($r,$g,$b)` |
| `Encoder` | `my $e = Encoder->new($a, $b, precision => 4)`; `$e->read` (前回からのクリック数), `$e->position`, `$e->read_raw` (割り込みデコード)。コールバック: `Encoder->new(3, 4, on_turn => sub { my ($clicks) = @_; ... })` |
| `Ssd1306` | `my $d = Ssd1306->new(sda => 12, scl => 13, address => 0x3C, width => 128, height => 64, rotation => 180)`; `$d->clear_display`, `$d->set_pixel($x,$y,$c)`, `$d->display`, `$d->size`, `$d->set_rotation`, `$d->invert`, `$d->contrast` + `Gfx` の描画メソッド |
| `Gfx` | フレームバッファ (C 実装): `line`, `rectangle`, `filled_rectangle`, `circle`, `filled_circle`, `triangle`, `filled_triangle`, `write_line($font, $x, $y, $text, $c, $scale)` (x,y はベースライン)、`Gfx::fonts()` |
| `Tinydraw` / `Tinyfont` | tinygo 風の関数形式: `Tinydraw::Rectangle($d, ...)`, `Tinyfont::WriteLine($d, Tinyfont::FREEMONO_BOLD_9PT, $x, $y, "hello", $c)`。内蔵フォント: `classic` (5x7), `freemono_bold_9pt`, `gophers_32pt`, `gophers_58pt`, `shnmk12` (東雲 12px、UTF-8 でかな + JIS 第 1/2 水準漢字、フラッシュ +約 160 KB) |
| `Midi` | USB MIDI: `my $m = Midi->new`; `$m->note_on($cable, $ch, Midi::C4, $vel)`, `note_off`, `control_change` (`Midi::CC_MODULATION_WHEEL` など), `pitch_bend($cable, $ch, $v14)`, `program_change`, `write($cable, @bytes)`。音名定数 `Midi::A0`〜`Midi::G9` (= MIDI ノート番号) || `I2c` (低レベル) | `I2c::_init($bus, $sda, $scl, $hz)`, `I2c::_write($bus, $addr, $bytes)`, `I2c::_read($bus, $addr, $n)` |
| `Adc` | `my $a = Adc->new(29)` (GPIO26-29); `$a->get` (0..0xFFFF、TinyGo 互換), `$a->read_raw` (0..4095), `$a->voltage`; `Adc->new(Adc::TEMPERATURE)->temperature` |
| `Hid` | `Hid::keyboard_report($mod, @keys)`, `Hid::ready()`, `Hid::leds()`, `Hid::consumer_report($usage)` (0xE2 ミュート, 0xE9/0xEA 音量, 0 で離す), `Hid::consumer_ready()`, `Hid::mouse_report($buttons, $dx, $dy, $wheel, $pan)`, `Hid::mouse_ready()`。`use Hid qw(keyboard_report)` で import 可 |
| `Mouse` | `my $m = Mouse->new`; `$m->press(Mouse::LEFT)`, `$m->release(...)`, `$m->click(Mouse::RIGHT)`, `$m->move($dx, $dy)`, `$m->wheel($n)`, `$m->wheel_up` / `wheel_down` (`LEFT`, `RIGHT`, `MIDDLE`, `BACK`, `FORWARD`) |
| `Pwm` | `my $p = Pwm->new($pin, frequency => 1000, duty => 0.5)`; `$p->frequency($hz)` (実際の周波数を返す), `$p->duty(0..1)`, `$p->stop`, `$p->top`。GPIO n と n+16 は同じスライス、隣り合う 2 ピンは周期を共有 |
| `Tone` | `my $s = Tone->new($pin)`; `$s->set_note(Tone::C5)` (MIDI ノート番号、`Tone::A0`〜`Tone::G9`、`Tone::CS4` = C#4), `$s->set_frequency($hz)`, `$s->set_period($ns)`, `$s->stop`; `Tone::frequency($note)`, `Tone::note("CS4")` |
| `Matrix` | `Matrix::scan(\@drive, \@read, $active_high, $settle_us)` → 0/1 の配列リファレンス (C 実装、`PicoPerl::Keyboard` が使用) |
| `Time::HiRes` | `use Time::HiRes qw(usleep sleep time)`: `usleep($us)`, `sleep(0.5)`, `time()` (起動からの秒、小数)。組み込みの `sleep` も小数秒を受け付ける |
| `Usb` | `Usb::mounted()`, `Usb::suspended()`, `Usb::remote_wakeup()`, `Usb::cdc_connected()` |
| `Board` | `Board::led($on)` (LED の無いボードでは何もしない), `Board::reboot_bootsel()`, `Board::reboot()`, `Board::heap_free()`, 定数 `Board::NAME`, `Board::LED_PIN` |
| `Fs` | `Fs::read($path)`, `Fs::write($path, $data)`, `Fs::mounted()`。行単位なら組み込みの `open` が使える (下記) |
| `PicoPerl::Keycodes` | `KC_*` (QMK 名、`KC_MUTE`/`KC_VOLU` などのメディアキー含む), `MO(n)`, `TG(n)`, `KC_BOOTSEL`, `is_modifier`, `is_consumer`, `keycode_value("KC_A")`, `keycode_name(4)`。定数はファームウェア内蔵の表から使ったものだけ生成 |
| `PicoPerl::Keyboard` | `keyboard_run(%opts)` / `keyboard_init`, `keyboard_step`, `keyboard_tap`, `keyboard_layer` |

`print`, `printf`, `warn`, `die` の出力先は USB CDC シリアルと UART0 (GP0/GP1)、`<STDIN>` は CDC から 1 行読みます。
USB の処理 (`tud_task`) はインタプリタが `sleep` の中や一定ステートメントごとに自動で呼ぶので、
スクリプト側で意識する必要はありません。

### 割り込みコールバック

GPIO とエンコーダには perl の "safe signals" と同じ遅延ディスパッチのコールバックを
登録できます: 割り込みハンドラ (C) はイベントを積むだけで、コールバックは**文の切れ目**に
通常の Perl コードとして呼ばれます (割り込みコンテキストでは走らず、ハンドラ内の `die` も
普通に伝播します)。タイマは perl 標準の `alarm` + `$SIG{ALRM}` です:

```perl
my $btn = Gpio->new(2, 'input_pullup', on_falling => sub { print "pressed\n" });
my $enc = Encoder->new(3, 4, on_turn => sub { my ($clicks) = @_; ... });
$SIG{ALRM} = sub { ... };  alarm 5;
while (1) { sleep 0.1 }        # イベントが来ると sleep は早く戻る (perl のシグナルと同じ)
```

エッジはノイズも拾うため、「エッジで起こしてレベルで確定」を既定にしています:
ディスパッチ時 (エッジの数 ms 後) にピンを読み直し、エッジ方向と一致しないイベント
(エンコーダ操作のクロストークなど µs 級のグリッチ) は捨てます。それでも機械式
スイッチの押下自体はチャタリングするので、連打検出が要る場面ではハンドラ側の
時間デバウンスも併用してください (`scripts/irq_demo.pl` に例があります)。

## 対応している Perl の範囲

インタプリタは本物の Perl 5 との出力一致をテストで確認しています (`make test`)。

対応: スカラー/配列/ハッシュ、リファレンスと自動生成 (`$h{a}{b}[0] = 1`)、
オブジェクト (`bless`, `$obj->method`, `Class->new`, 間接オブジェクト構文 `new Class(ARGS)`, `@ISA` / `use parent`, `SUPER::`, `can`, `isa`, `ref`)、
無名配列/ハッシュ/サブルーチン、クロージャ、`my`/`our`/`local`、
`if/elsif/unless/while/until/for/foreach` と後置修飾子、ラベル付き `last/next/redo`、
`sub`、`return`、`wantarray`、`@_`、`shift`、文字列補間 (`"$x $a[0] $h{k} $r->{k}[1] @a @{[ expr ]}"`)、
`sort`/`map`/`grep`、`split`/`join`/`sprintf`/`substr`/`index` などの組み込み関数、
`eval {}`/`die`/`$@`、`package`/`use`/`require`/`@EXPORT`、`use constant` (`\%hash` 形式も)、
`qw()`、`x` 演算子、範囲 `..`、ヒアドキュメント (`<<EOF` `<<"..."` `<<'...'` `<<~EOF`)、
`each` (イテレータは `keys` でリセット)、`state` (クロージャごとに独立)、
`for my ($k, $v) (%hash)` の複数変数 foreach、論理 XOR `^^` / `^^=`、
`try/catch/finally` (catch 内で `$@` は空、`return` は外へ、finally は常に実行)、
`isa` 演算子、後置デリファレンス (`$r->@*` `$r->%*` `$r->$*` `$r->$#*`)、
`any { }` / `all { }` (短絡)、`use builtin qw(true false ceil floor trim blessed
reftype refaddr indexed)`、
文字列のマジックインクリメント、`__LINE__`/`__FILE__`/`__PACKAGE__`、POD、
`use vars qw($x @y %z)` (`our` と同じ扱い)。

**型グロブへの代入**に対応しています: `*name = \&func;` / `*name = sub {...};` (サブの別名・定義)、
`*name = \@array;` / `\%hash` / `\$scalar` (変数の別名)、`*a = *b;` (4 種類すべて)、
動的な名前 `*{"${class}::$name"} = sub {...};` / `*$name = ...`。
**シンボリックリファレンス** (`&{"name"}` `&$name()` `\&{"name"}` `@{"Pkg::x"}` `${"name"}` `%{"name"}`
`defined &{"name"}`) も使えます。perl と同じく `use strict` の下では
`Can't use string ("x") as an ARRAY ref while "strict refs" in use` で die し、`no strict 'refs'` で許可されます。

**値としてのグロブ**:

- `\*name` はグロブへのリファレンスです (`ref` は `GLOB`、同じ名前なら同じリファレンス)。
  `*$ref` / `*{$ref}` で元のグロブに戻り、`${*$ref}` `@{*$ref}` `&{*$ref}()` で中身を取り出せます
- `*name{SLOT}` (`*$ref{SLOT}`、`*{"Pkg::name"}{CODE}`): `SCALAR` `ARRAY` `HASH` `CODE` `GLOB` はリファレンス
  (無ければ undef)、`NAME` と `PACKAGE` は名前、`IO` / `FILEHANDLE` はハンドルがあればそのグロブへのリファレンス
- `local *name;` はスカラー、配列、ハッシュ、サブ、ファイルハンドルをまとめて空にし、スコープを抜けると戻します。
  `local *name = \&code;` はその 1 つだけを差し替え、`local *name = *other;` は全体を別名にします
- **bareword のファイルハンドル**: `open(FH, '<', $path)`、`<FH>`、`print FH LIST`、`close(FH)`、`eof(FH)`。
  `\*FH`、`*FH`、`*FH{IO}` を値として渡せ、`print $fh ...` / `print {$fh} ...` / `<$fh>` / `readline($fh)` で使えます。
  `open($ref, ...)` に `\*{"Pkg::NAME"}` (`Symbol::gensym` が返すもの) を渡すと、そのグロブにファイルが付きます
- `\*STDOUT` `\*STDERR` `\*STDIN` も同じように渡せます。`fileno` は標準ハンドルに 0 / 1 / 2、開いているファイルに 3 を返します
- `open(my $dup, '>&', \*STDOUT)`、`'>&1'`、`'>&='`、`'<&'` はハンドルの複製です。本当の複製ではなく
  同じハンドルを指すだけなので、バッファは共有で、複製を `close` すると元も閉じます (標準ハンドルは閉じません)

perl との違い: グロブの値そのもの (`my $g = *name;`) は文字列 `"*main::name"` です (`ref(\$g)` は `SCALAR`)。
`*name{IO}` は `IO::File` オブジェクトではなくグロブへのリファレンスを返し、ハンドルのメソッド呼び出し
(`$fh->print(...)`、`STDOUT->autoflush(1)`) は未対応です。

**パッケージの名前表** (`%Pkg::`、`%main::`、`$::{name}`、`%{"${class}::"}`) は読み取りだけできます。
`exists $::{"Pkg::"}` (そのパッケージに何かあるか)、`exists $Pkg::{name}`、`keys %Pkg::`、`$Pkg::{name}`
(値は文字列 `"*Pkg::name"`) が使えます。picoperl は名前表を実体として持たず、使うたびにシンボルから
組み立てるので、代入や `delete $Pkg::{name}` / `delete @Pkg::{...}` は受け付けますがシンボルには反映されません。
`$::name` / `@::name` は `$main::name` と同じです (文字列の中でも展開されます)。
`use strict` の下でも、大文字だけの bareword (`FH`、`LOG2`) はファイルハンドルの名前として通ります
(perl はハンドルを書ける位置でだけ通すので、打ち間違えた定数名がコンパイルエラーになりません)。

**型グロブへの代入**に対応しています: `*name = \&func;` / `*name = sub {...};` (サブの定義)、
`*name = \@array;` / `\%hash` / `\$scalar` (変数の別名)、`*a = *b;` (4 種類すべての別名)。
名前は `*Pkg::name`、`*{"${class}::$name"}`、`*$name` の形でも書けます。
`BEGIN { *DEBUG = sub () { 0 } }` のように `BEGIN` で定義したものは定数として使えます。
`}` の直後の `*` は行頭にあるときだけグロブとして読みます (`$h{w} *$x` を乗算として読むため)。

**シンボリックリファレンス** (`&{"name"}`、`\&{"Pkg::name"}`、`@{"Pkg::ISA"}`、`${"name"}`、`%{"name"}`、
`defined &{"name"}`) は `no strict 'refs'` の範囲で使えます。`use strict` の下では perl と同じ
メッセージ (`Can't use string ("x") as an ARRAY ref while "strict refs" in use`) で die します。

**`goto &sub`** (`goto &name`、`goto &$code`、`goto &{...}`、`goto \&name`) は、現在の `@_` を渡して
別のサブに移ります。移った先のサブから見た `caller` は、perl と同じく元のサブの呼び出し元です。
内部的には `return &sub;` として動くので、スタックは 1 段消費します (深い末尾再帰には使えません)。

**複数行にまたがる文の行番号**: 文の中の呼び出しは、perl と同じく文が始まる行を報告します (`die`、`warn`、`caller`)。
文の中に無名サブや `map` / `grep` のブロックがあるときは、そのブロックが閉じる行になります (perl の動作に寄せた近似で、
ブロックの後ろに引数が続く形などでは 1 行ずれることがあります)。

**`#line 28`** (行頭のコメントによる行番号の指定) に対応しています。その次の行が 28 行目として数えられ、`__LINE__`、`die` / `warn` のメッセージ、
`caller` に反映されます。ファイル名の指定 (`#line 28 "name"`) は行番号だけ使います。

**`caller`** に対応しています: `caller` / `caller()` は呼び出し元の (パッケージ, ファイル, 行)、
`caller(N)` は N 段上のフレームでさらに (サブ名, hasargs, wantarray) を返します。スカラーコンテキストでは
パッケージ名、呼び出し元がなければ空リスト / undef です。`package DB` の中から `caller(N)` を呼ぶと、その呼び出しの引数が `@DB::args` に入ります
(本物の Carp がスタックトレースに引数を出すのに使います。`shift` で取り出した分は含まれません)。制限: `caller(N)` の 7 番目以降の要素はなく、
`eval { }`、`eval` 文字列、`require` は perl と同じく 1 段として数えます (サブ名は `(eval)`)。ファイル名は実行中のファイル (サブが定義されたファイルではない) です。

**ファイルテスト** `-e` `-f` `-d` `-s` `-z` `-r` `-w` `-x` `-l` `-T` `-B` (と、答えが決まっている `-R -W -X -o -O -p -S -b -c -u -g -k`) に対応しています (perl と同じく真は `1`、
偽は空文字列、存在しないパスは undef、`-s` はサイズ)。対象はスカラー変数か文字列リテラルで始まる式に限ります
(`-e $path`、`-d "$dir/sub"`、`-f _`。`-e` 単独やファイルハンドルは未対応)。
`-M` `-A` `-C` (更新、アクセス、変更からの日数。プログラムの開始時刻 `$^T` が基準) はホスト版で使えます
(ボードにはファイルの時刻が無いので 0 を返します)。

**括弧なしの呼び出し**: 宣言済みのサブ (取り込んだものを含む) は、最初の引数が `[ ... ]` や `{ ... }` でも括弧なしで呼べます
(`is_deeply [sort @got], [1, 2, 3], 'name';`)。空のプロトタイプのサブ (`sub pi () { ... }`) は、モジュールから取り込んだものも
引数を取らない定数として読みます (`pi/2`、`tau + 1`)。繰り返しの `x` は回数とくっつけて書けます (`"\0"x256`)。
正規表現の `/a` `/u` `/l` `/p` は受け付けて無視します (文字クラスはもともと ASCII の範囲だけです)。
ボードでは PICOPERL ドライブ (FAT) を
見るので権限の区別はなく、読み取りは常に真、書き込みは読み取り専用属性がなければ真、`-x` はディレクトリだけ真です。
`-t` だけはハンドルを取ります (`-t STDIN`、`-t $fh`): 標準ハンドルは端末なら真 (ボードでは常に真)、ファイルは偽です。

`shift->[0]`、`shift->{key}`、`shift->method(...)` のように、引数なしの `shift` / `pop` に `->` を続けられます。

**`%ENV`** はホスト版ではプロセスの環境変数のコピーです (書き換えても実際の環境は変わりません)。
ボードでは空です。**`local`** は変数だけでなく、要素とスライスにも使えます
(`local $h{k} = 1;`、`local $a[0];`、`local @ENV{qw(PATH HOME)};`、`local $r->{k};`)。
括弧のリストにも混ぜて書けます (`local($@, $!, $SIG{__DIE__});`、`local($x, $h{k}) = (1, 2);`)。
`local @a;` / `local %h;` / `local %h = %h;` は、スコープを抜けると元の中身に戻ります。

**`$!`** は、文字列として読むとエラーメッセージ、数値演算や数値比較の相手にするとエラー番号になります
(`open(...) or die "$!"`、`$! + 0`、`$! == 2`、`$! = 0`、`local $!`)。perl のような「両方の値を持つ変数」
ではないので、`my $e = $!;` で写した先はメッセージの文字列だけになります (`$! = $e` で戻すことはできます)。
`sprintf("%d", $!)` や `int($!)` のように関数に渡した場合も文字列として渡ります。`%!` はありません。

`print` は **`$,`** (引数の間に入れる文字列) と **`$\`** (最後に付ける文字列) に従います。`say` は `$\` の代わりに改行を付けます。

**`${^NAME}`** 形式の変数と **`%^H`** を読み書きできます。値が入っているのは `${^TAINT}` (0)、
`${^UNICODE}` (0)、`${^GLOBAL_PHASE}` (`RUN`、`END` ブロックの中では `END`) だけで、
`${^WARNING_BITS}` などは undef です。`%^H` に書いてもレキシカルなヒントとしては働きません。

`BEGIN { ... }` の中から、ファイルの `my` 変数を読み書きできます (`my $x; BEGIN { $x = init(); }`)。
`BEGIN` の時点では後ろの `= 初期値` はまだ実行されていないので、初期値付きで宣言した変数は実行時に
その値で上書きされます (perl と同じ)。外側のブロックやサブの `my` 変数は、まだ存在しないので見えません。

`BEGIN { ... }`、`use`、`use constant NAME => helper();` のようにパース中に実行されるコードから、
その行より上で定義した名前付きサブを呼べます (perl と同じ)。その時点ではファイルの `my` 変数には
まだ値が入っていません。

ブロックの `my` 変数を使う名前付きサブ (`{ my $n = 0; sub next_id { ++$n } }`) は、そのブロックが
実行されたときに定義されます。ブロックより前で呼ぶと `Undefined subroutine` になり、ループの中に
書くと繰り返しのたびに定義し直されます (perl は最初の 1 回の変数を使い続けます)。

**`FindBin`** の picoperl 版 (`lib/FindBin.pm`): `use FindBin qw($Bin $Script);` で、実行中のスクリプトがあるディレクトリ (絶対パス) と
ファイル名が取れます。シンボリックリンクはたどらないので `$RealBin` は `$Bin` と同じです。

**`B`** の picoperl 版 (`lib/B.pm`): 本物は perl の内部を覗く XS ですが、サブについて調べる部分だけを用意しています。
`B::svref_2object(\&f)` から、サブの名前 (`->GV->NAME`)、パッケージ (`->GV->STASH->NAME`)、定義されたファイル (`->FILE`)、
最初と最後の文の行番号 (`->START` から `->line` / `->next`) が取れます。`B::perlstring` / `B::cstring` もあります。
Test2 や autodie がサブの素性を知るのに使う範囲で、構文木をたどる機能はありません。

**`Storable`** の picoperl 版 (`lib/Storable.pm`): `dclone` (データ構造の深いコピー) だけを用意しています。ハッシュ・配列・スカラーへの
参照をたどって複製し、bless されたものは同じクラスになります。同じものを 2 か所から指す構造や循環参照も、複製の中で同じ形になります。
ファイルやバイト列への保存 (`store` / `retrieve` / `freeze` / `thaw`) は未対応で、呼ぶとその旨のエラーになります。

**`Errno`** の picoperl 版 (`lib/Errno.pm`): エラー番号の定数 (`ENOENT`、`EEXIST` など約 130 個、Linux の値) と `%!` です。
`use Errno qw(ENOENT);` のあと `$! == ENOENT`、`use Errno;` のあと `$!{ENOENT}` と書けます。Fcntl と同じく、定数は使われたものだけ作られます。
perl との違い: `%!` は `use Errno` (または `require Errno`) したあとでだけ使えます (perl は `%!` を書くと自動で読み込みます)。

**`umask`** は値を覚えるだけです (既定 022。ファイルのパーミッションには効きません)。`$(` と `$)` (グループ ID) は常に `"0 0"` です。

**`times`** は (起動からの秒数, 0, 0, 0) を返します (プロセスごとの CPU 時間の区別はありません)。

**`Fcntl`** の picoperl 版 (`lib/Fcntl.pm`) は定数だけを返します (`O_RDONLY`、`LOCK_EX`、`SEEK_SET`、`S_IFMT`、`S_ISDIR($mode)` など。値は Linux のもの)。
`seek` の `SEEK_*` は実際に使えますが、ロックやパーミッションの定数は番号として使えるだけです。
約 100 個の定数はインタプリタが ROM に表を持っていて、スクリプトに書いた定数だけが作られます (`use Fcntl;` だけなら RAM はほとんど使いません)。
perl との違い: `use Fcntl qw(:seek)` のようにタグを指定しても、ほかの定数 (`O_RDONLY` など) も使える状態になります。

**`List::Util`**、**`Scalar::Util`**、**`mro`** の picoperl 版を `lib/` に同梱しています (本家は XS なので
読み込めないため)。`lib/` は perl 本体のライブラリより先に探すので、`use List::Util qw(sum max first);` と
書けばこちらが使われます。

- `List::Util`: `sum sum0 product min max minstr maxstr first any all none notall reduce reductions
  shuffle uniq uniqnum uniqint uniqstr pairs unpairs pairkeys pairvalues pairmap pairgrep pairfirst
  head tail zip mesh` (`_longest` / `_shortest` 付き)
- `Scalar::Util`: `blessed reftype refaddr looks_like_number` は本家と同じ。`weaken` は何もしません
  (参照カウントだけで弱参照が無いので、循環参照は解放されません)。`readonly` `tainted` `isweak` は常に偽、
  `dualvar` は die します
- `mro`: `get_linear_isa` など。メソッド探索は常に深さ優先で、`use mro 'c3'` は受け付けるだけです

- `Config`: 本家は tie したハッシュですが、picoperl 版は普通のハッシュです。`osname` `archname` `version`
  `path_sep` `ivsize` などスクリプトがよく調べるキーだけを持ちます。C コンパイラ、動的ロード、fork、
  スレッドのキー (`d_fork`、`useithreads` など) は入れていないので、本家の「無い」と同じ undef になります

- `Carp`: `croak` `carp` `confess` `cluck` `shortmess` `longmess`。`croak` / `carp` は本家と同じく、
  呼んだパッケージの外で最初の呼び出し元の位置を付けます。`confess` などの呼び出し履歴に引数は出ません
  (`main::f() called at ...`)。`@CARP_NOT` は見ません

- `Cwd`: `cwd` `getcwd` `abs_path` `realpath`。ホスト版では OS に問い合わせます (シンボリックリンクも解決)。
  ボードにはカレントディレクトリが無いので `cwd` は常に `/` で、`abs_path` は `.` と `..` を畳むだけです。
  `use Cwd 'chdir'` の `chdir` は組み込みの `chdir` を呼んで `$ENV{PWD}` を合わせます (ボードでは常に失敗)

- `File::Spec` / `File::Spec::Functions`: `catfile` `catdir` `canonpath` `splitpath` `splitdir` `catpath`
  `rel2abs` `abs2rel` `file_name_is_absolute` `no_upwards` `tmpdir` `path` など。本家は OS ごとの実装に
  切り替えますが、picoperl 版は Unix 形式 (`/` 区切り) だけです。ボードのドライブのパスにもそのまま使えます

`use Module LIST;` は、モジュールが `import` を**継承している**場合もそれを呼びます
(`use Test::Simple tests => 5;` は親クラス `Test::Builder::Module` の `import` を実行します)。
継承元が `Exporter` の場合は、組み込みのエクスポート処理を使います。
`$obj->Pkg::method(...)` のように、探し始めるパッケージを指定したメソッド呼び出しも書けます。
`main->method` / `main->can(...)`、文字列リテラルをクラス名にした `"Class"->method(...)` も書けます。
`use Module ();` / `use Module qw();` は読み込むだけで、`import` を呼びません。
`UNIVERSAL::isa($x, 'ARRAY')` と `UNIVERSAL::can($x, 'method')` を関数として呼べます
(オブジェクト、クラス名、bless していないリファレンスのどれにも使えます)。

- `Exporter`: `use Module;` のエクスポートはインタプリタが直接行うので、普段は読み込まれません。
  `require Exporter; our @ISA = ('Exporter');` という昔ながらの書き方や、`Module->import(...)` /
  `export_to_level` を自分で呼ぶコードのために置いてあります (`@EXPORT` `@EXPORT_OK` `%EXPORT_TAGS` に対応)

`main` 以外のパッケージで `use Module qw($x %h);` と変数を取り込んだ場合、そのパッケージの中の `$x` / `%h` は
取り込んだ変数を指します (それ以外の、宣言していないグローバルは従来どおり `main` のものです)。

**`(&...)` で始まるプロトタイプ**を持つサブには、`first { $_ > 1 } @list` のようにブロックを渡せます
(`sub apply(&@) { my $f = shift; ... }`)。ブロックは無名サブとして渡されます。

bless したリファレンスを文字列にすると、perl と同じ `Class=HASH(0x...)` の形になります。

`substr($s, OFF, LEN) = VALUE;` と書けます (4 引数の `substr` と同じ動作)。`substr(...) =~ s/a/b/`、`=~ tr/a-z/A-Z/`、
`.=` などの複合代入も対象の部分だけを書き換えます (`\substr(...)`、`for (substr(...))` での書き換え、サブの引数経由の書き換えは未対応)。ヒアドキュメントの区切りは、引用符で囲めば空白や記号を含められます (`<<"    EOT"`)。

**`eval STRING`** に対応しています。文字列は実行時にパースされ、`eval` を書いた場所から見える `my` 変数を
読み書きできます (`my $x = 1; eval '$x++';`)。パッケージと `use strict` / `use warnings` も引き継ぎます。
構文エラーを含むエラーは `$@` に入り、`eval` は undef を返します。文字列の中の `return` は `eval` を終わらせます。
エラーメッセージのファイル名は `(eval N)` です。

- 文字列の中で定義したサブやクロージャは残ります。それ以外の AST は実行後に解放するので、
  ループの中で `eval` を繰り返してもメモリは増え続けません
- 文字列の中の `BEGIN` ブロックからは、外側の `my` 変数は見えません
- ボード上では、パースのたびにヒープと C スタックを使います。長い文字列や深い入れ子は
  `source nesting too deep` やメモリ不足で die します (クラッシュはしません)

**`INIT { ... }`** / **`CHECK { ... }`** は、プログラムの最初の文より前に実行されます (CHECK はコンパイルした
逆順、INIT はコンパイルした順。`use` で読み込んだモジュールのものも含む)。**`UNITCHECK`** はそのファイルの
実行開始時です。perl と同じく、プログラムの実行が始まった後に `require` したファイルの INIT は実行されません。
ブロックの中や `eval STRING` の中に書いた INIT / CHECK も実行されません。

`utf8::unicode_to_native` / `native_to_unicode` (そのまま返す)、`utf8::is_utf8` (常に偽)、
`utf8::encode` / `decode` / `upgrade` / `downgrade` / `valid` (何もせず真) を `use utf8` なしで呼べます。

`require 5.006;` / `require v5.10.1;` は最低バージョンの宣言として扱います (ファイルは探しません)。
5.44.0 以下なら何もせず、それより新しいバージョンは perl と同じメッセージで die します。

**`goto LABEL`** に対応しています。ラベルは、いま実行中の文の並び (同じブロック、外側のブロック、
呼び出し元) から探します。前方にも後方にも飛べ、入れ子のブロックやループの中から外へ出られます。
ループに付けたラベル (`OUTER: for ...`) にも飛べます。見つからなければ `Can't find label X` で die します。
制限: ブロックやループの**中**にあるラベルへ、外から飛び込むことはできません。`goto EXPR`
(ラベル名を式で作る形) は未対応です。

文字列の中で **`\U` `\L` `\Q`** (`\E` まで、または最後まで) と **`\u` `\l`** (次の 1 文字) が使えます
(`"\u\L$name\E"`、`"_IS_\U$os"`)。ヒアドキュメントでも同じです。正規表現リテラルの中では `\Q...\E` だけです。

比較、照合、`!` が返す「偽」は空文字列ですが、perl と同じく警告なしで数値の 0 としても使えます
(`$count += ($x == $y);`)。文字列として書いた `""` を数値に使うと、従来どおり警告が出ます。

`s///e` の置換式には複数の文を書けます (`s{\t}{ my $n = ...; " " x $n }ge`)。置換式の中の `pos()` は、
いまの一致の開始位置です。

`die` / `warn` / 実行時エラー / `caller` が報告するファイル名は、実行中のサブが書かれているファイルです
(これまでは、別のファイルのサブの中でも呼び出し元のファイル名に、そのサブの行番号を付けて報告していました)。

**`use lib 'dir';`** と **`@INC`** / **`%INC`** に対応しています。`require` / `use` は、まず `@INC` の
ディレクトリを順に探し、見つからなければ従来の場所 (スクリプトのディレクトリ、`lib/` など) を探します。
読み込んだファイルは `%INC` に入ります (`strict` など組み込みで処理するものは入りません)。

括弧で囲んだ式に `->` を続けられます (`(sub { ... })->(1)`、`($x || $y)->method`、`(LIST)[0]->()`、
`do { ... }->()`)。`Class::->method` のように末尾に `::` を付けたクラス名 (文字列 `"Class"` と同じ) と、
`::name()` / `&::name` (`main::name` と同じ) も書けます。

**`pack`** / **`unpack`**: `a A Z` (文字列)、`b B h H` (ビット列・16 進)、`c C s S l L q Q j J i I` (整数)、
`n N v V` (バイト順固定)、`f d F` (浮動小数点)、`U W` (文字コード)、`w` (BER 圧縮整数)、`x X @` (位置)。
回数 (`C3`、`a*`、`C[2]`)、修飾子 (`<` `>` `!`)、グループ (`(C n)2`、`(a2 C)*`)、長さ付き文字列 (`n/a*`)、
テンプレート中の空白と `#` コメントに対応しています。perl との違い: 文字列がバイト列なので `U` は UTF-8 の
バイト列を作り / 読みます (`pack("U", 0x3042)` は `"あ"` の 3 バイト)。`l!` `L!` の幅は処理系の `long`
(ホスト版 8 バイト、ボード 4 バイト) です。`unpack` のチェックサム (`%32C*`: 値の合計を指定のビット数に収める。`b` `B` は立っているビットの数) に対応しています。
`p P u` は未対応です。

**`vec`**: `vec($s, $offset, $bits)` (1 2 4 8 16 32 64 ビット) の読み出しと、`vec($s, $offset, $bits) = $value;`
の代入。代入は `=` だけで、`|=` などの複合代入や `\vec(...)` は使えません。

**ファイルとディレクトリの操作**: `mkdir` `rmdir` `unlink` `rename`、`opendir` `readdir` `rewinddir` `closedir`
(`opendir(my $dh, $path)` と bareword の `opendir(DH, $path)` の両方)。失敗すると偽を返して `$!` に理由が入ります。
ホスト版とボード (PICOPERL ドライブ) の両方で使えます。ブラウザ版 (wasm) には無く、呼ぶと `$!` が
`Function not implemented` になります。ディレクトリは開いた時点で全部読むので、開いた後の変更は `readdir` に
反映されません。ディレクトリハンドルの実体は配列なので `ref($dh)` は `ARRAY` です。ボードでは `.` と `..` も返します
(FAT のルートには無いので補っています)。スクリプトがドライブを書き換えても、PC 側には取り外して付け直すまで
見えないことがあります。

ホスト版だけ: `stat` `lstat` (13 要素)、`chmod` `chown` `utime`、`symlink` `link` `readlink`、`$<` `$>`。

ファイルテストと `stat` では **`_`** (直前に調べたファイル) が使えます (`-f $path && -r _`、`stat(_)`)。
perl と違って結果を保存しているわけではなく、同じパスをもう一度調べます。

**`localtime`** / **`gmtime`** が使えます (リストコンテキストで 9 要素、スカラーコンテキストで
`"Thu Nov 19 12:34:56 2026"` の形)。ボードにはタイムゾーンが無いので `localtime` は `gmtime` と同じ結果です。
ボードには時計も無いので、引数なしの値は `time` と同じく起動からの秒数を 1970 年からの秒数として読んだものになります。

**`$$`** はホスト版ではプロセス番号、ボードでは 1 です。`last LABEL` / `next LABEL` で、実行中のループに
無いラベルを指定すると `Label not found for "last LABEL"` で die します (これまでは黙ってプログラムが終わっていました)。
`getpwnam` `getpwuid` `getgrnam` `getgrgid` はホスト版だけにあります。

`print STDERR (LIST);` / `printf STDOUT (...)` のように、ファイルハンドルの後ろのリストを括弧で囲めます。

**`chdir`** はホスト版ではカレントディレクトリを変えます。ボードにはカレントディレクトリが無いので、
常に失敗します (偽を返す。`chdir` を含むスクリプトは読み込めます)。

**`END { ... }`** はプログラムの終了時 (`die` や `exit` の後も) に、コンパイルされた順の逆で実行されます (モジュールの `END` も含めて)。
ブロックの中では終了コードが **`$?`** に入っていて、書き換えると終了コードが変わります。
perl と違い、登録されるのは `END` の文に実行が到達したときです (到達する前に `exit` すると、その `END` は実行されません)。

**`Test::More`** の小さな実装が `test/lib/Test/More.pm` にあります (本家は依存が深くて読み込めないため)。
ホスト版のテスト用で、uf2 には入りません。ボードで使うときはドライブの `lib/Test/More.pm` にコピーしてください。
`ok is isnt like unlike cmp_ok is_deeply pass fail can_ok isa_ok new_ok use_ok require_ok
diag note explain plan done_testing skip todo_skip subtest BAIL_OUT` と `$TODO` が使え、
TAP の出力、失敗時の診断、終了コードは本家と同じです。本家との違い: `use_ok` は `@EXPORT` の名前だけを
取り込む、`explain` は簡易表示、`Test::More->builder` (Test::Builder のオブジェクト) は無い。

`=~` / `!~` の直後なら、区切り文字がカンマの `s,a,b,` / `m,a,` / `tr,a,b,` も書けます
(それ以外の位置の `s,` は、従来どおり識別子 `s` とカンマとして読みます)。

特殊変数 **`$]`** は `"5.044000"` (picoperl が追従している perl のバージョン)、**`$^O`** は
PC のホスト版ではその OS 名 (`linux` など)、ボードとブラウザ版では `picoperl` を返します。
`$^W` など他の `$^X` 形式の変数は普通のグローバル変数として読み書きできるだけで、効果はありません。

**`use v5.36;` のようなバージョン宣言**も受理します: picoperl の機能は常時すべて
有効で、perl と同じく v5.12 以上の宣言で `strict`、v5.36 以上で `warnings` が
そのスコープに有効になります。

名前解決は Perl と同じで、修飾なしで呼べるのは自パッケージのサブと `use` で import したものだけです。
`use` / `use constant` / `BEGIN` はパース時に実行されるので、import したサブは組み込み関数を
上書きできます (`use Time::HiRes qw(time)`)。`our` 変数はパッケージごとに分かれます (`$Foo::x`)。

**`use strict`** は `vars` (未宣言変数はコンパイルエラー) と `subs` (bareword の禁止) を Perl と同じ
メッセージで検査します (`no strict`、`use strict 'vars'` も可)。レキシカル変数はパース時に
(深さ, スロット) へ解決されるので、実行時に名前検索は行いません。
**`use warnings`** は主要な実行時警告を出します: `Use of uninitialized value $x in addition (+)`、
`Argument "abc" isn't numeric in ...`、`Odd number of elements in hash assignment` / `anonymous hash`
(`no warnings` はブロック単位で有効)。コンパイル時警告 (`Useless use of ...` など) はありません。

**サブルーチンシグネチャ** (`sub f($x, $y = 10, @rest) { ... }`) に対応しています:
必須パラメータ、デフォルト付き (`=` / `//=` / `||=`、デフォルト式は先行パラメータを参照可)、
末尾の `@array` / `%hash` (奇数個なら perl と同じく die)。引数の数が合わないと perl と
同じメッセージで die します (`Too few arguments for subroutine 'main::f' (got 1; expected 2)`)。
5.44 の**名前付きパラメータ** (`sub f($x, :$width = 10, :$label)` — `:$name` は必須、
`= default` で省略可、末尾 `%rest` が未知のキーを回収。Missing/Unrecognized/Odd の
エラーメッセージも perl と同一) にも対応しています。
`sub f($$)` / `sub f(&$)` / `sub f(\@\@)` のような識別子を含まない括弧はプロトタイプとして読み飛ばします
(`$` の位置の引数はスカラーコンテキストで評価します: `is(grep(...), 1)` は個数を比べます。サブの定義より後ろの呼び出しと、
`use` で取り込んだサブが対象です。`\@` の自動リファレンス化は未対応)。
名前のない引数 (`sub f($x, $)`) も書けます。`sub f() {}` の空括弧は
引数チェックをしません (perl のシグネチャは 0 個を強制する点が違います)。

perl と挙動が違う箇所は、スクリプト実行ごとに初回だけ `picoperl:` で始まる注意を stderr に出します:
`use utf8` (無視される、文字列はバイト列)、`map`/`grep` 内での `$_` の変更 (元のリストに反映されない)、
`foreach` 変数の書き戻し (変数・要素・配列を並べたリストなら反映。関数の戻り値などを含むと、それより後ろは反映されない)。比較演算子は perl と同じく非結合で、
`0 < $x < 3` は構文エラーになります。

**ビット演算と `use integer`**: `& | ^ ~ << >>` は perl と同じく符号なし 64 ビットとして計算します (`-1 >> 60` は 15、
`~0 >> 1` は 9223372036854775807)。picoperl の整数は符号付き 64 ビットなので、最上位ビットが立った結果は
ビット列をそのまま保った負の整数になります (`~5` は -6。`~5 & 0xFF` は 250 で正しい)。ただし全ビットが 1 の結果
(`~0`) だけは perl と同じ 18446744073709551615 という数になり、大小比較・`%`・`printf "%x"`・表示が perl と一致します。
`use integer` / `no integer` はブロック単位で効き、その中では `+ - * / % << >> & | ^ ~` と数値の比較が符号付き整数の演算になります
(`7 / 2` は 3、`-7 % 3` は -1、`~0` は -1、`-8 >> 1` は -4)。これで `Math::BigInt` が動きます (ホスト版で perl 本体のライブラリを使う場合)。

**DESTROY**: bless されたオブジェクトへの最後の参照が消えると、そのクラスの `DESTROY` メソッドが呼ばれます
(スコープを抜ける、`undef` する、別の値を代入する、配列やハッシュから消す、など)。呼ばれるのは**その文が終わった時点**です。
プログラムの終わりには、ファイルスコープの `my` 変数が持っているオブジェクトの `DESTROY` が END ブロックの前に呼ばれます
(END ブロックが使っている変数は、END が終わるまで中身を保ち、その後で `DESTROY` が呼ばれます)。
`DESTROY` の中の die は警告になります。`DESTROY` を定義したクラスが 1 つも無ければ、速度への影響はありません。perl との違い:
パッケージ変数 (`our`) が持っているオブジェクトは、プログラム終了時に `DESTROY` が呼ばれない。循環参照は解放されないので
`DESTROY` も呼ばれない (弱参照は未対応)。ボードでスクリプトが再読み込みされるときは、待ち行列に残っていた分は呼ばれない。

**演算子オーバーロード** (`use overload`、`lib/overload.pm`) に対応しています: 算術 (`+ - * / % **`)、ビット (`<< >> & | ^`)、
比較 (`== != < > <= >= <=>`、`eq ne lt gt le ge cmp`)、`.` と `x`、それぞれの代入形 (`+=` など)、変換 (`""` `0+` `bool`)、
`neg` `!` `abs` `++` `--`、関数 `abs int sqrt sin cos exp log`。ハンドラは `handler($a, $b, $swapped)` で呼ばれ、継承もされます。
値にはコード参照のほかメソッド名の文字列も書けます。`overload::StrVal` `overload::Overloaded` `overload::Method` もあります。
オーバーロードを使わないスクリプトの速度は変わりません (演算の対象が bless された参照のときだけ調べます)。perl との違い:
`fallback` の値は見ず、常に「代用できるものは代用する」として扱う (比較は `<=>` / `cmp` から、足りない演算は `0+` や `""` で
変換してから計算。perl が `no method found` で die する場面でも計算される)。`=` (コピーコンストラクタ) は呼ばないので、
`++` や `+=` のハンドラがオブジェクトを書き換えるクラスでは、代入しただけの別の変数も一緒に変わる。
参照外し (`@{}` `%{}` `&{}` `${}`)、`<>`、`-X`、`~~`、`qr`、`nomethod`、`atan2`、`overload::constant` は未対応。

**tie** (`tie` / `tied` / `untie`) はスカラー・配列・ハッシュ・ファイルハンドルに対応しています。
tie した変数の読み書きは `FETCH` / `STORE` (ハッシュは `EXISTS` `DELETE` `CLEAR` `FIRSTKEY` `NEXTKEY`、
配列は `FETCHSIZE`、ハンドルは `PRINT` `PRINTF` `READLINE` `CLOSE` `EOF` `BINMODE` `FILENO`) の呼び出しになります。
tie していない変数の速度と RAM は変わりません (変数ごとのフラグを 1 バイト見るだけ)。perl との違い:
`keys` やリストとしての展開など全体を読む操作は、その都度 `FIRSTKEY` / `NEXTKEY` / `FETCH` (配列は `FETCHSIZE` / `FETCH`) で
中身を取り直してから行う。`push` `pop` `shift` `unshift` `splice` と全体への代入は `PUSH` などではなく `CLEAR` と `STORE` で書き戻す。
`STORE` は代入の直後か、遅くとも文の終わりに呼ばれる。`local` した tie 変数、要素の tie (`tie $h{x}, ...`)、
`UNTIE` / `DESTROY` の呼び出し、`select` での出力先の切り替えは未対応。`Tie::Hash` などの基底クラスは `lib/` に入れていません。

**正規表現** は独自のバックトラックエンジン (`src/pp_regex.c`) で動きます:
`m//` (`//`)、`s///`、`tr///` (`y///`)、`qr//`、`=~` / `!~`、`split /re/`、修飾子 `i m s x g e r` (`tr` は `c d s r`)、
`$1`.. と `$&`、名前付きキャプチャ `(?<name>...)` と `%+`、`@-` / `@+` / `$+`、
リストコンテキストでのキャプチャ取得、`/g` の `pos()`、パターン先頭の `\G` と `/gc` (字句解析のループ `if ($s =~ /\G\s+/gc) {...} elsif ...` が書けます。`\G` は先頭にあるときだけ有効)、`\Q..\E` と変数の補間 (`/x` のコメントの中は補間しない)。
パターン側は文字クラス (`[a-z]`, `[^..]`, `\d \w \s`, `[[:alpha:]]`, Unicode プロパティ `\pL` `\p{Lower}` `\PM` `\p{^Space}` — ASCII の範囲だけ)、`.`、アンカー (`^ $ \A \z \Z \b \B`)、
グループ (`( )`, `(?: )`, 先読み `(?= )` `(?! )`, 後読み `(?<= )` `(?<! )`, アトミック `(?> )`, `(?i)`, `(?^i: )`。文字セット系のフラグ `a u l d` と `p` は受け付けて無視)、`|`、量指定子 (`* + ? {n,m}` と非貪欲 `?`)、後方参照 `\1` に対応します。
制限: 文字列はバイト列として扱う (UTF-8 の 1 文字に `.` は一致しない)、後読みの中身は perl と同じく長さに上限が必要 (255 バイトまで。`(?<=a+)` は不可)、
`$1` / `$&` / `%+` / `@-` / `@+` はサブごとに復元されます (サブの中のマッチは、呼び出し側の `$1` を変えません)。
サブ以外のブロック (`{ ... }`、`if`、ループ、`eval`) を抜けたときには復元されません。
極端なバックトラック (`(a+)+b` のような) は途中で `Regexp backtracking limit exceeded` で die します。

**ファイル I/O**: 3 引数 `open` (2 引数の `open(FH, "<$path")` も可) + レキシカルハンドルの最小セットに対応しています:
**`<>`** は `@ARGV` に並んだファイルを順に読みます (読んでいるファイル名は `$ARGV`。`@ARGV` が空なら STDIN)。
`<Pkg::FH>` のようにパッケージ名を付けたハンドルも読めます。`<STDIN>` もリストコンテキストでは全行を返します。

`open(my $fh, '<', $path)` (`> >> +< +>`)、`<$fh>` (スカラーで 1 行、リストで全行、`while (<$fh>)` は
perl と同じく `$_` に読み込む)、`print/printf/say $fh ...` と `print {$fh} ...`、`close`、`eof`、
`seek($fh, $off, 0/1/2)`、`tell`。失敗時は `$!` にメッセージが入ります。ボードではパスは
PICOPERL ドライブ上 (`main.pl`、`lib/...`)。書き込みはフラッシュ消去を伴うので低頻度の用途向きです。
未対応: パイプを開く `open` (`"cmd |"`)・IO レイヤー (`:utf8` など)・STDIN に対する `$/` の変更・
警告や die の `, <$fh> line N` 表記 — IO レイヤーと STDIN での `$/` は初回に `picoperl:` 警告を出します。
ファイルの読み込みでは `$/` が効きます (`undef` で残り全部、任意の文字列を区切りに、`""` で段落ごと)。
`read(FH, $buf, LEN, OFFSET)` / `sysread` / `getc` でバイト単位に読めます。
`open(my $fh, ...)` のハンドルは `*$fh` でグロブとして使えます: `${*$fh}`、`%{*$fh}` (`${*$fh}{key}`、`*$fh->{key}`)、`@{*$fh}` に
値を持たせられます (File::Temp や IO::* がオブジェクトのデータ置き場に使う形)。最初に `*$fh` と書いたときにだけ
グロブに作り替えるので、使わないハンドルは軽いままです。最後の `$fh` が消えると、ファイルを閉じて中身も片付けます。

`sysopen(FH, $path, O_RDWR | O_CREAT | O_EXCL, $perms)` は、`O_*` のフラグを `open` のモードに読み替えて開きます
(`O_CREAT | O_EXCL` は既にあると失敗、`O_CREAT` なしは無いと失敗)。パーミッションの指定は、ファイルを新しく作るときに `umask` を引いて付けます (ホスト版のみ。ボードでは効きません)。
メインスクリプトの `__END__` / `__DATA__` より後ろと、モジュールの `__DATA__` より後ろは `<DATA>` で読めます。
`open(my $fh, '<', \$text)` / `'>'` / `'>>'` で、スカラー変数を読み書きするメモリ上のファイルを開けます
(ハッシュや配列の要素への参照 `\$h{k}` も書き込み先にできます)。
**`$.`** は最後に読んだハンドルの行番号です (`close` で 0 に戻ります。文字列の中でも展開されます)。
`${^LAST_FH}` は常に undef です。

**ファイルハンドルのメソッド呼び出し** (`$fh->print(...)`、`STDOUT->autoflush(1)`、`$fh->getline`): perl と同じく `IO::File` のメソッドを
呼びます。`IO::File` / `IO::Handle` は同梱していないので、perl のライブラリが見える環境 (ホスト版) で使えます。ボードでは
ドライブの `lib/IO/` に置かない限り、今までどおり `Can't call method ... on unblessed reference` になります。
同梱の `lib/IO.pm` は、本物の `IO` が XS で提供している部分 (`flush`、`getline`、`blocking`、`getpos` など) の代わりです。

**組み込み関数と同じ名前のサブ**: パッケージが自分で定義した `sub close`、`sub open`、`sub fileno` などは、perl と同じく組み込み関数を
上書きしません (その中で書いた `close($fh)` は組み込みの close です)。上書きするのは、ほかのモジュールから取り込んだサブだけです
(`use Time::HiRes qw(time)` など)。以前は自分で定義したサブも上書きしていました。

**`$"`** (配列を文字列に埋め込むときの区切り): `local $" = ', '; print "f(@args)";` のように変えられます (既定は空白 1 つ)。

**`CORE::name`**: `CORE::open`、`CORE::caller`、`CORE::stat`、`CORE::wantarray` など、同じ名前のサブを定義しているパッケージの中からでも
組み込み関数そのものを呼べます。

**`prototype`**: `prototype(\&f)`、`prototype("name")` は、サブに書かれたプロトタイプの文字列を返します (無ければ undef)。
`prototype("CORE::open")` のように組み込み関数の名前を渡すと、perl と同じプロトタイプを返します (`print` や `system` のように
perl が持たないものは undef)。`Scalar::Util::set_prototype` で、コード参照ごとに付け替えられます。

**`AUTOLOAD`**: パッケージに `sub AUTOLOAD` があると、そのパッケージの未定義のサブ・メソッドの呼び出しを受け取ります
(`our $AUTOLOAD` に呼ばれた名前が入ります。メソッドは `@ISA` をたどって探します。`DESTROY` は渡しません)。
`defined &name` や `can` は、perl と同じく AUTOLOAD があっても偽のままです。

**名前で指した変数への参照** `\${"Pkg::name"}` / `\$$name` はパッケージ変数そのものを指し、`\$$ref` は `$ref` と同じ参照です
(`tie ${"Pkg::name"}, ...` でその変数に tie できます。`Env` がこの形を使います)。

**要素への参照** `\$h{k}` / `\$a[i]` は、その要素を読み書きします (`my $r = \$opt{name}; $$r = "x";` で `$opt{name}` が変わります)。
perl との違い: 同じ要素への参照を 2 回作ると別々の参照になります (`\$h{k} == \$h{k}` は偽)。参照を作ったあとで要素を
`delete` すると、参照の先は undef になります (perl では値が残ります)。tie した配列・ハッシュの要素への参照は、値のコピーを指します。

**`@_` 経由の書き換え**: `sub trim { $_[0] =~ s/^\s+// }` のように `$_[N]` を書き換えるサブ (代入、`s///`、`tr`、`++`、`chomp`、
`for (@_)`) は、呼び出し元の変数に反映されます。perl と違って `@_` はコピーなので、サブから戻った時点で書き戻します
(`die` で抜けた場合も書き戻します)。反映される引数は、スカラー変数、添字が定数か変数の要素 (`$a[0]`、`$h{$k}`、`$r->{name}`)、
配列 (`f(@list)`) です。`shift` のあとの `$_[0]` も元の位置に書き戻します。
反映されないもの: `unshift` / `splice` / `pop` / `@_ = ...` のあとの書き換え、`@_` や `$_[0]` をそのまま別のサブに渡して
そちらで書き換える形、`\$_[0]` 経由、関数呼び出しやハッシュを引数に含む場合のそれより後ろの引数。

非対応 (今のところ): `sprintf` の位置指定 (`%2$s`)、
`system`/バッククォート、`format`、`class` 構文 (5.38+、bless の OOP で代替)、
`my method` / `->&` (5.42)、`foreach` のエイリアス (`for ($x, @list, $h{k}) { s/..// }` は各反復のあとの書き戻しで代用)、
変数を使う間接オブジェクト構文 (`new $class(ARGS)`。クラス名を直接書く形だけ対応)、パッケージの名前表 (`%Pkg::`) への書き込み。
`use Exporter; use lib; use utf8;` などは受け付けて無視します。
**`%SIG`** は `__WARN__` / `__DIE__` フックと `alarm` + `$SIG{ALRM}` に対応しています
(perl の safe signals と同じく文の切れ目で配送。未捕捉の ALRM は `Alarm clock` で die)。
他のシグナル名は受け付けて無視します。

**XS には対応していません** (DynaLoader も perl の C API もありません)。`JSON::XS` や
`Digest::SHA` のように XS を使う CPAN モジュール (依存に含む場合も) は動きません。
C で機能を足したい場合は、ボード API と同じ方式 — C 関数を `pp_register()` で
`Pkg::_name` として登録し、`lib/` の Perl モジュールで包む — になります
(ファームウェアの再ビルドが必要です)。

## メモリと安全装置

- RP2040 では 64 KiB のスタックを確保し、インタプリタが C スタックの使用量を監視して
  上限を超えると `die` します (クラッシュではなくコンソールにメッセージが出ます)。
  監視は実行時 (`Out of stack space (expression or call nesting too deep)`) と
  パース時 (`... (source nesting too deep)`、機械生成の深い式など) の両方で行い、
  perl レベルの再帰は先に深さ 200 (`Deep recursion limit exceeded`) で止まります。
  深くネストした配列/ハッシュの解放は明示的なワークリストで反復処理するので、
  破棄時にスタックを消費しません。
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
- メモリが尽きた場合は `picoperl: out of memory` を表示して再起動します。再起動後は main.pl を実行せず、
  理由をシリアルに 3 秒おきに表示して (LED は点滅)、ドライブ上のファイルが保存し直されるのを待ちます。
  スクリプトを小さくして保存すれば、そこから読み込み直します。
- 値は参照カウントで管理します。データどうしの循環参照 (`$a->{b} = $b; $b->{a} = $a`) は解放されません。
- クロージャは、作られたスコープの変数をまとめて保持します。`my $cb = sub { ... };` のように、クロージャを同じスコープの変数
  (または、そこに置いた配列やハッシュ) に入れると輪になりますが、スコープを抜けるとき、または外に渡したクロージャが
  手放されたときに、外から届かなくなった輪を見つけて解放します。解放されないのは、輪の途中に複数から共有されている
  値がある場合 (`my $c; my $r = \$c; $$r = sub { ... }`) と、`eval` 文字列や `BEGIN` の中で作った輪です。
