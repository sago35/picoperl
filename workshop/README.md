# tinygo-keeb/workshop を picoperl で

https://github.com/tinygo-keeb/workshop の各例を、同じディレクトリ構成で Perl に移植したものです。
`NN_name/main.pl` をボードの `PICOPERL` ドライブに `main.pl` としてコピーするか、
リポジトリのルートで次のようにビルドした uf2 を書き込みます (起動ごとにドライブを初期化する版):

```sh
make uf2 SCRIPT=workshop/01_blinky1/main.pl NAME=01_blinky1   # -> dist/picoperl-01_blinky1.uf2
```

| 例 | 内容 | 使う API |
|---|---|---|
| `01_blinky1` | RP2040-Zero の RGB LED (GPIO16) を点滅 | `ws2812_init`, `ws2812_put_color` |
| `02_blinky2` | 12 キーの LED (GPIO1) を順に点灯 | `ws2812_init`, `ws2812_write_raw` |
| `03_usbcdc-serial` | USB シリアルに 1 秒ごとに hello world | `print`, `sleep_ms` |
| `04_usbcdc-echo` | USB シリアルから 1 行読んでエコー | `<STDIN>`, `chomp` |
| `05_rotary` | ロータリーエンコーダの位置を表示 | `Encoder->new`, `position` |
| `06_joystick` | ジョイスティックの X/Y (ADC) とボタンを表示 | `Adc->new`, `get`, `Gpio->new` |
