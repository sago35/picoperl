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
| `16_oled_inverted_hw` | 09 を 180 度回転して表示 | `set_rotation(180)` |
| `20_rotary_gopher` | エンコーダで gophers フォントのアルファベットをスクロール | `Encoder`, `Tinyfont::LineWidth`, `clear_buffer` |
