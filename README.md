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

- USB HID キーボード (boot protocol、6KRO) + メディアキー (consumer control) + マウス + USB CDC シリアルコンソール + USB マスストレージ
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
make sim PRESSED=5:9,6:10  # scripts/main.pl を GPIO シミュレーションで実行 (駆動ピン:読み取りピン、単独ピンは "2")
```

ホスト版の `use`/`require` は スクリプトのディレクトリ、`./lib`、`<script dir>/lib`、
`<script dir>/../lib`、`$PICOPERL_LIB` を探します。

### ブラウザ (WebAssembly) 版

emscripten (emcc) が PATH にあれば、ブラウザで動くプレイグラウンドをビルドできます
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
| `Tinydraw` / `Tinyfont` | tinygo 風の関数形式: `Tinydraw::Rectangle($d, ...)`, `Tinyfont::WriteLine($d, Tinyfont::FREEMONO_BOLD_9PT, $x, $y, "hello", $c)`。内蔵フォント: `classic` (5x7), `freemono_bold_9pt`, `gophers_32pt`, `gophers_58pt` |
| `I2c` (低レベル) | `I2c::_init($bus, $sda, $scl, $hz)`, `I2c::_write($bus, $addr, $bytes)`, `I2c::_read($bus, $addr, $n)` |
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

**サブルーチンシグネチャ** (`sub f($x, $y = 10, @rest) { ... }`) に対応しています:
必須パラメータ、デフォルト付き (`=` / `//=` / `||=`、デフォルト式は先行パラメータを参照可)、
末尾の `@array` / `%hash` (奇数個なら perl と同じく die)。引数の数が合わないと perl と
同じメッセージで die します (`Too few arguments for subroutine 'main::f' (got 1; expected 2)`)。
`sub f($$)` のような識別子を含まない括弧は従来どおりプロトタイプとして無視されます。
未対応: 無名パラメータ (`sub f($x, $)` はコンパイルエラー)。`sub f() {}` の空括弧は
引数チェックをしません (perl のシグネチャは 0 個を強制する点が違います)。

perl と挙動が違う箇所は、スクリプト実行ごとに初回だけ `picoperl:` で始まる注意を stderr に出します:
`use utf8` (無視される、文字列はバイト列)、`map`/`grep` 内での `$_` の変更 (元のリストに反映されない)、
`foreach` 変数の書き戻し (単一配列を回すときだけ反映)。比較演算子は perl と同じく非結合で、
`0 < $x < 3` は構文エラーになります。

**正規表現** は独自のバックトラックエンジン (`src/pp_regex.c`) で動きます:
`m//` (`//`)、`s///`、`tr///` (`y///`)、`qr//`、`=~` / `!~`、`split /re/`、修飾子 `i m s x g e r` (`tr` は `c d s r`)、
`$1`.. と `$&`、リストコンテキストでのキャプチャ取得、`/g` の `pos()`、`\Q..\E` と変数の補間。
パターン側は文字クラス (`[a-z]`, `[^..]`, `\d \w \s`, `[[:alpha:]]`)、`.`、アンカー (`^ $ \A \z \Z \b \B`)、
グループ (`( )`, `(?: )`, `(?= )`, `(?! )`, `(?i)`, `(?^i: )`)、`|`、量指定子 (`* + ? {n,m}` と非貪欲 `?`)、後方参照 `\1` に対応します。
制限: 文字列はバイト列として扱う (UTF-8 の 1 文字に `.` は一致しない)、後読み (`(?<=`) と名前付きキャプチャは未対応、
`$1` などは perl のようにスコープごとに復元されずグローバル、`pos($x) = n` の代入と `@-`/`@+` は未対応、
極端なバックトラック (`(a+)+b` のような) は途中で `Regexp backtracking limit exceeded` で die します。

**ファイル I/O**: 3 引数 `open` + レキシカルハンドルの最小セットに対応しています:
`open(my $fh, '<', $path)` (`> >> +< +>`)、`<$fh>` (スカラーで 1 行、リストで全行、`while (<$fh>)` は
perl と同じく `$_` に読み込む)、`print/printf/say $fh ...` と `print {$fh} ...`、`close`、`eof`、
`seek($fh, $off, 0/1/2)`、`tell`。失敗時は `$!` にメッセージが入ります。ボードではパスは
PICOPERL ドライブ上 (`main.pl`、`lib/...`)。書き込みはフラッシュ消去を伴うので低頻度の用途向きです。
未対応: 2 引数 `open`・バレワードハンドル (`open FH, ...`)・IO レイヤー (`:utf8` など)・`$/` の変更・
`$.` (警告の `, <$fh> line N` 表記) — 該当するものは初回に `picoperl:` 警告を出します。

非対応 (今のところ): `sprintf` の `%vd` と位置指定 (`%2$s`)、
`DESTROY`、`AUTOLOAD`、`eval STRING`、`each`、ヒアドキュメント、`redo`、`goto`、
lvalue の `substr` (4 引数形式は可)、`localtime`/`gmtime`、ファイルテスト (`-e`)、
`system`/バッククォート、`tie`、`format`、`state`、
新しめの構文 (後置デリファレンス `$r->@*`、`isa` 演算子、`try/catch`、`for my ($k, $v)`、
`builtin::`、`class`)、`foreach` のエイリアス (単一配列に対する書き戻しで代用)。
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
- メモリが尽きた場合は `picoperl: out of memory` を表示して再起動します。
- 値は参照カウントで管理します。循環参照は解放されません。
