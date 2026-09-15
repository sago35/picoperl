# 18_midi: USB MIDI キーボード。12 キーで音を鳴らし、エンコーダのボタンでコードを
# 切り替え、エンコーダでオクターブ移動 (ボタンを押しながら回すとプログラムチェンジ)、
# ジョイスティック X でピッチベンド、Y でモジュレーション。
# Chrome で https://www.onlinemusictools.com/kb/ を開くと簡単に試せます。
use strict;
use warnings;
use Gpio;
use Encoder;
use Adc;
use Matrix;
use Midi;
use Time::HiRes qw(usleep);

my $cable = 0;
my $channel = 1;
my $velocity = 0x40;

my $m = Midi->new;
my $btn = Gpio->new(2, 'input_pullup');
my @cols = (5, 6, 7, 8);
my @rows = (9, 10, 11);
Gpio::configure($_, 'output') for @cols;
Gpio::configure($_, 'input_pulldown') for @rows;

my @notes = (Midi::D5, Midi::G4, Midi::C4,
             Midi::E5, Midi::A4, Midi::D4,
             Midi::F5, Midi::B4, Midi::E4,
             Midi::G5, Midi::C5, Midi::F4);

my @chords = (
    [ 'C ', Midi::C4, Midi::E4, Midi::G4 ],
    [ 'G ', Midi::G3, Midi::B3, Midi::D4 ],
    [ 'Am', Midi::A3, Midi::C4, Midi::E4 ],
    [ 'F ', Midi::F3, Midi::A3, Midi::C4 ],
);

my $enc = Encoder->new(3, 4);
my $ax = Adc->new(29);
my $ay = Adc->new(28);

my ($prev_btn, $index, $pc) = (1, 0, 0);
my ($prev_x, $prev_y) = (0, 0);
my @pressed = (0) x 12;
my @edge_ms = (0) x 12;

while (1) {
    # ジョイスティック: X = ピッチベンド、Y = モジュレーションホイール
    my $x = $ax->get;
    $x = 0x8000 if $x >= 0x7000 && $x <= 0x9000;      # 中央の遊び
    if ($x != $prev_x) { $m->pitch_bend($cable, $channel, $x >> 2); $prev_x = $x }
    my $y = $ay->get;
    $y = 0x8000 if $y >= 0x7000 && $y <= 0x9000;
    if ($y >= 0x8000 && $y != $prev_y) {
        $m->control_change($cable, $channel, Midi::CC_MODULATION_WHEEL, ($y - 0x8000) >> 8);
        $prev_y = $y;
    }

    # エンコーダ: 通常はオクターブ移動、ボタンを押しながらでプログラムチェンジ
    my $current = $btn->read;
    if (my $d = $enc->read) {
        if ($current) {
            if ($d < 0) { if ($notes[0] >= 12) { $_ -= 12 for @notes } }
            else        { if ($notes[0] <= 96) { $_ += 12 for @notes } }
        } else {
            $pc = ($pc + $d) % 128;
            $pc += 128 if $pc < 0;
            $m->program_change($cable, $channel, $pc);
        }
    }

    # ボタン: 押している間コードを鳴らし、離すと次のコードへ
    if ($prev_btn != $current) {
        my (undef, @cn) = @{ $chords[$index] };
        if ($current) {
            $m->note_off($cable, $channel, $_, $velocity) for @cn;
            $index = ($index + 1) % @chords;
        } else {
            $m->note_on($cable, $channel, $_, $velocity) for @cn;
        }
        $prev_btn = $current;
    }

    # 12 キー: 変化は最初のスキャンで即送信し、エッジ直後 20ms は再変化を
    # 無視する (時間ベースのデバウンス。2 回連続一致方式は確定に 1 周期
    # 余分にかかり、押してから鳴るまでの遅れになる)
    my $keys = Matrix::scan(\@cols, \@rows, 1, 30);
    my $now = Time::HiRes::_time_ms();
    for my $i (0 .. 11) {
        my $s = $keys->[$i] ? 1 : 0;
        next if $s == $pressed[$i];
        next if $now - $edge_ms[$i] < 20;
        $pressed[$i] = $s;
        $edge_ms[$i] = $now;
        if ($s) { $m->note_on($cable, $channel, $notes[$i], $velocity) }
        else    { $m->note_off($cable, $channel, $notes[$i], $velocity) }
    }
    usleep(1_000);
}
