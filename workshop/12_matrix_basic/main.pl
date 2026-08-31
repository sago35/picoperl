# tinygo-keeb/workshop 12_matrix_basic in picoperl:
# scan the 4x3 key matrix by hand and print "swN pressed".
#
# zero-kb02: the columns (GPIO5-8) are outputs, the rows (GPIO9-11) are inputs with
# pull-downs; driving one column high and reading a high row means the switch at
# that (row, col) is pressed. sw1..sw4 are row 0, sw5..sw8 row 1, sw9..sw12 row 2.
# The Go version spells out the four columns; here the same thing is a loop.
# (PicoPerl::Keyboard and Matrix::scan do this scan in C; this is the manual version.)
use strict;
use Gpio;
use Time::HiRes qw(usleep);

my @col_pins = map { Gpio->new($_, 'output') } (5, 6, 7, 8);
my @row_pins = map { Gpio->new($_, 'input_pulldown') } (9, 10, 11);

$_->low for @col_pins;

while (1) {
    for my $col (0 .. $#col_pins) {
        # COL1 .. COL4: drive only this column high
        for my $c (0 .. $#col_pins) {
            if ($c == $col) { $col_pins[$c]->high } else { $col_pins[$c]->low }
        }
        usleep(1000);

        for my $row (0 .. $#row_pins) {
            if ($row_pins[$row]->read) {
                my $sw = $row * @col_pins + $col + 1;
                print "sw$sw pressed\n";
                sleep 0.1;
            }
        }
    }
}
