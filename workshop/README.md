# tinygo-keeb/workshop を picoperl で

https://github.com/tinygo-keeb/workshop の各例を、同じディレクトリ構成で Perl に移植したものです。
`NN_name/main.pl` をボードの `PICOPERL` ドライブに `main.pl` としてコピーするか、
リポジトリのルートで次のようにビルドした uf2 を書き込みます (起動ごとにドライブを初期化する版):

```sh
make uf2 SCRIPT=workshop/01_blinky1/main.pl NAME=01_blinky1   # -> dist/picoperl-01_blinky1.uf2
```

| 例 | 内容 | 使う API |
|---|---|---|
| `01_blinky1` | RP2040-Zero の RGB LED (GPIO16) を点滅 | `Ws2812->new(16)`, `put_color` |
| `02_blinky2` | 12 キーの LED (GPIO1) を順に点灯 | `Ws2812->new(1)`, `write_raw` |
| `03_usbcdc-serial` | USB シリアルに 1 秒ごとに hello world | `print`, `sleep` |
| `04_usbcdc-echo` | USB シリアルから 1 行読んでエコー | `<STDIN>`, `chomp` |
| `05_rotary` | ロータリーエンコーダの位置を表示 | `Encoder->new`, `position` |
| `06_joystick` | ジョイスティックの X/Y (ADC) とボタンを表示 | `Adc->new`, `get`, `Gpio->new` |
| `07_oled` | OLED を 2x2 のブロックで塗りつぶす | `Ssd1306->new`, `set_pixel`, `display` |
| `08_oled_tinydraw` | 矩形・塗り円・三角を描く | `Tinydraw::Rectangle` / `FilledCircle` / `Triangle` |
| `09_oled_tinyfont` | FreeMono Bold 9pt と gophers 32pt で文字を描く | `Tinyfont::WriteLine` |
| `10_oled_rotated` | 縦向き (64x128) に回転して描く | `set_rotation(270)`, `size` |
| `11_oled_animation` | 文字列を回転させるアニメーション (180 度回転) | `rotation => 180`, `clear_buffer` |
| `12_matrix_basic` | 4x3 マトリクスを手動でスキャンして swN pressed を表示 | `Gpio->new`, `high` / `low` / `read` |
| `13_rotary_button` | エンコーダのボタン (GPIO2) の pressed / released を表示 | `Gpio->new(2, 'input_pullup')` |
| `14_hid_keyboard` | ボタンを押している間 `a` を送る | `Hid::keyboard_report`, `KC_A` |
| `15_hid_mouse` | ボタンを押している間マウスの左ボタンを押す | `Mouse->new`, `press(Mouse::LEFT)`, `release` |
| `16_oled_inverted_hw` | 09 を 180 度回転して表示 | `set_rotation(180)` |
| `17_oled_japanese_font` | 東雲フォントで「こんにちは世界」を表示 | `Tinyfont::SHNMK12` (12px、かな + JIS 漢字) |
| `18_midi` | USB MIDI キーボード (12 キー + コード + オクターブ + ピッチベンド) | `Midi->new`, `note_on`, `pitch_bend`, `control_change` || `20_rotary_gopher` | エンコーダで gophers フォントのアルファベットをスクロール | `Encoder`, `Tinyfont::LineWidth`, `clear_buffer` |
| `22_buzzer` | 圧電ブザー (EX01 = GPIO14) でドレミファソラシド | `Tone->new(14)`, `set_note(Tone::C5)` |
